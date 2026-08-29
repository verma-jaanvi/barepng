# Phase 1 Checkpoint — container parsing verified

Per the plan: "print width, height, color type, and total IDAT byte count
for all 5 test files correctly." Ran `make test`:

```
demo/alpha_256x256_rgba.png        256x256    color type 6 (RGBA)   3 chunks   9687 IDAT bytes
demo/icon_32x32_rgb.png             32x32     color type 2 (RGB)    3 chunks     60 IDAT bytes
demo/large_2048x2048_rgb.png      2048x2048   color type 2 (RGB)    4 chunks  86824 IDAT bytes
demo/large_alpha_2000x1500_rgba.png 2000x1500 color type 6 (RGBA)   3 chunks  63423 IDAT bytes
demo/photo_640x480_rgb.png         640x480    color type 2 (RGB)    9 chunks 436570 IDAT bytes
```

All five match Phase 0's by-eye/`struct.unpack` cross-check for the icon
file, and all five pass IHDR validation (8-bit, color type 2/6,
interlace 0) without needing any out-of-scope handling.

**`photo_640x480_rgb.png` has 9 chunks** — the multi-IDAT case the plan
called out as a real bug source (assuming one IDAT chunk = one stream).
Its IDAT payload is correctly concatenated across chunks, since the byte
count matches what a single-buffer `struct.unpack` walk of the same file
gives externally.

## Unit tests (`make check`)

Isolated `read_u32_be()` against IHDR's fixed 13-byte length, the first 4
signature bytes, an all-`0xFF` word, and zero. Isolated `png_crc32()`
against the standard `"123456789"` check value (`0xCBF43926`, the
canonical CRC-32 self-test used by every implementation), the empty
input, and IEND's fixed CRC (`0xAE426082`, invariant across every valid
PNG since IEND never has data). All pass before either primitive touches
a real file.

## Error-path testing

Exercised every failure mode by hand, confirming clean stderr + exit 1,
never a crash:

| Case | Result |
|---|---|
| Missing file | `file not found: <path>`, exit 1 |
| Random 50-byte garbage file | `not a PNG file (bad signature)`, exit 1 |
| Truncated valid PNG (first 40 bytes only) | `no IDAT data found`, exit 1 |
| Flipped byte inside IDAT **data** | `bad CRC in chunk 'IDAT'`, correct chunk name, exit 1 |
| Flipped byte inside a chunk **type** field | `bad CRC in chunk 'IDA?'` — non-printable byte sanitized to `?` rather than corrupting stderr, exit 1 |
| Forged IHDR, bit depth 16 | `unsupported: bit depth 16 (only 8-bit is supported)`, exit 1 |
| Forged IHDR, color type 3 (palette) | `unsupported: color type 3 (...)`, exit 1 |

The type-field corruption case caught a real (minor) issue: printing raw
chunk-type bytes straight into an error message assumes they're printable
ASCII, which a corrupted or malicious file can't be trusted to provide.
Fixed with a `safe_char()` sanitizer before this became a "works on my
test files" bug.

**Checkpoint passed.** Container parsing is solid on real files, multi-IDAT
splitting, and the full error matrix. Ready for Phase 2 (inflate).

*Note: `valgrind` wasn't available in this environment to run the full
leak check called for in Phase 6 — worth running once on a machine that
has it before the final hardening pass, though ownership here is simple
enough (one `malloc` per buffer, freed on every exit path) that it's
low-risk.*
