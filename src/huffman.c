/* huffman.c — canonical Huffman construction and decode.
 *
 * IMPORTANT bit-order note, the single easiest thing to get wrong in this
 * whole project: DEFLATE packs almost everything (LEN/NLEN, extra bits,
 * HLIT/HDIST/HCLEN, ...) least-significant-bit-first — that's what
 * bit_reader.c's read_bits() implements. Huffman codes are the one
 * explicit EXCEPTION (RFC 1951 sec 3.1.1): "Huffman codes are packed
 * starting with the most-significant bit of the code." So huffman_decode()
 * below reads bits one at a time and builds the code MSB-first (shift
 * left, OR in the new bit at the bottom), which is a completely different
 * assembly order than read_bits() — both are individually correct, they
 * just answer different questions about the same underlying LSB-first
 * byte stream.
 */
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
        tree->count[lengths[i]]++; /* count[0] silently accumulates unused
                                       symbols; never consulted below since
                                       the decode loop starts at len=1 */
    }

    /* Validate the lengths form a legal prefix code before trusting them
     * to build anything. `left` tracks how much of the binary code space
     * remains unassigned as we sweep from the shortest possible code to
     * the longest: at each length there are 2x as many available slots
     * as were left over from the previous (shorter) length, minus
     * whatever this length claims. If that ever goes negative, more
     * codes were assigned at some length than the space allows —
     * "oversubscribed," and not decodable. */
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
    /* left > 0 means some code space was never assigned to any symbol.
     * DEFLATE explicitly allows this in one specific situation, spelled
     * out in RFC 1951 sec 3.2.7: a distance alphabet with either zero
     * codes in use (an all-literal block — the array's one entry is
     * length 0) or exactly one code in use (transmitted with length 1,
     * "not zero bits," per the spec's own wording — leaving the other
     * 1-bit code permanently unassigned). Both cases have total_assigned
     * <= 1. The reason this is safe rather than ambiguous: a conforming
     * encoder simply never emits the missing code, because there's
     * nothing for it to mean. If a corrupt stream ever does produce that
     * bit pattern, decode() will correctly run off the end of the loop
     * and return HUFFMAN_ERR_INVALID_SYMBOL — which is exactly the
     * right response to that being invalid input.
     * Any other incompleteness (multiple symbols, code space still left
     * over) is a genuine error: a real ambiguous/malformed code. */
    if (left > 0 && total_assigned > 1) {
        return HUFFMAN_ERR_INCOMPLETE;
    }

    /* offsets[len] = index into tree->symbols[] where the block of
     * symbols with that code length begins. Derived from the per-length
     * counts, then consumed (incremented) as symbols are placed below —
     * so by the time we're done, each offsets[len] has advanced past its
     * own block and into the start of the next. */
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
    int first = 0;  /* first (numerically smallest) code at the current length */
    int index = 0;  /* index into tree->symbols[] where the current length's block starts */

    for (int len = 1; len <= HUFFMAN_MAX_BITS; len++) {
        unsigned bit;
        if (bitreader_read_bit(br, &bit) != 0) {
            return HUFFMAN_ERR_TRUNCATED_INPUT;
        }
        /* MSB-first assembly — see file header note. Each new bit becomes
         * the new least-significant bit of `code`, but every bit already
         * in `code` gets shifted up first (below, at the end of this same
         * iteration), so the first bit read ends up as the highest bit of
         * whatever length code we ultimately match. */
        code |= (int)bit;

        int count = tree->count[len];
        if (code - first < count) {
            /* Found it: `code` falls within this length's assigned
             * range, at offset (code - first) from its start. */
            *out_symbol = tree->symbols[index + (code - first)];
            return HUFFMAN_OK;
        }

        /* Not in this length's range — advance to the next length's
         * numbering. Both `index` and `first` skip past this length's
         * block; `first` and `code` both shift left because moving to a
         * one-bit-longer code doubles the numeric code space. */
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }

    /* Exhausted HUFFMAN_MAX_BITS without a match. If build() validated
     * this tree (no oversubscription, no incompleteness), this should be
     * unreachable — every possible bit sequence up to the max length
     * resolves to some symbol. Reaching here means either an unvalidated
     * tree was used, or there's a bug in this function. */
    return HUFFMAN_ERR_INVALID_SYMBOL;
}
