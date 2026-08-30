/* test_huffman.c - isolated tests for canonical Huffman build/decode.
 *
 * The main worked example (lengths [2,1,3,3]) was cross-checked against
 * an independent Python implementation of canonical code assignment -
 * same reasoning as the bit reader tests: derive the expected answer
 * from a second, separately-written implementation, not from running
 * this C code and copying its output.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "huffman.h"
#include "bit_reader.h"

/* Packs a string of '0'/'1' characters into bytes using the same
 * LSB-first-within-a-byte convention bit_reader.c reads with (verified
 * independently in test_bitreader.c) - bits[0] becomes bit 0 of byte 0,
 * bits[1] becomes bit 1 of byte 0, and so on. This is the *read order*
 * bit_reader_read_bit() will hand back the bits in, which for Huffman
 * codes is also MSB-first-per-code order (see huffman.c's file header). */
static void pack_bit_string(const char *bits, uint8_t *out, size_t out_cap) {
    size_t n = strlen(bits);
    size_t nbytes = (n + 7) / 8;
    assert(nbytes <= out_cap);
    memset(out, 0, nbytes);
    for (size_t i = 0; i < n; i++) {
        if (bits[i] == '1') {
            out[i / 8] |= (uint8_t)(1u << (i % 8));
        }
    }
}

/* Symbols 0..3 with lengths [2,1,3,3]. Independently verified canonical
 * codes: symbol0="10", symbol1="0", symbol2="110", symbol3="111".
 * Encoding the symbol sequence [1,0,2,3,1] concatenates to the bit
 * string "0101101110" (10 bits), computed by a separate Python script,
 * not derived from this C implementation. */
static void test_decode_known_example(void) {
    uint8_t lengths[4] = {2, 1, 3, 3};
    huffman_tree_t tree;
    assert(huffman_build(&tree, lengths, 4) == HUFFMAN_OK);

    uint8_t buf[2];
    pack_bit_string("0101101110", buf, sizeof(buf));

    bit_reader_t br;
    bitreader_init(&br, buf, sizeof(buf));

    int expected[5] = {1, 0, 2, 3, 1};
    for (int i = 0; i < 5; i++) {
        int sym;
        huffman_status_t hs = huffman_decode(&tree, &br, &sym);
        assert(hs == HUFFMAN_OK);
        assert(sym == expected[i]);
    }

    printf("test_decode_known_example: PASS\n");
}

/* Oversubscribed: three symbols all claiming length 1 - only 2 possible
 * 1-bit codes exist (0 and 1), so a third is impossible. */
static void test_oversubscribed(void) {
    uint8_t lengths[3] = {1, 1, 1};
    huffman_tree_t tree;
    assert(huffman_build(&tree, lengths, 3) == HUFFMAN_ERR_OVERSUBSCRIBED);

    printf("test_oversubscribed: PASS\n");
}

/* Genuine incompleteness: two symbols, lengths [1,2]. Kraft sum =
 * 2^-1 + 2^-2 = 0.75 < 1, so real code space is left unassigned with
 * MORE than one symbol present - this is an actual malformed/ambiguous
 * code, not the DEFLATE-legal single-symbol exception tested below. */
static void test_incomplete_multi_symbol(void) {
    uint8_t lengths[2] = {1, 2};
    huffman_tree_t tree;
    assert(huffman_build(&tree, lengths, 2) == HUFFMAN_ERR_INCOMPLETE);

    printf("test_incomplete_multi_symbol: PASS\n");
}

/* DEFLATE-legal exception (RFC 1951 sec 3.2.7): a code with exactly one
 * symbol in use is allowed to be "incomplete" - the one real code
 * decodes correctly, and the encoder is guaranteed to never emit the
 * other, unassigned bit pattern. This is precisely the shape Phase 2d's
 * distance alphabet takes when a dynamic block uses only one distance
 * value. Confirms both halves: the valid code decodes correctly, and
 * the never-emitted code is correctly rejected rather than silently
 * matching something. */
