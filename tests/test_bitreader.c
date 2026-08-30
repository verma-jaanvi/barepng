/* test_bitreader.c - isolated tests for the Phase 2a bit reader, checked
 * against hand-constructed byte patterns (values cross-checked with an
 * independent Python script, not just derived from the same C logic
 * being tested - see comments below for the reasoning).
 */
#include <assert.h>
#include <stdio.h>
#include "bit_reader.h"

/* 0xA1 = 1010 0001. LSB-first means read_bit() walks from bit 0 (LSB)
 * up to bit 7 (MSB): 1,0,0,0,0,1,0,1. */
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

    /* buffer exhausted: next read must fail cleanly, not crash or wrap */
    unsigned bit;
    assert(bitreader_read_bit(&br, &bit) == -1);

    printf("test_read_bit_single_byte: PASS\n");
}

/* Same 0xA1 byte, read as two 4-bit groups instead of eight single bits.
 * First group packs bits 0..3 of the byte (1,0,0,0) -> 0b0001 = 1.
 * Second group packs bits 4..7 (0,1,0,1) -> 0b1010 = 10 (0xA).
 * Note this is NOT the same as splitting 0xA1 into its two nibbles
 * (0x1 and 0xA happen to match here only because 0xA1's nibbles are
 * themselves bit-reversal-symmetric under this grouping; the 12-bit
 * cross-byte test below is the one that actually rules out an
 * accidental big-endian implementation). */
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

/* Cross-byte read: {0xA1, 0x3C}, read 12 bits in one call. Expected value
 * 0xCA1 (3233 decimal) was computed independently in Python by packing
 * each byte's bits LSB-first and OR-ing bit i into position i of the
 * result - the same definition as the header's contract, derived
 * separately from this C implementation so the test can't just be
 * confirming its own logic. This also confirms the reader correctly
 * advances byte_pos/bit_pos across the boundary rather than only working
 * within a single byte. */
static void test_read_bits_across_byte_boundary(void) {
    uint8_t buf[2] = {0xA1, 0x3C};
    bit_reader_t br;
    bitreader_init(&br, buf, 2);

    uint32_t v;
    assert(bitreader_read_bits(&br, 12, &v) == 0);
    assert(v == 0xCA1u);

    /* remaining 4 bits (top nibble of 0x3C, LSB-first) */
    assert(bitreader_read_bits(&br, 4, &v) == 0);
    assert(v == 0x3u);

    printf("test_read_bits_across_byte_boundary: PASS\n");
}

/* read_bits must be all-or-nothing: a failed read leaves the reader's
 * position exactly where it was, so a caller retrying (or falling back
 * to bit-by-bit reads) never sees a half-consumed stream. */
static void test_read_bits_failure_is_atomic(void) {
    uint8_t buf[3] = {0xFF, 0xFF, 0xFF}; /* 24 bits total */
    bit_reader_t br;
    bitreader_init(&br, buf, 3);

    uint32_t v;
    assert(bitreader_read_bits(&br, 25, &v) == -1); /* only 24 bits exist */

    /* position must be untouched: a 24-bit read should still succeed
     * and consume the whole buffer */
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
    assert(!bitreader_is_byte_aligned(&br)); /* 3 bits into byte 0 */

    bitreader_align_to_byte(&br);
    assert(bitreader_is_byte_aligned(&br));

    /* must now be at the start of byte 1 (0x3C), not still in byte 0 */
    uint32_t v;
    assert(bitreader_read_bits(&br, 8, &v) == 0);
    assert(v == 0x3Cu);

    /* aligning when already aligned is a no-op */
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
    assert(bitreader_bytes_remaining(&br) == 2); /* consumed exactly byte 0 */

    assert(bitreader_read_bit(&br, &bit) == 0); /* 1 bit into byte 1 */
    /* byte_pos only advances once bit_pos wraps past 7, so 1 bit into
     * byte 1 still leaves byte_pos == 1 - bytes_remaining is coarse by
     * design (counts from byte_pos, ignores a partially-read byte), so
     * this is still "2" here, not "1". */
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
    assert(bitreader_is_byte_aligned(&br)); /* consumed nothing */

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
