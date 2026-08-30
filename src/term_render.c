/* Terminal image renderer: ANSI half-block (U+2580) truecolor/256/ASCII. */
#include "term_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#  include <io.h>
#  ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#    define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#  endif
#else
#  include <sys/ioctl.h>
#  include <unistd.h>
#endif

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

static int stdout_is_tty(void) {
#ifdef _WIN32
    return _isatty(_fileno(stdout));
#else
    return isatty(STDOUT_FILENO);
#endif
}

static int enable_vt_and_utf8(void) {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (!GetConsoleMode(h, &mode)) {
        return 0;
    }
    if (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) {
        return 1;
    }
    return SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) ? 1 : 0;
#else
    return 1;
#endif
}

static term_mode_t detect_mode(int vt_ok) {
    const char *ct = getenv("COLORTERM");
    if (ct) {
        if (strcmp(ct, "truecolor") == 0 || strcmp(ct, "24bit") == 0) {
            return TERM_MODE_TRUECOLOR;
        }
        if (strcmp(ct, "256color") == 0) {
            return TERM_MODE_256COLOR;
        }
    }

    const char *term = getenv("TERM");
    if (term) {
        if (strstr(term, "256color")) return TERM_MODE_256COLOR;
        if (strstr(term, "truecolor")) return TERM_MODE_TRUECOLOR;
    }

    if (vt_ok && stdout_is_tty()) {
        return TERM_MODE_TRUECOLOR;
    }

    if (!stdout_is_tty()) {
        return TERM_MODE_ASCII;
    }

    return TERM_MODE_256COLOR;
}

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

/* Alpha compositing against solid background */
static inline uint8_t composite(uint8_t src, uint8_t alpha, uint8_t bg) {
    return (uint8_t)(((unsigned)alpha * src +
                      (unsigned)(255 - alpha) * bg + 127u) / 255u);
}

static void sample_pixel(const png_pixels_t *px,
                          uint32_t x, uint32_t y,
                          uint8_t bg_r, uint8_t bg_g, uint8_t bg_b,
                          uint8_t *out_r, uint8_t *out_g, uint8_t *out_b) {
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

/* Map RGB to closest xterm-256 color index */
static int rgb_to_256(uint8_t r, uint8_t g, uint8_t b) {
    int ir = r, ig = g, ib = b;
    int avg = (ir + ig + ib) / 3;

    /* Grayscale ramp check */
    if (abs(ir - avg) <= 12 && abs(ig - avg) <= 12 && abs(ib - avg) <= 12) {
        if (avg < 8)   return 16;
        if (avg > 238) return 231;
        return 232 + (avg - 8) * 24 / 240;
    }

    /* 6x6x6 color cube */
    int ri = (ir * 5 + 127) / 255;
    int gi = (ig * 5 + 127) / 255;
    int bi = (ib * 5 + 127) / 255;
    return 16 + 36 * ri + 6 * gi + bi;
}

static const char ASCII_RAMP[] = " .:-=+*#%@";
#define ASCII_RAMP_LEN ((int)(sizeof(ASCII_RAMP) - 1))

static char luminance_char(uint8_t r, uint8_t g, uint8_t b) {
    unsigned lum = (299u * r + 587u * g + 114u * b) / 1000u;
    int idx = (int)(lum * (unsigned)(ASCII_RAMP_LEN - 1) / 255u);
    return ASCII_RAMP[idx];
}

static const uint8_t UPPER_HALF_BLOCK[3] = {0xE2, 0x96, 0x80};

static void write_half_block(void) {
    fwrite(UPPER_HALF_BLOCK, 1, 3, stdout);
}

void term_render(const png_pixels_t *px, const term_render_opts_t *opts) {
    if (!px || !px->pixels || px->width == 0 || px->height == 0) return;

    int max_cols = (opts->max_cols > 0) ? opts->max_cols : 80;
    uint32_t out_cols = (px->width < (uint32_t)max_cols)
                        ? px->width
                        : (uint32_t)max_cols;

    /* 16.16 fixed-point scaling factors */
    uint32_t scale_x = (px->width << 16) / out_cols;
    uint32_t row_scale = 2 * scale_x;
    uint32_t out_rows = (uint32_t)(((uint64_t)px->height << 16) + row_scale - 1) / row_scale;
    if (out_rows == 0) out_rows = 1;

    uint8_t bg_r = opts->bg_r, bg_g = opts->bg_g, bg_b = opts->bg_b;

    for (uint32_t yt = 0; yt < out_rows; yt++) {
        uint32_t img_row_top = (uint32_t)(((uint64_t)yt * row_scale + (row_scale >> 2)) >> 16);
        uint32_t img_row_bot = (uint32_t)(((uint64_t)yt * row_scale + ((uint64_t)row_scale * 3 >> 2)) >> 16);
        if (img_row_top >= px->height) img_row_top = px->height - 1;
        if (img_row_bot >= px->height) img_row_bot = px->height - 1;

        for (uint32_t xt = 0; xt < out_cols; xt++) {
            uint32_t img_col = (xt * scale_x + (scale_x >> 1)) >> 16;
            if (img_col >= px->width) img_col = px->width - 1;

            uint8_t tr, tg, tb;
            uint8_t br, bg_p, bb;

            sample_pixel(px, img_col, img_row_top, bg_r, bg_g, bg_b, &tr, &tg, &tb);
            sample_pixel(px, img_col, img_row_bot,  bg_r, bg_g, bg_b, &br, &bg_p, &bb);

            switch (opts->mode) {
                case TERM_MODE_TRUECOLOR:
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
                    putchar(luminance_char(
                        (uint8_t)((tr + br) / 2),
                        (uint8_t)((tg + bg_p) / 2),
                        (uint8_t)((tb + bb) / 2)));
                    break;
            }
        }

        if (opts->mode != TERM_MODE_ASCII) {
            fputs("\x1b[0m", stdout);
        }
        putchar('\n');
    }

    fflush(stdout);
}
