# Scope Lock — do not revisit before ship

Locked at Phase 0. If Hour 40 you're tempted to add one of the "OUT"
items below, don't — open a `FUTURE.md` note instead and keep moving.

## IN scope
- PNG signature + chunk parsing: `IHDR`, `PLTE`(rejected, see below), `IDAT`, `IEND`, `IEND` CRC check
- Bit depth: **8-bit only**
- Color types: **2 (truecolor/RGB)** and **6 (truecolor+alpha/RGBA)** only
- Interlace method: **0 (none)** only
- zlib/DEFLATE decompression of IDAT stream (via zlib or hand-rolled inflate — decide in Phase 1)
- All 5 PNG filter types (None, Sub, Up, Average, Paeth) un-filtering per scanline
- CRC32 validation on chunks (at least IHDR/IDAT/IEND)
- Output: decoded raw RGB/RGBA pixel buffer usable by a simple viewer/dumper

## OUT of scope (reject cleanly, don't silently mishandle)
- Palette / indexed color (color type 3)
- Grayscale (color type 0) and grayscale+alpha (color type 4)
- 16-bit channel depth
- Adam7 interlacing (interlace method 1)
- Ancillary chunks beyond what's needed to skip them safely (tEXt, gAMA, sRGB, etc. — read length, skip, don't parse)
- Animated PNG (APNG)
- Writing/encoding PNGs (decode-only project)

## Enforcement
On encountering an out-of-scope IHDR (bit depth != 8, color type not in {2,6},
or interlace != 0): print a clear "unsupported: <reason>" error and exit
non-zero. Do not attempt partial decode.
