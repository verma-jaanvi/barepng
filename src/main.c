/* main.c — Phase 5: CLI surface.
 *
 * Usage:
 *   imgview <file.png> [options]
 *
 * Options:
 *   --width N              cap render width to N columns (default: terminal width)
 *   --mode=truecolor|256|ascii  override color mode auto-detection
 *   --info                 print file/decode stats only, skip render
 *   --help                 print usage and exit 0
 *
 * Exit codes:
 *   0  success (render completed or --info printed)
 *   1  any error (file not found, bad PNG, bad CRC, unsupported format, ...)
 *
 * All error messages go to stderr; stdout carries only the render output
 * (and info lines in --info mode) so piped usage works cleanly.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>   /* clock() for decode timing */

#include "png_decoder.h"
#include "zlib_wrapper.h"
#include "inflate.h"
#include "png_unfilter.h"
#include "term_render.h"

/* -----------------------------------------------------------------------
 * Human-readable helpers
 * --------------------------------------------------------------------- */

static const char *color_type_name(uint8_t ct) {
    switch (ct) {
        case PNG_COLOR_TYPE_RGB:  return "RGB (truecolor)";
        case PNG_COLOR_TYPE_RGBA: return "RGBA (truecolor+alpha)";
        default:                  return "unknown";
    }
}

static int channels_for_color_type(uint8_t ct) {
    switch (ct) {
        case PNG_COLOR_TYPE_RGB:  return 3;
        case PNG_COLOR_TYPE_RGBA: return 4;
        default:                  return 0;
    }
}

/* Format a byte count as "X.Y MB" or "X KB" for the --info line. */
static void fmt_bytes(char *buf, size_t bufsz, size_t n) {
    if (n >= 1024 * 1024) {
        snprintf(buf, bufsz, "%.1f MB", (double)n / (1024.0 * 1024.0));
    } else if (n >= 1024) {
        snprintf(buf, bufsz, "%zu KB", n / 1024);
    } else {
        snprintf(buf, bufsz, "%zu B", n);
    }
}

/* -----------------------------------------------------------------------
 * --help
 * --------------------------------------------------------------------- */

static void print_help(const char *prog) {
    printf("Usage: %s <file.png> [options]\n", prog);
    printf("\n");
    printf("Decode and render a PNG file in the terminal.\n");
    printf("\n");
    printf("Options:\n");
    printf("  --width N              cap render width to N columns\n");
    printf("                         (default: terminal width)\n");
    printf("  --mode=truecolor       force 24-bit truecolor output\n");
    printf("  --mode=256             force xterm-256 color output\n");
    printf("  --mode=ascii           force ASCII luminance ramp output\n");
    printf("  --info                 print decode stats only, no render\n");
    printf("  --help                 print this help and exit\n");
    printf("\n");
    printf("Supported PNG types: 8-bit RGB (color type 2) and RGBA (color type 6).\n");
    printf("Unsupported files (palette, grayscale, 16-bit, interlaced) exit with\n");
    printf("an explicit error message — never a crash.\n");
}

/* -----------------------------------------------------------------------
 * Argument parsing
 * --------------------------------------------------------------------- */

typedef struct {
    const char *filename;   /* required positional arg */
    int         info_only;  /* --info */
    int         width;      /* --width N, 0 = auto */
    int         mode_set;   /* 1 if --mode= was given */
    term_mode_t mode;
    int         help;
} cli_args_t;

/* Returns 0 on success, -1 on parse error (message already printed). */
static int parse_args(int argc, char **argv, cli_args_t *out) {
    memset(out, 0, sizeof(*out));
    const char *prog = argv[0];

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            out->help = 1;
            return 0;
        }
        if (strcmp(a, "--info") == 0) {
            out->info_only = 1;
            continue;
        }
        if (strcmp(a, "--mode=truecolor") == 0) { out->mode = TERM_MODE_TRUECOLOR; out->mode_set = 1; continue; }
        if (strcmp(a, "--mode=256")       == 0) { out->mode = TERM_MODE_256COLOR;  out->mode_set = 1; continue; }
        if (strcmp(a, "--mode=ascii")     == 0) { out->mode = TERM_MODE_ASCII;     out->mode_set = 1; continue; }

        if (strcmp(a, "--width") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: --width requires a numeric argument\n", prog);
                return -1;
            }
            char *end;
            long v = strtol(argv[++i], &end, 10);
            if (*end != '\0' || v <= 0 || v > 65535) {
                fprintf(stderr, "%s: --width: invalid value '%s' (must be 1-65535)\n", prog, argv[i]);
                return -1;
            }
            out->width = (int)v;
            continue;
        }

        /* Anything starting with '--' that we didn't recognise is an error. */
        if (a[0] == '-' && a[1] == '-') {
            fprintf(stderr, "%s: unknown option '%s'\n", prog, a);
            fprintf(stderr, "Run '%s --help' for usage.\n", prog);
            return -1;
        }

        /* First non-option arg is the filename. */
        if (!out->filename) {
            out->filename = a;
        } else {
            fprintf(stderr, "%s: unexpected argument '%s'\n", prog, a);
            fprintf(stderr, "Run '%s --help' for usage.\n", prog);
            return -1;
        }
    }
    return 0;
}

/* -----------------------------------------------------------------------
 * Main
 * --------------------------------------------------------------------- */

