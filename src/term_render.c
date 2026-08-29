/* term_render.c — Phase 4: terminal image renderer.
 *
 * Half-block technique: U+2580 UPPER HALF BLOCK (▀, UTF-8: E2 96 80)
 * fills the top half of a cell. With foreground = top pixel and
 * background = bottom pixel, one terminal row displays two image rows.
 *
 * Rendering modes (auto-detected, or --mode= override):
 *   TRUECOLOR : \x1b[38;2;r;g;bm\x1b[48;2;r;g;bm▀   (24-bit)
 *   256COLOR  : \x1b[38;5;Nm\x1b[48;5;Nm▀            (xterm-256)
 *   ASCII     : one luminance-mapped ASCII char per pair of image rows
 *
 * All modes emit \x1b[0m at line ends to prevent color bleed into the prompt.
 */
#include "term_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#  include <io.h>       /* _isatty, _fileno */
#  ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#    define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#  endif
#else
#  include <sys/ioctl.h>
#  include <unistd.h>
#endif

/* -----------------------------------------------------------------------
 * Platform: terminal width
 * --------------------------------------------------------------------- */

static int get_terminal_cols(void) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        int cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        return (cols > 0) ? cols : 80;
    }
    return 80;
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return (int)ws.ws_col;
    }
    return 80;
#endif
}

/* -----------------------------------------------------------------------
 * Platform: stdout tty check
 * --------------------------------------------------------------------- */

static int stdout_is_tty(void) {
#ifdef _WIN32
    return _isatty(_fileno(stdout));
#else
    return isatty(STDOUT_FILENO);
#endif
}

/* -----------------------------------------------------------------------
 * Windows: enable VT processing + UTF-8 output.
 * Returns 1 if VT was successfully enabled (implies truecolor support),
 * 0 if legacy console (fallback to 256/ASCII).
 * On non-Windows this is a no-op that always returns 1.
 * --------------------------------------------------------------------- */

static int enable_vt_and_utf8(void) {
#ifdef _WIN32
    SetConsoleOutputCP(65001); /* UTF-8 so ▀ renders correctly */
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (!GetConsoleMode(h, &mode)) {
        return 0; /* not a console handle — piped */
    }
    if (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) {
        return 1; /* already on */
    }
    return SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) ? 1 : 0;
#else
    return 1;
#endif
}

/* -----------------------------------------------------------------------
 * Color mode detection.
 * Priority: explicit $COLORTERM → $TERM suffix → Windows VT → fallback.
 * --------------------------------------------------------------------- */

static term_mode_t detect_mode(int vt_ok) {
    /* $COLORTERM is the canonical hint for 24-bit support */
    const char *ct = getenv("COLORTERM");
    if (ct) {
        if (strcmp(ct, "truecolor") == 0 || strcmp(ct, "24bit") == 0) {
            return TERM_MODE_TRUECOLOR;
        }
        if (strcmp(ct, "256color") == 0) {
            return TERM_MODE_256COLOR;
        }
    }

    /* $TERM often encodes color depth */
    const char *term = getenv("TERM");
    if (term) {
        if (strstr(term, "256color")) return TERM_MODE_256COLOR;
        if (strstr(term, "truecolor")) return TERM_MODE_TRUECOLOR;
    }

    /* Windows Terminal / modern ConHost: VT succeeded → truecolor */
    if (vt_ok && stdout_is_tty()) {
        return TERM_MODE_TRUECOLOR;
    }

    /* Piped output: ASCII is the only mode that works without escapes */
    if (!stdout_is_tty()) {
        return TERM_MODE_ASCII;
    }

    /* Unknown tty with no COLORTERM hint: 256-color is the conservative safe bet */
    return TERM_MODE_256COLOR;
}

/* -----------------------------------------------------------------------
 * Public: term_render_detect
 * --------------------------------------------------------------------- */

term_render_opts_t term_render_detect(void) {
    int vt_ok = enable_vt_and_utf8();
    term_render_opts_t opts;
    opts.max_cols  = get_terminal_cols();
    opts.bg_r      = 128;
    opts.bg_g      = 128;
    opts.bg_b      = 128;
    opts.mode      = detect_mode(vt_ok);
    return opts;
}

