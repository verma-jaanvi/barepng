#ifndef ZLIB_WRAPPER_H
#define ZLIB_WRAPPER_H

#include <stddef.h>
#include <stdint.h>

/* zlib_wrapper.h — Phase 2e: PNG's IDAT payload isn't raw DEFLATE, it's
 * a zlib stream (RFC 1950): a 2-byte header, the raw DEFLATE data
 * inflate.c already knows how to decode, then a 4-byte Adler-32
 * checksum of the *decompressed* data. This module handles that
 * envelope only — it never touches DEFLATE itself, matching the
 * project's existing split between container framing (png_container.c)
 * and compressed-data framing.
 */

typedef enum {
    ZLIB_WRAPPER_OK = 0,
    ZLIB_WRAPPER_ERR_TOO_SHORT,          /* fewer than 6 bytes: not enough
                                             room for a 2-byte header plus
                                             4-byte trailer */
    ZLIB_WRAPPER_ERR_BAD_HEADER,         /* compression method isn't 8
                                             (deflate), or the CMF/FLG
                                             check-bits don't validate */
    ZLIB_WRAPPER_ERR_PRESET_DICTIONARY,  /* FDICT bit set — legal zlib,
                                             but PNG never uses a preset
                                             dictionary, so unsupported */
    ZLIB_WRAPPER_ERR_BAD_ADLER32         /* trailing checksum doesn't match
                                             the decompressed data */
} zlib_wrapper_status_t;

/* Validates the 2-byte zlib header at the start of `data` and reports
 * where the raw DEFLATE payload starts and how long it is (i.e. `data`
 * with the 2-byte header and 4-byte trailing Adler-32 both stripped).
 * Does not decompress anything — hand the returned start pointer/length
 * to inflate() for that.
 */
zlib_wrapper_status_t zlib_wrapper_strip(const uint8_t *data, size_t len,
                                          const uint8_t **deflate_start,
                                          size_t *deflate_len);

/* Validates the trailing 4-byte Adler-32 in `data` (its last 4 bytes,
 * big-endian per RFC 1950) against a freshly computed Adler-32 of
 * `decompressed`. Call this after inflate() succeeds, as an end-to-end
 * correctness check independent of inflate()'s own internal logic —
 * this checksum was computed by whatever originally encoded the PNG, so
 * matching it is strong evidence the decode is byte-for-byte correct. */
zlib_wrapper_status_t zlib_wrapper_check_adler32(const uint8_t *data, size_t len,
                                                  const uint8_t *decompressed,
                                                  size_t decompressed_len);

/* Standard Adler-32 (RFC 1950 sec 8), independent of PNG's CRC-32 —
 * different algorithm, different purpose (zlib stream integrity vs.
 * per-chunk integrity), so implemented separately from png_crc32(). */
uint32_t zlib_adler32(const uint8_t *data, size_t len);

const char *zlib_wrapper_status_str(zlib_wrapper_status_t status);

#endif /* ZLIB_WRAPPER_H */
