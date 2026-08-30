#ifndef PNG_DECODER_H
#define PNG_DECODER_H

#include <stddef.h>
#include <stdint.h>

#define PNG_SIGNATURE_LEN 8

/* Supported PNG color types (RFC 2083 / ISO 15948) */
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
    PNG_ERR_IHDR_BAD_DIMENSIONS,
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

/* Parsed IHDR header */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t  bit_depth;
    uint8_t  color_type;
    uint8_t  compression_method;
    uint8_t  filter_method;
    uint8_t  interlace_method;
} png_ihdr_t;

/* Container structure holding parsed header and concatenated IDAT data */
typedef struct {
    png_ihdr_t ihdr;
    uint8_t   *idat_data;     /* Concatenated IDAT payloads */
    size_t     idat_size;
    size_t     idat_capacity;
    uint32_t   chunk_count;
} png_container_t;

/* Parse PNG container, validate chunks, and assemble IDAT stream */
png_status_t png_read_container(const char *path, png_container_t *out,
                                 char *errbuf, size_t errbuf_len);

/* Release memory allocated for IDAT stream */
void png_container_free(png_container_t *c);

/* Read 32-bit big-endian unsigned integer */
uint32_t png_read_u32_be(const uint8_t *p);

/* Table-driven CRC-32 (PNG spec Annex D) */
uint32_t png_crc32(const uint8_t *data, size_t len);

#endif /* PNG_DECODER_H */