const char *term_mode_name(term_mode_t mode) {
    switch (mode) {
        case TERM_MODE_TRUECOLOR: return "truecolor";
        case TERM_MODE_256COLOR:  return "256color";
        case TERM_MODE_ASCII:     return "ascii";
        default:                  return "unknown";
    }
}

/* -----------------------------------------------------------------------
 * Alpha compositing: composite one channel against a solid background.
 * src_alpha=255 → fully opaque src, =0 → fully opaque bg.
 * +127 rounds to nearest integer (avoids systematic darkening).
 * --------------------------------------------------------------------- */

static inline uint8_t composite(uint8_t src, uint8_t alpha, uint8_t bg) {
    return (uint8_t)(((unsigned)alpha * src +
                      (unsigned)(255 - alpha) * bg + 127u) / 255u);
}

/* -----------------------------------------------------------------------
 * Sample one pixel from the buffer, compositing alpha if present.
 * Clamps (x, y) to image bounds (nearest-neighbor downscale safety).
 * Outputs composited (r, g, b) via pointer arguments.
 * --------------------------------------------------------------------- */

static void sample_pixel(const png_pixels_t *px,
                          uint32_t x, uint32_t y,
                          uint8_t bg_r, uint8_t bg_g, uint8_t bg_b,
                          uint8_t *out_r, uint8_t *out_g, uint8_t *out_b) {
    /* clamp to image bounds */
    if (x >= px->width)  x = px->width  - 1;
    if (y >= px->height) y = px->height - 1;

    size_t idx = ((size_t)y * px->width + x) * (size_t)px->bytes_per_pixel;
    uint8_t r = px->pixels[idx + 0];
    uint8_t g = px->pixels[idx + 1];
    uint8_t b = px->pixels[idx + 2];

    if (px->bytes_per_pixel == 4) {
        uint8_t a = px->pixels[idx + 3];
        r = composite(r, a, bg_r);
        g = composite(g, a, bg_g);
        b = composite(b, a, bg_b);
    }

    *out_r = r;
    *out_g = g;
    *out_b = b;
}

/* -----------------------------------------------------------------------
 * 256-color quantization: map (r, g, b) to the closest xterm-256 index.
 *
 * The xterm-256 palette:
 *   0-15:   system colors — skip (too terminal-dependent to predict)
 *   16-231: 6×6×6 RGB cube; index = 16 + 36*ri + 6*gi + bi
 *           where ri = round(r * 5 / 255) etc.
 *   232-255: 24-step grayscale from #080808 to #eeeeee
 *
 * For grayscale inputs we pick from the 24-step ramp (fine resolution);
 * for chromatic inputs the 6×6×6 cube is close enough for a viewport.
 * --------------------------------------------------------------------- */

static int rgb_to_256(uint8_t r, uint8_t g, uint8_t b) {
    /* If all channels are close to each other, use the gray ramp
     * (steps of 10, starting at 8: 8, 18, 28, … 238).
     * The gray ramp starts at color 232 and has 24 entries. */
    int ir = r, ig = g, ib = b;
    int avg = (ir + ig + ib) / 3;
    if (abs(ir - avg) <= 12 && abs(ig - avg) <= 12 && abs(ib - avg) <= 12) {
        /* map avg [0-255] to gray index [232-255] */
        if (avg < 8)   return 16;  /* closest cube black */
        if (avg > 238) return 231; /* closest cube white */
        return 232 + (avg - 8) * 24 / 240;
    }

    /* RGB cube: each channel snapped to nearest of 6 levels
     * (0, 95, 135, 175, 215, 255). Approximate: ri = r * 5 / 255. */
    int ri = (ir * 5 + 127) / 255;
    int gi = (ig * 5 + 127) / 255;
    int bi = (ib * 5 + 127) / 255;
    return 16 + 36 * ri + 6 * gi + bi;
}

/* -----------------------------------------------------------------------
 * ASCII luminance ramp.
 * BT.601 luma: 0.299R + 0.587G + 0.114B (integer approximation).
 * Ramp chosen dark→light so a bright pixel → dense character.
 * --------------------------------------------------------------------- */

static const char ASCII_RAMP[] = " .:-=+*#%@";
#define ASCII_RAMP_LEN ((int)(sizeof(ASCII_RAMP) - 1))  /* exclude NUL */

