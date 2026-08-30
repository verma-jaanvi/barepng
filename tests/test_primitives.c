#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "png_decoder.h"

static void test_read_u32_be(void) {
    uint8_t a[4] = {0x00, 0x00, 0x00, 0x0D};
    assert(png_read_u32_be(a) == 13);

    uint8_t b[4] = {0x89, 0x50, 0x4E, 0x47};
    assert(png_read_u32_be(b) == 0x89504E47u);

    uint8_t c[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    assert(png_read_u32_be(c) == 0xFFFFFFFFu);

    uint8_t d[4] = {0x00, 0x00, 0x00, 0x00};
    assert(png_read_u32_be(d) == 0u);

    printf("test_read_u32_be: PASS\n");
}

static void test_crc32(void) {
    /* Standard test vector */
    const uint8_t check[] = "123456789";
    uint32_t crc = png_crc32(check, strlen((const char *)check));
    assert(crc == 0xCBF43926u);

    /* Empty input */
    assert(png_crc32(NULL, 0) == 0x00000000u);

    /* IEND chunk type CRC */
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
