#include <unity.h>
#include <string.h>
#include <stdio.h>
#include "test_spec_common.h"

static void verify_encode(const char *name, const char *input_hex,
                           const char *expected_hex)
{
    uint8_t input[SPEC_MAX_FRAME], expected[SPEC_MAX_FRAME], output[SPEC_MAX_FRAME];
    size_t input_len = spec_hex_to_bytes(input_hex, input, sizeof(input));
    size_t exp_len = spec_hex_to_bytes(expected_hex, expected, sizeof(expected));
    TEST_ASSERT_GREATER_THAN_size_t_MESSAGE(0, exp_len, name);
    size_t out_len = llp_build_frame(output, sizeof(output), input, (uint16_t)input_len);
    TEST_ASSERT_GREATER_THAN_size_t_MESSAGE(0, out_len, name);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(exp_len, out_len, name);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected, output, exp_len, name);
}

static void verify_decode_frame(const char *name, const char *frame_hex,
                                 const char *expected_payload_hex)
{
    uint8_t frame[SPEC_MAX_FRAME], expected[SPEC_MAX_FRAME];
    size_t frame_len = spec_hex_to_bytes(frame_hex, frame, sizeof(frame));
    size_t exp_len = spec_hex_to_bytes(expected_payload_hex, expected, sizeof(expected));
    llp_parser_t parser;
    llp_parser_init(&parser);
    int result = spec_feed_frame(&parser, frame, frame_len, NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, result, name);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE((uint16_t)exp_len, parser.frame.payload_len, name);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected, parser.frame.payload, exp_len, name);
}

static void verify_decode_error(const char *name, const char *frame_hex,
                                 int expected_error)
{
    uint8_t frame[SPEC_MAX_FRAME];
    size_t frame_len = spec_hex_to_bytes(frame_hex, frame, sizeof(frame));
    llp_parser_t parser;
    llp_parser_init(&parser);
    int last_result = 0;
    spec_feed_frame(&parser, frame, frame_len, &last_result);
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, last_result, name);
    TEST_ASSERT_EQUAL_INT_MESSAGE(expected_error, parser.error_code, name);
}

typedef struct {
    const char *name;
    const char *input_hex;
    const char *expected_hex;
} encode_vector_t;

typedef struct {
    const char *name;
    const char *frame_hex;
    const char *payload_hex;
} decode_frame_vector_t;

typedef struct {
    const char *name;
    const char *frame_hex;
    int error_code;
} decode_error_vector_t;

static const encode_vector_t transport_valid_encode[] = {
    {"empty_payload", "00", "AA550100008883"},
    {"single_byte_42", "0042", "AA5502000042B1DA"},
    {"hello_world", "0048656C6C6F", "AA5506000048656C6C6F3798"},
    {"payload_null_byte", "0000", "AA550200000037B2"},
    {"payload_ff_byte", "00FF", "AA55020000FFC7AC"},
    {"payload_aa_byte", "00AA", "AA55020000AA0097A6"},
    {"payload_aa55", "00AA55", "AA55030000AA00552DE2"},
    {"payload_triple_aa", "00AAAAAA", "AA55040000AA00AA00AA00722F"},
    {"payload_mixed_aa", "0001AA02AA03", "AA5506000001AA0002AA0003DBD7"},
    {"payload_zeros_16", "0000000000000000000000000000000000", "AA551100000000000000000000000000000000000036A3"},
    {"payload_ones_16", "0001010101010101010101010101010101", "AA551100000101010101010101010101010101010161B6"},
    {"payload_incremental", "00000102030405060708090A0B0C0D0E0F", "AA55110000000102030405060708090A0B0C0D0E0F0BF2"},
    {"payload_all_ff_16", "00FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", "AA55110000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF77A3"},
    {"payload_aa_prefix", "00AA42", "AA55030000AA0042FB80"},
    {"payload_aa_suffix", "0042AA", "AA5503000042AA00C665"},
    {"payload_aa_boundary", "00AAAA00AA", "AA55050000AA00AA0000AA00F9F9"},
    {"payload_alternating", "00AA00AA00AA", "AA55060000AA0000AA0000AA00D651"},
    {"payload_seq_32", "00000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F", "AA55210000000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F9845"},
    {"payload_55_byte", "0055", "AA550200005567B8"},
    {"payload_byte_0x7F", "007F", "AA550200007F4F3D"},
    {"payload_byte_0x80", "0080", "AA5502000080BF23"},
    {"payload_byte_0xFE", "00FE", "AA55020000FEE6BC"},
    {"payload_ascii_test", "0054657374", "AA550500005465737491B9"},
    {"payload_ascii_fox", "0054686520717569636B2062726F776E20666F78", "AA5514000054686520717569636B2062726F776E20666F78BB2C"},
    {"payload_eight_bytes", "000001020304050607", "AA5509000000010203040506079D89"},
    {"payload_fifteen_null", "00000000000000000000000000000000", "AA55100000000000000000000000000000000000EC09"},
    {"payload_thirty_null", "00000000000000000000000000000000000000000000000000000000000000", "AA551F00000000000000000000000000000000000000000000000000000000000000006AC1"},
    {"payload_sixtyfour_seq", "00000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F202122232425262728292A2B2C2D2E2F303132333435363738393A3B3C3D3E3F", "AA55410000000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F202122232425262728292A2B2C2D2E2F303132333435363738393A3B3C3D3E3F07DF"},
    {"payload_repeated_55", "005555555555555555", "AA5509000055555555555555556E3D"},
    {"payload_aa_55_pairs", "00AA55AA55AA55", "AA55070000AA0055AA0055AA0055FCFE"},
    {"payload_mixed_55_aa", "0055AA55AA55AA55AA", "AA5509000055AA0055AA0055AA0055AA002313"},
    {"payload_double_zero", "000000", "AA550300000000C81A"},
    {"payload_ten_AS", "0041414141414141414141", "AA550B00004141414141414141414125FF"},
};

void test_spec_transport_valid_encode(void)
{
    for (size_t i = 0; i < sizeof(transport_valid_encode) / sizeof(transport_valid_encode[0]); i++) {
        const encode_vector_t *v = &transport_valid_encode[i];
        verify_encode(v->name, v->input_hex, v->expected_hex);
    }
}

