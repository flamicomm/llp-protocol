#include <unity.h>
#include <string.h>
#include "llp_protocol.h"

static size_t build_simple_frame(uint8_t* buf, size_t buf_size,
                                  const uint8_t* data, uint16_t data_len)
{
    uint8_t llp_payload[LLP_MAX_PAYLOAD];
    size_t payload_len = llp_build_final_payload(llp_payload, sizeof(llp_payload),
                                                   data, data_len);
    return llp_build_frame(buf, buf_size, llp_payload, payload_len);
}

void test_pp_incomplete_frame_returns_zero(void)
{
    llp_parser_t parser;
    llp_parser_init(&parser);

    int result = llp_parser_process_byte(&parser, LLP_MAGIC_1, 0);
    TEST_ASSERT_EQUAL_INT(0, result);

    result = llp_parser_process_byte(&parser, LLP_MAGIC_2, 0);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_pp_complete_frame_returns_one(void)
{
    uint8_t frame_buf[LLP_MAX_FRAME_SIZE(1)];
    size_t frame_len = build_simple_frame(frame_buf, sizeof(frame_buf), NULL, 0);

    llp_parser_t parser;
    llp_parser_init(&parser);

    int result = 0;
    for (size_t i = 0; i < frame_len; i++) {
        result = llp_parser_process_byte(&parser, frame_buf[i], 0);
    }
    TEST_ASSERT_EQUAL_INT(1, result);
}

void test_pp_extracts_correct_data(void)
{
    uint8_t data[] = {0xCA, 0xFE, 0xBA, 0xBE};
    uint8_t frame_buf[LLP_MAX_FRAME_SIZE(sizeof(data) + 1)];
    size_t frame_len = build_simple_frame(frame_buf, sizeof(frame_buf),
                                           data, sizeof(data));

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
    TEST_ASSERT_EQUAL_HEX8(0xCA, extracted[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFE, extracted[1]);
    TEST_ASSERT_EQUAL_HEX8(0xBA, extracted[2]);
    TEST_ASSERT_EQUAL_HEX8(0xBE, extracted[3]);
}

void test_pp_crc_error_returns_negative_one(void)
{
    uint8_t frame_buf[LLP_MAX_FRAME_SIZE(1)];
    size_t frame_len = build_simple_frame(frame_buf, sizeof(frame_buf), NULL, 0);

    frame_buf[frame_len - 1] ^= 0xFF;

    llp_parser_t parser;
    llp_parser_init(&parser);

    int result = 0;
    for (size_t i = 0; i < frame_len; i++) {
        int r = llp_parser_process_byte(&parser, frame_buf[i], 0);
        if (r != 0) result = r;
    }
    TEST_ASSERT_EQUAL_INT(-1, result);
    TEST_ASSERT_EQUAL_INT(LLP_ERR_CHECKSUM, parser.error_code);
}

void test_pp_timeout_returns_negative_one(void)
{
    llp_parser_t parser;
    llp_parser_init(&parser);

    llp_parser_process_byte(&parser, LLP_MAGIC_1, 0);
    llp_parser_process_byte(&parser, LLP_MAGIC_2, 0);

    int result = llp_parser_process_byte(&parser, 0x00,
                                          LLP_FRAME_TIMEOUT_MS + 1);

    TEST_ASSERT_EQUAL_INT(-1, result);
    TEST_ASSERT_EQUAL_INT(LLP_ERR_TIMEOUT, parser.error_code);
}

void test_pp_recovers_from_noise(void)
{
    llp_parser_t parser;
    llp_parser_init(&parser);

    llp_parser_process_byte(&parser, 0xFF, 0);
    llp_parser_process_byte(&parser, 0x00, 0);

    uint8_t frame_buf[LLP_MAX_FRAME_SIZE(1)];
    size_t frame_len = build_simple_frame(frame_buf, sizeof(frame_buf), NULL, 0);

    int result = 0;
    for (size_t i = 0; i < frame_len; i++) {
        result = llp_parser_process_byte(&parser, frame_buf[i], 0);
    }
    TEST_ASSERT_EQUAL_INT(1, result);
}

void test_pp_multiple_frames(void)
{
    uint8_t frame_buf[LLP_MAX_FRAME_SIZE(1)];
    size_t frame_len = build_simple_frame(frame_buf, sizeof(frame_buf), NULL, 0);

    llp_parser_t parser;
    llp_parser_init(&parser);

    int frames_received = 0;
    for (int f = 0; f < 3; f++) {
        int result = 0;
        for (size_t i = 0; i < frame_len; i++) {
            result = llp_parser_process_byte(&parser, frame_buf[i], 0);
        }
        TEST_ASSERT_EQUAL_INT(1, result);
        frames_received++;
    }

    TEST_ASSERT_EQUAL_INT(3, frames_received);
    TEST_ASSERT_EQUAL_INT(3, parser.frames_ok);
}

void test_pp_increments_error_counter(void)
{
    uint8_t good[LLP_MAX_FRAME_SIZE(1)];
    size_t good_len = build_simple_frame(good, sizeof(good), NULL, 0);

    uint8_t bad[LLP_MAX_FRAME_SIZE(1)];
    size_t bad_len = build_simple_frame(bad, sizeof(bad), NULL, 0);
    bad[bad_len - 1] ^= 0xFF;

    llp_parser_t parser;
    llp_parser_init(&parser);

    for (size_t i = 0; i < bad_len; i++) {
        llp_parser_process_byte(&parser, bad[i], 0);
    }
    for (size_t i = 0; i < good_len; i++) {
        llp_parser_process_byte(&parser, good[i], 0);
    }

    TEST_ASSERT_EQUAL_INT(1, parser.frames_ok);
    TEST_ASSERT_EQUAL_INT(1, parser.frames_error);
}

void test_pp_stuffed_byte(void)
{
    uint8_t data[] = {0xAA};
    uint8_t frame_buf[LLP_MAX_FRAME_SIZE(sizeof(data) + 1)];
    size_t frame_len = build_simple_frame(frame_buf, sizeof(frame_buf),
                                           data, sizeof(data));

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
    TEST_ASSERT_EQUAL_INT(1, extracted_len);
    TEST_ASSERT_EQUAL_HEX8(0xAA, extracted[0]);
}

void test_pp_sync_error_on_aa55_inside_frame(void)
{
    llp_parser_t parser;
    llp_parser_init(&parser);

    llp_parser_process_byte(&parser, 0xAA, 0);
    llp_parser_process_byte(&parser, 0x55, 0);
    llp_parser_process_byte(&parser, 0x01, 0);
    llp_parser_process_byte(&parser, 0x00, 0);

    int r = llp_parser_process_byte(&parser, 0xAA, 0);
    TEST_ASSERT_EQUAL_INT(0, r);

    r = llp_parser_process_byte(&parser, 0x55, 0);
    TEST_ASSERT_EQUAL_INT(-1, r);
    TEST_ASSERT_EQUAL_INT(LLP_ERR_SYNC, parser.error_code);
}

void test_pp_payload_len_error(void)
{
    llp_parser_t parser;
    llp_parser_init(&parser);

    llp_parser_process_byte(&parser, 0xAA, 0);
    llp_parser_process_byte(&parser, 0x55, 0);

    int r = llp_parser_process_byte(&parser, (LLP_MAX_PAYLOAD + 1) & 0xFF, 0);
    TEST_ASSERT_EQUAL_INT(0, r);

    r = llp_parser_process_byte(&parser, (LLP_MAX_PAYLOAD + 1) >> 8, 0);
    TEST_ASSERT_EQUAL_INT(-1, r);
    TEST_ASSERT_EQUAL_INT(LLP_ERR_PAYLOAD_LEN, parser.error_code);
}
