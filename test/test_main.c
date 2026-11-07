#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

/* Wire format 4.0.0 core (include/llp_v4.h) */
void test_v4_encode_vectors(void);
void test_v4_decode_vectors(void);
void test_v4_frame_size(void);
void test_v4_crc_matches_bitwise_reference(void);
void test_v4_crc_seed_covers_magic(void);
void test_v4_parser_state_is_small(void);
void test_v4_encode_rejects_small_buffer(void);
void test_v4_encode_empty_payload(void);
void test_v4_encoder_stuffs_everything_but_magic(void);
void test_v4_zero_capacity_accepts_only_empty_payload(void);
void test_v4_capacity_error_before_payload(void);
void test_v4_bad_crc_is_not_delivered_then_recovers(void);
void test_v4_timeout_wraps_at_16_bits(void);

/* Ring helper (include/llp_v4_ring.h) */
void test_v4_ring_init(void);
void test_v4_ring_push_pop(void);
void test_v4_ring_fills_to_capacity(void);
void test_v4_ring_wraps_indices(void);
void test_v4_ring_smallest_size(void);

/* Profile R1 (include/llp_v4_rel.h) — see llp-spec docs/RELIABILITY.md */
void test_rel_tx_starts_idle(void);
void test_rel_tx_defaults(void);
void test_rel_tx_start_builds_payload(void);
void test_rel_tx_start_rejects_busy_window(void);
void test_rel_tx_start_rejects_oversized_data(void);
void test_rel_tx_retransmits_identical_payload_after_timeout(void);
void test_rel_tx_exhausts_and_backs_off(void);
void test_rel_tx_ack_releases_window(void);
void test_rel_tx_abort_releases_window(void);
void test_rel_tx_time_wraps_at_16_bits(void);
void test_rel_rx_first_frame_and_duplicate(void);
void test_rel_rx_sequence_wrap(void);
void test_rel_rx_sequence_jump_is_accepted(void);
void test_rel_trace_t1_clean_delivery(void);
void test_rel_trace_t2_lost_command(void);
void test_rel_trace_t3_lost_ack(void);
void test_rel_trace_t4_duplicate_with_other_data(void);
void test_rel_trace_t5_wrap(void);
void test_rel_trace_t6_exhaustion(void);
void test_rel_trace_t7_unknown_ack(void);
void test_rel_trace_t8_late_duplicate_after_exhaustion(void);
void test_rel_trace_t9_hello_resets_session(void);
void test_rel_payload_fits_one_frame(void);
void test_rel_roundtrip_command_and_ack(void);

int main(void)
{
    UNITY_BEGIN();

    /* Conformance vectors (spec/vectors/v4/, generated into test/) */
    RUN_TEST(test_v4_encode_vectors);
    RUN_TEST(test_v4_decode_vectors);

    /* Core behaviour */
    RUN_TEST(test_v4_frame_size);
    RUN_TEST(test_v4_crc_matches_bitwise_reference);
    RUN_TEST(test_v4_crc_seed_covers_magic);
    RUN_TEST(test_v4_parser_state_is_small);
    RUN_TEST(test_v4_encode_rejects_small_buffer);
    RUN_TEST(test_v4_encode_empty_payload);
    RUN_TEST(test_v4_encoder_stuffs_everything_but_magic);
    RUN_TEST(test_v4_zero_capacity_accepts_only_empty_payload);
    RUN_TEST(test_v4_capacity_error_before_payload);
    RUN_TEST(test_v4_bad_crc_is_not_delivered_then_recovers);
    RUN_TEST(test_v4_timeout_wraps_at_16_bits);

    /* Ring helper */
    RUN_TEST(test_v4_ring_init);
    RUN_TEST(test_v4_ring_push_pop);
    RUN_TEST(test_v4_ring_fills_to_capacity);
    RUN_TEST(test_v4_ring_wraps_indices);
    RUN_TEST(test_v4_ring_smallest_size);

    /* Profile R1: reliable command channel */
    RUN_TEST(test_rel_tx_starts_idle);
    RUN_TEST(test_rel_tx_defaults);
    RUN_TEST(test_rel_tx_start_builds_payload);
    RUN_TEST(test_rel_tx_start_rejects_busy_window);
    RUN_TEST(test_rel_tx_start_rejects_oversized_data);
    RUN_TEST(test_rel_tx_retransmits_identical_payload_after_timeout);
    RUN_TEST(test_rel_tx_exhausts_and_backs_off);
    RUN_TEST(test_rel_tx_ack_releases_window);
    RUN_TEST(test_rel_tx_abort_releases_window);
    RUN_TEST(test_rel_tx_time_wraps_at_16_bits);
    RUN_TEST(test_rel_rx_first_frame_and_duplicate);
    RUN_TEST(test_rel_rx_sequence_wrap);
    RUN_TEST(test_rel_rx_sequence_jump_is_accepted);
    RUN_TEST(test_rel_trace_t1_clean_delivery);
    RUN_TEST(test_rel_trace_t2_lost_command);
    RUN_TEST(test_rel_trace_t3_lost_ack);
    RUN_TEST(test_rel_trace_t4_duplicate_with_other_data);
    RUN_TEST(test_rel_trace_t5_wrap);
    RUN_TEST(test_rel_trace_t6_exhaustion);
    RUN_TEST(test_rel_trace_t7_unknown_ack);
    RUN_TEST(test_rel_trace_t8_late_duplicate_after_exhaustion);
    RUN_TEST(test_rel_trace_t9_hello_resets_session);
    RUN_TEST(test_rel_payload_fits_one_frame);
    RUN_TEST(test_rel_roundtrip_command_and_ack);

    return UNITY_END();
}
