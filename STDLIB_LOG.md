# STDLIB_LOG.md

Chronological engineering log (see [STDLIB.md](STDLIB.md) for substitution index).

---

**Phase 1 (Container parsing):** The standard instinct for CRC-32 is to reach for
zlib's `crc32()`, since PNG's variant uses the same polynomial.
Implemented the table-driven version from the reference polynomial
(`0xEDB88320`, reflected, init/final XOR `0xFFFFFFFF`) instead, in
`png_container.c`. Verified independently against the `"123456789"` canonical
self-test value (`0xCBF43926`) and IEND's fixed CRC (`0xAE426082`) before
processing images.

**Phase 2 (Huffman construction):** Canonical Huffman construction from
code lengths alone is commonly delegated to a Huffman library.
Implemented the code-assignment algorithm (RFC 1951 sec 3.2.2) directly
in `huffman.c`. Handled MSB-first bit-packing for Huffman codes (distinct
from LSB-first bit extraction in the bit reader) with dedicated unit tests.

**Phase 2 (Dynamic Huffman blocks):** Dynamic Huffman block decoding
(HLIT/HDIST/HCLEN) decodes a code length tree that describes the literal
and distance trees. Implemented repeat-code expansion (symbols 16/17/18 in
the code-length alphabet, RFC 1951 sec 3.2.7) with explicit error handling
for invalid repeats and oversubscribed lengths.

**Phase 4 (Terminal rendering):** Rather than using third-party rendering
libraries (`chafa`, `viu`, `libsixel`), implemented ANSI escape sequences
directly: `\x1b[38;2;r;g;bm` (foreground), `\x1b[48;2;r;g;bm` (background)
with `▀` half-blocks. Implemented xterm-256 quantization (6x6x6 RGB cube and
24-step grayscale ramp) and Windows console VT mode activation via
`SetConsoleMode` without third-party dependencies.

**Phase 7 (Aspect ratio refinement):** Fixed downscaling calculation in
`term_render.c` so vertical scaling tracks column scaling (`row_scale = 2 * scale_x`),
ensuring correct aspect ratios when `--width` is specified. Added automated
regression coverage in `tools/check_render_aspect.py` (tested via `make test`).
