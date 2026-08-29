#ifndef HUFFMAN_H
#define HUFFMAN_H

#include <stdint.h>
#include "bit_reader.h"

/* huffman.c — canonical Huffman tree construction and decode, per
 * RFC 1951 sec 3.2.2. This module knows nothing about DEFLATE's literal/
 * length/distance alphabets specifically — it just turns "array of code
 * lengths" into "decode one symbol at a time," which is the whole trick
 * behind canonical Huffman: the code lengths *alone* are sufficient to
 * reconstruct every code, with no explicit tree structure or transmitted
 * code values needed. inflate.c supplies the DEFLATE-specific alphabets
 * (fixed trees in 2c, dynamically transmitted ones in 2d) on top of this.
 */

#define HUFFMAN_MAX_BITS 15    /* DEFLATE never uses codes longer than this */
#define HUFFMAN_MAX_SYMBOLS 288 /* covers the largest DEFLATE alphabet
                                    (288 literal/length symbols); distance
                                    (32) and code-length (19, Phase 2d)
                                    alphabets both fit comfortably */

typedef enum {
    HUFFMAN_OK = 0,
    HUFFMAN_ERR_TOO_MANY_SYMBOLS,  /* num_lengths exceeds HUFFMAN_MAX_SYMBOLS */
    HUFFMAN_ERR_BAD_CODE_LENGTH,   /* a length > HUFFMAN_MAX_BITS */
    HUFFMAN_ERR_OVERSUBSCRIBED,    /* lengths don't form a valid prefix code —
                                       too many codes assigned at some length */
    HUFFMAN_ERR_INCOMPLETE,        /* code space left unassigned, AND more than
                                       one symbol is in use — see huffman.c for
                                       the narrow case (0 or 1 symbols) this does
                                       NOT cover, which DEFLATE explicitly allows */
    HUFFMAN_ERR_TRUNCATED_INPUT,   /* bit reader ran out of data mid-decode */
    HUFFMAN_ERR_INVALID_SYMBOL     /* no symbol matched after HUFFMAN_MAX_BITS
                                       bits — implies a bug in build(), since
                                       a validated tree should never reach
                                       this during decode() */
} huffman_status_t;

typedef struct {
    int      count[HUFFMAN_MAX_BITS + 1]; /* count[len] = number of symbols
                                              assigned that code length;
                                              count[0] unused */
    uint16_t symbols[HUFFMAN_MAX_SYMBOLS]; /* symbol values, grouped by code
                                               length (ascending) then by
                                               symbol index (ascending) within
                                               a length — this ordering *is*
                                               the canonical assignment */
    int      num_symbols;
} huffman_tree_t;

/* Builds a canonical Huffman decode table from `lengths[0..num_lengths)`
 * (0 = symbol unused). Validates that the lengths form a complete,
 * non-oversubscribed prefix code before returning HUFFMAN_OK — malformed
 * lengths (corrupt or hostile input) are rejected here, not discovered
 * later as a decode-time crash or infinite loop.
 */
huffman_status_t huffman_build(huffman_tree_t *tree, const uint8_t *lengths,
                                int num_lengths);

/* Decodes exactly one symbol from br using tree. Reads one bit at a
 * time and tracks, for the code length seen so far, how many valid
 * codes exist at that length and where they start — the moment the
 * bits read so far fall within that length's assigned range, we've
 * found the symbol. No explicit binary tree or pointer-chasing needed;
 * that's the payoff of canonical assignment. */
huffman_status_t huffman_decode(const huffman_tree_t *tree, bit_reader_t *br,
                                 int *out_symbol);

#endif /* HUFFMAN_H */
