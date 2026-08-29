# Phase 2b/2c Checkpoint — stored blocks + fixed Huffman + LZ77 verified

Per the plan's 2b checkpoint ("end-to-end decoder on trivial input") and
2c checkpoint ("decode a real DEFLATE stream compressed with fixed
Huffman... zlib only generates the fixture, never ships in your binary").

## What's built

- **`huffman.c`/`.h`** — canonical Huffman build + decode, generic
  (doesn't know about DEFLATE's specific alphabets). This is deliberately
  separated from `inflate.c` because Phase 2d's dynamic trees will reuse
  it completely unchanged — only the *source* of the code lengths differs
  (fixed constants here vs. transmitted-and-decoded in 2d).
- **`inflate.c`/`.h`** — the block loop (BFINAL/BTYPE dispatch), stored
  block handling (2b), fixed-tree construction + LZ77 back-reference
  resolution (2c). BTYPE 10 currently returns
  `INFLATE_ERR_NOT_IMPLEMENTED` (honest placeholder for 2d); BTYPE 11
  is a hard error (reserved, never valid).
- Output buffer doubles as the LZ77 window — no separate circular
  buffer, we just index back into everything decoded so far. Simple and
  clearly correct; revisit only if Phase 6 profiling says the 2048x2048
  test image needs it.

## The MSB-first gotcha

The one detail in this phase most likely to eat an evening if missed:
DEFLATE packs the bitstream least-significant-bit-first for *almost*
everything (LEN/NLEN, extra bits, HLIT/HDIST/HCLEN) — except Huffman
codes themselves, which the spec explicitly packs **most-significant-bit
first**. `bit_reader.c`'s `read_bits()` implements the former;
`huffman_decode()` implements the latter with its own bit-by-bit
MSB-first assembly, on top of the *same* underlying LSB-first byte
stream. Documented prominently in `huffman.c`'s file header so it isn't
rediscovered the hard way during 2d.

## Unit tests (`make check`)

**Huffman layer** (`test_huffman.c`): a 4-symbol canonical tree
(lengths `[2,1,3,3]`) with codes and a 5-symbol encoded sequence
independently derived from a separate Python implementation of
canonical code assignment — not from running this C code. Also covers
oversubscribed lengths, incomplete codes, an out-of-range code length,
a flat equal-length code, and truncated input.

**One test bug caught here too:** my first draft of the truncation test
tried to simulate "2 real bits then cut off" by packing 2 bits into a
1-byte buffer. That doesn't work — a `bit_reader_t` only tracks whole
bytes, so the buffer actually had 8 readable bits, and the zero-padding
past my intended cutoff happened to spell out a real 3-bit code by
coincidence. Fixed by using a genuinely empty (0-byte) buffer instead.
Worth recording because it's the same class of mistake as the Phase 2a
`bytes_remaining` bug: assuming what a helper *should* return instead of
tracing what it actually does.

**Inflate layer** (`test_inflate.c`): four DEFLATE fixtures generated
externally via Python's `zlib` module (raw deflate, negative `wbits`,
never linked into the binary):
- a stored block (`'AB'×10`)
- a fixed-Huffman block with a realistic mix of literals and backrefs
- 300 repeated `'a'` bytes, chosen specifically to force a length-258
  backref (LZ77's max match length, length code 285 — 0 extra bits, the
  top edge of the length table) with distance=1, which forces the
  overlapping-copy path (memcpy would be wrong here; the implementation
  copies byte-by-byte for exactly this reason)
- a 1024-byte chunk repeated after 5000 bytes of filler, forcing a
  backref distance >4096 to exercise the higher-order distance
  extra-bits codes
Plus five hand-corrupted error-path cases: truncated stream, reserved
BTYPE (11), dynamic Huffman (10, confirms the honest not-yet-implemented
status rather than a crash or silent misdecode), and a stored block with
mismatched LEN/NLEN.

```
test_read_u32_be: PASS
test_crc32: PASS
all primitive tests passed
test_read_bit_single_byte: PASS ... (7/7)
all bit reader tests passed
test_decode_known_example: PASS ... (6/6)
all huffman tests passed
test_stored_block: PASS
test_fixed_huffman_block: PASS
test_fixed_huffman_max_length_run: PASS
test_fixed_huffman_large_distance: PASS
test_truncated_stream_fails_cleanly: PASS
test_reserved_btype_fails_cleanly: PASS
test_dynamic_huffman_reports_not_implemented: PASS
test_bad_stored_len_fails_cleanly: PASS
all inflate tests passed
```

## Real-file sanity check (beyond what the plan asked for)

Pulled the actual IDAT stream out of `demo/icon_32x32_rgb.png`, stripped
the zlib wrapper by hand, and ran it raw:

- **The real file uses BTYPE=10 (dynamic Huffman)** — confirms the
  plan's own warning ("most modern encoders default to it for anything
  non-trivial") even on a 32×32 icon. `inflate()` correctly reports
  `INFLATE_ERR_NOT_IMPLEMENTED` rather than crashing or misdecoding —
  Phase 2d isn't optional for real files, as expected.
- **To prove 2c works on real image bytes, not just text fixtures:** took
  that same icon's actual decompressed pixel bytes (3104 bytes) and
  re-compressed them fixed-Huffman-only via `zlib.Z_FIXED`. Our
  `inflate()` decoded the result **byte-for-byte identical** to the
  original pixel data.
- All of the above, plus every unit test, also run clean under
  `-fsanitize=address,undefined` — no leaks, no UB. Valgrind still isn't
  available in this environment (same gap noted since Phase 1); ASan is
  a reasonable stand-in and was run explicitly this phase specifically
  *because* valgrind isn't available, not skipped.

## Known limitation (expected, tracked)

`inflate()` cannot yet decode any real-world PNG's actual IDAT stream
end-to-end, because every demo file (per the pattern above) uses dynamic
Huffman. This is exactly on schedule per the plan — 2d is next — but
worth stating plainly rather than implying more progress than exists:
**Phase 2e's "run against all 5 test PNGs" checkpoint is blocked on 2d.**

**Checkpoint passed** for what 2b/2c scope: stored blocks and fixed
Huffman + LZ77 both verified against hand fixtures, generated fixtures,
and real (re-encoded) image bytes. Ready for 2d (dynamic Huffman) —
`huffman_build()`/`huffman_decode()` need no changes, only a new
"decode the transmitted code lengths" step feeding into the same
`huffman_build()` call fixed trees already use.