static void test_single_symbol_incomplete_is_legal(void) {
    uint8_t lengths[1] = {1}; /* one symbol (index 0), canonical code "0" */
    huffman_tree_t tree;
    assert(huffman_build(&tree, lengths, 1) == HUFFMAN_OK);

    /* the real, valid code: bit 0 */
    uint8_t buf_valid[1] = {0x00};
    bit_reader_t br_valid;
    bitreader_init(&br_valid, buf_valid, 1);
    int sym;
    assert(huffman_decode(&tree, &br_valid, &sym) == HUFFMAN_OK);
    assert(sym == 0);

    /* the never-emitted code: bit 1 - must be rejected, not misdecoded.
     * Needs at least HUFFMAN_MAX_BITS (15) bits available: decode()
     * keeps searching longer and longer codes until it either matches
     * or exhausts all 15 possible lengths, so a too-short buffer would
     * hit HUFFMAN_ERR_TRUNCATED_INPUT first rather than genuinely
     * proving "no match exists" - a 1-byte (8-bit) buffer isn't enough
     * to distinguish those two cases; 2 bytes (16 bits) is. */
    uint8_t buf_invalid[2] = {0x01, 0x00};
    bit_reader_t br_invalid;
    bitreader_init(&br_invalid, buf_invalid, sizeof(buf_invalid));
    assert(huffman_decode(&tree, &br_invalid, &sym) == HUFFMAN_ERR_INVALID_SYMBOL);

    printf("test_single_symbol_incomplete_is_legal: PASS\n");
}

/* The other DEFLATE-legal case: zero symbols in use at all (an
 * all-literal block's distance alphabet). Must build successfully with
 * num_symbols == 0; this tree is simply never handed to huffman_decode()
 * in that scenario (inflate.c never reaches a backreference). */
static void test_zero_symbols_is_legal(void) {
    uint8_t lengths[1] = {0}; /* single alphabet entry, unused */
    huffman_tree_t tree;
    assert(huffman_build(&tree, lengths, 1) == HUFFMAN_OK);
    assert(tree.num_symbols == 0);

    printf("test_zero_symbols_is_legal: PASS\n");
}

/* A code length beyond what DEFLATE ever legally uses (>15) must be
 * rejected outright, not silently truncated or overflowed into. */
static void test_bad_code_length(void) {
    uint8_t lengths[2] = {1, 16};
    huffman_tree_t tree;
    assert(huffman_build(&tree, lengths, 2) == HUFFMAN_ERR_BAD_CODE_LENGTH);

    printf("test_bad_code_length: PASS\n");
}

/* A single-length, exactly-complete code (like DEFLATE's fixed distance
 * tree: 32 symbols, all length 5) is the simplest possible valid tree -
 * useful as its own sanity check independent of the more elaborate
 * mixed-length example above. */
static void test_flat_complete_code(void) {
    uint8_t lengths[4] = {2, 2, 2, 2}; /* 4 symbols, all length 2 -> exactly fills 2^2 */
    huffman_tree_t tree;
    assert(huffman_build(&tree, lengths, 4) == HUFFMAN_OK);

    /* canonical codes for 4 equal-length symbols in order: 00,01,10,11 */
    uint8_t buf[1];
    pack_bit_string("00011011", buf, sizeof(buf));
    bit_reader_t br;
    bitreader_init(&br, buf, sizeof(buf));

    int expected[4] = {0, 1, 2, 3};
    for (int i = 0; i < 4; i++) {
        int sym;
        assert(huffman_decode(&tree, &br, &sym) == HUFFMAN_OK);
        assert(sym == expected[i]);
    }

    printf("test_flat_complete_code: PASS\n");
}

/* Decoding must fail cleanly (not crash or infinite-loop) when the
 * underlying bit stream runs out. Note: a bit_reader_t only tracks whole
 * bytes, so a 1-byte buffer always has a full 8 bits available - trying
 * to simulate "2 bits then truncated" by packing 2 real bits into a
 * 1-byte buffer doesn't work, because the other 6 bits are zero-padding
 * that huffman_decode will happily read as if they were real stream
 * data (and for this tree, they can even spell out a valid code by
 * coincidence - bits "11" followed by padding zero "0" is exactly
 * symbol2's real code "110"). An empty (0-byte) buffer is the
 * unambiguous way to force truncation. */
static void test_decode_truncated_input(void) {
    uint8_t lengths[4] = {2, 1, 3, 3}; /* same tree as the main example */
    huffman_tree_t tree;
    assert(huffman_build(&tree, lengths, 4) == HUFFMAN_OK);

    bit_reader_t br;
    bitreader_init(&br, NULL, 0);

    int sym;
    assert(huffman_decode(&tree, &br, &sym) == HUFFMAN_ERR_TRUNCATED_INPUT);

    printf("test_decode_truncated_input: PASS\n");
}

int main(void) {
    test_decode_known_example();
    test_oversubscribed();
    test_incomplete_multi_symbol();
    test_single_symbol_incomplete_is_legal();
    test_zero_symbols_is_legal();
    test_bad_code_length();
    test_flat_complete_code();
    test_decode_truncated_input();
    printf("all huffman tests passed\n");
    return 0;
}
