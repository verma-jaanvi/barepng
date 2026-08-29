#ifndef TERM_RENDER_H
#define TERM_RENDER_H

/* term_render.h — Phase 4: terminal image renderer.
 *
 * Renders a decoded PNG pixel buffer to stdout using ANSI escape sequences
 * and the Unicode UPPER HALF BLOCK character U+2580 (▀), so each terminal
 * cell carries two image rows (top = foreground, bottom = background).
 *
 * Three rendering modes, selected by auto-detection or explicit override:
 *   TRUECOLOR  — 24-bit RGB: \x1b[38;2;r;g;bm + \x1b[48;2;r;g;bm + ▀
 *   256COLOR   — xterm 256-color palette (6×6×6 RGB cube + 24-step gray)
 *   ASCII      — luminance mapped to " .:-=+*#%@", one char per 2 rows
 *
 * RGBA images are composited against a configurable solid background color
 * before rendering (default: mid-gray 128,128,128).
 *
 * Cross-platform:
 *   Windows — GetConsoleScreenBufferInfo for width, SetConsoleMode for VT,
 *             SetConsoleOutputCP(65001) for UTF-8 output of ▀.
 *   POSIX   — ioctl(STDOUT_FILENO, TIOCGWINSZ) for width.
 */

#include "png_unfilter.h"  /* for png_pixels_t */
#include <stdint.h>

typedef enum {
    TERM_MODE_TRUECOLOR,  /* 24-bit ANSI: \x1b[38;2;r;g;bm */
    TERM_MODE_256COLOR,   /* xterm-256 palette              */
    TERM_MODE_ASCII,      /* luminance ramp " .:-=+*#%@"   */
} term_mode_t;

typedef struct {
    int        max_cols;      /* cap output width to this many columns (0 = use terminal width) */
    uint8_t    bg_r;          /* compositing background: red   channel (default 128) */
    uint8_t    bg_g;          /* compositing background: green channel (default 128) */
    uint8_t    bg_b;          /* compositing background: blue  channel (default 128) */
    term_mode_t mode;
} term_render_opts_t;

/* Auto-detect terminal width and best color mode from the environment.
 * On Windows, also enables VT processing and UTF-8 output as a side effect
 * (safe to call multiple times — SetConsoleMode is idempotent).
 * Returns a fully-populated opts struct; caller may override fields after. */
term_render_opts_t term_render_detect(void);

/* Render `px` to stdout using `opts`.
 *
 * Downscales image to fit opts.max_cols columns (nearest-neighbor; quality
 * is deliberately not the point here — terminal pixels are huge).
 * Two image rows map to one terminal row via the half-block technique.
 * Alpha channel (if present) is composited against opts.bg_{r,g,b} first.
 * Emits \x1b[0m at the end of every terminal row to prevent color bleed. */
void term_render(const png_pixels_t *px, const term_render_opts_t *opts);

/* Human-readable name for a mode, for --mode display / error messages. */
const char *term_mode_name(term_mode_t mode);

#endif /* TERM_RENDER_H */
