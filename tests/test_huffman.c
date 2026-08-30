#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "huffman.h"
#include "bit_reader.h"

/* Pack '0'/'1' string into LSB-first bytes */
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

static void test_oversubscribed(void) {
    uint8_t lengths[3] = {1, 1, 1};
    huffman_tree_t tree;
    assert(huffman_build(&tree, lengths, 3) == HUFFMAN_ERR_OVERSUBSCRIBED);

    printf("test_oversubscribed: PASS\n");
}

static void test_incomplete_multi_symbol(void) {
    uint8_t lengths[2] = {1, 2};
    huffman_tree_t tree;
    assert(huffman_build(&tree, lengths, 2) == HUFFMAN_ERR_INCOMPLETE);

    printf("test_incomplete_multi_symbol: PASS\n");
}

static void test_single_symbol_incomplete_is_legal(void) {
    uint8_t lengths[1] = {1};
    huffman_tree_t tree;
    assert(huffman_build(&tree, lengths, 1) == HUFFMAN_OK);

    /* Valid symbol */
    uint8_t buf_valid[1] = {0x00};
    bit_reader_t br_valid;
    bitreader_init(&br_valid, buf_valid, 1);
    int sym;
    assert(huffman_decode(&tree, &br_valid, &sym) == HUFFMAN_OK);
    assert(sym == 0);

    /* Unassigned symbol pattern */
    uint8_t buf_invalid[2] = {0x01, 0x00};
    bit_reader_t br_invalid;
    bitreader_init(&br_invalid, buf_invalid, sizeof(buf_invalid));
    assert(huffman_decode(&tree, &br_invalid, &sym) == HUFFMAN_ERR_INVALID_SYMBOL);

    printf("test_single_symbol_incomplete_is_legal: PASS\n");
}

static void test_zero_symbols_is_legal(void) {
    uint8_t lengths[1] = {0};
    huffman_tree_t tree;
    assert(huffman_build(&tree, lengths, 1) == HUFFMAN_OK);
    assert(tree.num_symbols == 0);

    printf("test_zero_symbols_is_legal: PASS\n");
}

static void test_bad_code_length(void) {
    uint8_t lengths[2] = {1, 16};
    huffman_tree_t tree;
    assert(huffman_build(&tree, lengths, 2) == HUFFMAN_ERR_BAD_CODE_LENGTH);

    printf("test_bad_code_length: PASS\n");
}

static void test_flat_complete_code(void) {
    uint8_t lengths[4] = {2, 2, 2, 2};
    huffman_tree_t tree;
    assert(huffman_build(&tree, lengths, 4) == HUFFMAN_OK);

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

static void test_decode_truncated_input(void) {
    uint8_t lengths[4] = {2, 1, 3, 3};
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
