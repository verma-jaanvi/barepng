#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "zlib_wrapper.h"

/* Captured zlib stream for "the quick brown fox jumps over the lazy dog" * 3 */
static const uint8_t REAL_ZLIB_STREAM[] = {
    0x78, 0x9c, 0x2b, 0xc9, 0x48, 0x55, 0x28, 0x2c, 0xcd, 0x4c, 0xce, 0x56,
    0x48, 0x2a, 0xca, 0x2f, 0xcf, 0x53, 0x48, 0xcb, 0xaf, 0x50, 0xc8, 0x2a,
    0xcd, 0x2d, 0x28, 0x56, 0xc8, 0x2f, 0x4b, 0x2d, 0x52, 0x28, 0x01, 0x4a,
    0xe7, 0x24, 0x56, 0x55, 0x2a, 0xa4, 0xe4, 0xa7, 0x97, 0xd0, 0x44, 0x29,
    0x00, 0x30, 0xb4, 0x2f, 0xec,
};
static const size_t REAL_ZLIB_STREAM_LEN = sizeof(REAL_ZLIB_STREAM);
static const char *REAL_ZLIB_EXPECTED_TEXT =
    "the quick brown fox jumps over the lazy dogthe quick brown fox "
    "jumps over the lazy dogthe quick brown fox jumps over the lazy dog";

static void test_strip_real_stream(void) {
    const uint8_t *deflate_start;
    size_t deflate_len;
    zlib_wrapper_status_t status =
        zlib_wrapper_strip(REAL_ZLIB_STREAM, REAL_ZLIB_STREAM_LEN,
                            &deflate_start, &deflate_len);
    assert(status == ZLIB_WRAPPER_OK);
    assert(deflate_start == REAL_ZLIB_STREAM + 2);
    assert(deflate_len == REAL_ZLIB_STREAM_LEN - 6);

    printf("test_strip_real_stream: PASS\n");
}

static void test_adler32_matches_reference(void) {
    uint32_t computed = zlib_adler32(
        (const uint8_t *)REAL_ZLIB_EXPECTED_TEXT, strlen(REAL_ZLIB_EXPECTED_TEXT));
    assert(computed == 0x30b42fecu);

    printf("test_adler32_matches_reference: PASS\n");
}

static void test_check_adler32_against_real_trailer(void) {
    zlib_wrapper_status_t status = zlib_wrapper_check_adler32(
        REAL_ZLIB_STREAM, REAL_ZLIB_STREAM_LEN,
        (const uint8_t *)REAL_ZLIB_EXPECTED_TEXT, strlen(REAL_ZLIB_EXPECTED_TEXT));
    assert(status == ZLIB_WRAPPER_OK);

    printf("test_check_adler32_against_real_trailer: PASS\n");
}

static void test_check_adler32_detects_mismatch(void) {
    const char *wrong_text = "not the actual decompressed data at all";
    zlib_wrapper_status_t status = zlib_wrapper_check_adler32(
        REAL_ZLIB_STREAM, REAL_ZLIB_STREAM_LEN,
        (const uint8_t *)wrong_text, strlen(wrong_text));
    assert(status == ZLIB_WRAPPER_ERR_BAD_ADLER32);

    printf("test_check_adler32_detects_mismatch: PASS\n");
}

static void test_bad_compression_method_rejected(void) {
    uint8_t buf[6] = {0x70, 0x03, 0, 0, 0, 0};
    assert(((0x70u << 8 | 0x03u) % 31u) == 0);

    const uint8_t *deflate_start;
    size_t deflate_len;
    zlib_wrapper_status_t status =
        zlib_wrapper_strip(buf, sizeof(buf), &deflate_start, &deflate_len);
    assert(status == ZLIB_WRAPPER_ERR_BAD_HEADER);

    printf("test_bad_compression_method_rejected: PASS\n");
}

static void test_bad_check_bits_rejected(void) {
    uint8_t buf[6] = {0x78, 0x00, 0, 0, 0, 0};
    assert(((0x78u << 8 | 0x00u) % 31u) != 0);

    const uint8_t *deflate_start;
    size_t deflate_len;
    zlib_wrapper_status_t status =
        zlib_wrapper_strip(buf, sizeof(buf), &deflate_start, &deflate_len);
    assert(status == ZLIB_WRAPPER_ERR_BAD_HEADER);

    printf("test_bad_check_bits_rejected: PASS\n");
}

static void test_preset_dictionary_rejected(void) {
    uint8_t buf[6] = {0x78, 0x20, 0, 0, 0, 0};
    assert(((0x78u << 8 | 0x20u) % 31u) == 0);

    const uint8_t *deflate_start;
    size_t deflate_len;
    zlib_wrapper_status_t status =
        zlib_wrapper_strip(buf, sizeof(buf), &deflate_start, &deflate_len);
    assert(status == ZLIB_WRAPPER_ERR_PRESET_DICTIONARY);

    printf("test_preset_dictionary_rejected: PASS\n");
}

static void test_too_short_rejected(void) {
    uint8_t buf[5] = {0x78, 0x9c, 0, 0, 0};
    const uint8_t *deflate_start;
    size_t deflate_len;
    zlib_wrapper_status_t status =
        zlib_wrapper_strip(buf, sizeof(buf), &deflate_start, &deflate_len);
    assert(status == ZLIB_WRAPPER_ERR_TOO_SHORT);

    printf("test_too_short_rejected: PASS\n");
}

int main(void) {
    test_strip_real_stream();
    test_adler32_matches_reference();
    test_check_adler32_against_real_trailer();
    test_check_adler32_detects_mismatch();
    test_bad_compression_method_rejected();
    test_bad_check_bits_rejected();
    test_preset_dictionary_rejected();
    test_too_short_rejected();
    printf("all zlib wrapper tests passed\n");
    return 0;
}
