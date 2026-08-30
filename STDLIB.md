# STDLIB.md: Zero-Dependency Substitutions

Every place a typical PNG-touching project reaches for a library, and what
this project does instead. `imgview` links nothing but the C standard library
and one POSIX call (named explicitly at the bottom).

---

## The claim

This project fully replaces **zlib** and **libpng** for the 8-bit RGB/RGBA
truecolor PNG subset defined in `SCOPE.md`. There is no partial substitution
and no fallback import: if `zlib.h` or `png.h` were deleted from the build
machine entirely, `imgview` would compile and decode every fixture in
`tests/fixtures/` identically.

| Normally reached for | What it does | Replaced by | Where |
|---|---|---|---|
| `zlib` / `miniz` | DEFLATE decompression | Hand-rolled inflate (RFC 1951) | `src/inflate.c` |
| `zlib` (`inflateInit`/`inflate`) | RFC 1950 zlib stream wrapper | Hand-rolled header + Adler-32 | `src/zlib_wrapper.c` |
| `libpng` | PNG container parsing | Hand-rolled chunk loop + CRC | `src/png_container.c` |
| `libpng` | Scanline filter reconstruction | Hand-rolled unfilter, all 5 types | `src/png_unfilter.c` |
| `zlib` (`crc32()`) | CRC-32 per chunk | Table-driven from PNG spec Annex D | `src/png_container.c` |
| `chafa` / `viu` / `libsixel` | Terminal image rendering | Hand-rolled ANSI half-block renderer | `src/term_render.c` |

---

## DEFLATE decompression

**Normally:** `zlib` or `miniz` (standard imports for PNG decompression).

**Instead:** A complete DEFLATE inflater from RFC 1951, built in three layers:

* **`src/bit_reader.c`**: LSB-first bit extraction from a byte buffer.
  DEFLATE packs bit sequences LSB-first, except Huffman codes themselves
  (MSB-first). Both bit orders are handled and tested separately.
* **`src/huffman.c`**: canonical Huffman table construction from code lengths
  alone. No explicit tree nodes; the canonical code-assignment algorithm (RFC
  1951 sec 3.2.2) maps sorted bit-lengths directly to codes. Handles the
  legal 0/1-symbol incomplete alphabet edge cases (RFC 1951 sec 3.2.7).
* **`src/inflate.c`**: block loop and decoding. Handles `BTYPE=00` stored blocks
  (byte-aligned literal copy), `BTYPE=01` fixed Huffman (spec-hardcoded tables),
  and `BTYPE=10` dynamic Huffman (decodes code length tree, expands lit/dist
  lengths with repeat codes 16/17/18). LZ77 back-references are resolved
  byte-by-byte to handle overlapping copies correctly (distance < length).

**Tradeoff:** Decode-only, no compression. Preset dictionaries (FDICT) rejected.

---

## RFC 1950 zlib stream wrapper

**Normally:** `zlib`'s `inflateInit`/`inflate` strips the two-byte header and
verifies the trailing Adler-32 checksum.

**Instead:** `src/zlib_wrapper.c` validates CMF/FLG header bytes (CM=8
check, FCHECK mod-31 check), rejects preset dictionaries (FDICT bit), then
passes the raw DEFLATE payload to `inflate()`. Computes Adler-32 (RFC 1950 sec 8)
over decompressed data and verifies it against the 4-byte big-endian trailer.

**Tradeoff:** No preset-dictionary support (consistent with inflate scope).

---

## PNG container parsing

**Normally:** `libpng` reads signature, iterates chunks, validates CRC-32,
and extracts IHDR fields.

**Instead:** `src/png_container.c` implements the complete chunk loop:
8-byte signature validation, length/type/data/CRC iteration, and CRC-32
checks on every chunk before parsing. The CRC-32 table uses polynomial
`0xEDB88320` (reflected, init/final XOR `0xFFFFFFFF`) from PNG spec Annex D.

IHDR fields are validated for 8-bit depth, color types 2 (RGB) and 6 (RGBA),
and interlace method 0. Unsupported formats return clear error messages.
Multiple `IDAT` chunks are concatenated in stream order.

**Tradeoff:** 8-bit RGB/RGBA truecolor only (documented in `SCOPE.md`).

---

## Scanline unfiltering

**Normally:** `libpng` reverses PNG scanline filter transforms during decoding.

**Instead:** `src/png_unfilter.c` reverses all five PNG filter types:

* **None** (type 0): raw scanline bytes
* **Sub** (type 1): add left pixel byte (`bpp` stride)
* **Up** (type 2): add byte from previous row
* **Average** (type 3): add floor((left + above) / 2)
* **Paeth** (type 4): add `paeth_predictor(left, above, upper-left)` (PNG spec sec 9.4)

Row 0's prior row is treated as all zeros. The previous row pointer references
the output buffer directly, avoiding extra buffer copies.

**Tradeoff:** None for supported color types.

---

## CRC-32

**Normally:** Bundled inside `zlib` (`crc32()`).

**Instead:** `build_crc_table()` and `png_crc32()` in `src/png_container.c`.
Table-driven CRC-32 (polynomial `0xEDB88320`, reflected, init/final `0xFFFFFFFF`)
implemented independently from spec Annex D.

**Tradeoff:** None. Complete spec-accurate implementation.

---

## Terminal image rendering

**Normally:** `chafa`, `viu`, `libsixel` (third-party terminal image renderers).

**Instead:** `src/term_render.c` implements three standalone rendering modes:

* **Truecolor** (`\x1b[38;2;R;G;Bm` / `\x1b[48;2;R;G;Bm`): uses `▀` (U+2580)
  half-block cells to render two pixel rows per text cell with 24-bit RGB.
* **256-color** (`\x1b[38;5;Nm` / `\x1b[48;5;Nm`): maps pixels to the xterm-256
  cube (6x6x6 RGB cube + 24-step grayscale ramp).
* **ASCII**: BT.601 luminance (`0.299R + 0.587G + 0.114B`) mapped to `" .:-=+*#%@"`.

Includes nearest-neighbor scaling preserving aspect ratio and alpha compositing
against mid-gray `(128, 128, 128)`.

**Tradeoff:** Half-block rendering only; no sixel or dithering.

---

## Command-line argument parsing

**Normally:** `getopt`, `getopt_long`, or external CLI libraries.

**Instead:** `parse_args()` in `src/main.c` handles `--width N`, `--width=N`,
`--mode=...`, `--info`, and `--help` with explicit bounds checking and validation.

**Tradeoff:** Focused flag surface tailored to application requirements.

---

## Terminal size detection

`ioctl(STDOUT_FILENO, TIOCGWINSZ, ...)` in `src/term_render.c` detects terminal
dimensions for automatic scaling. This uses POSIX system interfaces rather
than external libraries.

---

## Test-only dependencies (never shipped in the artifact)

Per submission rules, test-only tools are disclosed:

* **Python `zlib` module** (`tools/verify_inflate.py`): generates ground truth
  for unit tests. Never linked into C binaries.
* **Pillow** (`tools/verify_pixels.py`, `tools/verify_render.py`): verifies decoded
  pixels and downscaled rendering. Never linked into C binaries.

Removing both tools does not affect building or running `imgview`.

---

*For design background and substitution history, see [`STDLIB_LOG.md`](STDLIB_LOG.md).*