static const decode_frame_vector_t transport_valid_decode[] = {
    {"empty_payload", "AA550100008883", "00"},
    {"single_byte_42", "AA5502000042B1DA", "0042"},
    {"hello_world", "AA5506000048656C6C6F3798", "0048656C6C6F"},
    {"payload_null_byte", "AA550200000037B2", "0000"},
    {"payload_ff_byte", "AA55020000FFC7AC", "00FF"},
    {"payload_aa_byte", "AA55020000AA0097A6", "00AA"},
    {"payload_aa55", "AA55030000AA00552DE2", "00AA55"},
    {"payload_triple_aa", "AA55040000AA00AA00AA00722F", "00AAAAAA"},
    {"payload_mixed_aa", "AA5506000001AA0002AA0003DBD7", "0001AA02AA03"},
    {"payload_zeros_16", "AA551100000000000000000000000000000000000036A3", "0000000000000000000000000000000000"},
    {"payload_ones_16", "AA551100000101010101010101010101010101010161B6", "0001010101010101010101010101010101"},
    {"payload_incremental", "AA55110000000102030405060708090A0B0C0D0E0F0BF2", "00000102030405060708090A0B0C0D0E0F"},
    {"payload_all_ff_16", "AA55110000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF77A3", "00FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"},
    {"payload_aa_prefix", "AA55030000AA0042FB80", "00AA42"},
    {"payload_aa_suffix", "AA5503000042AA00C665", "0042AA"},
    {"payload_aa_boundary", "AA55050000AA00AA0000AA00F9F9", "00AAAA00AA"},
    {"payload_alternating", "AA55060000AA0000AA0000AA00D651", "00AA00AA00AA"},
    {"payload_seq_32", "AA55210000000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F9845", "00000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F"},
    {"payload_55_byte", "AA550200005567B8", "0055"},
    {"payload_byte_0x7F", "AA550200007F4F3D", "007F"},
    {"payload_byte_0x80", "AA5502000080BF23", "0080"},
    {"payload_byte_0xFE", "AA55020000FEE6BC", "00FE"},
    {"payload_ascii_test", "AA550500005465737491B9", "0054657374"},
    {"payload_ascii_fox", "AA5514000054686520717569636B2062726F776E20666F78BB2C", "0054686520717569636B2062726F776E20666F78"},
    {"payload_eight_bytes", "AA5509000000010203040506079D89", "000001020304050607"},
    {"payload_fifteen_null", "AA55100000000000000000000000000000000000EC09", "00000000000000000000000000000000"},
    {"payload_thirty_null", "AA551F00000000000000000000000000000000000000000000000000000000000000006AC1", "00000000000000000000000000000000000000000000000000000000000000"},
    {"payload_sixtyfour_seq", "AA55410000000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F202122232425262728292A2B2C2D2E2F303132333435363738393A3B3C3D3E3F07DF", "00000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F202122232425262728292A2B2C2D2E2F303132333435363738393A3B3C3D3E3F"},
#if LLP_MAX_PAYLOAD >= 65
    {"payload_repeated_55", "AA5509000055555555555555556E3D", "005555555555555555"},
    {"payload_aa_55_pairs", "AA55070000AA0055AA0055AA0055FCFE", "00AA55AA55AA55"},
#endif
    {"payload_mixed_55_aa", "AA5509000055AA0055AA0055AA0055AA002313", "0055AA55AA55AA55AA"},
    {"payload_double_zero", "AA550300000000C81A", "000000"},
    {"payload_ten_AS", "AA550B00004141414141414141414125FF", "0041414141414141414141"},
};

void test_spec_transport_valid_decode(void)
{
    for (size_t i = 0; i < sizeof(transport_valid_decode) / sizeof(transport_valid_decode[0]); i++) {
        const decode_frame_vector_t *v = &transport_valid_decode[i];
        verify_decode_frame(v->name, v->frame_hex, v->payload_hex);
    }
}

