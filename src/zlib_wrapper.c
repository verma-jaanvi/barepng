#include "zlib_wrapper.h"

#define ADLER32_MOD 65521u

uint32_t zlib_adler32(const uint8_t *data, size_t len) {
    /* RFC 1950 sec 8's reference definition, computed directly: a runs
     * mod 65521, b accumulates a's running sum mod 65521. Real zlib
     * batches many bytes between mod operations for speed (NMAX
     * batching) — not worth the complexity here; our images are at most
     * a few megabytes and this runs once per file, not once per demo
     * frame, so straightforward correctness wins over microseconds. */
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
    if (len < 6) { /* 2-byte header + 4-byte trailer, minimum, even for
                       an empty payload */
        return ZLIB_WRAPPER_ERR_TOO_SHORT;
    }

    uint8_t cmf = data[0];
    uint8_t flg = data[1];

    uint8_t compression_method = cmf & 0x0Fu;
    if (compression_method != 8) {
        /* 8 = "deflate", the only compression method RFC 1950 defines;
         * anything else is either a corrupt stream or a wrapper format
         * this project never claimed to support */
        return ZLIB_WRAPPER_ERR_BAD_HEADER;
    }

    /* RFC 1950: the 16-bit value (CMF<<8 | FLG) must be a multiple of
     * 31 — a simple checksum over the header itself, independent of the
     * Adler-32 trailer, that catches most bit-corruption or "this isn't
     * actually a zlib stream" cases immediately. */
    uint16_t check = ((uint16_t)cmf << 8) | flg;
    if (check % 31u != 0) {
        return ZLIB_WRAPPER_ERR_BAD_HEADER;
    }

    if (flg & 0x20u) {
        /* FDICT bit: stream expects a preset dictionary (with its
         * Adler-32 id inserted right after the header). PNG's encoder
         * never sets this — nothing in a standalone image file could
         * supply an external dictionary — so a stream with FDICT set is
         * outside this project's scope rather than a bug to work around. */
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
                       ((uint32_t)trailer[2] << 8) |
                       (uint32_t)trailer[3]; /* Adler-32 is stored big-endian */

    uint32_t computed = zlib_adler32(decompressed, decompressed_len);

    return (stored == computed) ? ZLIB_WRAPPER_OK : ZLIB_WRAPPER_ERR_BAD_ADLER32;
}

const char *zlib_wrapper_status_str(zlib_wrapper_status_t status) {
    switch (status) {
        case ZLIB_WRAPPER_OK:                  return "OK";
        case ZLIB_WRAPPER_ERR_TOO_SHORT:       return "zlib stream too short for header+trailer";
        case ZLIB_WRAPPER_ERR_BAD_HEADER:      return "invalid zlib header (bad compression method or check bits)";
        case ZLIB_WRAPPER_ERR_PRESET_DICTIONARY: return "zlib stream requires a preset dictionary (unsupported)";
        case ZLIB_WRAPPER_ERR_BAD_ADLER32:     return "Adler-32 checksum mismatch";
        default:                               return "unknown zlib wrapper status";
    }
}
