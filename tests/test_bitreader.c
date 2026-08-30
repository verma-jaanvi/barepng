#include <assert.h>
#include <stdio.h>
#include "bit_reader.h"

static void test_read_bit_single_byte(void) {
    uint8_t buf[1] = {0xA1};
    bit_reader_t br;
    bitreader_init(&br, buf, 1);

    unsigned expected[8] = {1, 0, 0, 0, 0, 1, 0, 1};
    for (int i = 0; i < 8; i++) {
        unsigned bit;
        assert(bitreader_read_bit(&br, &bit) == 0);
        assert(bit == expected[i]);
    }

    /* Out of bounds read returns EOF */
    unsigned bit;
    assert(bitreader_read_bit(&br, &bit) == -1);

    printf("test_read_bit_single_byte: PASS\n");
}

static void test_read_bits_within_byte(void) {
    uint8_t buf[1] = {0xA1};
    bit_reader_t br;
    bitreader_init(&br, buf, 1);

    uint32_t v;
    assert(bitreader_read_bits(&br, 4, &v) == 0);
    assert(v == 0x1u);

    assert(bitreader_read_bits(&br, 4, &v) == 0);
    assert(v == 0xAu);

    printf("test_read_bits_within_byte: PASS\n");
}

static void test_read_bits_across_byte_boundary(void) {
    uint8_t buf[2] = {0xA1, 0x3C};
    bit_reader_t br;
    bitreader_init(&br, buf, 2);

    uint32_t v;
    assert(bitreader_read_bits(&br, 12, &v) == 0);
    assert(v == 0xCA1u);

    assert(bitreader_read_bits(&br, 4, &v) == 0);
    assert(v == 0x3u);

    printf("test_read_bits_across_byte_boundary: PASS\n");
}

static void test_read_bits_failure_is_atomic(void) {
    uint8_t buf[3] = {0xFF, 0xFF, 0xFF};
    bit_reader_t br;
    bitreader_init(&br, buf, 3);

    uint32_t v;
    assert(bitreader_read_bits(&br, 25, &v) == -1);

    /* Position should remain untouched after failed read */
    assert(bitreader_read_bits(&br, 24, &v) == 0);
    assert(v == 0xFFFFFFu);

    printf("test_read_bits_failure_is_atomic: PASS\n");
}

static void test_align_to_byte(void) {
    uint8_t buf[2] = {0xA1, 0x3C};
    bit_reader_t br;
    bitreader_init(&br, buf, 2);

    assert(bitreader_is_byte_aligned(&br));

    unsigned bit;
    assert(bitreader_read_bit(&br, &bit) == 0);
    assert(bitreader_read_bit(&br, &bit) == 0);
    assert(bitreader_read_bit(&br, &bit) == 0);
    assert(!bitreader_is_byte_aligned(&br));

    bitreader_align_to_byte(&br);
    assert(bitreader_is_byte_aligned(&br));

    uint32_t v;
    assert(bitreader_read_bits(&br, 8, &v) == 0);
    assert(v == 0x3Cu);

    /* Idempotent alignment */
    bitreader_align_to_byte(&br);
    assert(bitreader_is_byte_aligned(&br));

    printf("test_align_to_byte: PASS\n");
}

static void test_bytes_remaining(void) {
    uint8_t buf[3] = {0x00, 0x00, 0x00};
    bit_reader_t br;
    bitreader_init(&br, buf, 3);

    assert(bitreader_bytes_remaining(&br) == 3);

    unsigned bit;
    for (int i = 0; i < 8; i++) {
        assert(bitreader_read_bit(&br, &bit) == 0);
    }
    assert(bitreader_bytes_remaining(&br) == 2);

    assert(bitreader_read_bit(&br, &bit) == 0);
    assert(bitreader_bytes_remaining(&br) == 2);

    printf("test_bytes_remaining: PASS\n");
}

static void test_zero_bit_read(void) {
    uint8_t buf[1] = {0xFF};
    bit_reader_t br;
    bitreader_init(&br, buf, 1);

    uint32_t v = 0xDEADBEEF;
    assert(bitreader_read_bits(&br, 0, &v) == 0);
    assert(v == 0u);
    assert(bitreader_is_byte_aligned(&br));

    printf("test_zero_bit_read: PASS\n");
}

int main(void) {
    test_read_bit_single_byte();
    test_read_bits_within_byte();
    test_read_bits_across_byte_boundary();
    test_read_bits_failure_is_atomic();
    test_align_to_byte();
    test_bytes_remaining();
    test_zero_bit_read();
    printf("all bit reader tests passed\n");
    return 0;
}
