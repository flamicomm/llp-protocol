/* LLP wire format 4.0.0 — unit and conformance tests.
 *
 * The conformance cases come from spec/vectors/v4/*.json, converted to C by
 * `python3 scripts/gen_vectors_v4.py --emit-c test/v4_vectors_generated.h`.
 * Regenerate that header whenever the vectors change.
 */

#include <string.h>

#include <unity.h>

#include "llp_v4.h"
#include "v4_vectors_generated.h"

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static uint8_t tx_buf[600];
static size_t tx_len;

static void tx_sink(void *ctx, uint8_t byte)
{
    (void)ctx;
    if (tx_len < sizeof(tx_buf)) {
        tx_buf[tx_len++] = byte;
    }
}

static size_t encode_stream(uint8_t channel, const uint8_t *payload, uint8_t len)
{
    llp4_tx_t tx;
    uint8_t i;

    tx_len = 0;
    llp4_tx_begin(&tx, channel, len, tx_sink, NULL);
    for (i = 0; i < len; i++) {
        llp4_tx_byte(&tx, payload[i]);
    }
    llp4_tx_end(&tx);
    return tx_len;
}

/* Bitwise reference used to cross-check the (possibly table-driven) CRC. */
static uint16_t crc_ref(uint16_t crc, uint8_t byte)
{
    uint8_t i;
    crc ^= (uint16_t)byte << 8;
    for (i = 0; i < 8; i++) {
        crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                              : (uint16_t)(crc << 1);
    }
    return crc;
}

/* ------------------------------------------------------------------------- */
/* Conformance vectors                                                       */
/* ------------------------------------------------------------------------- */

void test_v4_encode_vectors(void)
{
    size_t i;

    for (i = 0; i < LLP4_VEC_ENCODE_COUNT; i++) {
        const llp4_vec_encode_t *c = &llp4_vec_encode[i];
        uint8_t out[600];

        /* Streaming writer */
        size_t n = encode_stream(c->channel, c->payload, c->payload_len);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(c->frame_len, (uint16_t)n, c->id);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(c->frame, tx_buf, c->frame_len, c->id);

        /* Buffer convenience wrapper */
        size_t m = llp4_encode(c->channel, c->payload, c->payload_len, out,
                               sizeof(out));
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(c->frame_len, (uint16_t)m, c->id);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(c->frame, out, c->frame_len, c->id);

        /* The observed size never exceeds the documented worst case. */
        TEST_ASSERT_TRUE(c->frame_len <= llp4_frame_size(c->payload_len));
    }
}

void test_v4_decode_vectors(void)
{
    uint8_t payload_buf[256];
    size_t i;

    for (i = 0; i < LLP4_VEC_DECODE_COUNT; i++) {
        const llp4_vec_decode_t *c = &llp4_vec_decode[i];
        llp4_parser_t p;
        uint16_t now = 0;
        uint16_t step;
        uint16_t step_count;
        uint8_t frames = 0;
        uint8_t errors = 0;

        llp4_parser_init(&p, payload_buf, c->capacity, c->timeout_ms);
        step_count = (c->steps != NULL) ? c->step_count : c->frame_len;

        for (step = 0; step < step_count; step++) {
            uint8_t byte;
            int rc;

            if (c->steps != NULL) {
                now = (uint16_t)(now + c->steps[step].dt_ms);
                byte = c->steps[step].byte;
            } else {
                byte = c->frame[step];
            }

            rc = llp4_parser_feed(&p, byte, now);

            if (rc == LLP4_EV_FRAME) {
                const llp4_vec_frame_t *f;
                TEST_ASSERT_TRUE_MESSAGE(frames < c->frame_count, c->id);
                f = &c->frames[frames];
                TEST_ASSERT_EQUAL_UINT8_MESSAGE(f->channel, p.channel, c->id);
                TEST_ASSERT_EQUAL_UINT8_MESSAGE(f->channel,
                                                llp4_parser_channel(&p), c->id);
                TEST_ASSERT_EQUAL_UINT8_MESSAGE(f->payload_len, p.length, c->id);
                if (f->payload_len > 0) {
                    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(f->payload, payload_buf,
                                                        f->payload_len, c->id);
                }
                {
                    uint8_t len_out = 0;
                    TEST_ASSERT_EQUAL_PTR_MESSAGE(
                        payload_buf, llp4_parser_payload(&p, &len_out), c->id);
                    TEST_ASSERT_EQUAL_UINT8_MESSAGE(f->payload_len, len_out, c->id);
                }
                frames++;
            } else if (rc < 0) {
                TEST_ASSERT_TRUE_MESSAGE(errors < c->error_count, c->id);
                TEST_ASSERT_EQUAL_UINT8_MESSAGE(c->errors[errors],
                                                (uint8_t)(-rc), c->id);
                TEST_ASSERT_EQUAL_UINT8_MESSAGE(c->errors[errors], p.error, c->id);
                errors++;
            } else {
                TEST_ASSERT_EQUAL_INT_MESSAGE(LLP4_EV_NONE, rc, c->id);
                TEST_ASSERT_EQUAL_UINT8_MESSAGE(LLP4_OK, p.error, c->id);
            }
        }

        TEST_ASSERT_EQUAL_UINT8_MESSAGE(c->frame_count, frames, c->id);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(c->error_count, errors, c->id);
#if LLP4_ENABLE_STATS
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(c->timeouts, (uint8_t)p.timeouts, c->id);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(c->error_count), (uint8_t)p.frames_error);
#endif
    }
}

