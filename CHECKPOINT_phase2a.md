# Phase 2a Checkpoint — bit reader verified

Per the plan: "write `read_bit()` and `read_bits(n)` as isolated,
unit-testable functions before Huffman... test against a hand-constructed
byte with known bit pattern."

`bit_reader.c` implements `bitreader_read_bit()` / `bitreader_read_bits()`
plus two things the plan didn't ask for but Huffman (2c/2d) will need
immediately: `bitreader_align_to_byte()` (stored blocks need byte
alignment regardless of bit position) and `bitreader_bytes_remaining()`
(bounds-check before trusting a length field).

## Unit tests (`make check`)

Hand-constructed `0xA1` (`1010 0001`) as the known pattern, confirming
`read_bit()` walks LSB→MSB: `1,0,0,0,0,1,0,1`, not the more "obvious"
MSB-first order a naive port from the container's big-endian reader would
produce.

The one test worth calling out: **`{0xA1, 0x3C}`, `read_bits(&br, 12,
...)` → `0xCA1`.** This value was computed independently in Python
(pack each byte's bits LSB-first, OR bit *i* into position *i* of the
result) rather than by running the C code and copying its output — a
test that derives its expected value from the same logic it's testing
proves nothing. The single-byte 4+4 split test technically wouldn't have
caught an accidental MSB-first (big-endian) implementation, since `0xA1`'s
two nibbles happen to look bit-reversal-symmetric under this particular
byte; the cross-byte 12-bit case is the one that actually rules that out,
since a big-endian reader would produce a different value entirely once a
byte boundary is crossed.

Also covered: atomicity on failure (a `read_bits()` call that runs out of
data mid-read leaves the reader's position completely unchanged, so
retrying isn't operating on a half-consumed stream), `align_to_byte()`
behavior including the already-aligned no-op case, `bytes_remaining()`,
and the `n == 0` edge case.

**Caught one bug in my own test, not the implementation:** an early draft
asserted `bytes_remaining() == 1` after consuming 9 bits (a full byte
plus one) from a 3-byte buffer. Actual/correct value is `2` —
`byte_pos` only advances once `bit_pos` wraps past 7, so 1 bit into byte
1 still leaves `byte_pos == 1`. Good reminder that "isolated unit test"
doesn't mean "trust the first assertion you write" — walked through the
struct fields by hand before deciding which number was wrong.

```
test_read_bit_single_byte: PASS
test_read_bits_within_byte: PASS
test_read_bits_across_byte_boundary: PASS
test_read_bits_failure_is_atomic: PASS
test_align_to_byte: PASS
test_bytes_remaining: PASS
test_zero_bit_read: PASS
all bit reader tests passed
```

Full project (`make all`) still builds clean under
`-Wall -Wextra -Wpedantic`, no new warnings from `bit_reader.c`.

**Checkpoint passed.** Ready for 2b (stored blocks) — `align_to_byte()`
is already there waiting for it — and then 2c (fixed Huffman), which is
the first thing that will actually exercise this reader against a real
DEFLATE stream rather than hand-built test bytes.

*Note: `valgrind` still isn't available in this environment (same gap
noted in the Phase 1 checkpoint) — no dynamic allocation in this module
so the risk is low, but worth a run once a machine with it is available.*
