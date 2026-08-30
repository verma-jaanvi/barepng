#ifndef PNG_UNFILTER_H
#define PNG_UNFILTER_H

#include <stddef.h>
#include <stdint.h>

/* PNG scanline unfiltering (RFC 2083 / ISO 15948 sec 9). */

typedef enum {
    PNG_UNFILTER_OK = 0,
    PNG_UNFILTER_ERR_BAD_FILTER_TYPE,
    PNG_UNFILTER_ERR_BAD_INPUT_SIZE,
    PNG_UNFILTER_ERR_OUT_OF_MEMORY,
} png_unfilter_status_t;

typedef struct {
    uint8_t *pixels;
    uint32_t width;
    uint32_t height;
    int      bytes_per_pixel;
} png_pixels_t;

/* Reconstruct scanlines from inflated data into contiguous pixel buffer */
png_unfilter_status_t png_unfilter(const uint8_t *inflated, size_t inflated_len,
                                    uint32_t width, uint32_t height,
                                    int bytes_per_pixel,
                                    png_pixels_t *out);

/* Release pixel buffer memory */
void png_pixels_free(png_pixels_t *p);

/* String representation of status code */
const char *png_unfilter_status_str(png_unfilter_status_t status);

#endif /* PNG_UNFILTER_H */
