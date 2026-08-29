#ifndef INFLATE_H
#define INFLATE_H

#include <stddef.h>
#include <stdint.h>

/* inflate.h — Phase 2: raw DEFLATE (RFC 1951) decompression.
 *
 * This operates on a raw DEFLATE bitstream, NOT a zlib stream. PNG's
 * IDAT payload is zlib-wrapped (RFC 1950): a 2-byte header, the raw
 * DEFLATE data, then a 4-byte Adler-32 trailer. Stripping that wrapper
 * and feeding just the DEFLATE portion to inflate() is Phase 2e's job
 * (multi-block integration) — kept separate so this module and its
 * tests can work against raw RFC 1951 fixtures without needing a real
 * PNG file at all.
 */

typedef enum {
    INFLATE_OK = 0,
    INFLATE_ERR_TRUNCATED,          /* ran out of input bits before a block/
                                        stream legally ended */
    INFLATE_ERR_BAD_BTYPE,          /* BTYPE 11 — reserved, never valid */
    INFLATE_ERR_BAD_STORED_LEN,     /* stored block's LEN/NLEN aren't
                                        one's-complements of each other */
    INFLATE_ERR_BAD_HUFFMAN_CODE,   /* code lengths didn't form a valid
                                        canonical Huffman tree */
    INFLATE_ERR_BAD_DYNAMIC_HEADER, /* a dynamic block's HLIT/HDIST/HCLEN
                                        header or code-length repeat sequence
                                        (codes 16/17/18) was structurally
                                        malformed — e.g. code 16 with no
                                        preceding length to repeat, or a
                                        repeat run overshooting the
                                        declared HLIT+HDIST total */
    INFLATE_ERR_BAD_SYMBOL,         /* decoded a symbol that's structurally
                                        valid but semantically illegal here
                                        (e.g. literal/length 286/287, or a
                                        reserved distance code 30/31) */
    INFLATE_ERR_BAD_BACKREF,        /* LZ77 distance points before the start
                                        of the output produced so far */
    INFLATE_ERR_OUT_OF_MEMORY
} inflate_status_t;

typedef struct {
    uint8_t *data;
    size_t   size;
    size_t   capacity; /* internal, for growth bookkeeping */
} inflate_buffer_t;

/* Inflates a raw DEFLATE stream. Handles BTYPE 00 (stored, Phase 2b),
 * BTYPE 01 (fixed Huffman, Phase 2c), and BTYPE 10 (dynamic Huffman,
 * Phase 2d) blocks, looping until a block with BFINAL=1 completes.
 * BTYPE 11 is always INFLATE_ERR_BAD_BTYPE (reserved per spec, not a
 * future feature — no encoder ever legally emits it).
 *
 * On INFLATE_OK, *out is fully populated and must be freed with
 * inflate_buffer_free(). On any other status, *out is left zeroed.
 */
inflate_status_t inflate(const uint8_t *deflate_data, size_t deflate_len,
                          inflate_buffer_t *out);

void inflate_buffer_free(inflate_buffer_t *buf);

/* Human-readable name for a status code, for error messages / test output. */
const char *inflate_status_str(inflate_status_t status);

#endif /* INFLATE_H */