static char luminance_char(uint8_t r, uint8_t g, uint8_t b) {
    unsigned lum = (299u * r + 587u * g + 114u * b) / 1000u;
    int idx = (int)(lum * (unsigned)(ASCII_RAMP_LEN - 1) / 255u);
    return ASCII_RAMP[idx];
}

/* -----------------------------------------------------------------------
 * ▀ as raw UTF-8 bytes (U+2580 = E2 96 80).
 * Written via fwrite to bypass any locale/encoding layer in printf.
 * --------------------------------------------------------------------- */

static const uint8_t UPPER_HALF_BLOCK[3] = {0xE2, 0x96, 0x80};

static void write_half_block(void) {
    fwrite(UPPER_HALF_BLOCK, 1, 3, stdout);
}

/* -----------------------------------------------------------------------
 * Main render entry point.
 * --------------------------------------------------------------------- */

void term_render(const png_pixels_t *px, const term_render_opts_t *opts) {
    if (!px || !px->pixels || px->width == 0 || px->height == 0) return;

    /* --- compute output dimensions ---
     * Fit image width into available columns (nearest-neighbor).
     * Each terminal column = one pixel column (downscaled).
     * Each terminal row    = two image rows (half-block). */
    int max_cols = (opts->max_cols > 0) ? opts->max_cols : 80;
    uint32_t out_cols = (px->width < (uint32_t)max_cols)
                        ? px->width
                        : (uint32_t)max_cols;

    /* step size in image pixels per output column/row-pair */
    /* Use fixed-point scaling to avoid float: multiply image coords by
     * SCALE_DENOM before dividing, so we stay in integer arithmetic. */
    uint32_t scale_x = (px->width  << 16) / out_cols; /* 16.16 fixed-point */
    uint32_t scale_y = 2; /* half-block: each terminal row = 2 image rows */

    uint32_t out_rows = (px->height + 1) / 2; /* ceiling division */

    uint8_t bg_r = opts->bg_r, bg_g = opts->bg_g, bg_b = opts->bg_b;

    for (uint32_t yt = 0; yt < out_rows; yt++) {
        uint32_t img_row_top = yt * scale_y;
        uint32_t img_row_bot = img_row_top + 1;
        /* clamp bottom row for odd-height images */
        if (img_row_bot >= px->height) img_row_bot = px->height - 1;

        for (uint32_t xt = 0; xt < out_cols; xt++) {
            /* nearest-neighbor: map output column xt to image column */
            uint32_t img_col = (xt * scale_x + (scale_x >> 1)) >> 16;
            if (img_col >= px->width) img_col = px->width - 1;

            uint8_t tr, tg, tb; /* top (foreground) pixel */
            uint8_t br, bg_p, bb; /* bottom (background) pixel */

            sample_pixel(px, img_col, img_row_top, bg_r, bg_g, bg_b, &tr, &tg, &tb);
            sample_pixel(px, img_col, img_row_bot,  bg_r, bg_g, bg_b, &br, &bg_p, &bb);

            switch (opts->mode) {
                case TERM_MODE_TRUECOLOR:
                    /* fg = top, bg = bottom */
                    printf("\x1b[38;2;%u;%u;%um\x1b[48;2;%u;%u;%um",
                           (unsigned)tr, (unsigned)tg, (unsigned)tb,
                           (unsigned)br, (unsigned)bg_p, (unsigned)bb);
                    write_half_block();
                    break;

                case TERM_MODE_256COLOR: {
                    int fg256 = rgb_to_256(tr, tg, tb);
                    int bg256 = rgb_to_256(br, bg_p, bb);
                    printf("\x1b[38;5;%dm\x1b[48;5;%dm", fg256, bg256);
                    write_half_block();
                    break;
                }

                case TERM_MODE_ASCII:
                    /* Average top and bottom rows for a single luminance char */
                    putchar(luminance_char(
                        (uint8_t)((tr + br) / 2),
                        (uint8_t)((tg + bg_p) / 2),
                        (uint8_t)((tb + bb) / 2)));
                    break;
            }
        }

        /* Reset colors at end of every line — don't bleed into prompt */
        if (opts->mode != TERM_MODE_ASCII) {
            fputs("\x1b[0m", stdout);
        }
        putchar('\n');
    }

    fflush(stdout);
}
