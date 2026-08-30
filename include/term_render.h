#ifndef TERM_RENDER_H
#define TERM_RENDER_H

#include "png_unfilter.h"
#include <stdint.h>

/* Terminal image renderer: truecolor, 256-color, and ASCII modes. */

typedef enum {
    TERM_MODE_TRUECOLOR,  /* 24-bit ANSI escape codes */
    TERM_MODE_256COLOR,   /* xterm-256 color palette  */
    TERM_MODE_ASCII,      /* ASCII luminance ramp     */
} term_mode_t;

typedef struct {
    int         max_cols; /* Maximum column width (0 = auto-detect) */
    uint8_t     bg_r;     /* Alpha compositing background R (default 128) */
    uint8_t     bg_g;     /* Alpha compositing background G (default 128) */
    uint8_t     bg_b;     /* Alpha compositing background B (default 128) */
    term_mode_t mode;
} term_render_opts_t;

/* Auto-detect terminal width and optimal color mode */
term_render_opts_t term_render_detect(void);

/* Render pixel buffer to stdout using half-block characters */
void term_render(const png_pixels_t *px, const term_render_opts_t *opts);

/* String representation of render mode */
const char *term_mode_name(term_mode_t mode);

#endif /* TERM_RENDER_H */
