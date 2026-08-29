#ifndef PNG_UNFILTER_H
#define PNG_UNFILTER_H

/* png_unfilter.h — Phase 3: PNG scanline unfiltering.
 *
 * inflate()'s output for a PNG IDAT stream is not yet raw pixels — each
 * scanline is prefixed with a 1-byte filter type that tells the decoder
 * which of the 5 PNG prediction schemes the encoder applied before
 * compression. This module reverses that prediction row by row, using
 * the already-unfiltered previous scanline as context, and produces a
 * clean pixel buffer that Phase 4 (output) can use directly.
 *
 * Scope (per SCOPE.md): 8-bit only, color types 2 (RGB) and 6 (RGBA),
 * no interlacing. All 5 filter types (0-4) are handled; any other filter
 * byte is rejected cleanly rather than silently mishandled.
 */

#include <stddef.h>
#include <stdint.h>

typedef enum {
    PNG_UNFILTER_OK = 0,
    PNG_UNFILTER_ERR_BAD_FILTER_TYPE, /* filter byte not in [0, 4] */
    PNG_UNFILTER_ERR_BAD_INPUT_SIZE,  /* inflated_len ≠ height * (width*bpp + 1) */
    PNG_UNFILTER_ERR_OUT_OF_MEMORY,
} png_unfilter_status_t;

/* Decoded pixel buffer produced by png_unfilter(). Owns its `pixels`
 * allocation; free with png_pixels_free() when done. */
typedef struct {
    uint8_t *pixels;        /* height * width * bytes_per_pixel bytes, row-major */
    uint32_t width;
    uint32_t height;
    int      bytes_per_pixel; /* 3 for RGB, 4 for RGBA */
} png_pixels_t;

/* Unfilters all scanlines in `inflated` and writes reconstructed pixels
 * into `out->pixels`.
 *
 * `inflated` is inflate()'s raw output: `height` scanlines of
 * (1 + width * bytes_per_pixel) bytes each, filter-type byte first.
 *
 * On PNG_UNFILTER_OK, *out is fully populated and must be freed with
 * png_pixels_free(). On any other status, *out is left zeroed.
 */
png_unfilter_status_t png_unfilter(const uint8_t *inflated, size_t inflated_len,
                                    uint32_t width, uint32_t height,
                                    int bytes_per_pixel,
                                    png_pixels_t *out);

void png_pixels_free(png_pixels_t *p);

const char *png_unfilter_status_str(png_unfilter_status_t status);

#endif /* PNG_UNFILTER_H */
