#ifndef HUFFMAN_H
#define HUFFMAN_H

#include <stdint.h>
#include "bit_reader.h"

/* Canonical Huffman tree construction and decoding (RFC 1951 sec 3.2.2). */

#define HUFFMAN_MAX_BITS 15
#define HUFFMAN_MAX_SYMBOLS 288

typedef enum {
    HUFFMAN_OK = 0,
    HUFFMAN_ERR_TOO_MANY_SYMBOLS,
    HUFFMAN_ERR_BAD_CODE_LENGTH,
    HUFFMAN_ERR_OVERSUBSCRIBED,
    HUFFMAN_ERR_INCOMPLETE,
    HUFFMAN_ERR_TRUNCATED_INPUT,
    HUFFMAN_ERR_INVALID_SYMBOL
} huffman_status_t;

typedef struct {
    int      count[HUFFMAN_MAX_BITS + 1];  /* Symbol counts per bit-length */
    uint16_t symbols[HUFFMAN_MAX_SYMBOLS]; /* Canonical symbol order */
    int      num_symbols;
} huffman_tree_t;

/* Build canonical Huffman decode table from symbol bit-lengths.
 * Returns HUFFMAN_OK on success, or an error code on invalid tree. */
huffman_status_t huffman_build(huffman_tree_t *tree, const uint8_t *lengths,
                                int num_lengths);

/* Decode a single symbol from the bit reader (MSB-first per RFC 1951). */
huffman_status_t huffman_decode(const huffman_tree_t *tree, bit_reader_t *br,
                                 int *out_symbol);

#endif /* HUFFMAN_H */
