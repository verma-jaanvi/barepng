/* Canonical Huffman construction and decoding (RFC 1951 sec 3.2.2). */
#include "huffman.h"

#include <string.h>

huffman_status_t huffman_build(huffman_tree_t *tree, const uint8_t *lengths,
                                int num_lengths) {
    if (num_lengths > HUFFMAN_MAX_SYMBOLS) {
        return HUFFMAN_ERR_TOO_MANY_SYMBOLS;
    }

    memset(tree->count, 0, sizeof(tree->count));
    for (int i = 0; i < num_lengths; i++) {
        if (lengths[i] > HUFFMAN_MAX_BITS) {
            return HUFFMAN_ERR_BAD_CODE_LENGTH;
        }
        tree->count[lengths[i]]++;
    }

    /* Verify code space availability (Kraft inequality) */
    int left = 1;
    int total_assigned = 0;
    for (int len = 1; len <= HUFFMAN_MAX_BITS; len++) {
        left <<= 1;
        left -= tree->count[len];
        if (left < 0) {
            return HUFFMAN_ERR_OVERSUBSCRIBED;
        }
        total_assigned += tree->count[len];
    }

    /* Single-symbol or zero-symbol alphabets are valid in DEFLATE (RFC 1951 sec 3.2.7) */
    if (left > 0 && total_assigned > 1) {
        return HUFFMAN_ERR_INCOMPLETE;
    }

    /* Compute symbol offsets for each code length */
    int offsets[HUFFMAN_MAX_BITS + 1];
    offsets[1] = 0;
    for (int len = 1; len < HUFFMAN_MAX_BITS; len++) {
        offsets[len + 1] = offsets[len] + tree->count[len];
    }

    tree->num_symbols = 0;
    for (int sym = 0; sym < num_lengths; sym++) {
        int len = lengths[sym];
        if (len != 0) {
            tree->symbols[offsets[len]] = (uint16_t)sym;
            offsets[len]++;
            tree->num_symbols++;
        }
    }

    return HUFFMAN_OK;
}

huffman_status_t huffman_decode(const huffman_tree_t *tree, bit_reader_t *br,
                                 int *out_symbol) {
    int code = 0;
    int first = 0;
    int index = 0;

    /* Decode bit-by-bit MSB-first (RFC 1951 sec 3.1.1) */
    for (int len = 1; len <= HUFFMAN_MAX_BITS; len++) {
        unsigned bit;
        if (bitreader_read_bit(br, &bit) != 0) {
            return HUFFMAN_ERR_TRUNCATED_INPUT;
        }
        code |= (int)bit;

        int count = tree->count[len];
        if (code - first < count) {
            *out_symbol = tree->symbols[index + (code - first)];
            return HUFFMAN_OK;
        }

        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }

    return HUFFMAN_ERR_INVALID_SYMBOL;
}
