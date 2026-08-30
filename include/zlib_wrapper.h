#ifndef ZLIB_WRAPPER_H
#define ZLIB_WRAPPER_H

#include <stddef.h>
#include <stdint.h>

/* RFC 1950 zlib stream header parsing and Adler-32 verification. */

typedef enum {
    ZLIB_WRAPPER_OK = 0,
    ZLIB_WRAPPER_ERR_TOO_SHORT,
    ZLIB_WRAPPER_ERR_BAD_HEADER,
    ZLIB_WRAPPER_ERR_PRESET_DICTIONARY,
    ZLIB_WRAPPER_ERR_BAD_ADLER32
} zlib_wrapper_status_t;

/* Validate 2-byte zlib header and locate DEFLATE stream payload. */
zlib_wrapper_status_t zlib_wrapper_strip(const uint8_t *data, size_t len,
                                          const uint8_t **deflate_start,
                                          size_t *deflate_len);

/* Verify trailing 4-byte Adler-32 checksum against decompressed data. */
zlib_wrapper_status_t zlib_wrapper_check_adler32(const uint8_t *data, size_t len,
                                                  const uint8_t *decompressed,
                                                  size_t decompressed_len);

/* Compute RFC 1950 Adler-32 checksum over buffer. */
uint32_t zlib_adler32(const uint8_t *data, size_t len);

/* String representation of status code. */
const char *zlib_wrapper_status_str(zlib_wrapper_status_t status);

#endif /* ZLIB_WRAPPER_H */
