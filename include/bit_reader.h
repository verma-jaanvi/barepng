#ifndef BIT_READER_H
#define BIT_READER_H

#include <stddef.h>
#include <stdint.h>

/* bit_reader.c — Phase 2a: LSB-first bit reader over an in-memory buffer.
 *
 * DEFLATE (RFC 1951, sec 3.1.1) packs bits into bytes "starting with the
 * least-significant bit of the byte." Concretely: for a single byte
 * 0bABCDEFGH (H is bit 0 / LSB), the bits are consumed in the order
 * H, G, F, E, D, C, B, A — i.e. read_bit() returns bit 0 first, then
 * bit 1, and so on, walking up toward bit 7 before moving to the next
 * byte.
 *
 * This is the foundation every later Huffman/LZ77 step reads through, so
 * it's kept deliberately small and tested against hand-constructed byte
 * patterns *before* anything in Phase 2b+ is written on top of it.
 */

typedef struct {
    const uint8_t *data;
    size_t         len;      /* total bytes in the buffer */
    size_t         byte_pos; /* index of the byte currently being read */
    int            bit_pos;  /* 0..7: which bit of data[byte_pos] is next,
                                 0 = least-significant bit */
} bit_reader_t;

/* Binds a bit_reader_t to a buffer. Does not copy or take ownership;
 * `data` must outlive the reader. */
void bitreader_init(bit_reader_t *br, const uint8_t *data, size_t len);

/* Reads a single bit LSB-first. On success, writes 0 or 1 to *out_bit and
 * returns 0. Returns -1 (leaving *out_bit and the reader's position
 * untouched) if the buffer is exhausted — this is the signal for
 * "truncated/corrupt compressed stream," not a crash. */
int bitreader_read_bit(bit_reader_t *br, unsigned *out_bit);

/* Reads n bits (0 <= n <= 32), LSB-first, and packs them so the first bit
 * read becomes bit 0 of the result, the second bit read becomes bit 1,
 * and so on. This is the exact convention DEFLATE uses for packed
 * multi-bit values (e.g. HLIT/HDIST/HCLEN, extra-length/distance bits) —
 * NOT big-endian, and not the order a naive "shift left, OR in" reader
 * would produce.
 *
 * On success, writes the value to *out and returns 0. On failure (fewer
 * than n bits remain), returns -1 and leaves both *out and the reader's
 * position unchanged (the read is all-or-nothing, so a caller never ends
 * up in a half-consumed state after an error). */
int bitreader_read_bits(bit_reader_t *br, int n, uint32_t *out);

/* Discards any partially-read byte, advancing to the start of the next
 * byte. No-op if already byte-aligned. Needed before a stored (BTYPE=00)
 * block's LEN/NLEN fields, which are byte-aligned per spec regardless of
 * where the preceding bit stream left off. */
void bitreader_align_to_byte(bit_reader_t *br);

/* True if the reader is currently sitting exactly on a byte boundary
 * (bit_pos == 0). Mostly useful in tests/assertions. */
int bitreader_is_byte_aligned(const bit_reader_t *br);

/* Number of whole bytes remaining from the current byte position to the
 * end of the buffer (does not account for a partially-consumed current
 * byte — it's a coarse "how much data is left" check, e.g. before
 * reading a stored block's declared LEN). */
size_t bitreader_bytes_remaining(const bit_reader_t *br);

/* Copies `len` raw bytes into dst, advancing the reader by `len` bytes.
 * Only valid when the reader is currently byte-aligned (call
 * bitreader_align_to_byte() first) — this is the bulk-copy path a
 * stored (BTYPE=00) block's data uses once past LEN/NLEN, as opposed to
 * the bit-by-bit reads everything else in DEFLATE needs.
 * Returns -1 (leaving the reader's position unchanged) if not currently
 * byte-aligned, or if fewer than `len` bytes remain. */
int bitreader_read_raw_bytes(bit_reader_t *br, uint8_t *dst, size_t len);

#endif /* BIT_READER_H */
