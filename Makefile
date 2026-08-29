CC       := gcc
CFLAGS   := -std=c11 -Wall -Wextra -Wpedantic -O2 -Iinclude
LDFLAGS  :=
BUILD    := build
SRC      := $(wildcard src/*.c)
OBJ      := $(patsubst src/%.c,$(BUILD)/%.o,$(SRC))

ifeq ($(OS),Windows_NT)
    # Full path to Python — avoids picking up MSYS2's stub which lacks PIL
    PYTHON   := C:/Users/JAANVI/AppData/Local/Programs/Python/Python311/python.exe
    BIN      := $(BUILD)/pngdecoder.exe
    HEXDUMP  := tools/hexdump.exe
    MKDIR_P  := powershell -Command "if (!(Test-Path $(BUILD))) { New-Item -ItemType Directory -Path $(BUILD) | Out-Null }"
    RM_RF    := powershell -Command "if (Test-Path $(BUILD)) { Remove-Item -Recurse -Force $(BUILD) }; if (Test-Path $(HEXDUMP)) { Remove-Item -Force $(HEXDUMP) }"
    TEST_RUN := powershell -Command "Get-ChildItem demo/*.png | ForEach-Object { Write-Host ('--- ' + $$_.Name + ' ---'); .\$(BIN) $$_.FullName }"
else
    BIN      := $(BUILD)/pngdecoder
    HEXDUMP  := tools/hexdump
    MKDIR_P  := mkdir -p $(BUILD)
    RM_RF    := rm -rf $(BUILD) $(HEXDUMP)
    TEST_RUN := for f in demo/*.png; do echo "--- $$f ---"; ./$(BIN) $$f; done
    PYTHON   := python3
endif

TESTBIN := $(BUILD)/test_primitives
BITTESTBIN := $(BUILD)/test_bitreader
HUFFTESTBIN := $(BUILD)/test_huffman
INFLATETESTBIN := $(BUILD)/test_inflate
ZLIBTESTBIN := $(BUILD)/test_zlib_wrapper
UNFILTERTESTBIN := $(BUILD)/test_unfilter
MEMCHECKBIN     := $(BUILD)/test_memcheck
ifeq ($(OS),Windows_NT)
    TESTBIN := $(BUILD)/test_primitives.exe
    BITTESTBIN := $(BUILD)/test_bitreader.exe
    HUFFTESTBIN := $(BUILD)/test_huffman.exe
    INFLATETESTBIN := $(BUILD)/test_inflate.exe
    ZLIBTESTBIN := $(BUILD)/test_zlib_wrapper.exe
    UNFILTERTESTBIN := $(BUILD)/test_unfilter.exe
    MEMCHECKBIN     := $(BUILD)/test_memcheck.exe
endif

.PHONY: all clean hexdump test check corpus fuzz analyze debug perf verify-inflate verify-pixels verify-render memcheck

all: $(BIN) $(HEXDUMP)

# Debug build with sanitizers — for correctness/memory stages
debug: CFLAGS += -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer
debug: $(BIN)

# Optimized build for perf measurement — never profile a debug binary
perf: CFLAGS += -O3 -DNDEBUG
perf: $(BIN)

$(BIN): $(OBJ) | $(BUILD)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	@$(MKDIR_P)

hexdump: $(HEXDUMP)

$(HEXDUMP): tools/hexdump.c
	$(CC) -O2 -Wall -Wextra -o $@ $<

# Quick smoke test: run the decoder against every demo image, then the
# Phase 7 aspect-ratio regression check (row scaling must track column
# scaling — see tools/check_render_aspect.py for the bug this catches).
test: all
	@$(TEST_RUN)
	@$(PYTHON) tools/check_render_aspect.py $(BIN)

# Isolated unit tests: read_u32_be/CRC-32 (Phase 1), the bit reader
# (Phase 2a), canonical Huffman build/decode (Phase 2c), end-to-end
# inflate() against real DEFLATE fixtures (Phase 2b/2c/2d), the
# RFC 1950 zlib wrapper (Phase 2e), and PNG scanline unfiltering (Phase 3).
# Each layer's tests assume the layer below it is already correct — run
# this whole target, in order, before trusting anything new built on top.
check: $(TESTBIN) $(BITTESTBIN) $(HUFFTESTBIN) $(INFLATETESTBIN) $(ZLIBTESTBIN) $(UNFILTERTESTBIN)
	@./$(TESTBIN)
	@./$(BITTESTBIN)
	@./$(HUFFTESTBIN)
	@./$(INFLATETESTBIN)
	@./$(ZLIBTESTBIN)
	@./$(UNFILTERTESTBIN)

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

$(UNFILTERTESTBIN): tests/test_unfilter.c $(BUILD)/png_unfilter.o | $(BUILD)
	$(CC) $(CFLAGS) tests/test_unfilter.c $(BUILD)/png_unfilter.o -o $@

$(MEMCHECKBIN): tests/test_memcheck.c $(BUILD)/png_container.o $(BUILD)/zlib_wrapper.o $(BUILD)/inflate.o $(BUILD)/huffman.o $(BUILD)/bit_reader.o $(BUILD)/png_unfilter.o | $(BUILD)
	$(CC) $(CFLAGS) tests/test_memcheck.c $(BUILD)/png_container.o $(BUILD)/zlib_wrapper.o $(BUILD)/inflate.o $(BUILD)/huffman.o $(BUILD)/bit_reader.o $(BUILD)/png_unfilter.o -o $@ "-Wl,--wrap=malloc" "-Wl,--wrap=calloc" "-Wl,--wrap=realloc" "-Wl,--wrap=free"

# Phase 6: generate diverse PNG corpus from Python (requires Pillow)
corpus: all
	$(PYTHON) tools/gen_corpus.py

# Phase 6: malformed-input gauntlet — every case must exit 1, no crashes
fuzz: all corpus
	$(PYTHON) tools/fuzz_malformed.py

# Stage 2a: verify inflate output against Python zlib ground truth
verify-inflate: all corpus
	$(PYTHON) tools/verify_inflate.py $(BIN)

# Stage 2b: verify unfilter pixel output against Pillow ground truth
verify-pixels: all corpus
	$(PYTHON) tools/verify_pixels.py $(BIN)

# Stage 2c: verify ANSI rendering, downsampling, and alpha compositing
verify-render: all corpus
	$(PYTHON) tools/verify_render.py $(BIN)

# Stage 4: memory correctness & zero-leak verification across corpus and error paths
memcheck: $(MEMCHECKBIN) corpus
	$(PYTHON) tools/verify_memory.py

# Phase 6 / Stage 4: GCC static analysis across every source file
analyze:
	$(PYTHON) tools/analyze_static.py

clean:
	@$(RM_RF)
