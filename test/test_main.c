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

/* Spec: Transport Valid - Encode */
void test_spec_transport_valid_encode(void);

/* Spec: Transport Valid - Decode */
void test_spec_transport_valid_decode(void);

/* Spec: Transport Valid - Stream */
void test_spec_transport_valid_stream_two_empty(void);
void test_spec_transport_valid_stream_empty_then_hello(void);
void test_spec_transport_valid_stream_three_mixed(void);

/* Spec: Transport CRC */
void test_spec_transport_crc(void);

/* Spec: Transport Stuffing */
void test_spec_transport_stuffing_valid(void);
void test_spec_transport_stuffing_invalid(void);

/* Spec: Transport Resync */
void test_spec_transport_resync_noise_before_frame(void);
void test_spec_transport_resync_noise_between_frames(void);
void test_spec_transport_resync_corrupt_magic1(void);
void test_spec_transport_resync_corrupt_magic2(void);
void test_spec_transport_resync_aa_no_false_resync(void);
void test_spec_transport_resync_aa55_no_false_resync(void);
void test_spec_transport_resync_invalid_escape_then_valid(void);
void test_spec_transport_resync_garbage_three_frames(void);

/* Spec: Transport Truncation */
void test_spec_transport_truncation_after_magic1(void);
void test_spec_transport_truncation_after_magic2(void);
void test_spec_transport_truncation_after_len_l(void);
void test_spec_transport_truncation_after_len_h(void);
void test_spec_transport_truncation_mid_payload(void);
void test_spec_transport_truncation_mid_crc_low(void);
void test_spec_transport_truncation_empty_stream(void);
void test_spec_transport_truncation_magic_only(void);

/* Spec: Transport Timeout */
void test_spec_transport_timeout_mid_frame(void);
void test_spec_transport_timeout_then_valid(void);
void test_spec_transport_timeout_between_frames(void);

/* Spec: Layers Passthrough */
void test_spec_layers_passthrough_encode(void);
void test_spec_layers_passthrough_decode(void);
void test_spec_layers_passthrough_extended_meta_zero(void);
void test_spec_layers_passthrough_extended_meta_255(void);

/* Spec: Layers Transform */
void test_spec_layers_transform_encode(void);
void test_spec_layers_transform_decode(void);

/* Spec: Layers Malformed */
void test_spec_layers_malformed_truncated_metadata(void);
void test_spec_layers_malformed_empty_payload(void);
void test_spec_layers_malformed_extended_meta_truncated(void);
void test_spec_layers_malformed_reserved_id_FF(void);

/* Spec: Layers Traversal */
void test_spec_layers_traversal_three_passthrough(void);
void test_spec_layers_traversal_single_passthrough(void);
void test_spec_layers_traversal_direct_finalnode(void);
void test_spec_layers_traversal_empty_final_payload(void);
void test_spec_layers_traversal_transform_blocked(void);

/* Spec: Parser Incremental */
void test_spec_parser_incremental_byte_by_byte_hello(void);
void test_spec_parser_incremental_two_bytes(void);
void test_spec_parser_incremental_mixed_chunks(void);

/* Spec: Parser Fragmented */
void test_spec_parser_fragmented_after_magic1(void);
void test_spec_parser_fragmented_mid_stuffing(void);
void test_spec_parser_fragmented_at_crc_boundary(void);

/* Spec: Parser Recovery */
void test_spec_parser_recovery_after_crc_error(void);
void test_spec_parser_recovery_after_sync_error(void);
void test_spec_parser_recovery_garbage_then_two_frames(void);
void test_spec_parser_recovery_multiple_errors_then_valid(void);

