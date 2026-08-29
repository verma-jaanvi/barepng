/* png_unfilter.c — Phase 3: PNG scanline unfiltering.
 *
 * PNG filter method 0 defines 5 filter types (per RFC 2083 / ISO 15948
 * sec 9). Filtering is applied per-scanline, per-byte, using previously
 * decoded bytes as predictors. The decoder reverses each filter using:
 *   - x    : the current filtered byte
 *   - a    : the decoded byte `bpp` positions to the left in the CURRENT row
 *            (zero if x is within the first `bpp` bytes)
 *   - b    : the decoded byte directly above (same column, previous row;
 *            zero for row 0)
 *   - c    : the decoded byte diagonally above-left (zero at boundaries)
 *
 * Key invariant: `prev` always points at fully-reconstructed pixel data
 * (either an all-zeros sentinel for row 0, or the previously written row
 * in the output pixel buffer). We never touch the inflated input again
 * once we've read the filter byte and the filtered bytes for that row.
 */
#include "png_unfilter.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------
 * Paeth predictor (RFC 2083 sec 9.4, verbatim reference algorithm).
 *
 * Returns whichever of a (left), b (above), c (above-left) is nearest
 * to the linear prediction p = a + b - c. Integer-only arithmetic;
 * the abs() calls expand to a handful of instructions with -O2.
 *
 * The spec's exact wording: "return the input value closest to p" with
 * ties broken in order a, b, c. We follow that order precisely — getting
 * the tie-break wrong would silently corrupt images that happen to hit it.
 * ------------------------------------------------------------------- */

static inline int iabs(int x) { return x < 0 ? -x : x; }

static inline uint8_t paeth_predictor(uint8_t a, uint8_t b, uint8_t c) {
    int p  = (int)a + (int)b - (int)c;
    int pa = iabs(p - (int)a);
    int pb = iabs(p - (int)b);
    int pc = iabs(p - (int)c);
    /* ties: a wins over b, b wins over c */
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc)             return b;
    return c;
}

/* ---------------------------------------------------------------------
 * Main entry point
 * ------------------------------------------------------------------- */

png_unfilter_status_t png_unfilter(const uint8_t *inflated, size_t inflated_len,
                                    uint32_t width, uint32_t height,
                                    int bytes_per_pixel,
                                    png_pixels_t *out) {
    memset(out, 0, sizeof(*out));

    /* stride = bytes per row in the pixel buffer (no filter byte) */
    size_t stride = (size_t)width * (size_t)bytes_per_pixel;

    /* Each scanline in the inflated buffer is 1 (filter byte) + stride.
     * The total must match exactly — any mismatch means the inflate output
     * is wrong or the caller passed incorrect dimensions. */
    size_t expected_len = (size_t)height * (stride + 1);
    if (inflated_len != expected_len) {
        return PNG_UNFILTER_ERR_BAD_INPUT_SIZE;
    }

    /* Edge case: 0-pixel image is trivially valid and produces no output. */
    if (height == 0 || width == 0) {
        return PNG_UNFILTER_OK;
    }

    uint8_t *pixels = (uint8_t *)malloc((size_t)height * stride);
    if (!pixels) {
        return PNG_UNFILTER_ERR_OUT_OF_MEMORY;
    }

    /* All-zeros sentinel used as the "previous row" for row 0.
     * Allocated on the heap (not the stack) so that very wide images
     * (stride up to ~8000 bytes for a 2000px RGBA row) don't risk a
     * stack-size issue on constrained platforms. */
    uint8_t *zeros = (uint8_t *)calloc(stride, 1);
    if (!zeros) {
        free(pixels);
        return PNG_UNFILTER_ERR_OUT_OF_MEMORY;
    }

    png_unfilter_status_t status = PNG_UNFILTER_OK;
    int bpp = bytes_per_pixel;

    for (uint32_t y = 0; y < height; y++) {
        /* Pointer to this scanline's data in the inflated buffer:
         * filter_byte | filtered pixel bytes [0 .. stride-1] */
        const uint8_t *src  = inflated + y * (stride + 1);
        uint8_t        filt = src[0];
        const uint8_t *row  = src + 1; /* filtered bytes for this scanline */

        /* Destination row in the output pixel buffer. */
        uint8_t *dst  = pixels + y * stride;

        /* Previously-reconstructed row (zeros for y==0, else the row
         * we just wrote at y-1). This is NOT the raw inflated bytes —
         * it's always already-unfiltered data, which is exactly what
         * the Up/Average/Paeth predictors require. */
        const uint8_t *prev = (y == 0) ? zeros : (pixels + (y - 1) * stride);

        switch (filt) {
            case 0: /* None — filtered bytes ARE the pixel bytes */
                memcpy(dst, row, stride);
                break;

            case 1: /* Sub — prediction: byte `bpp` positions to the left */
                for (size_t x = 0; x < stride; x++) {
                    uint8_t a = (x >= (size_t)bpp) ? dst[x - bpp] : 0;
                    dst[x] = row[x] + a;
                }
                break;

            case 2: /* Up — prediction: byte directly above */
                for (size_t x = 0; x < stride; x++) {
                    dst[x] = row[x] + prev[x];
                }
                break;

            case 3: /* Average — prediction: floor((left + above) / 2) */
                for (size_t x = 0; x < stride; x++) {
                    uint8_t a = (x >= (size_t)bpp) ? dst[x - bpp] : 0;
                    uint8_t b = prev[x];
                    /* Cast to unsigned to avoid signed-overflow UB;
                     * the sum fits in uint16_t, the shift is exact. */
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

    out->pixels         = pixels;
    out->width          = width;
    out->height         = height;
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
        case PNG_UNFILTER_OK:                   return "OK";
        case PNG_UNFILTER_ERR_BAD_FILTER_TYPE:  return "unknown filter type (byte not in 0-4)";
        case PNG_UNFILTER_ERR_BAD_INPUT_SIZE:   return "inflated size does not match image dimensions";
        case PNG_UNFILTER_ERR_OUT_OF_MEMORY:    return "out of memory";
        default:                                return "unknown png_unfilter status";
    }
}
