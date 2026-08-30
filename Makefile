CC       := gcc
CFLAGS   := -std=c11 -Wall -Wextra -Wpedantic -O2 -Iinclude
LDFLAGS  :=
BUILD    := build
SRC      := $(wildcard src/*.c)
OBJ      := $(patsubst src/%.c,$(BUILD)/%.o,$(SRC))

ifeq ($(OS),Windows_NT)
    _PYTHON_FULL := C:/Users/JAANVI/AppData/Local/Programs/Python/Python311/python.exe
    ifeq ($(wildcard $(_PYTHON_FULL)),$(_PYTHON_FULL))
        PYTHON := $(_PYTHON_FULL)
    else
        PYTHON := python
    endif
    BIN      := $(BUILD)/pngdecoder.exe
    HEXDUMP  := tools/hexdump.exe
    MKDIR_P  := powershell -Command "if (!(Test-Path $(BUILD))) { New-Item -ItemType Directory -Path $(BUILD) | Out-Null }"
    RM_RF    := powershell -Command "if (Test-Path $(BUILD)) { Remove-Item -Recurse -Force $(BUILD) }; if (Test-Path $(HEXDUMP)) { Remove-Item -Force $(HEXDUMP) }"
    TEST_RUN := powershell -Command "Get-ChildItem demo/*.png | ForEach-Object { Write-Host ('--- ' + $$_.Name + ' ---'); .\\$(BIN) $$_.FullName }"
    SINGLE_BIN := $(BUILD)/imgview_single.exe
else
    BIN      := $(BUILD)/pngdecoder
    HEXDUMP  := tools/hexdump
    MKDIR_P  := mkdir -p $(BUILD)
    RM_RF    := rm -rf $(BUILD) $(HEXDUMP)
    TEST_RUN := for f in demo/*.png; do echo "--- $$f ---"; ./$(BIN) $$f; done
    PYTHON   := python3
    SINGLE_BIN := $(BUILD)/imgview_single
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

.PHONY: all clean hexdump test check corpus fuzz analyze debug perf repro verify-inflate verify-pixels verify-render memcheck bench regression single

all: $(BIN) $(HEXDUMP)

debug: CFLAGS += -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer
debug: $(BIN)

perf: CFLAGS += -O3 -DNDEBUG
perf: $(BIN)

REPRO_CFLAGS := $(CFLAGS) -frandom-seed=0 -fdebug-prefix-map=$(CURDIR)=.
repro: $(OBJ) | $(BUILD)
	$(CC) $(REPRO_CFLAGS) $(OBJ) -o $(BIN) $(LDFLAGS)

$(BIN): $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDFLAGS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	@$(MKDIR_P)

hexdump: $(HEXDUMP)

$(HEXDUMP): tools/hexdump.c
	$(CC) -O2 -Wall -Wextra -o $@ $<

test: all
	@$(TEST_RUN)
	@$(PYTHON) tools/check_render_aspect.py $(BIN)

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

corpus: all
	$(PYTHON) tools/gen_corpus.py

fuzz: all corpus
	$(PYTHON) tools/fuzz_malformed.py

verify-inflate: all corpus
	$(PYTHON) tools/verify_inflate.py $(BIN)

verify-pixels: all corpus
	$(PYTHON) tools/verify_pixels.py $(BIN)

verify-render: all corpus
	$(PYTHON) tools/verify_render.py $(BIN)

memcheck: $(MEMCHECKBIN) corpus
	$(PYTHON) tools/verify_memory.py

bench: perf
	$(PYTHON) tools/bench_perf.py $(BIN)

analyze:
	$(PYTHON) tools/analyze_static.py

regression:
	$(PYTHON) tools/regression_check.py

single: $(BUILD)
	$(PYTHON) tools/amalgamate.py > $(BUILD)/imgview_single.c
	$(CC) $(CFLAGS) $(BUILD)/imgview_single.c -o $(SINGLE_BIN)

clean:
	@$(RM_RF)
