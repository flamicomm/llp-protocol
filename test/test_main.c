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

    return UNITY_END();
}
