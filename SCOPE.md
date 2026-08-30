# Scope Lock

Locked scope definitions for the decoder implementation.

## IN scope
- PNG signature and chunk parsing: `IHDR`, `IDAT`, `IEND`, chunk CRC check
- Bit depth: **8-bit only**
- Color types: **2 (truecolor/RGB)** and **6 (truecolor+alpha/RGBA)** only
- Interlace method: **0 (none)** only
- zlib/DEFLATE decompression of IDAT stream
- All 5 PNG filter types (None, Sub, Up, Average, Paeth) un-filtering per scanline
- CRC-32 validation on chunks (IHDR/IDAT/IEND)
- Output: decoded raw RGB/RGBA pixel buffer for terminal rendering and stats

## OUT of scope (rejected cleanly with error exit)
- Palette / indexed color (color type 3)
- Grayscale (color type 0) and grayscale+alpha (color type 4)
- 16-bit channel depth
- Adam7 interlacing (interlace method 1)
- Ancillary chunks beyond what is needed to skip safely (tEXt, gAMA, sRGB, etc.)
- Animated PNG (APNG)
- Writing / encoding PNGs (decode-only project)

## Enforcement
On encountering an unsupported IHDR (bit depth != 8, color type not in {2,6},
or interlace != 0): print a clear "unsupported: <reason>" error and exit
with non-zero code.
