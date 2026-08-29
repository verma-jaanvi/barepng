CC       := gcc
CFLAGS   := -std=c11 -Wall -Wextra -Wpedantic -O2 -Iinclude
LDFLAGS  :=
BUILD    := build
SRC      := $(wildcard src/*.c)
OBJ      := $(patsubst src/%.c,$(BUILD)/%.o,$(SRC))

ifeq ($(OS),Windows_NT)
    BIN      := $(BUILD)/pngdecoder.exe
    HEXDUMP  := tools/hexdump.exe
    MKDIR_P  := powershell -Command "if (!(Test-Path $(BUILD))) { New-Item -ItemType Directory -Path $(BUILD) | Out-Null }"
    RM_RF    := powershell -Command "Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $(BUILD), $(HEXDUMP)"
    TEST_RUN := powershell -Command "Get-ChildItem demo/*.png | ForEach-Object { Write-Host ('--- ' + $$_.Name + ' ---'); .\$(BIN) $$_.FullName }"
else
    BIN      := $(BUILD)/pngdecoder
    HEXDUMP  := tools/hexdump
    MKDIR_P  := mkdir -p $(BUILD)
    RM_RF    := rm -rf $(BUILD) $(HEXDUMP)
    TEST_RUN := for f in demo/*.png; do echo "--- $$f ---"; ./$(BIN) $$f; done
endif

TESTBIN := $(BUILD)/test_primitives
BITTESTBIN := $(BUILD)/test_bitreader
HUFFTESTBIN := $(BUILD)/test_huffman
INFLATETESTBIN := $(BUILD)/test_inflate
ZLIBTESTBIN := $(BUILD)/test_zlib_wrapper
ifeq ($(OS),Windows_NT)
    TESTBIN := $(BUILD)/test_primitives.exe
    BITTESTBIN := $(BUILD)/test_bitreader.exe
    HUFFTESTBIN := $(BUILD)/test_huffman.exe
    INFLATETESTBIN := $(BUILD)/test_inflate.exe
    ZLIBTESTBIN := $(BUILD)/test_zlib_wrapper.exe
endif

.PHONY: all clean hexdump test check

all: $(BIN) $(HEXDUMP)

$(BIN): $(OBJ) | $(BUILD)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	@$(MKDIR_P)

hexdump: $(HEXDUMP)

$(HEXDUMP): tools/hexdump.c
	$(CC) -O2 -Wall -Wextra -o $@ $<

# Quick smoke test: run the decoder against every demo image
test: all
	@$(TEST_RUN)

# Isolated unit tests: read_u32_be/CRC-32 (Phase 1), the bit reader
# (Phase 2a), canonical Huffman build/decode (Phase 2c), end-to-end
# inflate() against real DEFLATE fixtures (Phase 2b/2c/2d), and the
# RFC 1950 zlib wrapper (Phase 2e). Each layer's tests assume the layer
# below it is already correct — run this whole target, in order, before
# trusting anything new built on top.
check: $(TESTBIN) $(BITTESTBIN) $(HUFFTESTBIN) $(INFLATETESTBIN) $(ZLIBTESTBIN)
	@./$(TESTBIN)
	@./$(BITTESTBIN)
	@./$(HUFFTESTBIN)
	@./$(INFLATETESTBIN)
	@./$(ZLIBTESTBIN)

$(TESTBIN): tests/test_primitives.c $(BUILD)/png_container.o | $(BUILD)
	$(CC) $(CFLAGS) tests/test_primitives.c $(BUILD)/png_container.o -o $@

$(BITTESTBIN): tests/test_bitreader.c $(BUILD)/bit_reader.o | $(BUILD)
	$(CC) $(CFLAGS) tests/test_bitreader.c $(BUILD)/bit_reader.o -o $@

$(HUFFTESTBIN): tests/test_huffman.c $(BUILD)/huffman.o $(BUILD)/bit_reader.o | $(BUILD)
	$(CC) $(CFLAGS) tests/test_huffman.c $(BUILD)/huffman.o $(BUILD)/bit_reader.o -o $@

$(INFLATETESTBIN): tests/test_inflate.c $(BUILD)/inflate.o $(BUILD)/huffman.o $(BUILD)/bit_reader.o | $(BUILD)
	$(CC) $(CFLAGS) tests/test_inflate.c $(BUILD)/inflate.o $(BUILD)/huffman.o $(BUILD)/bit_reader.o -o $@

$(ZLIBTESTBIN): tests/test_zlib_wrapper.c $(BUILD)/zlib_wrapper.o | $(BUILD)
	$(CC) $(CFLAGS) tests/test_zlib_wrapper.c $(BUILD)/zlib_wrapper.o -o $@

clean:
	@$(RM_RF)
