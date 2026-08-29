/* test_primitives.c — isolated tests for the two Phase 1 primitives that
 * are easy to get subtly wrong: the big-endian u32 reader and CRC-32.
 * No framework, no dependencies — just asserts, per project philosophy.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "png_decoder.h"

static void test_read_u32_be(void) {
    uint8_t a[4] = {0x00, 0x00, 0x00, 0x0D};
    assert(png_read_u32_be(a) == 13);              /* IHDR's fixed length */

    uint8_t b[4] = {0x89, 0x50, 0x4E, 0x47};
    assert(png_read_u32_be(b) == 0x89504E47u);      /* first 4 signature bytes */

    uint8_t c[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    assert(png_read_u32_be(c) == 0xFFFFFFFFu);      /* max value, no overflow */

    uint8_t d[4] = {0x00, 0x00, 0x00, 0x00};
    assert(png_read_u32_be(d) == 0u);

    printf("test_read_u32_be: PASS\n");
}

static void test_crc32(void) {
    /* Known-answer test: CRC-32 (PNG/zlib variant) of the ASCII bytes
     * "123456789" is the standard check value 0xCBF43926, used by every
     * implementation's test suite for this exact reason. */
    const uint8_t check[] = "123456789";
    uint32_t crc = png_crc32(check, strlen((const char *)check));
    assert(crc == 0xCBF43926u);

    /* Empty input must return the identity value (init XOR final-xor
     * cancel out with no bytes processed). */
    assert(png_crc32(NULL, 0) == 0x00000000u);

    /* Sanity: an IEND chunk (type, no data) has a fixed, well-known CRC
     * because it never varies — good regression anchor. */
    const uint8_t iend_type[4] = {'I', 'E', 'N', 'D'};
    uint32_t iend_crc = png_crc32(iend_type, 4);
    assert(iend_crc == 0xAE426082u);

    printf("test_crc32: PASS\n");
}

int main(void) {
    test_read_u32_be();
    test_crc32();
    printf("all primitive tests passed\n");
    return 0;
}