/* Bug regression tests */
void test_bug_timeout_does_not_update_last_byte_time(void);
void test_bug_timeout_between_frames(void);
void test_bug_final_payload_returns_zero_without_final_node(void);

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

    /* Spec: Transport Valid Encode (31 vectors) */
    RUN_TEST(test_spec_transport_valid_encode);

    /* Spec: Transport Valid Decode (31 vectors) */
    RUN_TEST(test_spec_transport_valid_decode);

    /* Spec: Transport Valid Stream (3 vectors) */
    RUN_TEST(test_spec_transport_valid_stream_two_empty);
    RUN_TEST(test_spec_transport_valid_stream_empty_then_hello);
    RUN_TEST(test_spec_transport_valid_stream_three_mixed);

    /* Spec: Transport CRC (28 vectors) */
    RUN_TEST(test_spec_transport_crc);

    /* Spec: Transport Stuffing (4 valid + 4 invalid) */
    RUN_TEST(test_spec_transport_stuffing_valid);
    RUN_TEST(test_spec_transport_stuffing_invalid);

    /* Spec: Transport Resync (8 vectors) */
    RUN_TEST(test_spec_transport_resync_noise_before_frame);
    RUN_TEST(test_spec_transport_resync_noise_between_frames);
    RUN_TEST(test_spec_transport_resync_corrupt_magic1);
    RUN_TEST(test_spec_transport_resync_corrupt_magic2);
    RUN_TEST(test_spec_transport_resync_aa_no_false_resync);
    RUN_TEST(test_spec_transport_resync_aa55_no_false_resync);
    RUN_TEST(test_spec_transport_resync_invalid_escape_then_valid);
    RUN_TEST(test_spec_transport_resync_garbage_three_frames);

    /* Spec: Transport Truncation (8 vectors) */
    RUN_TEST(test_spec_transport_truncation_after_magic1);
    RUN_TEST(test_spec_transport_truncation_after_magic2);
    RUN_TEST(test_spec_transport_truncation_after_len_l);
    RUN_TEST(test_spec_transport_truncation_after_len_h);
    RUN_TEST(test_spec_transport_truncation_mid_payload);
    RUN_TEST(test_spec_transport_truncation_mid_crc_low);
    RUN_TEST(test_spec_transport_truncation_empty_stream);
    RUN_TEST(test_spec_transport_truncation_magic_only);

    /* Spec: Transport Timeout (3 vectors) */
    RUN_TEST(test_spec_transport_timeout_mid_frame);
    RUN_TEST(test_spec_transport_timeout_then_valid);
    RUN_TEST(test_spec_transport_timeout_between_frames);

    /* Spec: Layers Passthrough (15 encode + 15 decode + 2 extended) */
    RUN_TEST(test_spec_layers_passthrough_encode);
    RUN_TEST(test_spec_layers_passthrough_decode);
    RUN_TEST(test_spec_layers_passthrough_extended_meta_zero);
    RUN_TEST(test_spec_layers_passthrough_extended_meta_255);

    /* Spec: Layers Transform (6 encode + 7 decode) */
    RUN_TEST(test_spec_layers_transform_encode);
    RUN_TEST(test_spec_layers_transform_decode);

    /* Spec: Layers Malformed (4 vectors) */
    RUN_TEST(test_spec_layers_malformed_truncated_metadata);
    RUN_TEST(test_spec_layers_malformed_empty_payload);
    RUN_TEST(test_spec_layers_malformed_extended_meta_truncated);
    RUN_TEST(test_spec_layers_malformed_reserved_id_FF);

    /* Spec: Layers Traversal (5 vectors) */
    RUN_TEST(test_spec_layers_traversal_three_passthrough);
    RUN_TEST(test_spec_layers_traversal_single_passthrough);
    RUN_TEST(test_spec_layers_traversal_direct_finalnode);
    RUN_TEST(test_spec_layers_traversal_empty_final_payload);
    RUN_TEST(test_spec_layers_traversal_transform_blocked);

    /* Spec: Parser Incremental (3 vectors) */
    RUN_TEST(test_spec_parser_incremental_byte_by_byte_hello);
    RUN_TEST(test_spec_parser_incremental_two_bytes);
    RUN_TEST(test_spec_parser_incremental_mixed_chunks);

    /* Spec: Parser Fragmented (3 vectors) */
    RUN_TEST(test_spec_parser_fragmented_after_magic1);
    RUN_TEST(test_spec_parser_fragmented_mid_stuffing);
    RUN_TEST(test_spec_parser_fragmented_at_crc_boundary);

    /* Spec: Parser Recovery (4 vectors) */
    RUN_TEST(test_spec_parser_recovery_after_crc_error);
    RUN_TEST(test_spec_parser_recovery_after_sync_error);
    RUN_TEST(test_spec_parser_recovery_garbage_then_two_frames);
    RUN_TEST(test_spec_parser_recovery_multiple_errors_then_valid);

    /* Bug tests */
    RUN_TEST(test_bug_timeout_does_not_update_last_byte_time);
    RUN_TEST(test_bug_timeout_between_frames);
    RUN_TEST(test_bug_final_payload_returns_zero_without_final_node);

    return UNITY_END();
}