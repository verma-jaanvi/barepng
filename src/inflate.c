/* DEFLATE decompression (RFC 1951): stored, fixed, and dynamic blocks. */
#include "inflate.h"
#include "huffman.h"
#include "bit_reader.h"

#include <stdlib.h>
#include <string.h>

/* RFC 1951 sec 3.2.5 length and distance tables */
static const uint16_t LENGTH_BASE[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t LENGTH_EXTRA_BITS[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};

static const uint16_t DIST_BASE[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const uint8_t DIST_EXTRA_BITS[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static int out_reserve(inflate_buffer_t *buf, size_t extra) {
    if (buf->size + extra <= buf->capacity) {
        return 0;
    }
    size_t new_cap = buf->capacity ? buf->capacity * 2 : 4096;
    while (new_cap < buf->size + extra) {
        new_cap *= 2;
    }
    uint8_t *grown = (uint8_t *)realloc(buf->data, new_cap);
    if (!grown) {
        return -1;
    }
    buf->data = grown;
    buf->capacity = new_cap;
    return 0;
}

static int out_append_byte(inflate_buffer_t *buf, uint8_t byte) {
    if (out_reserve(buf, 1) != 0) {
        return -1;
    }
    buf->data[buf->size++] = byte;
    return 0;
}

/* Build RFC 1951 sec 3.2.6 fixed Huffman tables */
static int build_fixed_trees(huffman_tree_t *lit_tree, huffman_tree_t *dist_tree) {
    uint8_t lit_lengths[288];
    int i = 0;
    for (; i < 144; i++) lit_lengths[i] = 8;
    for (; i < 256; i++) lit_lengths[i] = 9;
    for (; i < 280; i++) lit_lengths[i] = 7;
    for (; i < 288; i++) lit_lengths[i] = 8;

    if (huffman_build(lit_tree, lit_lengths, 288) != HUFFMAN_OK) {
        return -1;
    }

    uint8_t dist_lengths[32];
    for (i = 0; i < 32; i++) dist_lengths[i] = 5;

    if (huffman_build(dist_tree, dist_lengths, 32) != HUFFMAN_OK) {
        return -1;
    }

    return 0;
}

/* Decode Huffman-coded symbols and resolve LZ77 back-references */
static inflate_status_t decode_huffman_block(bit_reader_t *br,
                                               const huffman_tree_t *lit_tree,
                                               const huffman_tree_t *dist_tree,
                                               inflate_buffer_t *buf) {
    for (;;) {
        int sym;
        huffman_status_t hs = huffman_decode(lit_tree, br, &sym);
        if (hs == HUFFMAN_ERR_TRUNCATED_INPUT) return INFLATE_ERR_TRUNCATED;
        if (hs != HUFFMAN_OK) return INFLATE_ERR_BAD_HUFFMAN_CODE;

        if (sym < 256) {
            /* Literal byte */
            if (out_append_byte(buf, (uint8_t)sym) != 0) {
                return INFLATE_ERR_OUT_OF_MEMORY;
            }
            continue;
        }

        if (sym == 256) {
            return INFLATE_OK; /* End of block */
        }

        if (sym > 285) {
            return INFLATE_ERR_BAD_SYMBOL;
        }

        /* Length/distance back-reference */
        int length_index = sym - 257;
        uint32_t length = LENGTH_BASE[length_index];
        if (LENGTH_EXTRA_BITS[length_index] > 0) {
            uint32_t extra;
            if (bitreader_read_bits(br, LENGTH_EXTRA_BITS[length_index], &extra) != 0) {
                return INFLATE_ERR_TRUNCATED;
            }
            length += extra;
        }

        int dsym;
        huffman_status_t dhs = huffman_decode(dist_tree, br, &dsym);
        if (dhs == HUFFMAN_ERR_TRUNCATED_INPUT) return INFLATE_ERR_TRUNCATED;
        if (dhs != HUFFMAN_OK) return INFLATE_ERR_BAD_HUFFMAN_CODE;
        if (dsym < 0 || dsym > 29) {
            return INFLATE_ERR_BAD_SYMBOL;
        }

        uint32_t distance = DIST_BASE[dsym];
        if (DIST_EXTRA_BITS[dsym] > 0) {
            uint32_t extra;
            if (bitreader_read_bits(br, DIST_EXTRA_BITS[dsym], &extra) != 0) {
                return INFLATE_ERR_TRUNCATED;
            }
            distance += extra;
        }

        if (distance == 0 || distance > buf->size) {
            return INFLATE_ERR_BAD_BACKREF;
        }

        if (out_reserve(buf, length) != 0) {
            return INFLATE_ERR_OUT_OF_MEMORY;
        }

        /* Copy byte-by-byte for overlapping back-references (distance < length) */
        size_t start = buf->size - distance;
        for (uint32_t k = 0; k < length; k++) {
            buf->data[buf->size] = buf->data[start + k];
            buf->size++;
        }
    }
}

/* Order of code-length alphabet (RFC 1951 sec 3.2.7) */
static const uint8_t CODE_LENGTH_ORDER[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

#define DYNAMIC_MAX_LIT_LEN_SYMBOLS 288
#define DYNAMIC_MAX_DIST_SYMBOLS 32
#define DYNAMIC_MAX_TOTAL_SYMBOLS (DYNAMIC_MAX_LIT_LEN_SYMBOLS + DYNAMIC_MAX_DIST_SYMBOLS)

/* Parse dynamic block header and build code-length Huffman tree (RFC 1951 sec 3.2.7) */
static inflate_status_t read_dynamic_header(bit_reader_t *br,
                                             int *hlit, int *hdist,
                                             huffman_tree_t *cl_tree) {
    uint32_t hlit_v, hdist_v, hclen_v;
    if (bitreader_read_bits(br, 5, &hlit_v) != 0 ||
        bitreader_read_bits(br, 5, &hdist_v) != 0 ||
        bitreader_read_bits(br, 4, &hclen_v) != 0) {
        return INFLATE_ERR_TRUNCATED;
    }

    *hlit  = (int)hlit_v  + 257;
    *hdist = (int)hdist_v + 1;
    int hclen = (int)hclen_v + 4;

    uint8_t cl_lengths[19];
    memset(cl_lengths, 0, sizeof(cl_lengths));
    for (int i = 0; i < hclen; i++) {
        uint32_t v;
        if (bitreader_read_bits(br, 3, &v) != 0) return INFLATE_ERR_TRUNCATED;
        cl_lengths[CODE_LENGTH_ORDER[i]] = (uint8_t)v;
    }

    if (huffman_build(cl_tree, cl_lengths, 19) != HUFFMAN_OK) {
        return INFLATE_ERR_BAD_HUFFMAN_CODE;
    }
    return INFLATE_OK;
}

/* Expand literal/length and distance code lengths using repeat codes 16/17/18 */
static inflate_status_t expand_code_lengths(bit_reader_t *br,
                                             const huffman_tree_t *cl_tree,
                                             uint8_t *lengths, int total) {
    int n = 0;
    int prev = -1;

    while (n < total) {
        int sym;
        huffman_status_t hs = huffman_decode(cl_tree, br, &sym);
        if (hs == HUFFMAN_ERR_TRUNCATED_INPUT) return INFLATE_ERR_TRUNCATED;
        if (hs != HUFFMAN_OK)                  return INFLATE_ERR_BAD_HUFFMAN_CODE;

        if (sym <= 15) {
            /* Literal code length */
            lengths[n++] = (uint8_t)sym;
            prev = sym;
        } else if (sym == 16) {
            /* Repeat previous length 3-6 times */
            if (prev < 0) return INFLATE_ERR_BAD_DYNAMIC_HEADER;
            uint32_t extra;
            if (bitreader_read_bits(br, 2, &extra) != 0) return INFLATE_ERR_TRUNCATED;
            int repeat = 3 + (int)extra;
            if (n + repeat > total) return INFLATE_ERR_BAD_DYNAMIC_HEADER;
            for (int k = 0; k < repeat; k++) lengths[n++] = (uint8_t)prev;
        } else if (sym == 17) {
            /* Repeat 0 for 3-10 times */
            uint32_t extra;
            if (bitreader_read_bits(br, 3, &extra) != 0) return INFLATE_ERR_TRUNCATED;
            int repeat = 3 + (int)extra;
            if (n + repeat > total) return INFLATE_ERR_BAD_DYNAMIC_HEADER;
            for (int k = 0; k < repeat; k++) lengths[n++] = 0;
            prev = 0;
        } else if (sym == 18) {
            /* Repeat 0 for 11-138 times */
            uint32_t extra;
            if (bitreader_read_bits(br, 7, &extra) != 0) return INFLATE_ERR_TRUNCATED;
            int repeat = 11 + (int)extra;
            if (n + repeat > total) return INFLATE_ERR_BAD_DYNAMIC_HEADER;
            for (int k = 0; k < repeat; k++) lengths[n++] = 0;
            prev = 0;
        } else {
            return INFLATE_ERR_BAD_SYMBOL;
        }
    }
    return INFLATE_OK;
}

static inflate_status_t decode_dynamic_block(bit_reader_t *br, inflate_buffer_t *buf) {
    inflate_status_t s;
    int hlit, hdist;
    huffman_tree_t cl_tree;

    s = read_dynamic_header(br, &hlit, &hdist, &cl_tree);
    if (s != INFLATE_OK) return s;

    int total = hlit + hdist;
    if (total > DYNAMIC_MAX_TOTAL_SYMBOLS) return INFLATE_ERR_BAD_DYNAMIC_HEADER;

    uint8_t lengths[DYNAMIC_MAX_TOTAL_SYMBOLS];
    s = expand_code_lengths(br, &cl_tree, lengths, total);
    if (s != INFLATE_OK) return s;

    huffman_tree_t lit_tree, dist_tree;
    if (huffman_build(&lit_tree, lengths, hlit) != HUFFMAN_OK) {
        return INFLATE_ERR_BAD_HUFFMAN_CODE;
    }
    if (huffman_build(&dist_tree, lengths + hlit, hdist) != HUFFMAN_OK) {
        return INFLATE_ERR_BAD_HUFFMAN_CODE;
    }

    return decode_huffman_block(br, &lit_tree, &dist_tree, buf);
}

inflate_status_t inflate(const uint8_t *deflate_data, size_t deflate_len,
                          inflate_buffer_t *out) {
    memset(out, 0, sizeof(*out));

    bit_reader_t br;
    bitreader_init(&br, deflate_data, deflate_len);

    inflate_buffer_t buf;
    memset(&buf, 0, sizeof(buf));

    huffman_tree_t fixed_lit_tree, fixed_dist_tree;
    int fixed_trees_built = 0;

    inflate_status_t status = INFLATE_OK;

    for (;;) {
        unsigned bfinal;
        if (bitreader_read_bit(&br, &bfinal) != 0) {
            status = INFLATE_ERR_TRUNCATED;
            break;
        }
        uint32_t btype;
        if (bitreader_read_bits(&br, 2, &btype) != 0) {
            status = INFLATE_ERR_TRUNCATED;
            break;
        }

        if (btype == 0) {
            /* Stored block (uncompressed) */
            bitreader_align_to_byte(&br);

            uint32_t len_v, nlen_v;
            if (bitreader_read_bits(&br, 16, &len_v) != 0 ||
                bitreader_read_bits(&br, 16, &nlen_v) != 0) {
                status = INFLATE_ERR_TRUNCATED;
                break;
            }
            if ((len_v & 0xFFFFu) != ((~nlen_v) & 0xFFFFu)) {
                status = INFLATE_ERR_BAD_STORED_LEN;
                break;
            }

            size_t len = (size_t)len_v;
            if (out_reserve(&buf, len) != 0) {
                status = INFLATE_ERR_OUT_OF_MEMORY;
                break;
            }
            if (bitreader_read_raw_bytes(&br, buf.data + buf.size, len) != 0) {
                status = INFLATE_ERR_TRUNCATED;
                break;
            }
            buf.size += len;

        } else if (btype == 1) {
            /* Fixed Huffman block */
            if (!fixed_trees_built) {
                if (build_fixed_trees(&fixed_lit_tree, &fixed_dist_tree) != 0) {
                    status = INFLATE_ERR_BAD_HUFFMAN_CODE;
                    break;
                }
                fixed_trees_built = 1;
            }
            status = decode_huffman_block(&br, &fixed_lit_tree, &fixed_dist_tree, &buf);
            if (status != INFLATE_OK) {
                break;
            }

        } else if (btype == 2) {
            /* Dynamic Huffman block */
            status = decode_dynamic_block(&br, &buf);
            if (status != INFLATE_OK) {
                break;
            }

        } else {
            status = INFLATE_ERR_BAD_BTYPE;
            break;
        }

        if (bfinal) {
            break;
        }
    }

    if (status != INFLATE_OK) {
        free(buf.data);
        memset(out, 0, sizeof(*out));
        return status;
    }

    *out = buf;
    return INFLATE_OK;
}

void inflate_buffer_free(inflate_buffer_t *buf) {
    if (!buf) return;
    free(buf->data);
    memset(buf, 0, sizeof(*buf));
}

const char *inflate_status_str(inflate_status_t status) {
    switch (status) {
        case INFLATE_OK:                     return "OK";
        case INFLATE_ERR_TRUNCATED:          return "truncated stream";
        case INFLATE_ERR_BAD_BTYPE:          return "reserved/invalid BTYPE (11)";
        case INFLATE_ERR_BAD_STORED_LEN:     return "stored block LEN/NLEN mismatch";
        case INFLATE_ERR_BAD_HUFFMAN_CODE:   return "invalid Huffman code lengths";
        case INFLATE_ERR_BAD_DYNAMIC_HEADER: return "malformed dynamic Huffman header/repeat sequence";
        case INFLATE_ERR_BAD_SYMBOL:         return "decoded illegal symbol";
        case INFLATE_ERR_BAD_BACKREF:        return "LZ77 backreference before start of output";
        case INFLATE_ERR_OUT_OF_MEMORY:      return "out of memory";
        default:                             return "unknown inflate status";
    }
}
