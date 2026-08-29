# Phase 2d/2e Checkpoint — dynamic Huffman + real-file integration

Per the plan: this is explicitly "your single highest-risk milestone."
**`git tag dynamic-huffman-working` is set** (commit `dynamic-huffman-working`),
achieved against all 5 demo files, not just the smallest — see below.

## What's built

- **2d**: `decode_dynamic_block()` in `inflate.c` — HLIT/HDIST/HCLEN
  header parsing, the 19-symbol code-length alphabet (built via the
  *same* `huffman_build()` fixed trees already use — this is the "tree
  describing another tree" the plan flags as needing real thinking
  time), repeat codes 16/17/18, then the literal/length and distance
  trees, then a call straight into `decode_huffman_block()` — completely
  unchanged from Phase 2c, exactly as the plan says: "decode using the
  same LZ77 copy logic."
- **A `huffman.c` fix this phase depended on**: DEFLATE explicitly allows
  one specific "incomplete" canonical code — an alphabet with 0 or 1
  symbols in use (RFC 1951 sec 3.2.7's "one distance code... encoded
  using one bit, not zero bits"). `huffman_build()` previously rejected
  *all* incompleteness; it now allows this narrow case and nothing
  broader. This wasn't optional — real dynamic blocks with a single
  distance value hit it immediately.
- **2e**: `zlib_wrapper.c` (new, small module) strips the RFC 1950
  header, validates it (compression method, check bits, rejects preset
  dictionaries), and verifies the trailing Adler-32 against `inflate()`'s
  actual output. `main.c` now runs the full pipeline — container parse →
  zlib strip → inflate → Adler-32 check → the Phase 2e byte-count
  checkpoint — and prints all of it.

## Unit tests

34 total across 5 binaries (`make check`), up from 22 at the 2b/2c
checkpoint:
- **Huffman** (+3 net): the old single-symbol "incomplete" test was
  actually testing the wrong thing once the spec's real behavior was
  understood — replaced with a genuine multi-symbol incompleteness case
  (still correctly an error) plus two new tests for the DEFLATE-legal
  exception itself (0-symbol and 1-symbol alphabets), including
  confirming the *unused* code in a 1-symbol tree is still correctly
  rejected if a corrupt stream ever emits it.
- **Inflate** (+1 net): the old "dynamic Huffman not implemented" test
  is obviously obsolete now — replaced with a real dynamic-Huffman
  fixture (400 randomly-repeated words, generated with zlib's *default*
  strategy specifically so it picks BTYPE=10, confirmed at generation
  time) plus a truncated-header case.
- **zlib_wrapper** (new, 8 tests): header validation, Adler-32 against a
  real Python-generated reference value, and the various malformed-input
  paths (bad compression method, bad check bits, preset dictionary,
  too-short buffer).

**Two more of my own test-authoring bugs caught this round** (same
pattern as Phase 2a/2c — worth tracking since it's now 4 for 4 on "trace
what the code actually does, don't assume"):
1. A single-symbol-tree test tried to prove the *unused* code gets
   rejected using a 1-byte (8-bit) buffer — but `huffman_decode()`
   searches up to `HUFFMAN_MAX_BITS` (15) before concluding "no match,"
   so an 8-bit buffer hits `TRUNCATED_INPUT` before it can reach
   `INVALID_SYMBOL`. Fixed by supplying 16 bits.
2. A zlib-wrapper test hand-picked an `FLG` byte (`0x0b`) to pair with a
   bad `CMF`, asserting it satisfied the header check-bits equation —
   without actually computing it. It didn't. The test's own sanity-check
   assertion (`assert((...) % 31 == 0)`) is exactly what caught this;
   fixed by computing the real value in Python (`0x03`) instead of
   guessing. I also caught myself hand-typing a compressed byte array
   from memory for the same test file before running it — regenerated it
   properly from Python rather than trusting the transcription.

## Real-file checkpoint (Phase 2e's actual ask)

Ran the full pipeline against all 5 demo PNGs — not just the smallest:

| file | dimensions | color | inflated size | Adler-32 | size check |
|---|---|---|---|---|---|
| icon_32x32_rgb.png | 32×32 | RGB | 3,104 B | verified | MATCH |
| photo_640x480_rgb.png | 640×480 | RGB | 922,080 B | verified | MATCH |
| alpha_256x256_rgba.png | 256×256 | RGBA | 262,400 B | verified | MATCH |
| large_2048x2048_rgb.png | 2048×2048 | RGB | 12,584,960 B | verified | MATCH |
| large_alpha_2000x1500_rgba.png | 2000×1500 | RGBA | 12,001,500 B | verified | MATCH |

Every file's inflated byte count matches `height × (width×channels + 1)`
exactly (Phase 2e's stated checkpoint), **and** every file's Adler-32
checksum — computed by whatever originally encoded the PNG, entirely
independent of any logic in this decoder — matches. That second signal
matters more than it might look: the size check alone could pass with
badly-scrambled pixel data (right total length, wrong content); Adler-32
matching means the actual bytes are correct too, well before Phase 3
(unfiltering) will make that visually obvious.

The multi-IDAT photo file (9 chunks, flagged back in the Phase 1
checkpoint) and both 12MB+ images went through cleanly, meaning BFINAL
looping across many blocks — and in practice a mix of block types, since
real encoders switch between stored/fixed/dynamic based on what
compresses best locally — is confirmed working, not just theoretically
correct.

**Performance, unoptimized:** 2048×2048 in 70.6ms, 2000×1500 RGBA in
64.9ms, the 9-chunk photo in 22.5ms. Already comfortably inside Phase
6's "sub-second" target with zero profiling done yet — a good sign, but
not a reason to skip Phase 6's pass later if it's needed for other
files.

**Everything above also re-run under `-fsanitize=address,undefined`**
against the three largest/trickiest files — clean, no leaks, no UB.

## Rollback point

```
git tag dynamic-huffman-working
```
set on this commit, per the plan's explicit instruction. If Phase 3
(unfiltering) or later work needs to backtrack, this is the point to
return to — a fully working decoder through inflate, verified against
real files two independent ways (size + checksum).

**Checkpoint passed**, ahead of what 2e strictly asked (all 5 files
rather than a single smallest-file smoke test). Ready for Phase 3
(unfiltering) — `inflate()`'s output for each file is exactly the raw
material Phase 3 needs: `height` scanlines of `1 + width×channels` bytes
each, filter-type byte first, confirmed by the size check above.
