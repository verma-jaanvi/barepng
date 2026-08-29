#include <stdio.h>
#include "png_decoder.h"
#include "zlib_wrapper.h"
#include "inflate.h"

static const char *color_type_name(uint8_t ct) {
    switch (ct) {
        case PNG_COLOR_TYPE_RGB:  return "RGB (truecolor)";
        case PNG_COLOR_TYPE_RGBA: return "RGBA (truecolor+alpha)";
        default:                  return "unknown";
    }
}

/* Channels per pixel for the color types SCOPE.md allows — needed to
 * compute the expected unfiltered byte count below. Grayscale/palette
 * would need a different table entry, but those are out of scope. */
static int channels_for_color_type(uint8_t ct) {
    switch (ct) {
        case PNG_COLOR_TYPE_RGB:  return 3;
        case PNG_COLOR_TYPE_RGBA: return 4;
        default:                  return 0;
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file.png>\n", argv[0]);
        return 1;
    }

    png_container_t container;
    char err[256];
    png_status_t status = png_read_container(argv[1], &container, err, sizeof(err));

    if (status != PNG_OK) {
        fprintf(stderr, "%s: %s\n", argv[0], err);
        return 1;
    }

    printf("%s\n", argv[1]);
    printf("  dimensions : %u x %u\n", container.ihdr.width, container.ihdr.height);
    printf("  color type : %u (%s)\n", container.ihdr.color_type,
           color_type_name(container.ihdr.color_type));
    printf("  bit depth  : %u\n", container.ihdr.bit_depth);
    printf("  chunks read: %u\n", container.chunk_count);
    printf("  IDAT bytes : %zu (concatenated, zlib-wrapped)\n", container.idat_size);

    /* --- Phase 2e: strip the zlib wrapper, inflate, verify --- */

    const uint8_t *deflate_data;
    size_t deflate_len;
    zlib_wrapper_status_t zstatus = zlib_wrapper_strip(
        container.idat_data, container.idat_size, &deflate_data, &deflate_len);
    if (zstatus != ZLIB_WRAPPER_OK) {
        fprintf(stderr, "%s: zlib wrapper error: %s\n", argv[0],
                zlib_wrapper_status_str(zstatus));
        png_container_free(&container);
        return 1;
    }

    inflate_buffer_t inflated;
    inflate_status_t istatus = inflate(deflate_data, deflate_len, &inflated);
    if (istatus != INFLATE_OK) {
        fprintf(stderr, "%s: inflate error: %s\n", argv[0],
                inflate_status_str(istatus));
        png_container_free(&container);
        return 1;
    }

    printf("  inflated   : %zu bytes\n", inflated.size);

    zstatus = zlib_wrapper_check_adler32(container.idat_data, container.idat_size,
                                          inflated.data, inflated.size);
    printf("  Adler-32   : %s\n",
           zstatus == ZLIB_WRAPPER_OK ? "verified" : zlib_wrapper_status_str(zstatus));

    /* Phase 2e checkpoint: output byte count must equal
     * height x (width x channels + 1) — one filter-type byte plus the
     * raw pixel bytes per scanline, before any unfiltering (Phase 3)
     * has happened. This is purely a size check on inflate()'s output;
     * it says nothing yet about whether the pixel bytes themselves are
     * correctly positioned (that's what unfiltering will need). */
    int channels = channels_for_color_type(container.ihdr.color_type);
    size_t expected_size = (size_t)container.ihdr.height *
                            ((size_t)container.ihdr.width * (size_t)channels + 1);
    int size_ok = (inflated.size == expected_size);
    printf("  size check : %zu expected, %zu actual -> %s\n",
           expected_size, inflated.size, size_ok ? "MATCH" : "MISMATCH");

    inflate_buffer_free(&inflated);
    png_container_free(&container);

    if (zstatus != ZLIB_WRAPPER_OK || !size_ok) {
        return 1;
    }
    return 0;
}
