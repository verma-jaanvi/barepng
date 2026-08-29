/* inflate.c — Phase 2b (stored blocks) + Phase 2c (fixed Huffman + LZ77)
 * + Phase 2d (dynamic Huffman).
 *
 * Design choice worth stating up front: the output buffer itself *is*
 * the LZ77 sliding window. DEFLATE back-references never point further
 * back than 32768 bytes, but rather than maintain a separate circular
 * window buffer, we just index back into everything decoded so far.
 * Simpler and clearly correct; if Phase 6 profiling ever finds this
 * costs real memory on the 2048x2048 test image, that's a solvable
 * problem later — not the point to solve preemptively.
 */
#include "inflate.h"
#include "huffman.h"
#include "bit_reader.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------
 * RFC 1951 sec 3.2.5 length/distance tables. These are spec-defined
 * constants (the same 29 length codes and 30 distance codes every
 * conforming DEFLATE implementation must use), not derived — there's
 * only one correct table here.
 * ------------------------------------------------------------------- */

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

/* ---------------------------------------------------------------------
 * Growable output buffer (same doubling-growth shape as png_container.c's
 * idat_append — deliberately consistent with the rest of the project).
 * ------------------------------------------------------------------- */

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

/* ---------------------------------------------------------------------
 * Fixed Huffman trees (RFC 1951 sec 3.2.6)
 * ------------------------------------------------------------------- */

static int build_fixed_trees(huffman_tree_t *lit_tree, huffman_tree_t *dist_tree) {
    uint8_t lit_lengths[288];
    int i = 0;
    for (; i < 144; i++) lit_lengths[i] = 8;   /* symbols 0-143 */
    for (; i < 256; i++) lit_lengths[i] = 9;   /* symbols 144-255 */
    for (; i < 280; i++) lit_lengths[i] = 7;   /* symbols 256-279 */
    for (; i < 288; i++) lit_lengths[i] = 8;   /* symbols 280-287 */

    if (huffman_build(lit_tree, lit_lengths, 288) != HUFFMAN_OK) {
        return -1;
    }

    /* All 32 distance codes get a fixed 5-bit length. Building with the
     * full 32-symbol alphabet (not just the 30 ever legally used) keeps
     * the code space exactly complete (2^5 = 32 codes, 32 symbols) so
     * huffman_build()'s completeness check passes cleanly; codes 30/31
     * decode structurally fine but are rejected as INFLATE_ERR_BAD_SYMBOL
     * at the point of use, per spec ("distance codes 30-31 will never
     * actually occur"). */
    uint8_t dist_lengths[32];
    for (i = 0; i < 32; i++) dist_lengths[i] = 5;

    if (huffman_build(dist_tree, dist_lengths, 32) != HUFFMAN_OK) {
        return -1;
    }

    return 0;
}

/* ---------------------------------------------------------------------
 * Decodes one Huffman-coded block (fixed, here in 2c; dynamic trees will
 * reuse this unchanged in 2d, since the LZ77 logic doesn't care how the
 * trees were built). Runs until end-of-block (symbol 256) or an error.
 * ------------------------------------------------------------------- */

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
            /* literal byte */
            if (out_append_byte(buf, (uint8_t)sym) != 0) {
                return INFLATE_ERR_OUT_OF_MEMORY;
            }
            continue;
        }

        if (sym == 256) {
            return INFLATE_OK; /* end of block */
        }

        if (sym > 285) {
            /* 286/287: fixed tree assigns these codes (RFC 1951 3.2.6
             * explicitly gives them length 8) but a conforming encoder
             * never emits them — treat as corrupt input, not a crash. */
            return INFLATE_ERR_BAD_SYMBOL;
        }

        /* length/distance back-reference (symbols 257-285) */
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
            return INFLATE_ERR_BAD_SYMBOL; /* codes 30/31: reserved, never valid */
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
            /* points before the start of decoded output — either a
             * corrupt stream or a bug upstream; never something to
             * silently clamp */
            return INFLATE_ERR_BAD_BACKREF;
        }

        if (out_reserve(buf, length) != 0) {
            return INFLATE_ERR_OUT_OF_MEMORY;
        }

        /* Copy byte-by-byte, NOT memcpy/memmove: when distance < length
         * (the common RLE-style case — e.g. distance=1 repeats the single
         * preceding byte `length` times), the source and destination
         * ranges overlap, and each output byte must exist before it can
         * be read again as a source byte later in the same copy. */
        size_t start = buf->size - distance;
        for (uint32_t k = 0; k < length; k++) {
            buf->data[buf->size] = buf->data[start + k];
            buf->size++;
        }
    }
}