/* ------------------------------------------------------------------------- */
/* Sizes and CRC                                                             */
/* ------------------------------------------------------------------------- */

void test_v4_frame_size(void)
{
    TEST_ASSERT_EQUAL_UINT16(10, (uint16_t)llp4_frame_size(0));
    TEST_ASSERT_EQUAL_UINT16(12, (uint16_t)llp4_frame_size(1));
    TEST_ASSERT_EQUAL_UINT16(20, (uint16_t)llp4_frame_size(5));
    TEST_ASSERT_EQUAL_UINT16(520, (uint16_t)llp4_frame_size(255));
}

void test_v4_crc_matches_bitwise_reference(void)
{
    uint8_t data[64];
    uint16_t crc_table = 0xFFFFu;
    uint16_t crc_bitwise = 0xFFFFu;
    size_t i;

    for (i = 0; i < sizeof(data); i++) {
        data[i] = (uint8_t)(i * 37u + 11u);
    }

    for (i = 0; i < sizeof(data); i++) {
        crc_table = llp4_crc16_update(crc_table, data[i]);
        crc_bitwise = crc_ref(crc_bitwise, data[i]);
    }

    TEST_ASSERT_EQUAL_HEX16(crc_bitwise, crc_table);
    TEST_ASSERT_EQUAL_HEX16(crc_bitwise, llp4_crc16(data, sizeof(data)));
}

void test_v4_crc_seed_covers_magic(void)
{
    uint16_t expected = crc_ref(crc_ref(0xFFFFu, 0xAAu), 0x55u);
    TEST_ASSERT_EQUAL_HEX16(expected, llp4_crc_seed());
}

void test_v4_parser_state_is_small(void)
{
    /* Non-pointer state: 15 bytes without stats (17 total on AVR), 21 + padding
     * with stats (23 on AVR). Pointers differ per platform, so compare the
     * remainder instead of the absolute size. */
#if LLP4_ENABLE_STATS
    TEST_ASSERT_TRUE(sizeof(llp4_parser_t) - sizeof(uint8_t *) <= 24);
#else
    TEST_ASSERT_TRUE(sizeof(llp4_parser_t) - sizeof(uint8_t *) <= 16);
#endif
}

/* ------------------------------------------------------------------------- */
/* Encoder edges                                                             */
/* ------------------------------------------------------------------------- */

void test_v4_encode_rejects_small_buffer(void)
{
    uint8_t payload[2] = {0x01, 0x02};
    uint8_t out[16];
    size_t need = llp4_frame_size(2);

    TEST_ASSERT_TRUE(llp4_encode(0, payload, 2, out, need - 1) == 0);
    TEST_ASSERT_TRUE(llp4_encode(0, payload, 2, NULL, sizeof(out)) == 0);
    TEST_ASSERT_TRUE(llp4_encode(0, payload, 2, out, need) > 0);
}

void test_v4_encode_empty_payload(void)
{
    uint8_t out[16];
    size_t n = llp4_encode(0, NULL, 0, out, sizeof(out));

    TEST_ASSERT_EQUAL_UINT8(6, (uint8_t)n);
    TEST_ASSERT_EQUAL_HEX8(0xAA, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x55, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[2]); /* LEN */
    TEST_ASSERT_EQUAL_HEX8(0x00, out[3]); /* CHANNEL */
}

