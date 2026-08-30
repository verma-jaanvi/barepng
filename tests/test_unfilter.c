#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "png_unfilter.h"

/* Filter 0 (None): identity pass-through */
static void test_none_filter(void) {
    uint8_t inflated[13] = {
        0x00,
        0x10, 0x20, 0x30,
        0x40, 0x50, 0x60,
        0x70, 0x80, 0x90,
        0xA0, 0xB0, 0xC0,
    };
    uint8_t expected[12] = {
        0x10, 0x20, 0x30,
        0x40, 0x50, 0x60,
        0x70, 0x80, 0x90,
        0xA0, 0xB0, 0xC0,
    };

    png_pixels_t px;
    png_unfilter_status_t s = png_unfilter(inflated, sizeof(inflated),
                                            4, 1, 3, &px);
    assert(s == PNG_UNFILTER_OK);
    assert(px.width  == 4);
    assert(px.height == 1);
    assert(px.bytes_per_pixel == 3);
    assert(memcmp(px.pixels, expected, sizeof(expected)) == 0);
    png_pixels_free(&px);
    printf("test_none_filter: PASS\n");
}

/* Filter 1 (Sub): add left neighbor */
static void test_sub_filter(void) {
    uint8_t inflated[10] = {
        0x01,
        0x10, 0x00, 0x00,
        0x02, 0x00, 0x00,
        0x03, 0x00, 0x00,
    };
    uint8_t expected[9] = {
        0x10, 0x00, 0x00,
        0x12, 0x00, 0x00,
        0x15, 0x00, 0x00,
    };

    png_pixels_t px;
    png_unfilter_status_t s = png_unfilter(inflated, sizeof(inflated),
                                            3, 1, 3, &px);
    assert(s == PNG_UNFILTER_OK);
    assert(memcmp(px.pixels, expected, sizeof(expected)) == 0);
    png_pixels_free(&px);
    printf("test_sub_filter: PASS\n");
}

/* Filter 2 (Up): add above row */
static void test_up_filter(void) {
    uint8_t inflated[14] = {
        /* row 0: filter=None */
        0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60,
        /* row 1: filter=Up */
        0x02, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    };
    uint8_t expected[12] = {
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60,
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
    };

    png_pixels_t px;
    png_unfilter_status_t s = png_unfilter(inflated, sizeof(inflated),
                                            2, 2, 3, &px);
    assert(s == PNG_UNFILTER_OK);
    assert(memcmp(px.pixels, expected, sizeof(expected)) == 0);
    png_pixels_free(&px);
    printf("test_up_filter: PASS\n");
}

/* Filter 3 (Average): add average of left and above */
static void test_average_filter(void) {
    uint8_t inflated[8] = {
        0x00, 0x10, 0x20, 0x30,
        0x03, 0x05, 0x04, 0x03,
    };
    uint8_t expected[6] = {
        0x10, 0x20, 0x30,
        0x0D, 0x14, 0x1B,
    };

    png_pixels_t px;
    png_unfilter_status_t s = png_unfilter(inflated, sizeof(inflated),
                                            1, 2, 3, &px);
    assert(s == PNG_UNFILTER_OK);
    assert(memcmp(px.pixels, expected, sizeof(expected)) == 0);
    png_pixels_free(&px);
    printf("test_average_filter: PASS\n");
}

/* Filter 4 (Paeth predictor) */
static void test_paeth_filter(void) {
    uint8_t inflated[14] = {
        0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60,
        0x04, 0x01, 0x02, 0x03, 0x01, 0x02, 0x03,
    };
    uint8_t expected[12] = {
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60,
        0x11, 0x22, 0x33, 0x41, 0x52, 0x63,
    };

    png_pixels_t px;
    png_unfilter_status_t s = png_unfilter(inflated, sizeof(inflated),
                                            2, 2, 3, &px);
    assert(s == PNG_UNFILTER_OK);
    assert(memcmp(px.pixels, expected, sizeof(expected)) == 0);
    png_pixels_free(&px);
    printf("test_paeth_filter: PASS\n");
}

static void test_bad_filter_type(void) {
    uint8_t inflated[4] = {
        0x05,
        0x00, 0x00, 0x00,
    };
    png_pixels_t px;
    png_unfilter_status_t s = png_unfilter(inflated, sizeof(inflated),
                                            1, 1, 3, &px);
    assert(s == PNG_UNFILTER_ERR_BAD_FILTER_TYPE);
    assert(px.pixels == NULL);
    printf("test_bad_filter_type: PASS\n");
}

static void test_bad_input_size(void) {
    uint8_t inflated[3] = {0x00, 0xAA, 0xBB};
    png_pixels_t px;
    png_unfilter_status_t s = png_unfilter(inflated, sizeof(inflated),
                                            1, 1, 3, &px);
    assert(s == PNG_UNFILTER_ERR_BAD_INPUT_SIZE);
    assert(px.pixels == NULL);
    printf("test_bad_input_size: PASS\n");
}

static void test_mixed_filter_rows(void) {
    uint8_t inflated[20] = {
        0x00, 0x10, 0x20, 0x30, 0x40,
        0x01, 0x01, 0x01, 0x01, 0x01,
        0x02, 0x10, 0x10, 0x10, 0x10,
        0x03, 0x05, 0x05, 0x05, 0x05,
    };
    uint8_t expected[16] = {
        0x10, 0x20, 0x30, 0x40,
        0x01, 0x01, 0x01, 0x01,
        0x11, 0x11, 0x11, 0x11,
        0x0D, 0x0D, 0x0D, 0x0D,
    };

    png_pixels_t px;
    png_unfilter_status_t s = png_unfilter(inflated, sizeof(inflated),
                                            1, 4, 4, &px);
    assert(s == PNG_UNFILTER_OK);
    assert(px.width  == 1);
    assert(px.height == 4);
    assert(px.bytes_per_pixel == 4);
    assert(memcmp(px.pixels, expected, sizeof(expected)) == 0);
    png_pixels_free(&px);
    printf("test_mixed_filter_rows: PASS\n");
}

static void test_rgb_vs_rgba_bpp(void) {
    uint8_t inflated_rgb[7] = {
        0x01,
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60,
    };
    uint8_t expected_rgb[6] = {
        0x10, 0x20, 0x30,
        0x50, 0x70, 0x90,
    };

    uint8_t inflated_rgba[5] = {
        0x01,
        0x10, 0x20, 0x30, 0x40,
    };
    uint8_t expected_rgba[4] = {
        0x10, 0x20, 0x30, 0x40,
    };

    png_pixels_t px;
    png_unfilter_status_t s;

    s = png_unfilter(inflated_rgb, sizeof(inflated_rgb), 2, 1, 3, &px);
    assert(s == PNG_UNFILTER_OK);
    assert(memcmp(px.pixels, expected_rgb, sizeof(expected_rgb)) == 0);
    png_pixels_free(&px);

    s = png_unfilter(inflated_rgba, sizeof(inflated_rgba), 1, 1, 4, &px);
    assert(s == PNG_UNFILTER_OK);
    assert(memcmp(px.pixels, expected_rgba, sizeof(expected_rgba)) == 0);
    png_pixels_free(&px);

    printf("test_rgb_vs_rgba_bpp: PASS\n");
}

int main(void) {
    test_none_filter();
    test_sub_filter();
    test_up_filter();
    test_average_filter();
    test_paeth_filter();
    test_bad_filter_type();
    test_bad_input_size();
    test_mixed_filter_rows();
    test_rgb_vs_rgba_bpp();
    printf("all unfilter tests passed\n");
    return 0;
}