/* ---------------------------------------------------------------------
 * Dynamic Huffman blocks (RFC 1951 sec 3.2.7) — Phase 2d.
 *
 * The block transmits its own literal/length and distance code lengths,
 * but doing that naively (one length per symbol, up to 288+32=320
 * values) would cost more than just sending the data uncompressed. So
 * the lengths themselves are Huffman-coded too, using a THIRD alphabet
 * (the "code length alphabet," 19 symbols: 0-15 mean "this literal
 * length value," 16/17/18 are run-length repeat instructions) whose
 * code lengths are sent almost raw (3 bits each, in a fixed shuffled
 * order chosen so that trailing all-zero entries can be omitted via a
 * shorter HCLEN). This is a Huffman tree describing another Huffman
 * tree's lengths — reuses huffman_build()/huffman_decode() completely
 * unchanged; only the code-length values it's built from are new here.
 * ------------------------------------------------------------------- */

/* Order in which the 19 code-length-alphabet code lengths are
 * transmitted — NOT symbol order. This exact permutation is a spec
 * constant (RFC 1951 sec 3.2.7), chosen so real-world data tends to
 * need few of the trailing entries, letting HCLEN stay small. */
static const uint8_t CODE_LENGTH_ORDER[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

#define DYNAMIC_MAX_LIT_LEN_SYMBOLS 288
#define DYNAMIC_MAX_DIST_SYMBOLS 32
#define DYNAMIC_MAX_TOTAL_SYMBOLS (DYNAMIC_MAX_LIT_LEN_SYMBOLS + DYNAMIC_MAX_DIST_SYMBOLS)

static inflate_status_t decode_dynamic_block(bit_reader_t *br, inflate_buffer_t *buf) {
    uint32_t hlit_v, hdist_v, hclen_v;
    if (bitreader_read_bits(br, 5, &hlit_v) != 0 ||
        bitreader_read_bits(br, 5, &hdist_v) != 0 ||
        bitreader_read_bits(br, 4, &hclen_v) != 0) {
        return INFLATE_ERR_TRUNCATED;
    }

    int hlit = (int)hlit_v + 257;  /* number of literal/length codes, 257-288 */
    int hdist = (int)hdist_v + 1;  /* number of distance codes, 1-32 */
    int hclen = (int)hclen_v + 4;  /* number of code-length codes transmitted, 4-19 */

    /* Read the HCLEN code-length-alphabet lengths, in CODE_LENGTH_ORDER;
     * any of the 19 alphabet entries not covered (because HCLEN < 19)
     * are implicitly length 0 (unused) — that's exactly what "trailing
     * entries omitted" means. */
    uint8_t cl_lengths[19];
    memset(cl_lengths, 0, sizeof(cl_lengths));
    for (int i = 0; i < hclen; i++) {
        uint32_t v;
        if (bitreader_read_bits(br, 3, &v) != 0) {
            return INFLATE_ERR_TRUNCATED;
        }
        cl_lengths[CODE_LENGTH_ORDER[i]] = (uint8_t)v;
    }

    huffman_tree_t cl_tree;
    if (huffman_build(&cl_tree, cl_lengths, 19) != HUFFMAN_OK) {
        return INFLATE_ERR_BAD_HUFFMAN_CODE;
    }

    int total = hlit + hdist;
    if (total > DYNAMIC_MAX_TOTAL_SYMBOLS) {
        /* HLIT/HDIST are 5-bit fields and can't actually produce a total
         * exceeding 288+32 by construction, but checked explicitly
         * rather than relying on that never changing. */
        return INFLATE_ERR_BAD_DYNAMIC_HEADER;
    }

    /* Decode `total` literal/length + distance code lengths, using
     * cl_tree. This is itself Huffman-coded data (via cl_tree), NOT raw
     * values — each decoded symbol is either a literal length value
     * (0-15) or a repeat instruction (16/17/18) referring back to values
     * already placed in `lengths`. */
    uint8_t lengths[DYNAMIC_MAX_TOTAL_SYMBOLS];
    int n = 0;
    int prev_length = -1; /* no length decoded yet; code 16 needs one to repeat */

    while (n < total) {
        int sym;
        huffman_status_t hs = huffman_decode(&cl_tree, br, &sym);
        if (hs == HUFFMAN_ERR_TRUNCATED_INPUT) return INFLATE_ERR_TRUNCATED;
        if (hs != HUFFMAN_OK) return INFLATE_ERR_BAD_HUFFMAN_CODE;

        if (sym <= 15) {
            lengths[n++] = (uint8_t)sym;
            prev_length = sym;

        } else if (sym == 16) {
            /* repeat the previous length 3-6 times (2 extra bits) */
            if (prev_length < 0) {
                return INFLATE_ERR_BAD_DYNAMIC_HEADER; /* nothing to repeat yet */
            }
            uint32_t extra;
            if (bitreader_read_bits(br, 2, &extra) != 0) return INFLATE_ERR_TRUNCATED;
            int repeat = 3 + (int)extra;
            if (n + repeat > total) return INFLATE_ERR_BAD_DYNAMIC_HEADER;
            for (int k = 0; k < repeat; k++) lengths[n++] = (uint8_t)prev_length;
            /* prev_length is unchanged: we just repeated it, so it's
             * still correct as-is for a following code 16 */

        } else if (sym == 17) {
            /* repeat length 0, 3-10 times (3 extra bits) */
            uint32_t extra;
            if (bitreader_read_bits(br, 3, &extra) != 0) return INFLATE_ERR_TRUNCATED;
            int repeat = 3 + (int)extra;
            if (n + repeat > total) return INFLATE_ERR_BAD_DYNAMIC_HEADER;
            for (int k = 0; k < repeat; k++) lengths[n++] = 0;
            prev_length = 0; /* "previous" tracks the last value in the
                                 sequence regardless of how it was
                                 produced, so a following code 16 here
                                 would correctly repeat zeros */

        } else if (sym == 18) {
            /* repeat length 0, 11-138 times (7 extra bits) */
            uint32_t extra;
            if (bitreader_read_bits(br, 7, &extra) != 0) return INFLATE_ERR_TRUNCATED;
            int repeat = 11 + (int)extra;
            if (n + repeat > total) return INFLATE_ERR_BAD_DYNAMIC_HEADER;
            for (int k = 0; k < repeat; k++) lengths[n++] = 0;
            prev_length = 0;

        } else {
            /* code-length alphabet is only 0-18 */
            return INFLATE_ERR_BAD_SYMBOL;
        }
    }

    huffman_tree_t lit_tree, dist_tree;
    if (huffman_build(&lit_tree, lengths, hlit) != HUFFMAN_OK) {
        return INFLATE_ERR_BAD_HUFFMAN_CODE;
    }
    if (huffman_build(&dist_tree, lengths + hlit, hdist) != HUFFMAN_OK) {
        /* huffman_build() itself tolerates the legal 0-or-1-symbol
         * incomplete distance tree (see huffman.c) — this only fires for
         * genuinely malformed distance lengths. */
        return INFLATE_ERR_BAD_HUFFMAN_CODE;
    }

    /* Same LZ77 copy logic as Phase 2c's fixed-tree blocks — it doesn't
     * care where the trees came from. */
    return decode_huffman_block(br, &lit_tree, &dist_tree, buf);
}

/* ---------------------------------------------------------------------
 * Main entry point: block loop, BTYPE dispatch
 * ------------------------------------------------------------------- */

inflate_status_t inflate(const uint8_t *deflate_data, size_t deflate_len,
                          inflate_buffer_t *out) {
    memset(out, 0, sizeof(*out));

    bit_reader_t br;
    bitreader_init(&br, deflate_data, deflate_len);

    inflate_buffer_t buf;
    memset(&buf, 0, sizeof(buf));

    huffman_tree_t fixed_lit_tree, fixed_dist_tree;
    int fixed_trees_built = 0; /* build lazily — only pay for it if a
                                   fixed-Huffman block actually appears */

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
            /* --- stored block (Phase 2b) --- */
            bitreader_align_to_byte(&br);

            uint32_t len_v, nlen_v;
            /* LEN/NLEN are two byte-aligned 16-bit fields. read_bits(16)
             * here is exactly equivalent to reading two raw little-endian
             * bytes: once byte-aligned, an 8-bit read_bits() reproduces a
             * byte's value directly, and the second 8 bits land at bit
             * position 8+ of the packed result — i.e. byte0 | (byte1<<8),
             * which is little-endian by construction. */
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
            /* --- fixed Huffman block (Phase 2c) --- */
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
            /* --- dynamic Huffman block (Phase 2d) --- */
            status = decode_dynamic_block(&br, &buf);
            if (status != INFLATE_OK) {
                break;
            }

        } else {
            /* btype == 3: reserved per spec, never a valid value */
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
        case INFLATE_OK:                   return "OK";
        case INFLATE_ERR_TRUNCATED:        return "truncated stream";
        case INFLATE_ERR_BAD_BTYPE:        return "reserved/invalid BTYPE (11)";
        case INFLATE_ERR_BAD_STORED_LEN:   return "stored block LEN/NLEN mismatch";
        case INFLATE_ERR_BAD_HUFFMAN_CODE: return "invalid Huffman code lengths";
        case INFLATE_ERR_BAD_DYNAMIC_HEADER: return "malformed dynamic Huffman header/repeat sequence";
        case INFLATE_ERR_BAD_SYMBOL:       return "decoded an illegal symbol";
        case INFLATE_ERR_BAD_BACKREF:      return "LZ77 backreference before start of output";
        case INFLATE_ERR_OUT_OF_MEMORY:    return "out of memory";
        default:                           return "unknown inflate status";
    }
}
