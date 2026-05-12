/**
 * cross_test_generate.c — Cross-language test vector generator for LLP v3.0.0
 *
 * Builds frames from known test vectors and writes them as binary files.
 * Also reads Java-generated frames and parses them with the C parser.
 *
 * Compile: gcc -std=c99 -I include -o /tmp/llp_cross_gen cross_test_generate.c
 * Usage:   /tmp/llp_cross_gen [output_dir]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "llp_protocol.h"

#ifndef CROSS_OUTPUT_DIR
#define CROSS_OUTPUT_DIR "/tmp/llp_cross"
#endif

typedef struct {
    const char* label;
    const uint8_t* data;
    uint16_t len;
} test_vector_t;

static int write_binary(const char* path, const uint8_t* data, size_t len) {
    FILE* f = fopen(path, "wb");
    if (!f) { perror("fopen"); return -1; }
    size_t w = fwrite(data, 1, len, f);
    fclose(f);
    return (w == len) ? 0 : -1;
}

static uint8_t* read_binary(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }
    uint8_t* buf = (uint8_t*)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    *out_len = (size_t)fread(buf, 1, (size_t)sz, f);
    fclose(f);
    return buf;
}

static void print_hex(const char* label, const uint8_t* data, size_t len) {
    printf("  %s (%zu bytes): ", label, len);
    for (size_t i = 0; i < len; i++) printf("%02X ", data[i]);
    printf("\n");
}

static int self_verify(const uint8_t* frame, size_t frame_len,
                        const uint8_t* expected_data, uint16_t expected_len) {
    llp_parser_t parser;
    llp_parser_init(&parser);

    int result = 0;
    for (size_t i = 0; i < frame_len; i++)
        result = llp_parser_process_byte(&parser, frame[i], 0);

    if (result != 1) {
        printf("  FAIL: C parser returned %d (expected 1)\n", result);
        return -1;
    }

    uint8_t extracted[LLP_MAX_PAYLOAD];
    int extracted_len = llp_get_final_payload(&parser.frame,
                                               extracted, sizeof(extracted));
    if (extracted_len < 0) {
        printf("  FAIL: llp_get_final_payload returned %d\n", extracted_len);
        return -2;
    }
    if ((uint16_t)extracted_len != expected_len) {
        printf("  FAIL: extracted len %d != expected %u\n",
               extracted_len, expected_len);
        return -3;
    }
    if (memcmp(extracted, expected_data, expected_len) != 0) {
        printf("  FAIL: extracted data differs\n");
        return -4;
    }
    return 0;
}

static int cross_verify(const uint8_t* frame, size_t frame_len,
                         const test_vector_t* vec) {
    llp_parser_t parser;
    llp_parser_init(&parser);

    int result = 0;
    for (size_t i = 0; i < frame_len; i++)
        result = llp_parser_process_byte(&parser, frame[i], 0);

    if (result != 1) {
        printf("  CROSS-VERIFY [%s]: parser returned %d (expected 1)\n",
               vec->label, result);
        return -1;
    }

    uint8_t extracted[LLP_MAX_PAYLOAD];
    int extracted_len = llp_get_final_payload(&parser.frame,
                                               extracted, sizeof(extracted));
    if (extracted_len < 0) {
        printf("  CROSS-VERIFY [%s]: get_final_payload returned %d\n",
               vec->label, extracted_len);
        return -2;
    }
    if ((uint16_t)extracted_len != vec->len) {
        printf("  CROSS-VERIFY [%s]: len %d != expected %u\n",
               vec->label, extracted_len, vec->len);
        printf("  Extracted: ");
        for (int i = 0; i < extracted_len; i++) printf("%02X ", extracted[i]);
        printf("\n");
        return -3;
    }
    if (memcmp(extracted, vec->data, vec->len) != 0) {
        printf("  CROSS-VERIFY [%s]: data differs\n", vec->label);
        printf("  Expected: ");
        for (uint16_t i = 0; i < vec->len; i++) printf("%02X ", vec->data[i]);
        printf("\n  Got:      ");
        for (int i = 0; i < extracted_len; i++) printf("%02X ", extracted[i]);
        printf("\n");
        return -4;
    }

    printf("  CROSS-VERIFY [%s]: OK (%u bytes)\n", vec->label, vec->len);
    return 0;
}

int main(int argc, char** argv) {
    const char* out_dir = (argc > 1) ? argv[1] : CROSS_OUTPUT_DIR;

    char mkdir_cmd[256];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", out_dir);
    system(mkdir_cmd);

    /* Test vectors: raw application data */
    const uint8_t empty_data[]  = {};
    const uint8_t single_data[] = {0x42};
    const uint8_t hello_data[]  = {'H', 'e', 'l', 'l', 'o'};
    const uint8_t aa_data[]     = {0xAA};
    const uint8_t aa55_data[]   = {0xAA, 0x55};
    const uint8_t triple_aa_data[] = {0xAA, 0xAA, 0xAA};
    const uint8_t mixed_data[]  = {0x01, 0xAA, 0x02, 0xAA, 0x03};
    const uint8_t seq_data[]    = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
                                   0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F};
    const uint8_t zeros_data[32]= {0};
    const uint8_t ff_data[16]   = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
                                   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

    test_vector_t vectors[] = {
        {"empty",      empty_data,     0},
        {"single",     single_data,    1},
        {"hello",      hello_data,     5},
        {"aa_byte",    aa_data,        1},
        {"aa55",       aa55_data,      2},
        {"triple_aa",  triple_aa_data, 3},
        {"mixed_aa",   mixed_data,     5},
        {"sequence",   seq_data,       16},
        {"zeros",      zeros_data,     32},
        {"all_ff",     ff_data,        16},
    };
    size_t num_vectors = sizeof(vectors) / sizeof(vectors[0]);
    int all_ok = 1;

    printf("========================================\n");
    printf("LLP v3.0.0 Cross-Language Test Generator\n");
    printf("========================================\n\n");

    /* Phase 1: Generate C frames */
    printf("--- Phase 1: Generating C frames ---\n\n");
    for (size_t v = 0; v < num_vectors; v++) {
        uint8_t layer_chain[LLP_MAX_PAYLOAD];
        size_t layer_len = llp_build_final_payload(
            layer_chain, sizeof(layer_chain),
            vectors[v].data, vectors[v].len);

        uint8_t frame[LLP_MAX_FRAME_SIZE(layer_len)];
        size_t frame_len = llp_build_frame(frame, sizeof(frame),
                                            layer_chain, layer_len);

        printf("Vector %zu: %s\n", v + 1, vectors[v].label);

        if (self_verify(frame, frame_len, vectors[v].data, vectors[v].len) == 0)
            printf("  SELF-VERIFY: OK\n");
        else {
            printf("  SELF-VERIFY: FAILED\n");
            all_ok = 0;
        }

        char path[128];
        snprintf(path, sizeof(path), "%s/c_frame_%s.bin", out_dir, vectors[v].label);
        write_binary(path, frame, frame_len);
        printf("  Wrote: %s (%zu bytes)\n", path, frame_len);

        snprintf(path, sizeof(path), "%s/c_layerchain_%s.bin", out_dir, vectors[v].label);
        write_binary(path, layer_chain, layer_len);
        printf("  Wrote: %s (%zu bytes)\n\n", path, layer_len);
    }

    /* Phase 2: Verify Java frames with C parser */
    printf("--- Phase 2: Verifying Java frames with C parser ---\n\n");
    int found_any = 0;
    for (size_t v = 0; v < num_vectors; v++) {
        char path[128];
        snprintf(path, sizeof(path), "%s/java_frame_%s.bin", out_dir, vectors[v].label);

        size_t frame_len;
        uint8_t* frame = read_binary(path, &frame_len);
        if (!frame) {
            printf("  [%s] No Java frame found (run Java test first)\n", vectors[v].label);
            continue;
        }
        found_any = 1;
        printf("  Java frame [%s]: %zu bytes\n", vectors[v].label, frame_len);

        if (cross_verify(frame, frame_len, &vectors[v]) != 0)
            all_ok = 0;

        free(frame);
    }

    if (!found_any)
        printf("  (No Java frames to verify)\n");

    printf("\n========================================\n");
    if (all_ok) {
        printf("RESULT: ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("RESULT: SOME TESTS FAILED\n");
        return 1;
    }
}
