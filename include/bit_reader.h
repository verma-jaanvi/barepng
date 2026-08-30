#ifndef BIT_READER_H
#define BIT_READER_H

#include <stddef.h>
#include <stdint.h>

/* LSB-first bit reader over an in-memory buffer (RFC 1951 sec 3.1.1). */

typedef struct {
    const uint8_t *data;
    size_t         len;      /* Total buffer size in bytes */
    size_t         byte_pos; /* Current byte index */
    int            bit_pos;  /* Current bit index (0..7, 0 = LSB) */
} bit_reader_t;

/* Initialize reader over a buffer. Buffer must outlive reader. */
void bitreader_init(bit_reader_t *br, const uint8_t *data, size_t len);

/* Read a single bit (LSB-first). Returns 0 on success, -1 on EOF. */
int bitreader_read_bit(bit_reader_t *br, unsigned *out_bit);

/* Read n bits (0 <= n <= 32) LSB-first. Returns 0 on success, -1 on EOF.
 * On failure, the read position is preserved (atomic). */
int bitreader_read_bits(bit_reader_t *br, int n, uint32_t *out);

/* Advance reader to the next byte boundary if not already aligned. */
void bitreader_align_to_byte(bit_reader_t *br);

/* Check if reader is currently byte-aligned. */
int bitreader_is_byte_aligned(const bit_reader_t *br);

/* Return number of unread whole bytes remaining. */
size_t bitreader_bytes_remaining(const bit_reader_t *br);

/* Read raw bytes directly from stream. Must be byte-aligned.
 * Returns 0 on success, -1 if unaligned or EOF. */
int bitreader_read_raw_bytes(bit_reader_t *br, uint8_t *dst, size_t len);

#endif /* BIT_READER_H */
