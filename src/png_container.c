/* PNG container parser: signature check, chunk iteration, CRC validation. */
#include "png_decoder.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t PNG_SIGNATURE[PNG_SIGNATURE_LEN] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
};

uint32_t png_read_u32_be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
           ((uint32_t)p[3]);
}

/* CRC-32 (PNG spec Annex D: polynomial 0xEDB88320, reflected) */
static uint32_t crc_table[256];
static int crc_table_ready = 0;

static void build_crc_table(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) {
            if (c & 1u) {
                c = 0xEDB88320u ^ (c >> 1);
            } else {
                c = c >> 1;
            }
        }
        crc_table[n] = c;
    }
    crc_table_ready = 1;
}

uint32_t png_crc32(const uint8_t *data, size_t len) {
    if (!crc_table_ready) build_crc_table();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        c = crc_table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

/* Append bytes to growing IDAT buffer */
static int idat_append(png_container_t *c, const uint8_t *data, size_t len) {
    if (len == 0) return 0;

    if (c->idat_size + len > c->idat_capacity) {
        size_t new_cap = c->idat_capacity ? c->idat_capacity * 2 : 4096;
        while (new_cap < c->idat_size + len) new_cap *= 2;
        uint8_t *grown = (uint8_t *)realloc(c->idat_data, new_cap);
        if (!grown) return -1;
        c->idat_data = grown;
        c->idat_capacity = new_cap;
    }
    memcpy(c->idat_data + c->idat_size, data, len);
    c->idat_size += len;
    return 0;
}

static int type_is(const uint8_t *type, const char *literal) {
    return memcmp(type, literal, 4) == 0;
}

static int type_is_critical(const uint8_t *type) {
    return (type[0] & 0x20) == 0;
}

static char safe_char(uint8_t c) {
    return (c >= 0x20 && c < 0x7F) ? (char)c : '?';
}

static void set_err(char *errbuf, size_t errbuf_len, const char *fmt, ...) {
    if (!errbuf || errbuf_len == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(errbuf, errbuf_len, fmt, ap);
    va_end(ap);
}

png_status_t png_read_container(const char *path, png_container_t *out,
                                 char *errbuf, size_t errbuf_len) {
    memset(out, 0, sizeof(*out));

    FILE *f = fopen(path, "rb");
    if (!f) {
        if (errno == ENOENT) {
            set_err(errbuf, errbuf_len, "file not found: %s", path);
            return PNG_ERR_FILE_NOT_FOUND;
        }
        set_err(errbuf, errbuf_len, "could not open %s: %s", path, strerror(errno));
        return PNG_ERR_IO;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        set_err(errbuf, errbuf_len, "could not seek in %s", path);
        return PNG_ERR_IO;
    }
    long file_size_l = ftell(f);
    if (file_size_l < 0) {
        fclose(f);
        set_err(errbuf, errbuf_len, "could not determine size of %s", path);
        return PNG_ERR_IO;
    }
    rewind(f);

    size_t file_size = (size_t)file_size_l;
    uint8_t *buf = (uint8_t *)malloc(file_size ? file_size : 1);
    if (!buf) {
        fclose(f);
        set_err(errbuf, errbuf_len, "out of memory reading %s", path);
        return PNG_ERR_OUT_OF_MEMORY;
    }

    size_t nread = fread(buf, 1, file_size, f);
    fclose(f);
    if (nread != file_size) {
        free(buf);
        set_err(errbuf, errbuf_len, "short read on %s (expected %zu bytes, got %zu)",
                path, file_size, nread);
        return PNG_ERR_IO;
    }

    if (file_size < PNG_SIGNATURE_LEN ||
        memcmp(buf, PNG_SIGNATURE, PNG_SIGNATURE_LEN) != 0) {
        free(buf);
        set_err(errbuf, errbuf_len, "not a PNG file (bad signature): %s", path);
        return PNG_ERR_BAD_SIGNATURE;
    }

    png_container_t c;
    memset(&c, 0, sizeof(c));

    int have_ihdr = 0;
    int have_iend = 0;
    size_t cursor = PNG_SIGNATURE_LEN;
    png_status_t status = PNG_OK;

    while (cursor + 8 <= file_size) {
        uint32_t length = png_read_u32_be(buf + cursor);
        const uint8_t *type = buf + cursor + 4;

        if (cursor + 8 + (size_t)length + 4 > file_size) {
            set_err(errbuf, errbuf_len,
                    "truncated file: chunk claims %u data bytes but file ends first",
                    length);
            status = PNG_ERR_TRUNCATED;
            break;
        }
        const uint8_t *data = buf + cursor + 8;
        uint32_t crc_stored = png_read_u32_be(data + length);

        uint32_t crc_computed = png_crc32(type, 4 + (size_t)length);
        if (crc_computed != crc_stored) {
            set_err(errbuf, errbuf_len,
                    "bad CRC in chunk '%c%c%c%c' (chunk #%u): expected %08x, got %08x",
                    safe_char(type[0]), safe_char(type[1]),
                    safe_char(type[2]), safe_char(type[3]),
                    c.chunk_count + 1, crc_stored, crc_computed);
            status = PNG_ERR_BAD_CRC;
            break;
        }

        c.chunk_count++;

        if (type_is(type, "IHDR")) {
            if (c.chunk_count != 1) {
                set_err(errbuf, errbuf_len, "IHDR must be the first chunk");
                status = PNG_ERR_IHDR_NOT_FIRST;
                break;
            }
            if (length != 13) {
                set_err(errbuf, errbuf_len,
                        "malformed IHDR: expected 13 bytes, got %u", length);
                status = PNG_ERR_IHDR_BAD_LENGTH;
                break;
            }
            c.ihdr.width              = png_read_u32_be(data + 0);
            c.ihdr.height             = png_read_u32_be(data + 4);
            c.ihdr.bit_depth          = data[8];
            c.ihdr.color_type         = data[9];
            c.ihdr.compression_method = data[10];
            c.ihdr.filter_method      = data[11];
            c.ihdr.interlace_method   = data[12];
            have_ihdr = 1;

            if (c.ihdr.width == 0 || c.ihdr.height == 0) {
                set_err(errbuf, errbuf_len,
                        "invalid dimensions in IHDR: width and height must be non-zero");
                status = PNG_ERR_IHDR_BAD_DIMENSIONS;
                break;
            }
            if (c.ihdr.width > 0x7FFFFFFFu || c.ihdr.height > 0x7FFFFFFFu) {
                set_err(errbuf, errbuf_len,
                        "unsupported: image dimensions exceed 2^31-1 (%u x %u)",
                        c.ihdr.width, c.ihdr.height);
                status = PNG_ERR_IHDR_BAD_DIMENSIONS;
                break;
            }

            if (c.ihdr.bit_depth != 8) {
                set_err(errbuf, errbuf_len,
                        "unsupported: bit depth %u (only 8-bit is supported)",
                        c.ihdr.bit_depth);
                status = PNG_ERR_UNSUPPORTED_BIT_DEPTH;
                break;
            }
            if (c.ihdr.color_type != PNG_COLOR_TYPE_RGB &&
                c.ihdr.color_type != PNG_COLOR_TYPE_RGBA) {
                set_err(errbuf, errbuf_len,
                        "unsupported: color type %u (only truecolor/2 and "
                        "truecolor+alpha/6 are supported)", c.ihdr.color_type);
                status = PNG_ERR_UNSUPPORTED_COLOR_TYPE;
                break;
            }
            if (c.ihdr.compression_method != 0) {
                set_err(errbuf, errbuf_len,
                        "unsupported: compression method %u",
                        c.ihdr.compression_method);
                status = PNG_ERR_UNSUPPORTED_COMPRESSION;
                break;
            }
            if (c.ihdr.filter_method != 0) {
                set_err(errbuf, errbuf_len,
                        "unsupported: filter method %u", c.ihdr.filter_method);
                status = PNG_ERR_UNSUPPORTED_FILTER_METHOD;
                break;
            }
            if (c.ihdr.interlace_method != 0) {
                set_err(errbuf, errbuf_len,
                        "unsupported: interlaced PNGs are not supported");
                status = PNG_ERR_UNSUPPORTED_INTERLACE;
                break;
            }
        } else if (type_is(type, "IDAT")) {
            if (!have_ihdr) {
                set_err(errbuf, errbuf_len, "IDAT chunk appeared before IHDR");
                status = PNG_ERR_NO_IHDR;
                break;
            }
            if (idat_append(&c, data, length) != 0) {
                set_err(errbuf, errbuf_len, "out of memory concatenating IDAT data");
                status = PNG_ERR_OUT_OF_MEMORY;
                break;
            }
        } else if (type_is(type, "IEND")) {
            have_iend = 1;
            cursor += 8 + (size_t)length + 4;
            break;
        } else if (type_is(type, "PLTE")) {
            /* Suggested palette in truecolor image; ignore contents */
        } else if (type_is_critical(type)) {
            set_err(errbuf, errbuf_len,
                    "unsupported: unrecognized critical chunk '%c%c%c%c'",
                    safe_char(type[0]), safe_char(type[1]),
                    safe_char(type[2]), safe_char(type[3]));
            status = PNG_ERR_UNSUPPORTED_CRITICAL_CHUNK;
            break;
        }

        cursor += 8 + (size_t)length + 4;
    }

    free(buf);

    if (status == PNG_OK) {
        if (!have_ihdr) {
            set_err(errbuf, errbuf_len, "no IHDR chunk found");
            status = PNG_ERR_NO_IHDR;
        } else if (c.idat_size == 0) {
            set_err(errbuf, errbuf_len, "no IDAT data found");
            status = PNG_ERR_NO_IDAT;
        } else if (!have_iend) {
            set_err(errbuf, errbuf_len, "no IEND chunk found (file may be truncated)");
            status = PNG_ERR_NO_IEND;
        }
    }

    if (status != PNG_OK) {
        free(c.idat_data);
        memset(out, 0, sizeof(*out));
        return status;
    }

    *out = c;
    return PNG_OK;
}

void png_container_free(png_container_t *c) {
    if (!c) return;
    free(c->idat_data);
    memset(c, 0, sizeof(*c));
}