void test_v4_encoder_stuffs_everything_but_magic(void)
{
    static const uint8_t expected[] = {0xAA, 0x55, 0x02, 0xAA, 0x00, 0xAA,
                                       0x00, 0xAA, 0x00, 0x7F, 0x2E};
    uint8_t payload[2] = {0xAA, 0xAA};
    uint8_t out[32];
    size_t n = llp4_encode(0xAA, payload, 2, out, sizeof(out));

    TEST_ASSERT_EQUAL_UINT8(sizeof(expected), (uint8_t)n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, sizeof(expected));
}

/* ------------------------------------------------------------------------- */
/* Parser edges                                                              */
/* ------------------------------------------------------------------------- */

void test_v4_zero_capacity_accepts_only_empty_payload(void)
{
    static const uint8_t empty_frame[] = {0xAA, 0x55, 0x00, 0x00, 0x23, 0xB3};
    llp4_parser_t p;
    size_t i;
    int rc = LLP4_EV_NONE;

    llp4_parser_init(&p, NULL, 0, LLP4_DEFAULT_TIMEOUT_MS);
    for (i = 0; i < sizeof(empty_frame); i++) {
        rc = llp4_parser_feed(&p, empty_frame[i], 0);
    }
    TEST_ASSERT_EQUAL_INT(LLP4_EV_FRAME, rc);
    TEST_ASSERT_EQUAL_UINT8(0, p.length);
}

void test_v4_capacity_error_before_payload(void)
{
    llp4_parser_t p;
    uint8_t buf[4];

    llp4_parser_init(&p, buf, sizeof(buf), LLP4_DEFAULT_TIMEOUT_MS);
    TEST_ASSERT_EQUAL_INT(LLP4_EV_NONE, llp4_parser_feed(&p, 0xAA, 0));
    TEST_ASSERT_EQUAL_INT(LLP4_EV_NONE, llp4_parser_feed(&p, 0x55, 0));
    TEST_ASSERT_EQUAL_INT(-LLP4_ERR_LEN, llp4_parser_feed(&p, 5, 0));
    TEST_ASSERT_EQUAL_UINT8(LLP4_ERR_LEN, p.error);
    TEST_ASSERT_EQUAL_UINT8(0, p.index);
}

void test_v4_bad_crc_is_not_delivered_then_recovers(void)
{
    static const uint8_t bad[] = {0xAA, 0x55, 0x03, 0x00, 0x6F, 0x6B, 0x0A,
                                  0x09, 0xDB}; /* wrong CRC */
    static const uint8_t good[] = {0xAA, 0x55, 0x03, 0x00, 0x6F, 0x6B, 0x0A,
                                   0x08, 0xDB};
    llp4_parser_t p;
    uint8_t buf[8];
    size_t i;
    int rc = LLP4_EV_NONE;

    llp4_parser_init(&p, buf, sizeof(buf), LLP4_DEFAULT_TIMEOUT_MS);
    for (i = 0; i < sizeof(bad); i++) {
        rc = llp4_parser_feed(&p, bad[i], 0);
        TEST_ASSERT_NOT_EQUAL(LLP4_EV_FRAME, rc);
    }
    TEST_ASSERT_EQUAL_INT(-LLP4_ERR_CRC, rc);

    for (i = 0; i < sizeof(good); i++) {
        rc = llp4_parser_feed(&p, good[i], 0);
    }
    TEST_ASSERT_EQUAL_INT(LLP4_EV_FRAME, rc);
    TEST_ASSERT_EQUAL_UINT8(3, p.length);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(good + 4, buf, 3);
}

void test_v4_timeout_wraps_at_16_bits(void)
{
    static const uint8_t frame[] = {0xAA, 0x55, 0x03, 0x00,
                                    0x6F, 0x6B, 0x0A, 0x08, 0xDB};
    llp4_parser_t p;
    uint8_t buf[8];
    uint16_t now = 0xFF00u; /* wraps in the middle of the frame */
    size_t i;
    int rc = LLP4_EV_NONE;

    llp4_parser_init(&p, buf, sizeof(buf), LLP4_DEFAULT_TIMEOUT_MS);
    for (i = 0; i < sizeof(frame); i++) {
        rc = llp4_parser_feed(&p, frame[i], now);
        now = (uint16_t)(now + 100u);
    }
    TEST_ASSERT_EQUAL_INT(LLP4_EV_FRAME, rc);
    TEST_ASSERT_EQUAL_UINT8(3, p.length);
}
