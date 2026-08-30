/* LSB-first bit reader implementation. */
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
        return -1;
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

    /* Save state for atomic rollback on failure */
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
    if (br->bit_pos != 0 || len > bitreader_bytes_remaining(br)) {
        return -1;
    }
    memcpy(dst, br->data + br->byte_pos, len);
    br->byte_pos += len;
    return 0;
}
