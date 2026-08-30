#ifndef INFLATE_H
#define INFLATE_H

#include <stddef.h>
#include <stdint.h>

/* Raw DEFLATE (RFC 1951) decompressor. */

typedef enum {
    INFLATE_OK = 0,
    INFLATE_ERR_TRUNCATED,
    INFLATE_ERR_BAD_BTYPE,
    INFLATE_ERR_BAD_STORED_LEN,
    INFLATE_ERR_BAD_HUFFMAN_CODE,
    INFLATE_ERR_BAD_DYNAMIC_HEADER,
    INFLATE_ERR_BAD_SYMBOL,
    INFLATE_ERR_BAD_BACKREF,
    INFLATE_ERR_OUT_OF_MEMORY
} inflate_status_t;

typedef struct {
    uint8_t *data;
    size_t   size;
    size_t   capacity;
} inflate_buffer_t;

/* Decompress raw DEFLATE stream (stored, fixed, dynamic blocks) */
inflate_status_t inflate(const uint8_t *deflate_data, size_t deflate_len,
                          inflate_buffer_t *out);

/* Free buffer allocated during inflation */
void inflate_buffer_free(inflate_buffer_t *buf);

/* String representation of status code */
const char *inflate_status_str(inflate_status_t status);

#endif /* INFLATE_H */
