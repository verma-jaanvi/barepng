/* PNG scanline unfiltering: None, Sub, Up, Average, Paeth. */
#include "png_unfilter.h"

#include <stdlib.h>
#include <string.h>

static inline int iabs(int x) {
    return x < 0 ? -x : x;
}

/* Paeth predictor (RFC 2083 sec 9.4) */
static inline uint8_t paeth_predictor(uint8_t a, uint8_t b, uint8_t c) {
    int p  = (int)a + (int)b - (int)c;
    int pa = iabs(p - (int)a);
    int pb = iabs(p - (int)b);
    int pc = iabs(p - (int)c);

    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc)             return b;
    return c;
}

png_unfilter_status_t png_unfilter(const uint8_t *inflated, size_t inflated_len,
                                    uint32_t width, uint32_t height,
                                    int bytes_per_pixel,
                                    png_pixels_t *out) {
    memset(out, 0, sizeof(*out));

    if (height == 0 || width == 0 || bytes_per_pixel <= 0) {
        return PNG_UNFILTER_ERR_BAD_INPUT_SIZE;
    }

    /* Stride per scanline in pixel buffer */
    if ((size_t)width > SIZE_MAX / (size_t)bytes_per_pixel) {
        return PNG_UNFILTER_ERR_BAD_INPUT_SIZE;
    }
    size_t stride = (size_t)width * (size_t)bytes_per_pixel;
    if (stride > SIZE_MAX - 1) {
        return PNG_UNFILTER_ERR_BAD_INPUT_SIZE;
    }

    size_t scanline_len = stride + 1;
    if ((size_t)height > SIZE_MAX / scanline_len) {
        return PNG_UNFILTER_ERR_BAD_INPUT_SIZE;
    }
    size_t expected_len = (size_t)height * scanline_len;
    if (inflated_len != expected_len) {
        return PNG_UNFILTER_ERR_BAD_INPUT_SIZE;
    }

    if ((size_t)height > SIZE_MAX / stride) {
        return PNG_UNFILTER_ERR_OUT_OF_MEMORY;
    }
    size_t pixel_buf_size = (size_t)height * stride;

    uint8_t *pixels = (uint8_t *)calloc(1, pixel_buf_size);
    if (!pixels) {
        return PNG_UNFILTER_ERR_OUT_OF_MEMORY;
    }

    /* Prior row buffer for row 0 */
    uint8_t *zeros = (uint8_t *)calloc(stride, 1);
    if (!zeros) {
        free(pixels);
        return PNG_UNFILTER_ERR_OUT_OF_MEMORY;
    }

    png_unfilter_status_t status = PNG_UNFILTER_OK;
    int bpp = bytes_per_pixel;

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *src  = inflated + y * (stride + 1);
        uint8_t        filt = src[0];
        const uint8_t *row  = src + 1;
        uint8_t       *dst  = pixels + y * stride;
        const uint8_t *prev = (y == 0) ? zeros : (pixels + (y - 1) * stride);

        switch (filt) {
            case 0: /* None */
                memcpy(dst, row, stride);
                break;

            case 1: /* Sub */
                for (size_t x = 0; x < stride; x++) {
                    uint8_t a = (x >= (size_t)bpp) ? dst[x - bpp] : 0;
                    dst[x] = row[x] + a;
                }
                break;

            case 2: /* Up */
                for (size_t x = 0; x < stride; x++) {
                    dst[x] = row[x] + prev[x];
                }
                break;

            case 3: /* Average */
                for (size_t x = 0; x < stride; x++) {
                    uint8_t a = (x >= (size_t)bpp) ? dst[x - bpp] : 0;
                    uint8_t b = prev[x];
                    dst[x] = row[x] + (uint8_t)(((uint16_t)a + (uint16_t)b) >> 1);
                }
                break;

            case 4: /* Paeth */
                for (size_t x = 0; x < stride; x++) {
                    uint8_t a = (x >= (size_t)bpp) ? dst[x - bpp]       : 0;
                    uint8_t b = prev[x];
                    uint8_t c = (x >= (size_t)bpp) ? prev[x - bpp]      : 0;
                    dst[x] = row[x] + paeth_predictor(a, b, c);
                }
                break;

            default:
                status = PNG_UNFILTER_ERR_BAD_FILTER_TYPE;
                goto done;
        }
    }

done:
    free(zeros);

    if (status != PNG_UNFILTER_OK) {
        free(pixels);
        memset(out, 0, sizeof(*out));
        return status;
    }

    out->pixels          = pixels;
    out->width           = width;
    out->height          = height;
    out->bytes_per_pixel = bytes_per_pixel;
    return PNG_UNFILTER_OK;
}

void png_pixels_free(png_pixels_t *p) {
    if (!p) return;
    free(p->pixels);
    memset(p, 0, sizeof(*p));
}

const char *png_unfilter_status_str(png_unfilter_status_t status) {
    switch (status) {
        case PNG_UNFILTER_OK:                  return "OK";
        case PNG_UNFILTER_ERR_BAD_FILTER_TYPE: return "unknown filter type";
        case PNG_UNFILTER_ERR_BAD_INPUT_SIZE:  return "inflated size mismatch";
        case PNG_UNFILTER_ERR_OUT_OF_MEMORY:   return "out of memory";
        default:                               return "unknown unfilter status";
    }
}
