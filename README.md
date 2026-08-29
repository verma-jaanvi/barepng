# imgview — A PNG Decoder and Terminal Renderer in C

Zero-dependency PNG decoder and terminal image renderer — including a hand-rolled
DEFLATE decompressor written from the RFC 1951 spec. No libpng. No zlib. No image
libraries of any kind. The entire decode pipeline — chunk parsing, inflate,
scanline unfiltering, and terminal rendering — is implemented from scratch in
~1,700 lines of C.

```
$ imgview demo/photo_640x480_rgb.png --info

  dimensions  : 640 x 480
  color type  : RGB (truecolor)
  IDAT stream : 426 KB compressed
  pixel buffer: 900 KB uncompressed
  decode time : 11.0 ms (inflate + unfilter)

$ imgview demo/photo_640x480_rgb.png --mode=ascii --width 60

                  .................:.:::::::::::::::-:--:---
                ... .............:..:.::::::::::::::-::-----
                 ....................:::::::::::::::-:-:----
            .   ... ...........:..:...:::::::::::-::::-::---
              . .. . ............:..:.::::::::::::::::------
```

On a color terminal the `--mode=truecolor` default renders each image with 24-bit
RGB using the [Unicode half-block character `▀`](https://www.fileformat.info/info/unicode/char/2580/index.htm) —
two image rows per terminal cell, foreground + background color.

---

## Build

Requires GCC (MinGW on Windows, or any C11 compiler). No other dependencies.

```bash
make           # build imgview binary
make check     # run all 43 unit tests
make test      # decode and render all demo PNGs
make fuzz      # 34-case malformed-input gauntlet
make analyze   # GCC -fanalyzer static analysis
```

**Windows (PowerShell):**
```powershell
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
mingw32-make all
.\build\pngdecoder.exe demo\photo_640x480_rgb.png
```

---

## Usage

```
imgview <file.png> [options]

  --width N              cap render width to N terminal columns
  --mode=truecolor       force 24-bit RGB (▀ half-blocks)
  --mode=256             force xterm-256 color palette
  --mode=ascii           force ASCII luminance ramp " .:-=+*#%@"
  --info                 print decode stats only, no render
  --help                 show this help
```

Color mode is **auto-detected** from the environment (`$COLORTERM`, `$TERM`,
Windows VT-processing API) and degrades gracefully:

| Environment | Mode selected |
|-------------|--------------|
| `$COLORTERM=truecolor` or `24bit` | Truecolor |
| Windows Terminal / modern ConHost | Truecolor (via `SetConsoleMode`) |
| `$TERM` contains `256color` | 256-color |
| stdout redirected / not a tty | ASCII (always printable) |
| Unknown tty | 256-color (safe fallback) |

Alpha channels (RGBA images) are composited against a mid-gray background
`(128, 128, 128)` before rendering.

---

## How It Works

**1. Chunk parsing** — [`src/png_container.c`](src/png_container.c)

A PNG file is a sequence of length-prefixed chunks, each with a 4-byte type
tag and a CRC-32 checksum. The parser reads the 8-byte signature, iterates
chunks in order, verifies every CRC-32 (spec Annex D algorithm), and
concatenates all `IDAT` payloads into a single buffer. Unsupported chunk types
in the critical set (`PLTE`, palette-type) are rejected with an explicit error;
ancillary chunks are skipped by length.

**2. DEFLATE inflate** — [`src/inflate.c`](src/inflate.c)

The concatenated `IDAT` buffer is a zlib stream (RFC 1950) wrapping a DEFLATE
bitstream (RFC 1951). After stripping the 2-byte zlib header and verifying the
Adler-32 trailer, the inflater processes blocks sequentially. Three block types:
`BTYPE=00` stored (literal copy), `BTYPE=01` fixed Huffman tables (hardcoded
per spec), and `BTYPE=10` dynamic Huffman — where a second Huffman tree encoded
in the block header describes the first one. This is the core complexity: the
19-symbol code-length alphabet, repeat codes 16/17/18, and the canonical
code-building algorithm that turns sorted bit-lengths into a decode table.
Back-references (length/distance pairs) are resolved via a sliding window over
the output buffer.

**3. Scanline unfiltering** — [`src/png_unfilter.c`](src/png_unfilter.c)

The inflated bytes are not raw pixels. Each scanline is prefixed with a filter
type byte (0–4), and the pixel bytes are delta-encoded. Reversing this requires
the already-decoded previous row. Five filters: **None** (no transform),
**Sub** (delta from left neighbor), **Up** (delta from pixel above), **Average**
(floor of average of left and above), and **Paeth** — a predictor function that
selects among left, above, and upper-left based on which is closest to a linear
prediction. Row 0's "above" is a virtual all-zeros row. The output buffer holds
reconstructed pixels; the `prev` pointer for row N+1 points directly into that
buffer at row N's offset — no copy.

**4. Terminal rendering** — [`src/term_render.c`](src/term_render.c)

The pixel buffer is downscaled to terminal width (nearest-neighbor; quality is
not the point) and rendered using ANSI escape sequences. In truecolor mode:
`\x1b[38;2;R;G;Bm` sets foreground, `\x1b[48;2;R;G;Bm` sets background, and
`▀` (U+2580, written as raw UTF-8 bytes `E2 96 80`) carries two image rows per
cell. `\x1b[0m` resets at every line end to prevent color bleeding into the
shell prompt. 256-color mode quantizes each pixel to the xterm-256 palette
(6×6×6 RGB cube + 24-step grayscale ramp). ASCII mode maps BT.601 luminance to
the ramp `" .:-=+*#%@"`.

---

## Scope

**Supported:** 8-bit RGB (color type 2) and RGBA (color type 6), no interlacing.
This covers the overwhelming majority of real-world PNGs — JPEG-replacement
photos, screenshots, UI assets, and anything exported by GIMP, Photoshop,
Pillow, or libpng with default settings.

**Out of scope (rejected cleanly with an error message, never silently
mishandled):** 16-bit depth, grayscale (types 0/4), palette/indexed color
(type 3), Adam7 interlacing, APNG, encoding.

Interlacing in particular is additive complexity, not a core format primitive —
the filters, inflate, and chunk parser work identically without it.
The unsupported types are good candidates for a follow-on iteration.

---

## Test Suite

```
43 unit tests across 6 test binaries:
  test_primitives   — CRC-32, big-endian u32 reader
  test_bitreader    — LSB-first bit extraction, alignment, atomicity
  test_huffman      — canonical code build, decode, edge cases (0/1 symbol)
  test_inflate      — stored, fixed Huffman, dynamic Huffman, LZ77 back-refs
  test_zlib_wrapper — header parse, Adler-32 verify, error paths
  test_unfilter     — all 5 filter types, Paeth predictor, mixed rows, bpp

34 malformed-input cases:
  truncation at 14 key byte offsets, empty files, bad signature,
  CRC flips on IHDR/IDAT, IDAT body corruption, bad DEFLATE BTYPE,
  invalid zlib header, missing IEND — all exit 1, none crash or hang.
```

---

## Project Structure

```
src/
  png_container.c   chunk parser, CRC-32, IHDR validation
  bit_reader.c      LSB-first bit extraction from a byte buffer
  huffman.c         canonical Huffman table build + single-symbol decode
  inflate.c         DEFLATE inflate: stored / fixed / dynamic blocks
  zlib_wrapper.c    RFC 1950 header strip + Adler-32 verification
  png_unfilter.c    PNG scanline unfiltering, all 5 filter types
  term_render.c     terminal renderer: truecolor / 256-color / ASCII
  main.c            CLI surface: argument parsing, pipeline orchestration

include/            public headers (one per module)
tests/              unit tests (one .c per binary, no test framework)
tools/
  gen_corpus.py     generates 9 diverse PNG fixtures for integration tests
  fuzz_malformed.py 34-case malformed-input gauntlet
demo/               five reference PNGs (32×32 to 2048×2048, RGB and RGBA)
tests/fixtures/     generated corpus (make corpus)
```

---

## Performance

All images decode well under 100ms wall time on a mid-range laptop:

| Image | Pixels | Decode + Unfilter |
|-------|--------|------------------|
| 32×32 RGB | 3 KB | < 1 ms |
| 256×256 RGBA | 256 KB | 1 ms |
| 640×480 RGB | 900 KB | 12 ms |
| 2048×2048 RGB | 12 MB | 20 ms |
| 2000×1500 RGBA | 12 MB | 20 ms |

The dominant cost is DEFLATE decode (dynamic Huffman symbol lookup per output
byte). There are no unnecessary copies: the inflate output buffer is consumed
directly by the unfilter step, which writes directly into the final pixel buffer.
