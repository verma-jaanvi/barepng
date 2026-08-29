# Phase 0 Checkpoint — chunk boundaries by eye

Ran `tools/hexdump demo/icon_32x32_rgb.png` (smallest file, 102 bytes,
so the whole thing fits on screen):

```
00000000  89 50 4e 47 0d 0a 1a 0a  00 00 00 0d 49 48 44 52  |.PNG........IHDR|
00000010  00 00 00 20 00 00 00 20  08 02 00 00 00 fc 18 ed  |... ... ........|
00000020  a3 00 00 00 2d 49 44 41  54 78 9c ed cd b1 09 00  |....-IDATx......|
00000030  20 10 00 b1 13 be f8 fd  27 76 09 ed 02 e9 73 aa  | .......'v....s.|
00000040  6d fe 99 76 ea 23 81 40  20 10 08 04 02 81 40 f0  |m..v.#.@ .....@.|
00000050  ca 05 3b 7f 12 6a 0d 00  68 17 00 00 00 00 49 45  |..;..j..h.....IE|
00000060  4e 44 ae 42 60 82                                 |ND.B`.|
```

## Manual walk (by eye)

- **Signature**: `0x00–0x07` — `89 50 4e 47 0d 0a 1a 0a`, matches spec exactly.
- **IHDR**: length field at `0x08` = `00 00 00 0d` (13, correct — IHDR is
  always 13 bytes). Type `49 48 44 52` = "IHDR" at `0x0C`. Data
  `0x10–0x1C`: width `00 00 00 20`=32, height `00 00 00 20`=32,
  bit depth `08`, color type `02` (truecolor/RGB — in scope), compression
  `00`, filter `00`, interlace `00` (in scope). CRC `0x1D–0x20`.
- **IDAT**: starts right after IHDR's CRC, at `0x21`. Length `00 00 00 2d`
  = 45 bytes. Type "IDAT" at `0x25`. Data `0x29–0x55` is zlib stream
  (starts `78 9c` — standard zlib header, default compression). CRC
  `0x56–0x59`.
- **IEND**: starts `0x5A`. Length `00 00 00 00` (always 0). Type "IEND"
  at `0x5E`. No data. CRC `0x62–0x65` = `ae 42 60 82` (this is the fixed
  IEND CRC — it's the same in every valid PNG since there's no data to vary it).
- File ends at `0x66` = 102 bytes = matches `ls -la` size exactly.

## Cross-check

Verified the by-eye offsets against a throwaway Python `struct.unpack`
walk of the same file — every offset matched:

```
IHDR  chunk@0x08  len= 13  data=[0x10:0x1d)  crc@0x1d-0x20  next@0x21
IDAT  chunk@0x21  len= 45  data=[0x29:0x56)  crc@0x56-0x59  next@0x5a
IEND  chunk@0x5a  len=  0  data=[0x62:0x62)  crc@0x62-0x65  next@0x66
```

**Checkpoint passed.** Chunk boundaries are legible by eye with only
signature knowledge + the 4-byte-length/4-byte-type/data/4-byte-CRC
chunk framing — no parser needed yet. Ready for Phase 1.