int main(int argc, char **argv) {
    const char *prog = argv[0];

    /* --- parse args --- */
    cli_args_t args;
    if (parse_args(argc, argv, &args) != 0) {
        return 1;
    }
    if (args.help) {
        print_help(prog);
        return 0;
    }
    if (!args.filename) {
        fprintf(stderr, "%s: no input file specified\n", prog);
        fprintf(stderr, "Run '%s --help' for usage.\n", prog);
        return 1;
    }

    /* --- auto-detect terminal, then apply any overrides --- */
    term_render_opts_t opts = term_render_detect();
    if (args.mode_set)  opts.mode     = args.mode;
    if (args.width > 0) opts.max_cols = args.width;

    /* ----------------------------------------------------------------
     * Phase 1: parse PNG container
     * ---------------------------------------------------------------- */
    png_container_t container;
    char err[256];
    png_status_t pstatus = png_read_container(args.filename, &container, err, sizeof(err));
    if (pstatus != PNG_OK) {
        /* All error messages are already human-readable from png_container.c.
         * Re-emit on stderr with the program name prefix. */
        fprintf(stderr, "%s: %s\n", prog, err);
        return 1;
    }

    int channels = channels_for_color_type(container.ihdr.color_type);

    /* ----------------------------------------------------------------
     * Phase 2: inflate (timed for --info's performance claim)
     * ---------------------------------------------------------------- */
    const uint8_t *deflate_data;
    size_t deflate_len;
    zlib_wrapper_status_t zstatus = zlib_wrapper_strip(
        container.idat_data, container.idat_size, &deflate_data, &deflate_len);
    if (zstatus != ZLIB_WRAPPER_OK) {
        fprintf(stderr, "%s: %s: bad zlib stream: %s\n", prog, args.filename,
                zlib_wrapper_status_str(zstatus));
        png_container_free(&container);
        return 1;
    }

    clock_t t0 = clock();

    inflate_buffer_t inflated;
    inflate_status_t istatus = inflate(deflate_data, deflate_len, &inflated);
    if (istatus != INFLATE_OK) {
        fprintf(stderr, "%s: %s: inflate failed: %s\n", prog, args.filename,
                inflate_status_str(istatus));
        png_container_free(&container);
        return 1;
    }

    clock_t t1 = clock();
    double inflate_ms = (double)(t1 - t0) * 1000.0 / CLOCKS_PER_SEC;

    /* Adler-32 verification (integrity check — fail loudly if bad) */
    zstatus = zlib_wrapper_check_adler32(container.idat_data, container.idat_size,
                                          inflated.data, inflated.size);
    if (zstatus != ZLIB_WRAPPER_OK) {
        fprintf(stderr, "%s: %s: Adler-32 mismatch — data may be corrupt\n",
                prog, args.filename);
        inflate_buffer_free(&inflated);
        png_container_free(&container);
        return 1;
    }

    size_t expected_size = (size_t)container.ihdr.height *
                            ((size_t)container.ihdr.width * (size_t)channels + 1);
    if (inflated.size != expected_size) {
        fprintf(stderr, "%s: %s: inflated size %zu does not match expected %zu\n",
                prog, args.filename, inflated.size, expected_size);
        inflate_buffer_free(&inflated);
        png_container_free(&container);
        return 1;
    }

    /* ----------------------------------------------------------------
     * Phase 3: unfilter
     * ---------------------------------------------------------------- */
    png_pixels_t pixels;
    png_unfilter_status_t ustatus = png_unfilter(
        inflated.data, inflated.size,
        container.ihdr.width, container.ihdr.height, channels,
        &pixels);

    inflate_buffer_free(&inflated);

    if (ustatus != PNG_UNFILTER_OK) {
        fprintf(stderr, "%s: %s: unfilter failed: %s\n", prog, args.filename,
                png_unfilter_status_str(ustatus));
        png_container_free(&container);
        return 1;
    }

    /* ----------------------------------------------------------------
     * --info output (always printed, render skipped when --info given)
     * ---------------------------------------------------------------- */
    char idat_fmt[32], pixel_fmt[32];
    fmt_bytes(idat_fmt, sizeof(idat_fmt), container.idat_size);
    fmt_bytes(pixel_fmt, sizeof(pixel_fmt), (size_t)pixels.width * pixels.height *
                                             (size_t)pixels.bytes_per_pixel);

    printf("%s\n", args.filename);
    printf("  dimensions  : %u x %u\n", container.ihdr.width, container.ihdr.height);
    printf("  color type  : %s\n", color_type_name(container.ihdr.color_type));
    printf("  bit depth   : %u\n", container.ihdr.bit_depth);
    printf("  chunks      : %u\n", container.chunk_count);
    printf("  IDAT stream : %s compressed\n", idat_fmt);
    printf("  pixel buffer: %s uncompressed\n", pixel_fmt);
    printf("  decode time : %.1f ms (inflate + unfilter)\n", inflate_ms);
    if (!args.info_only) {
        printf("  render mode : %s\n", term_mode_name(opts.mode));
    }

    png_container_free(&container);

    if (args.info_only) {
        png_pixels_free(&pixels);
        return 0;
    }

    /* ----------------------------------------------------------------
     * Phase 4: render
     * ---------------------------------------------------------------- */
    printf("\n");
    term_render(&pixels, &opts);

    png_pixels_free(&pixels);
    return 0;
}
