#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

/* ParserInit */
void test_init_sets_state_wait_magic1(void);
void test_init_clears_all_fields(void);
void test_init_clears_frame_payload_len(void);
void test_init_can_be_reinitialized(void);
void test_init_resets_stats(void);

/* FrameBuilder */
void test_fb_final_payload_empty(void);
void test_fb_final_payload_with_data(void);
void test_fb_final_payload_small_buffer(void);
void test_fb_magic_bytes(void);
void test_fb_payload_length_field(void);
void test_fb_roundtrip(void);
void test_fb_small_buffer(void);
void test_fb_oversized_payload(void);
void test_fb_stuffing(void);
void test_fb_deterministic(void);
void test_fb_empty_payload_chain(void);

/* ParserProcess */
void test_pp_incomplete_frame_returns_zero(void);
void test_pp_complete_frame_returns_one(void);
void test_pp_extracts_correct_data(void);
void test_pp_crc_error_returns_negative_one(void);
void test_pp_timeout_returns_negative_one(void);
void test_pp_recovers_from_noise(void);
void test_pp_multiple_frames(void);
void test_pp_increments_error_counter(void);
void test_pp_stuffed_byte(void);
void test_pp_sync_error_on_aa55_inside_frame(void);
void test_pp_payload_len_error(void);

/* Crc */
void test_crc_known_values(void);
void test_crc_differs_for_different_payloads(void);
void test_crc_detects_bit_flip(void);
void test_crc_detects_burst_error(void);
void test_crc_deterministic(void);
void test_crc_update_chain(void);
void test_crc_order_matters(void);
void test_crc_empty_input(void);

int main(void)
{
    UNITY_BEGIN();

    /* ParserInit (5 tests) */
    RUN_TEST(test_init_sets_state_wait_magic1);
    RUN_TEST(test_init_clears_all_fields);
    RUN_TEST(test_init_clears_frame_payload_len);
    RUN_TEST(test_init_can_be_reinitialized);
    RUN_TEST(test_init_resets_stats);

    /* FrameBuilder (11 tests) */
    RUN_TEST(test_fb_final_payload_empty);
    RUN_TEST(test_fb_final_payload_with_data);
    RUN_TEST(test_fb_final_payload_small_buffer);
    RUN_TEST(test_fb_magic_bytes);
    RUN_TEST(test_fb_payload_length_field);
    RUN_TEST(test_fb_roundtrip);
    RUN_TEST(test_fb_small_buffer);
    RUN_TEST(test_fb_oversized_payload);
    RUN_TEST(test_fb_stuffing);
    RUN_TEST(test_fb_deterministic);
    RUN_TEST(test_fb_empty_payload_chain);

    /* ParserProcess (11 tests) */
    RUN_TEST(test_pp_incomplete_frame_returns_zero);
    RUN_TEST(test_pp_complete_frame_returns_one);
    RUN_TEST(test_pp_extracts_correct_data);
    RUN_TEST(test_pp_crc_error_returns_negative_one);
    RUN_TEST(test_pp_timeout_returns_negative_one);
    RUN_TEST(test_pp_recovers_from_noise);
    RUN_TEST(test_pp_multiple_frames);
    RUN_TEST(test_pp_increments_error_counter);
    RUN_TEST(test_pp_stuffed_byte);
    RUN_TEST(test_pp_sync_error_on_aa55_inside_frame);
    RUN_TEST(test_pp_payload_len_error);

    /* Crc (8 tests) */
    RUN_TEST(test_crc_known_values);
    RUN_TEST(test_crc_differs_for_different_payloads);
    RUN_TEST(test_crc_detects_bit_flip);
    RUN_TEST(test_crc_detects_burst_error);
    RUN_TEST(test_crc_deterministic);
    RUN_TEST(test_crc_update_chain);
    RUN_TEST(test_crc_order_matters);
    RUN_TEST(test_crc_empty_input);

    return UNITY_END();
}
