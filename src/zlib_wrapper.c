/* RFC 1950 zlib stream header parsing and Adler-32 verification. */
#include "zlib_wrapper.h"

#define ADLER32_MOD 65521u

uint32_t zlib_adler32(const uint8_t *data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % ADLER32_MOD;
        b = (b + a) % ADLER32_MOD;
    }
    return (b << 16) | a;
}

zlib_wrapper_status_t zlib_wrapper_strip(const uint8_t *data, size_t len,
                                          const uint8_t **deflate_start,
                                          size_t *deflate_len) {
    if (len < 6) {
        return ZLIB_WRAPPER_ERR_TOO_SHORT;
    }

    uint8_t cmf = data[0];
    uint8_t flg = data[1];

    /* Compression method must be 8 (DEFLATE) */
    uint8_t compression_method = cmf & 0x0Fu;
    if (compression_method != 8) {
        return ZLIB_WRAPPER_ERR_BAD_HEADER;
    }

    /* Header checksum check (CMF * 256 + FLG must be multiple of 31) */
    uint16_t check = ((uint16_t)cmf << 8) | flg;
    if (check % 31u != 0) {
        return ZLIB_WRAPPER_ERR_BAD_HEADER;
    }

    /* Preset dictionary check */
    if (flg & 0x20u) {
        return ZLIB_WRAPPER_ERR_PRESET_DICTIONARY;
    }

    *deflate_start = data + 2;
    *deflate_len = len - 2 - 4;
    return ZLIB_WRAPPER_OK;
}

zlib_wrapper_status_t zlib_wrapper_check_adler32(const uint8_t *data, size_t len,
                                                  const uint8_t *decompressed,
                                                  size_t decompressed_len) {
    if (len < 4) {
        return ZLIB_WRAPPER_ERR_TOO_SHORT;
    }

    const uint8_t *trailer = data + len - 4;
    uint32_t stored = ((uint32_t)trailer[0] << 24) |
                      ((uint32_t)trailer[1] << 16) |
                      ((uint32_t)trailer[2] << 8)  |
                      (uint32_t)trailer[3];

    uint32_t computed = zlib_adler32(decompressed, decompressed_len);
    return (stored == computed) ? ZLIB_WRAPPER_OK : ZLIB_WRAPPER_ERR_BAD_ADLER32;
}

const char *zlib_wrapper_status_str(zlib_wrapper_status_t status) {
    switch (status) {
        case ZLIB_WRAPPER_OK:                    return "OK";
        case ZLIB_WRAPPER_ERR_TOO_SHORT:         return "zlib stream too short";
        case ZLIB_WRAPPER_ERR_BAD_HEADER:        return "invalid zlib header";
        case ZLIB_WRAPPER_ERR_PRESET_DICTIONARY: return "preset dictionary unsupported";
        case ZLIB_WRAPPER_ERR_BAD_ADLER32:       return "Adler-32 checksum mismatch";
        default:                                 return "unknown zlib error";
    }
}