void test_spec_transport_valid_stream_two_empty(void)
{
    const char *hex = "AA550100008883AA550100008883";
    uint8_t data[SPEC_MAX_FRAME];
    size_t len = spec_hex_to_bytes(hex, data, sizeof(data));
    llp_parser_t parser;
    llp_parser_init(&parser);
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream(&parser, data, len, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(2, event_count);
    TEST_ASSERT_EQUAL_INT(1, events[0].type);
    uint8_t exp0[] = {0x00};
    TEST_ASSERT_EQUAL_UINT16(1, events[0].payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp0, events[0].payload, 1);
    TEST_ASSERT_EQUAL_INT(1, events[1].type);
    TEST_ASSERT_EQUAL_UINT16(1, events[1].payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp0, events[1].payload, 1);
}

void test_spec_transport_valid_stream_empty_then_hello(void)
{
    const char *hex = "AA550100008883AA5506000048656C6C6F3798";
    uint8_t data[SPEC_MAX_FRAME];
    size_t len = spec_hex_to_bytes(hex, data, sizeof(data));
    llp_parser_t parser;
    llp_parser_init(&parser);
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream(&parser, data, len, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(2, event_count);
    TEST_ASSERT_EQUAL_INT(1, events[0].type);
    uint8_t exp0[] = {0x00};
    TEST_ASSERT_EQUAL_UINT16(1, events[0].payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp0, events[0].payload, 1);
    TEST_ASSERT_EQUAL_INT(1, events[1].type);
    uint8_t exp1[] = {0x00, 0x48, 0x65, 0x6C, 0x6C, 0x6F};
    TEST_ASSERT_EQUAL_UINT16(6, events[1].payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp1, events[1].payload, 6);
}

void test_spec_transport_valid_stream_three_mixed(void)
{
    const char *hex = "AA55020000AA0097A6AA550100008883AA5506000048656C6C6F3798";
    uint8_t data[SPEC_MAX_FRAME];
    size_t len = spec_hex_to_bytes(hex, data, sizeof(data));
    llp_parser_t parser;
    llp_parser_init(&parser);
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream(&parser, data, len, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(3, event_count);
    uint8_t exp_aa[] = {0x00, 0xAA};
    TEST_ASSERT_EQUAL_INT(1, events[0].type);
    TEST_ASSERT_EQUAL_UINT16(2, events[0].payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp_aa, events[0].payload, 2);
    uint8_t exp_empty[] = {0x00};
    TEST_ASSERT_EQUAL_INT(1, events[1].type);
    TEST_ASSERT_EQUAL_UINT16(1, events[1].payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp_empty, events[1].payload, 1);
    uint8_t exp_hello[] = {0x00, 0x48, 0x65, 0x6C, 0x6C, 0x6F};
    TEST_ASSERT_EQUAL_INT(1, events[2].type);
    TEST_ASSERT_EQUAL_UINT16(6, events[2].payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp_hello, events[2].payload, 6);
}

static const decode_error_vector_t transport_crc_vectors[] = {
    {"corrupted_last_byte_empty_payload", "AA55010000887C", LLP_ERR_CHECKSUM},
    {"corrupted_last_byte_single_byte_42", "AA5502000042B125", LLP_ERR_CHECKSUM},
    {"corrupted_last_byte_hello_world", "AA5506000048656C6C6F3767", LLP_ERR_CHECKSUM},
    {"corrupted_last_byte_payload_null_byte", "AA5502000000374D", LLP_ERR_CHECKSUM},
    {"corrupted_last_byte_payload_ff_byte", "AA55020000FFC753", LLP_ERR_CHECKSUM},
    {"corrupted_last_byte_payload_aa_byte", "AA55020000AA009759", LLP_ERR_CHECKSUM},
    {"corrupted_last_byte_payload_aa55", "AA55030000AA00552D1D", LLP_ERR_CHECKSUM},
    {"corrupted_last_byte_payload_triple_aa", "AA55040000AA00AA00AA0072D0", LLP_ERR_CHECKSUM},
    {"corrupted_last_byte_payload_mixed_aa", "AA5506000001AA0002AA0003DB28", LLP_ERR_CHECKSUM},
    {"corrupted_last_byte_payload_zeros_16", "AA5511000000000000000000000000000000000000365C", LLP_ERR_CHECKSUM},
    {"corrupted_last_byte_payload_ones_16", "AA55110000010101010101010101010101010101016149", LLP_ERR_CHECKSUM},
    {"corrupted_last_byte_payload_incremental", "AA55110000000102030405060708090A0B0C0D0E0F0B0D", LLP_ERR_CHECKSUM},
    {"corrupted_last_byte_payload_all_ff_16", "AA55110000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF775C", LLP_ERR_CHECKSUM},
    {"corrupted_last_byte_payload_aa_prefix", "AA55030000AA0042FB7F", LLP_ERR_CHECKSUM},
    {"corrupted_both_crc_bytes", "AA5506000048656C6C6FC867", LLP_ERR_CHECKSUM},
    {"crc_all_zero", "AA5506000048656C6C6F0000", LLP_ERR_CHECKSUM},
    {"crc_all_ones", "AA5506000048656C6C6FFFFF", LLP_ERR_CHECKSUM},
    {"crc_bit_flip_pos_0", "AA5506000048656C6C6F3698", LLP_ERR_CHECKSUM},
    {"crc_bit_flip_pos_1", "AA5506000048656C6C6F3598", LLP_ERR_CHECKSUM},
    {"crc_bit_flip_pos_2", "AA5506000048656C6C6F3398", LLP_ERR_CHECKSUM},
    {"crc_bit_flip_pos_3", "AA5506000048656C6C6F3F98", LLP_ERR_CHECKSUM},
    {"crc_bit_flip_pos_4", "AA5506000048656C6C6F2798", LLP_ERR_CHECKSUM},
    {"crc_bit_flip_pos_5", "AA5506000048656C6C6F1798", LLP_ERR_CHECKSUM},
    {"crc_bit_flip_pos_6", "AA5506000048656C6C6F7798", LLP_ERR_CHECKSUM},
    {"crc_bit_flip_pos_7", "AA5506000048656C6C6FB798", LLP_ERR_CHECKSUM},
    {"crc_from_different_frame", "AA5506000048656C6C6FB1DA", LLP_ERR_CHECKSUM},
    {"crc_swapped_bytes", "AA5506000048656C6C6F9837", LLP_ERR_CHECKSUM},
    {"corrupted_payload_byte", "AA55060000489A6C6C6F3798", LLP_ERR_CHECKSUM},
};

void test_spec_transport_crc(void)
{
    for (size_t i = 0; i < sizeof(transport_crc_vectors) / sizeof(transport_crc_vectors[0]); i++) {
        const decode_error_vector_t *v = &transport_crc_vectors[i];
        verify_decode_error(v->name, v->frame_hex, v->error_code);
    }
}

static const decode_frame_vector_t transport_stuffing_valid[] = {
    {"valid_single_aa", "AA55020000AA0097A6", "00AA"},
    {"valid_magic_overlap", "AA55030000AA00552DE2", "00AA55"},
    {"valid_triple_aa", "AA55040000AA00AA00AA00722F", "00AAAAAA"},
    {"valid_mixed_aa", "AA5506000001AA0002AA0003DBD7", "0001AA02AA03"},
};

void test_spec_transport_stuffing_valid(void)
{
    for (size_t i = 0; i < sizeof(transport_stuffing_valid) / sizeof(transport_stuffing_valid[0]); i++) {
        const decode_frame_vector_t *v = &transport_stuffing_valid[i];
        verify_decode_frame(v->name, v->frame_hex, v->payload_hex);
    }
}

static const decode_error_vector_t transport_stuffing_invalid[] = {
    {"invalid_escape_0x01", "AA55020000AA0197A6", LLP_ERR_SYNC},
    {"invalid_escape_0xFF", "AA55020000AAFF97A6", LLP_ERR_SYNC},
    {"invalid_escape_0xAA", "AA55020000AAAA97A6", LLP_ERR_SYNC},
    {"raw_aa_unescaped", "AA55020000AA97A6", LLP_ERR_SYNC},
};

void test_spec_transport_stuffing_invalid(void)
{
    for (size_t i = 0; i < sizeof(transport_stuffing_invalid) / sizeof(transport_stuffing_invalid[0]); i++) {
        const decode_error_vector_t *v = &transport_stuffing_invalid[i];
        verify_decode_error(v->name, v->frame_hex, v->error_code);
    }
}

void test_spec_transport_resync_noise_before_frame(void)
{
    const char *hex = "FFFFFFAA550100008883";
    uint8_t data[SPEC_MAX_FRAME];
    size_t len = spec_hex_to_bytes(hex, data, sizeof(data));
    llp_parser_t parser;
    llp_parser_init(&parser);
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream(&parser, data, len, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(1, event_count);
    uint8_t exp[] = {0x00};
    TEST_ASSERT_EQUAL_INT(1, events[0].type);
    TEST_ASSERT_EQUAL_UINT16(1, events[0].payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, events[0].payload, 1);
}

void test_spec_transport_resync_noise_between_frames(void)
{
    const char *hex = "AA550100008883DEADAA5506000048656C6C6F3798";
    uint8_t data[SPEC_MAX_FRAME];
    size_t len = spec_hex_to_bytes(hex, data, sizeof(data));
    llp_parser_t parser;
    llp_parser_init(&parser);
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream(&parser, data, len, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(2, event_count);
    uint8_t exp0[] = {0x00};
    TEST_ASSERT_EQUAL_INT(1, events[0].type);
    TEST_ASSERT_EQUAL_UINT16(1, events[0].payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp0, events[0].payload, 1);
    uint8_t exp1[] = {0x00, 0x48, 0x65, 0x6C, 0x6C, 0x6F};
    TEST_ASSERT_EQUAL_INT(1, events[1].type);
    TEST_ASSERT_EQUAL_UINT16(6, events[1].payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp1, events[1].payload, 6);
}

void test_spec_transport_resync_corrupt_magic1(void)
{
    const char *hex = "BB550100008883AA550100008883";
    uint8_t data[SPEC_MAX_FRAME];
    size_t len = spec_hex_to_bytes(hex, data, sizeof(data));
    llp_parser_t parser;
    llp_parser_init(&parser);
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream(&parser, data, len, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(1, event_count);
    TEST_ASSERT_EQUAL_INT(1, events[0].type);
    TEST_ASSERT_EQUAL_UINT16(1, events[0].payload_len);
}

void test_spec_transport_resync_corrupt_magic2(void)
{
    const char *hex = "AA440100008883AA550100008883";
    uint8_t data[SPEC_MAX_FRAME];
    size_t len = spec_hex_to_bytes(hex, data, sizeof(data));
    llp_parser_t parser;
    llp_parser_init(&parser);
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream(&parser, data, len, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(1, event_count);
    TEST_ASSERT_EQUAL_INT(1, events[0].type);
    TEST_ASSERT_EQUAL_UINT16(1, events[0].payload_len);
}

void test_spec_transport_resync_aa_no_false_resync(void)
{
    const char *hex = "AA55020000AA0097A6";
    uint8_t data[SPEC_MAX_FRAME];
    size_t len = spec_hex_to_bytes(hex, data, sizeof(data));
    llp_parser_t parser;
    llp_parser_init(&parser);
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream(&parser, data, len, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(1, event_count);
    uint8_t exp[] = {0x00, 0xAA};
    TEST_ASSERT_EQUAL_INT(1, events[0].type);
    TEST_ASSERT_EQUAL_UINT16(2, events[0].payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, events[0].payload, 2);
}

void test_spec_transport_resync_aa55_no_false_resync(void)
{
    const char *hex = "AA55030000AA00552DE2";
    uint8_t data[SPEC_MAX_FRAME];
    size_t len = spec_hex_to_bytes(hex, data, sizeof(data));
    llp_parser_t parser;
    llp_parser_init(&parser);
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream(&parser, data, len, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(1, event_count);
    uint8_t exp[] = {0x00, 0xAA, 0x55};
    TEST_ASSERT_EQUAL_INT(1, events[0].type);
    TEST_ASSERT_EQUAL_UINT16(3, events[0].payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, events[0].payload, 3);
}

void test_spec_transport_resync_invalid_escape_then_valid(void)
{
    const char *hex = "AA55020000AA9997A6AA550100008883";
    uint8_t data[SPEC_MAX_FRAME];
    size_t len = spec_hex_to_bytes(hex, data, sizeof(data));
    llp_parser_t parser;
    llp_parser_init(&parser);
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream(&parser, data, len, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(2, event_count);
    TEST_ASSERT_EQUAL_INT(-1, events[0].type);
    TEST_ASSERT_EQUAL_INT(LLP_ERR_SYNC, events[0].error_code);
    uint8_t exp[] = {0x00};
    TEST_ASSERT_EQUAL_INT(1, events[1].type);
    TEST_ASSERT_EQUAL_UINT16(1, events[1].payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, events[1].payload, 1);
}

void test_spec_transport_resync_garbage_three_frames(void)
{
    const char *hex = "AA550100008883FFAA5506000048656C6C6F3798AABBAA550100008883";
    uint8_t data[SPEC_MAX_FRAME];
    size_t len = spec_hex_to_bytes(hex, data, sizeof(data));
    llp_parser_t parser;
    llp_parser_init(&parser);
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream(&parser, data, len, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(3, event_count);
    uint8_t exp0[] = {0x00};
    TEST_ASSERT_EQUAL_INT(1, events[0].type);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp0, events[0].payload, 1);
    uint8_t exp1[] = {0x00, 0x48, 0x65, 0x6C, 0x6C, 0x6F};
    TEST_ASSERT_EQUAL_INT(1, events[1].type);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp1, events[1].payload, 6);
    uint8_t exp2[] = {0x00};
    TEST_ASSERT_EQUAL_INT(1, events[2].type);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp2, events[2].payload, 1);
}

void test_spec_transport_truncation_after_magic1(void)
{
    uint8_t data[] = {0xAA};
    llp_parser_t parser;
    llp_parser_init(&parser);
    int result = 0;
    for (size_t i = 0; i < sizeof(data); i++)
        result = llp_parser_process_byte(&parser, data[i], 0);
    result = llp_parser_process_byte(&parser, 0x00, LLP_FRAME_TIMEOUT_MS + 1);
    TEST_ASSERT_EQUAL_INT(-1, result);
    TEST_ASSERT_EQUAL_INT(LLP_ERR_TIMEOUT, parser.error_code);
}

void test_spec_transport_truncation_after_magic2(void)
{
    uint8_t data[] = {0xAA, 0x55};
    llp_parser_t parser;
    llp_parser_init(&parser);
    int result = 0;
    for (size_t i = 0; i < sizeof(data); i++)
        result = llp_parser_process_byte(&parser, data[i], 0);
    result = llp_parser_process_byte(&parser, 0x00, LLP_FRAME_TIMEOUT_MS + 1);
    TEST_ASSERT_EQUAL_INT(-1, result);
    TEST_ASSERT_EQUAL_INT(LLP_ERR_TIMEOUT, parser.error_code);
}

void test_spec_transport_truncation_after_len_l(void)
{
    uint8_t data[] = {0xAA, 0x55, 0x06};
    llp_parser_t parser;
    llp_parser_init(&parser);
    int result = 0;
    for (size_t i = 0; i < sizeof(data); i++)
        result = llp_parser_process_byte(&parser, data[i], 0);
    result = llp_parser_process_byte(&parser, 0x00, LLP_FRAME_TIMEOUT_MS + 1);
    TEST_ASSERT_EQUAL_INT(-1, result);
    TEST_ASSERT_EQUAL_INT(LLP_ERR_TIMEOUT, parser.error_code);
}

void test_spec_transport_truncation_after_len_h(void)
{
    uint8_t data[] = {0xAA, 0x55, 0x06, 0x00};
    llp_parser_t parser;
    llp_parser_init(&parser);
    int result = 0;
    for (size_t i = 0; i < sizeof(data); i++)
        result = llp_parser_process_byte(&parser, data[i], 0);
    result = llp_parser_process_byte(&parser, 0x00, LLP_FRAME_TIMEOUT_MS + 1);
    TEST_ASSERT_EQUAL_INT(-1, result);
    TEST_ASSERT_EQUAL_INT(LLP_ERR_TIMEOUT, parser.error_code);
}

void test_spec_transport_truncation_mid_payload(void)
{
    uint8_t data[] = {0xAA, 0x55, 0x06, 0x00, 0x00, 0x48};
    llp_parser_t parser;
    llp_parser_init(&parser);
    int result = 0;
    for (size_t i = 0; i < sizeof(data); i++)
        result = llp_parser_process_byte(&parser, data[i], 0);
    result = llp_parser_process_byte(&parser, 0x00, LLP_FRAME_TIMEOUT_MS + 1);
    TEST_ASSERT_EQUAL_INT(-1, result);
    TEST_ASSERT_EQUAL_INT(LLP_ERR_TIMEOUT, parser.error_code);
}

void test_spec_transport_truncation_mid_crc_low(void)
{
    uint8_t data[] = {0xAA, 0x55, 0x06, 0x00, 0x00, 0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x37};
    llp_parser_t parser;
    llp_parser_init(&parser);
    int result = 0;
    for (size_t i = 0; i < sizeof(data); i++)
        result = llp_parser_process_byte(&parser, data[i], 0);
    result = llp_parser_process_byte(&parser, 0x00, LLP_FRAME_TIMEOUT_MS + 1);
    TEST_ASSERT_EQUAL_INT(-1, result);
    TEST_ASSERT_EQUAL_INT(LLP_ERR_TIMEOUT, parser.error_code);
}

void test_spec_transport_truncation_empty_stream(void)
{
    llp_parser_t parser;
    llp_parser_init(&parser);
    TEST_ASSERT_EQUAL_INT(LLP_STATE_WAIT_MAGIC1, parser.state);
}

void test_spec_transport_truncation_magic_only(void)
{
    uint8_t data[] = {0xAA, 0x55};
    llp_parser_t parser;
    llp_parser_init(&parser);
    int result = 0;
    for (size_t i = 0; i < sizeof(data); i++)
        result = llp_parser_process_byte(&parser, data[i], 0);
    TEST_ASSERT_EQUAL_INT(0, result);
    result = llp_parser_process_byte(&parser, 0x00, LLP_FRAME_TIMEOUT_MS + 1);
    TEST_ASSERT_EQUAL_INT(-1, result);
    TEST_ASSERT_EQUAL_INT(LLP_ERR_TIMEOUT, parser.error_code);
}

void test_spec_transport_timeout_mid_frame(void)
{
    llp_parser_t parser;
    llp_parser_init(&parser);
    uint8_t bytes[] = {0xAA, 0x55, 0x06, 0x00};
    unsigned long times[] = {0, 1, 2, 5000};
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream_timed(&parser, bytes, times, 4, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(1, event_count);
    TEST_ASSERT_EQUAL_INT(-1, events[0].type);
    TEST_ASSERT_EQUAL_INT(LLP_ERR_TIMEOUT, events[0].error_code);
}

void test_spec_transport_timeout_then_valid(void)
{
    llp_parser_t parser;
    llp_parser_init(&parser);
    uint8_t bytes[] = {0xAA, 0x55, 0xAA, 0x55, 0x01, 0x00, 0x00, 0x88, 0x83};
    unsigned long times[] = {0, 1, 5000, 5001, 5002, 5003, 5004, 5005, 5006};
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream_timed(&parser, bytes, times, 9, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(2, event_count);
    TEST_ASSERT_EQUAL_INT(-1, events[0].type);
    TEST_ASSERT_EQUAL_INT(LLP_ERR_TIMEOUT, events[0].error_code);
    uint8_t exp[] = {0x00};
    TEST_ASSERT_EQUAL_INT(1, events[1].type);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, events[1].payload, 1);
}

void test_spec_transport_timeout_between_frames(void)
{
    llp_parser_t parser;
    llp_parser_init(&parser);
    uint8_t bytes[] = {0xAA,0x55,0x01,0x00,0x00,0x88,0x83, 0xAA,0x55,0x01,0x00,0x00,0x88,0x83};
    unsigned long times[] = {0,1,2,3,4,5,6, 5000,5001,5002,5003,5004,5005,5006};
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream_timed(&parser, bytes, times, 14, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(2, event_count);
    TEST_ASSERT_EQUAL_INT(1, events[0].type);
    TEST_ASSERT_EQUAL_INT(1, events[1].type);
}

static const encode_vector_t layers_passthrough_encode[] = {
    {"empty_chain", "00", "AA550100008883"},
    {"final_then_ff", "00FF", "AA55020000FFC7AC"},
    {"final_then_aa55", "00AA55", "AA55030000AA00552DE2"},
    {"final_hello", "0048656C6C6F", "AA5506000048656C6C6F3798"},
    {"single_passthrough", "01031020300048656C6C6F", "AA550B0001031020300048656C6C6F6191"},
    {"two_passthrough", "0101AA0202BBCC0042", "AA5509000101AA000202BBCC0042822E"},
    {"unknown_layer_id", "FF01000064617461", "AA550800FF010000646174615B24"},
    {"max_passthrough", "7F02F00F0078797A", "AA5508007F02F00F0078797ACA6E"},
    {"max_transform", "FE01A500010203", "AA550700FE01A5000102030750"},
    {"three_nested", "0101010201020301030064656570", "AA550E000101010201020301030064656570F451"},
    {"stuffing_metadata", "0104AA00AA55004F4B", "AA5509000104AA0000AA0055004F4BC83C"},
    {"zero_meta_len", "01000064617461", "AA55070001000064617461B65F"},
    {"four_nested", "010002000300040000656E64", "AA550C00010002000300040000656E643755"},
    {"passthrough_7F_zero", "7F00004142", "AA5505007F00004142DD3B"},
    {"missing_final_node", "014243", "AA550300014243F13E"},
};

void test_spec_layers_passthrough_encode(void)
{
    for (size_t i = 0; i < sizeof(layers_passthrough_encode) / sizeof(layers_passthrough_encode[0]); i++) {
        const encode_vector_t *v = &layers_passthrough_encode[i];
        verify_encode(v->name, v->input_hex, v->expected_hex);
    }
}

static const decode_frame_vector_t layers_passthrough_decode[] = {
    {"empty_chain", "AA550100008883", "00"},
    {"final_then_ff", "AA55020000FFC7AC", "00FF"},
    {"final_then_aa55", "AA55030000AA00552DE2", "00AA55"},
    {"final_hello", "AA5506000048656C6C6F3798", "0048656C6C6F"},
    {"single_passthrough", "AA550B0001031020300048656C6C6F6191", "01031020300048656C6C6F"},
    {"two_passthrough", "AA5509000101AA000202BBCC0042822E", "0101AA0202BBCC0042"},
    {"unknown_layer_id", "AA550800FF010000646174615B24", "FF01000064617461"},
    {"max_passthrough", "AA5508007F02F00F0078797ACA6E", "7F02F00F0078797A"},
    {"max_transform", "AA550700FE01A5000102030750", "FE01A500010203"},
    {"three_nested", "AA550E000101010201020301030064656570F451", "0101010201020301030064656570"},
    {"stuffing_metadata", "AA5509000104AA0000AA0055004F4BC83C", "0104AA00AA55004F4B"},
    {"zero_meta_len", "AA55070001000064617461B65F", "01000064617461"},
    {"four_nested", "AA550C00010002000300040000656E643755", "010002000300040000656E64"},
    {"passthrough_7F_zero", "AA5505007F00004142DD3B", "7F00004142"},
    {"missing_final_node", "AA550300014243F13E", "014243"},
};

void test_spec_layers_passthrough_decode(void)
{
    for (size_t i = 0; i < sizeof(layers_passthrough_decode) / sizeof(layers_passthrough_decode[0]); i++) {
        const decode_frame_vector_t *v = &layers_passthrough_decode[i];
        verify_decode_frame(v->name, v->frame_hex, v->payload_hex);
    }
}

void test_spec_layers_passthrough_extended_meta_zero(void)
{
#if LLP_MAX_PAYLOAD >= 264
    const char *frame_hex = "AA55090001FF000000646174612057";
    const char *payload_hex = "01FF00000064617461";
    verify_decode_frame("extended_meta_zero", frame_hex, payload_hex);
#endif
}

void test_spec_layers_passthrough_extended_meta_255(void)
{
#if LLP_MAX_PAYLOAD >= 262
    uint8_t payload[SPEC_MAX_FRAME];
    size_t pidx = 0;
    payload[pidx++] = 0x01;
    payload[pidx++] = 0xFF;
    payload[pidx++] = 0x00;
    payload[pidx++] = 0xFF;
    for (int i = 0; i < 255; i++) payload[pidx++] = 0xAA;
    payload[pidx++] = 0x00;
    payload[pidx++] = 0x61;
    payload[pidx++] = 0x62;

    uint8_t frame[SPEC_MAX_FRAME];
    size_t frame_len = llp_build_frame(frame, sizeof(frame), payload, (uint16_t)pidx);
    TEST_ASSERT_GREATER_THAN_size_t_MESSAGE(0, frame_len, "extended_meta_255 build_frame");

    llp_parser_t parser;
    llp_parser_init(&parser);
    int result = spec_feed_frame(&parser, frame, frame_len, NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, result, "extended_meta_255 parse");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE((uint16_t)pidx, parser.frame.payload_len,
                                     "extended_meta_255 payload_len");

    uint8_t out[LLP_MAX_PAYLOAD];
    int final_len = llp_get_final_payload(&parser.frame, out, sizeof(out));
    (void)final_len;
#endif
}

static const encode_vector_t layers_transform_encode[] = {
    {"transform_FE_meta5", "FE05010203040500FF", "AA550900FE05010203040500FF8C62"},
    {"layers_aa_meta", "0102AA00AA0203AABBCC006465616462656566", "AA5513000102AA0000AA000203AA00BBCC0064656164626565661481"},
    {"deep_nested_5", "0100010001000100010000656E64", "AA550E000100010001000100010000656E64A8D3"},
    {"zero_meta_then_final", "010000006162", "AA550600010000006162BE62"},
    {"transform_layer", "8004DEADBEEF004F4B", "AA5509008004DEADBEEF004F4BB396"},
    {"mixed_layers", "010211228101FF0055AA01", "AA550B00010211228101FF0055AA0001C753"},
};

void test_spec_layers_transform_encode(void)
{
    for (size_t i = 0; i < sizeof(layers_transform_encode) / sizeof(layers_transform_encode[0]); i++) {
        const encode_vector_t *v = &layers_transform_encode[i];
        verify_encode(v->name, v->input_hex, v->expected_hex);
    }
}

static const decode_frame_vector_t layers_transform_decode[] = {
    {"transform_FE_meta5", "AA550900FE05010203040500FF8C62", "FE05010203040500FF"},
    {"layers_aa_meta", "AA5513000102AA0000AA000203AA00BBCC0064656164626565661481", "0102AA00AA0203AABBCC006465616462656566"},
    {"deep_nested_5", "AA550E000100010001000100010000656E64A8D3", "0100010001000100010000656E64"},
    {"zero_meta_then_final", "AA550600010000006162BE62", "010000006162"},
    {"transform_layer", "AA5509008004DEADBEEF004F4BB396", "8004DEADBEEF004F4B"},
    {"mixed_layers", "AA550B00010211228101FF0055AA0001C753", "010211228101FF0055AA01"},
    {"transform_no_handler_decode", "AA5509008004DEADBEEF004F4BB396", "8004DEADBEEF004F4B"},
};

void test_spec_layers_transform_decode(void)
{
    for (size_t i = 0; i < sizeof(layers_transform_decode) / sizeof(layers_transform_decode[0]); i++) {
        const decode_frame_vector_t *v = &layers_transform_decode[i];
        verify_decode_frame(v->name, v->frame_hex, v->payload_hex);
    }
}

void test_spec_layers_malformed_truncated_metadata(void)
{
    const char *frame_hex = "AA550800010A10203000486535BD";
    uint8_t frame[SPEC_MAX_FRAME];
    size_t len = spec_hex_to_bytes(frame_hex, frame, sizeof(frame));
    llp_parser_t parser;
    llp_parser_init(&parser);
    int result = spec_feed_frame(&parser, frame, len, NULL);
    TEST_ASSERT_EQUAL_INT(1, result);
    uint8_t out[512];
    int final_len = llp_get_final_payload(&parser.frame, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(-1, final_len);
}

void test_spec_layers_malformed_empty_payload(void)
{
    verify_decode_frame("empty_payload", "AA550100008883", "00");
}

void test_spec_layers_malformed_extended_meta_truncated(void)
{
    const char *frame_hex = "AA55040001FF0000ED0A";
    uint8_t frame[SPEC_MAX_FRAME];
    size_t len = spec_hex_to_bytes(frame_hex, frame, sizeof(frame));
    llp_parser_t parser;
    llp_parser_init(&parser);
    int result = spec_feed_frame(&parser, frame, len, NULL);
    TEST_ASSERT_EQUAL_INT(1, result);
    uint8_t out[512];
    int final_len = llp_get_final_payload(&parser.frame, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(-1, final_len);
}

void test_spec_layers_malformed_reserved_id_FF(void)
{
    verify_decode_frame("reserved_id_FF", "AA550800FF010000646174615B24", "FF01000064617461");
}

void test_spec_layers_traversal_three_passthrough(void)
{
    const char *frame_hex = "AA550E000101010201020301030064656570F451";
    uint8_t frame[SPEC_MAX_FRAME];
    size_t len = spec_hex_to_bytes(frame_hex, frame, sizeof(frame));
    llp_parser_t parser;
    llp_parser_init(&parser);
    int result = spec_feed_frame(&parser, frame, len, NULL);
    TEST_ASSERT_EQUAL_INT(1, result);
    uint8_t out[256];
    int final_len = llp_get_final_payload(&parser.frame, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(4, final_len);
    uint8_t expected[] = {0x64, 0x65, 0x65, 0x70};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 4);
}

void test_spec_layers_traversal_single_passthrough(void)
{
    const char *frame_hex = "AA550B0001031020300048656C6C6F6191";
    uint8_t frame[SPEC_MAX_FRAME];
    size_t len = spec_hex_to_bytes(frame_hex, frame, sizeof(frame));
    llp_parser_t parser;
    llp_parser_init(&parser);
    int result = spec_feed_frame(&parser, frame, len, NULL);
    TEST_ASSERT_EQUAL_INT(1, result);
    uint8_t out[256];
    int final_len = llp_get_final_payload(&parser.frame, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(5, final_len);
    uint8_t expected[] = {0x48, 0x65, 0x6C, 0x6C, 0x6F};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 5);
}

void test_spec_layers_traversal_direct_finalnode(void)
{
    const char *frame_hex = "AA5502000042B1DA";
    uint8_t frame[SPEC_MAX_FRAME];
    size_t len = spec_hex_to_bytes(frame_hex, frame, sizeof(frame));
    llp_parser_t parser;
    llp_parser_init(&parser);
    int result = spec_feed_frame(&parser, frame, len, NULL);
    TEST_ASSERT_EQUAL_INT(1, result);
    uint8_t out[256];
    int final_len = llp_get_final_payload(&parser.frame, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(1, final_len);
    uint8_t expected[] = {0x42};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 1);
}

void test_spec_layers_traversal_empty_final_payload(void)
{
    const char *frame_hex = "AA550100008883";
    uint8_t frame[SPEC_MAX_FRAME];
    size_t len = spec_hex_to_bytes(frame_hex, frame, sizeof(frame));
    llp_parser_t parser;
    llp_parser_init(&parser);
    int result = spec_feed_frame(&parser, frame, len, NULL);
    TEST_ASSERT_EQUAL_INT(1, result);
    uint8_t out[256];
    int final_len = llp_get_final_payload(&parser.frame, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(0, final_len);
}

void test_spec_layers_traversal_transform_blocked(void)
{
    const char *frame_hex = "AA5509008004DEADBEEF004F4BB396";
    uint8_t frame[SPEC_MAX_FRAME];
    size_t len = spec_hex_to_bytes(frame_hex, frame, sizeof(frame));
    llp_parser_t parser;
    llp_parser_init(&parser);
    int result = spec_feed_frame(&parser, frame, len, NULL);
    TEST_ASSERT_EQUAL_INT(1, result);
    uint8_t out[256];
    int final_len = llp_get_final_payload(&parser.frame, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(-1, final_len);
}

void test_spec_parser_incremental_byte_by_byte_hello(void)
{
    const char *chunks[] = {"AA","55","06","00","00","48","65","6C","6C","6F","37","98"};
    uint8_t stream[SPEC_MAX_FRAME];
    size_t stream_len = 0;
    spec_concat_chunks(chunks, 12, stream, &stream_len, sizeof(stream));
    llp_parser_t parser;
    llp_parser_init(&parser);
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream(&parser, stream, stream_len, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(1, event_count);
    uint8_t expected[] = {0x00, 0x48, 0x65, 0x6C, 0x6C, 0x6F};
    TEST_ASSERT_EQUAL_INT(1, events[0].type);
    TEST_ASSERT_EQUAL_UINT16(6, events[0].payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, events[0].payload, 6);
}

void test_spec_parser_incremental_two_bytes(void)
{
    const char *chunks[] = {"AA55","0100","0088","83AA","5502","0000","AA00","97A6"};
    uint8_t stream[SPEC_MAX_FRAME];
    size_t stream_len = 0;
    spec_concat_chunks(chunks, 8, stream, &stream_len, sizeof(stream));
    llp_parser_t parser;
    llp_parser_init(&parser);
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream(&parser, stream, stream_len, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(2, event_count);
    uint8_t exp0[] = {0x00};
    TEST_ASSERT_EQUAL_INT(1, events[0].type);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp0, events[0].payload, 1);
    uint8_t exp1[] = {0x00, 0xAA};
    TEST_ASSERT_EQUAL_INT(1, events[1].type);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp1, events[1].payload, 2);
}

void test_spec_parser_incremental_mixed_chunks(void)
{
    const char *chunks[] = {"AA5501","00008883","AA55060000","48656C6C6F3798"};
    uint8_t stream[SPEC_MAX_FRAME];
    size_t stream_len = 0;
    spec_concat_chunks(chunks, 4, stream, &stream_len, sizeof(stream));
    llp_parser_t parser;
    llp_parser_init(&parser);
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream(&parser, stream, stream_len, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(2, event_count);
}

void test_spec_parser_fragmented_after_magic1(void)
{
    const char *chunks[] = {"AA","55020000AA0097A6"};
    uint8_t stream[SPEC_MAX_FRAME];
    size_t stream_len = 0;
    spec_concat_chunks(chunks, 2, stream, &stream_len, sizeof(stream));
    llp_parser_t parser;
    llp_parser_init(&parser);
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream(&parser, stream, stream_len, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(1, event_count);
    uint8_t exp[] = {0x00, 0xAA};
    TEST_ASSERT_EQUAL_INT(1, events[0].type);
    TEST_ASSERT_EQUAL_UINT16(2, events[0].payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, events[0].payload, 2);
}

void test_spec_parser_fragmented_mid_stuffing(void)
{
    const char *chunks[] = {"AA55020000AA","0097A6"};
    uint8_t stream[SPEC_MAX_FRAME];
    size_t stream_len = 0;
    spec_concat_chunks(chunks, 2, stream, &stream_len, sizeof(stream));
    llp_parser_t parser;
    llp_parser_init(&parser);
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream(&parser, stream, stream_len, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(1, event_count);
    uint8_t exp[] = {0x00, 0xAA};
    TEST_ASSERT_EQUAL_INT(1, events[0].type);
    TEST_ASSERT_EQUAL_UINT16(2, events[0].payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, events[0].payload, 2);
}

void test_spec_parser_fragmented_at_crc_boundary(void)
{
    const char *chunks[] = {"AA55020000AA0097","A6"};
    uint8_t stream[SPEC_MAX_FRAME];
    size_t stream_len = 0;
    spec_concat_chunks(chunks, 2, stream, &stream_len, sizeof(stream));
    llp_parser_t parser;
    llp_parser_init(&parser);
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream(&parser, stream, stream_len, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(1, event_count);
    TEST_ASSERT_EQUAL_INT(1, events[0].type);
    TEST_ASSERT_EQUAL_UINT16(2, events[0].payload_len);
}

void test_spec_parser_recovery_after_crc_error(void)
{
    const char *chunks[] = {"AA55010000887C","AA5506000048656C6C6F3798"};
    uint8_t stream[SPEC_MAX_FRAME];
    size_t stream_len = 0;
    spec_concat_chunks(chunks, 2, stream, &stream_len, sizeof(stream));
    llp_parser_t parser;
    llp_parser_init(&parser);
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream(&parser, stream, stream_len, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(2, event_count);
    TEST_ASSERT_EQUAL_INT(-1, events[0].type);
    TEST_ASSERT_EQUAL_INT(LLP_ERR_CHECKSUM, events[0].error_code);
    uint8_t exp[] = {0x00, 0x48, 0x65, 0x6C, 0x6C, 0x6F};
    TEST_ASSERT_EQUAL_INT(1, events[1].type);
    TEST_ASSERT_EQUAL_UINT16(6, events[1].payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, events[1].payload, 6);
}

void test_spec_parser_recovery_after_sync_error(void)
{
    const char *chunks[] = {"AA55020000AA9997A6","AA550100008883"};
    uint8_t stream[SPEC_MAX_FRAME];
    size_t stream_len = 0;
    spec_concat_chunks(chunks, 2, stream, &stream_len, sizeof(stream));
    llp_parser_t parser;
    llp_parser_init(&parser);
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream(&parser, stream, stream_len, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(2, event_count);
    TEST_ASSERT_EQUAL_INT(-1, events[0].type);
    TEST_ASSERT_EQUAL_INT(LLP_ERR_SYNC, events[0].error_code);
    uint8_t exp[] = {0x00};
    TEST_ASSERT_EQUAL_INT(1, events[1].type);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, events[1].payload, 1);
}

void test_spec_parser_recovery_garbage_then_two_frames(void)
{
    const char *chunks[] = {"DEADBEEF","AA550100008883","AA5506000048656C6C6F3798"};
    uint8_t stream[SPEC_MAX_FRAME * 2];
    size_t stream_len = 0;
    spec_concat_chunks(chunks, 3, stream, &stream_len, sizeof(stream));
    llp_parser_t parser;
    llp_parser_init(&parser);
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream(&parser, stream, stream_len, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(2, event_count);
    uint8_t exp0[] = {0x00};
    TEST_ASSERT_EQUAL_INT(1, events[0].type);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp0, events[0].payload, 1);
    uint8_t exp1[] = {0x00, 0x48, 0x65, 0x6C, 0x6C, 0x6F};
    TEST_ASSERT_EQUAL_INT(1, events[1].type);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp1, events[1].payload, 6);
}

void test_spec_parser_recovery_multiple_errors_then_valid(void)
{
    const char *chunks[] = {"AA55010000887C","AA5506000048656C6C6F3767","AA550100008883"};
    uint8_t stream[SPEC_MAX_FRAME * 2];
    size_t stream_len = 0;
    spec_concat_chunks(chunks, 3, stream, &stream_len, sizeof(stream));
    llp_parser_t parser;
    llp_parser_init(&parser);
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream(&parser, stream, stream_len, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(3, event_count);
    TEST_ASSERT_EQUAL_INT(-1, events[0].type);
    TEST_ASSERT_EQUAL_INT(LLP_ERR_CHECKSUM, events[0].error_code);
    TEST_ASSERT_EQUAL_INT(-1, events[1].type);
    TEST_ASSERT_EQUAL_INT(LLP_ERR_CHECKSUM, events[1].error_code);
    uint8_t exp[] = {0x00};
    TEST_ASSERT_EQUAL_INT(1, events[2].type);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, events[2].payload, 1);
}

void test_bug_timeout_does_not_update_last_byte_time(void)
{
    llp_parser_t parser;
    llp_parser_init(&parser);
    uint8_t bytes[] = {0xAA, 0x55, 0xAA, 0x55, 0x01, 0x00, 0x00, 0x88, 0x83};
    unsigned long times[] = {0, 1, 5000, 5001, 5002, 5003, 5004, 5005, 5006};
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream_timed(&parser, bytes, times, 9, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(2, event_count);
    TEST_ASSERT_EQUAL_INT(-1, events[0].type);
    TEST_ASSERT_EQUAL_INT(LLP_ERR_TIMEOUT, events[0].error_code);
    TEST_ASSERT_EQUAL_INT(1, events[1].type);
    uint8_t expected[] = {0x00};
    TEST_ASSERT_EQUAL_UINT16(1, events[1].payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, events[1].payload, 1);
}

void test_bug_timeout_between_frames(void)
{
    llp_parser_t parser;
    llp_parser_init(&parser);
    uint8_t bytes[] = {0xAA,0x55,0x01,0x00,0x00,0x88,0x83, 0xAA,0x55,0x01,0x00,0x00,0x88,0x83};
    unsigned long times[] = {0,1,2,3,4,5,6, 5000,5001,5002,5003,5004,5005,5006};
    spec_event_t events[SPEC_MAX_EVENTS];
    int event_count = 0;
    spec_feed_stream_timed(&parser, bytes, times, 14, events, &event_count, SPEC_MAX_EVENTS);
    TEST_ASSERT_EQUAL_INT(2, event_count);
    TEST_ASSERT_EQUAL_INT(1, events[0].type);
    uint8_t expected0[] = {0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected0, events[0].payload, 1);
    TEST_ASSERT_EQUAL_INT(1, events[1].type);
    uint8_t expected1[] = {0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected1, events[1].payload, 1);
}

void test_bug_final_payload_returns_zero_without_final_node(void)
{
    uint8_t payload[] = {0x01, 0xFF, 0x00, 0x00};
    llp_frame_t frame;
    frame.payload_len = 4;
    memcpy(frame.payload, payload, 4);
    uint8_t out[64];
    int result = llp_get_final_payload(&frame, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(-1, result);
}