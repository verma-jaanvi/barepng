/* bit_reader.c — Phase 2a: LSB-first bit reader.
 *
 * Deliberately the smallest possible thing that could work: no lookahead
 * buffer, no caching multiple bits at once. Huffman decode (2c/2d) will
 * want something faster, but correctness here is what everything else in
 * Phase 2 depends on — get it right and simple first, optimize later if
 * profiling in Phase 6 says so.
 */
#include "bit_reader.h"

#include <string.h>

void bitreader_init(bit_reader_t *br, const uint8_t *data, size_t len) {
    br->data = data;
    br->len = len;
    br->byte_pos = 0;
    br->bit_pos = 0;
}

int bitreader_read_bit(bit_reader_t *br, unsigned *out_bit) {
    if (br->byte_pos >= br->len) {
        return -1; /* out of data — caller treats this as truncated stream */
    }

    unsigned bit = (br->data[br->byte_pos] >> br->bit_pos) & 1u;

    br->bit_pos++;
    if (br->bit_pos == 8) {
        br->bit_pos = 0;
        br->byte_pos++;
    }

    *out_bit = bit;
    return 0;
}

int bitreader_read_bits(bit_reader_t *br, int n, uint32_t *out) {
    if (n < 0 || n > 32) {
        return -1;
    }
    if (n == 0) {
        *out = 0;
        return 0;
    }

    /* Snapshot position so a mid-read failure is fully rolled back —
     * callers should never have to reason about a partially-consumed
     * read on the error path. */
    size_t save_byte = br->byte_pos;
    int save_bit = br->bit_pos;

    uint32_t value = 0;
    for (int i = 0; i < n; i++) {
        unsigned bit;
        if (bitreader_read_bit(br, &bit) != 0) {
            br->byte_pos = save_byte;
            br->bit_pos = save_bit;
            return -1;
        }
        /* First bit read is the least-significant bit of the result —
         * this is the DEFLATE packing convention, see bit_reader.h. */
        value |= ((uint32_t)bit) << i;
    }

    *out = value;
    return 0;
}

void bitreader_align_to_byte(bit_reader_t *br) {
    if (br->bit_pos != 0) {
        br->bit_pos = 0;
        br->byte_pos++;
    }
}

int bitreader_is_byte_aligned(const bit_reader_t *br) {
    return br->bit_pos == 0;
}

size_t bitreader_bytes_remaining(const bit_reader_t *br) {
    return (br->byte_pos < br->len) ? (br->len - br->byte_pos) : 0;
}

int bitreader_read_raw_bytes(bit_reader_t *br, uint8_t *dst, size_t len) {
    if (br->bit_pos != 0) {
        return -1; /* caller forgot to align_to_byte() first */
    }
    if (len > bitreader_bytes_remaining(br)) {
        return -1;
    }
    memcpy(dst, br->data + br->byte_pos, len);
    br->byte_pos += len;
    return 0;
}
