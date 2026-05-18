#include <unity.h>
#include <string.h>
#include "llp_protocol.h"

void test_fb_final_payload_empty(void)
{
    uint8_t buf[10];
    size_t len = llp_build_final_payload(buf, sizeof(buf), NULL, 0);
    TEST_ASSERT_EQUAL_INT(1, len);
    TEST_ASSERT_EQUAL_HEX8(LLP_LAYER_ID_FINAL, buf[0]);
}

void test_fb_final_payload_with_data(void)
{
    uint8_t buf[10];
    uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    size_t len = llp_build_final_payload(buf, sizeof(buf), data, 4);
    TEST_ASSERT_EQUAL_INT(5, len);
    TEST_ASSERT_EQUAL_HEX8(LLP_LAYER_ID_FINAL, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xDE, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0xAD, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0xBE, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0xEF, buf[4]);
}

void test_fb_final_payload_small_buffer(void)
{
    uint8_t buf[2];
    uint8_t data[] = {0x01, 0x02};
    size_t len = llp_build_final_payload(buf, sizeof(buf), data, 2);
    TEST_ASSERT_EQUAL_INT(0, len);
}

void test_fb_magic_bytes(void)
{
    uint8_t payload[] = {LLP_LAYER_ID_FINAL};
    uint8_t buf[LLP_MAX_FRAME_SIZE(sizeof(payload))];
    size_t frame_len = llp_build_frame(buf, sizeof(buf), payload, sizeof(payload));
    TEST_ASSERT_GREATER_THAN_INT(0, frame_len);
    TEST_ASSERT_EQUAL_HEX8(LLP_MAGIC_1, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(LLP_MAGIC_2, buf[1]);
}

void test_fb_payload_length_field(void)
{
    uint8_t payload[] = {LLP_LAYER_ID_FINAL, 0x01, 0x02, 0x03};
    uint8_t buf[LLP_MAX_FRAME_SIZE(sizeof(payload))];
    size_t frame_len = llp_build_frame(buf, sizeof(buf), payload, sizeof(payload));
    TEST_ASSERT_GREATER_THAN_INT(0, frame_len);
    TEST_ASSERT_EQUAL_HEX8(sizeof(payload) & 0xFF, buf[2]);
    TEST_ASSERT_EQUAL_HEX8((sizeof(payload) >> 8) & 0xFF, buf[3]);
}

void test_fb_roundtrip(void)
{
    uint8_t original_data[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t llp_payload[LLP_MAX_PAYLOAD];
    size_t llp_payload_len = llp_build_final_payload(
        llp_payload, sizeof(llp_payload),
        original_data, sizeof(original_data));

    uint8_t frame_buf[LLP_MAX_FRAME_SIZE(llp_payload_len)];
    size_t frame_len = llp_build_frame(frame_buf, sizeof(frame_buf),
                                        llp_payload, llp_payload_len);
    TEST_ASSERT_GREATER_THAN_INT(0, frame_len);

    llp_parser_t parser;
    llp_parser_init(&parser);

    int result = 0;
    for (size_t i = 0; i < frame_len; i++) {
        result = llp_parser_process_byte(&parser, frame_buf[i], 0);
    }
    TEST_ASSERT_EQUAL_INT(1, result);

    uint8_t extracted[LLP_MAX_PAYLOAD];
    int extracted_len = llp_get_final_payload(&parser.frame,
                                               extracted, sizeof(extracted));
    TEST_ASSERT_EQUAL_INT(4, extracted_len);
    TEST_ASSERT_EQUAL_HEX8(0x01, extracted[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02, extracted[1]);
    TEST_ASSERT_EQUAL_HEX8(0x03, extracted[2]);
    TEST_ASSERT_EQUAL_HEX8(0x04, extracted[3]);
}

void test_fb_small_buffer(void)
{
    uint8_t payload[] = {LLP_LAYER_ID_FINAL};
    uint8_t buf[2];
    size_t frame_len = llp_build_frame(buf, sizeof(buf), payload, sizeof(payload));
    TEST_ASSERT_EQUAL_INT(0, frame_len);
}

void test_fb_oversized_payload(void)
{
    uint8_t big_payload[LLP_MAX_PAYLOAD + 1];
    memset(big_payload, 0xAA, sizeof(big_payload));
    big_payload[0] = LLP_LAYER_ID_FINAL;

    uint8_t buf[LLP_MAX_FRAME_SIZE(LLP_MAX_PAYLOAD + 1)];
    size_t frame_len = llp_build_frame(buf, sizeof(buf),
                                        big_payload, LLP_MAX_PAYLOAD + 1);
    TEST_ASSERT_EQUAL_INT(0, frame_len);
}

void test_fb_stuffing(void)
{
    uint8_t payload[] = {LLP_LAYER_ID_FINAL, 0xAA};
    uint8_t buf[LLP_MAX_FRAME_SIZE(sizeof(payload))];
    size_t frame_len = llp_build_frame(buf, sizeof(buf), payload, sizeof(payload));
    TEST_ASSERT_GREATER_THAN_INT(0, frame_len);

    llp_parser_t parser;
    llp_parser_init(&parser);

    int result = 0;
    for (size_t i = 0; i < frame_len; i++) {
        result = llp_parser_process_byte(&parser, buf[i], 0);
    }
    TEST_ASSERT_EQUAL_INT(1, result);

    uint8_t extracted[LLP_MAX_PAYLOAD];
    int extracted_len = llp_get_final_payload(&parser.frame,
                                               extracted, sizeof(extracted));
    TEST_ASSERT_EQUAL_INT(1, extracted_len);
    TEST_ASSERT_EQUAL_HEX8(0xAA, extracted[0]);
}

void test_fb_deterministic(void)
{
    uint8_t payload[] = {LLP_LAYER_ID_FINAL, 0x42};
    uint8_t buf1[LLP_MAX_FRAME_SIZE(sizeof(payload))];
    uint8_t buf2[LLP_MAX_FRAME_SIZE(sizeof(payload))];

    size_t len1 = llp_build_frame(buf1, sizeof(buf1), payload, sizeof(payload));
    size_t len2 = llp_build_frame(buf2, sizeof(buf2), payload, sizeof(payload));

    TEST_ASSERT_EQUAL_INT(len1, len2);
    TEST_ASSERT_EQUAL_INT8(0, memcmp(buf1, buf2, len1));
}

void test_fb_empty_payload_chain(void)
{
    uint8_t payload[] = {LLP_LAYER_ID_FINAL};
    uint8_t buf[LLP_MAX_FRAME_SIZE(sizeof(payload))];
    size_t frame_len = llp_build_frame(buf, sizeof(buf), payload, sizeof(payload));
    TEST_ASSERT_GREATER_THAN_INT(0, frame_len);

    llp_parser_t parser;
    llp_parser_init(&parser);

    int result = 0;
    for (size_t i = 0; i < frame_len; i++) {
        result = llp_parser_process_byte(&parser, buf[i], 0);
    }
    TEST_ASSERT_EQUAL_INT(1, result);

    uint8_t extracted[1];
    int extracted_len = llp_get_final_payload(&parser.frame,
                                               extracted, sizeof(extracted));
    TEST_ASSERT_EQUAL_INT(0, extracted_len);
}
