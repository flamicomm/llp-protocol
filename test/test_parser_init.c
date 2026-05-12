#include <unity.h>
#include "llp_protocol.h"

void test_init_sets_state_wait_magic1(void)
{
    llp_parser_t parser;
    llp_parser_init(&parser);
    TEST_ASSERT_EQUAL_INT(LLP_STATE_WAIT_MAGIC1, parser.state);
}

void test_init_clears_all_fields(void)
{
    llp_parser_t parser;
    memset(&parser, 0xFF, sizeof(parser));

    llp_parser_init(&parser);

    TEST_ASSERT_EQUAL_INT(LLP_STATE_WAIT_MAGIC1, parser.state);
    TEST_ASSERT_EQUAL_INT(0, parser.escape_pending);
    TEST_ASSERT_EQUAL_INT(0, parser.payload_idx);
    TEST_ASSERT_EQUAL_INT(LLP_ERR_OK, parser.error_code);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, parser.crc_calculated);
    TEST_ASSERT_EQUAL_INT(0, parser.crc_received);
    TEST_ASSERT_EQUAL_INT(0, parser.last_byte_time);
}

void test_init_clears_frame_payload_len(void)
{
    llp_parser_t parser;
    llp_parser_init(&parser);
    TEST_ASSERT_EQUAL_INT(0, parser.frame.payload_len);
}

void test_init_can_be_reinitialized(void)
{
    llp_parser_t parser;
    llp_parser_init(&parser);

    llp_parser_process_byte(&parser, 0xAA, 0);
    llp_parser_process_byte(&parser, 0x55, 0);

    llp_parser_init(&parser);
    TEST_ASSERT_EQUAL_INT(LLP_STATE_WAIT_MAGIC1, parser.state);
    TEST_ASSERT_EQUAL_INT(0, parser.frames_ok);
}

void test_init_resets_stats(void)
{
    llp_parser_t parser;
    llp_parser_init(&parser);

    TEST_ASSERT_EQUAL_INT(0, parser.frames_ok);
    TEST_ASSERT_EQUAL_INT(0, parser.frames_error);
    TEST_ASSERT_EQUAL_INT(0, parser.timeouts);
}
