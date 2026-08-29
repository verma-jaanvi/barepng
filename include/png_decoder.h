#ifndef PNG_DECODER_H
#define PNG_DECODER_H

#include <stddef.h>
#include <stdint.h>

/* PNG signature per spec: 89 50 4E 47 0D 0A 1A 0A */
#define PNG_SIGNATURE_LEN 8

/* Color types we accept — locked in SCOPE.md.
 * (Values match the PNG spec's IHDR color type byte; other spec values
 * exist — 0 grayscale, 3 palette, 4 grayscale+alpha — but are out of scope
 * and rejected at parse time.) */
#define PNG_COLOR_TYPE_RGB  2
#define PNG_COLOR_TYPE_RGBA 6

typedef enum {
    PNG_OK = 0,
    PNG_ERR_FILE_NOT_FOUND,
    PNG_ERR_IO,
    PNG_ERR_BAD_SIGNATURE,
    PNG_ERR_TRUNCATED,
    PNG_ERR_BAD_CRC,
    PNG_ERR_NO_IHDR,
    PNG_ERR_IHDR_NOT_FIRST,
    PNG_ERR_IHDR_BAD_LENGTH,
    PNG_ERR_UNSUPPORTED_BIT_DEPTH,
    PNG_ERR_UNSUPPORTED_COLOR_TYPE,
    PNG_ERR_UNSUPPORTED_COMPRESSION,
    PNG_ERR_UNSUPPORTED_FILTER_METHOD,
    PNG_ERR_UNSUPPORTED_INTERLACE,
    PNG_ERR_UNSUPPORTED_CRITICAL_CHUNK,
    PNG_ERR_NO_IDAT,
    PNG_ERR_NO_IEND,
    PNG_ERR_OUT_OF_MEMORY
} png_status_t;

/* Parsed IHDR fields, straight off the wire (big-endian already decoded). */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t  bit_depth;
    uint8_t  color_type;
    uint8_t  compression_method;
    uint8_t  filter_method;
    uint8_t  interlace_method;
} png_ihdr_t;

/* Result of Phase 1: header fields + the concatenated, CRC-verified IDAT
 * payload, ready to hand to the Phase 2 inflate decoder. Nothing in here
 * is decompressed yet. */
typedef struct {
    png_ihdr_t ihdr;
    uint8_t   *idat_data;     /* concatenation of every IDAT chunk's payload */
    size_t     idat_size;
    size_t     idat_capacity; /* internal, for growth bookkeeping */
    uint32_t   chunk_count;   /* total chunks read, informational */
} png_container_t;

/* Reads and validates the container structure of a PNG file:
 *   - 8-byte signature
 *   - chunk loop (length/type/data/crc), with CRC-32 verification per chunk
 *   - IHDR parsed and validated against SCOPE.md (8-bit, color type 2/6,
 *     compression 0, filter 0, interlace 0)
 *   - all IDAT payloads concatenated in file order
 *   - IEND presence confirmed
 *
 * On PNG_OK, *out is fully populated and must be freed with
 * png_container_free(). On any other status, *out is left zeroed and
 * *errbuf (if non-NULL) contains a human-readable reason suitable for
 * printing directly to stderr.
 */
png_status_t png_read_container(const char *path, png_container_t *out,
                                 char *errbuf, size_t errbuf_len);

void png_container_free(png_container_t *c);

/* Big-endian 4-byte reader, exposed for unit testing in isolation. */
uint32_t png_read_u32_be(const uint8_t *p);

/* Standard PNG/zlib CRC-32 (not zlib's crc32(); implemented from the
 * reference algorithm in the PNG spec, Annex D). Exposed for testing. */
uint32_t png_crc32(const uint8_t *data, size_t len);

#endif /* PNG_DECODER_H */
