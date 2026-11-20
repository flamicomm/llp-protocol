/* LLP wire format 4.0.0 — unit and conformance tests.
 *
 * The conformance cases are the canonical corpus of llp-spec, converted to C
 * by `python3 ../llp-spec/tools/build_vectors.py --emit-c
 * test/v4_vectors_generated.h` (or `sh scripts/sync_corpus.sh`, which also
 * vendors the JSON and its manifest). `scripts/check_corpus.py` fails if the
 * committed header or the vendored corpus drifts. Never edit the header.
 *
 * Profile R1's nine wire vectors are not emitted by `--emit-c` yet; they are
 * covered by test_v4_rel.c (see docs/CONFORMANCE.md, L-I1b).
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

    /* Corpus pin: the canonical llp-spec corpus emits 60 encode cases
     * (the 6 R1 encode vectors are not emitted yet, see CONFORMANCE.md).
     * A truncated fixture fails here before the loop can pass vacuously. */
    TEST_ASSERT_EQUAL_UINT16(60, (uint16_t)LLP4_VEC_ENCODE_COUNT);

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

    /* Corpus pin: 101 decode cases (decode + errors + timing + channels +
     * payload + streams) from llp-spec's canonical corpus; the 3 R1 decode
     * vectors are not emitted yet (see CONFORMANCE.md). */
    TEST_ASSERT_EQUAL_UINT16(101, (uint16_t)LLP4_VEC_DECODE_COUNT);

    for (i = 0; i < LLP4_VEC_DECODE_COUNT; i++) {
        const llp4_vec_decode_t *c = &llp4_vec_decode[i];
        llp4_parser_t p;
        uint16_t now = 0;
        uint16_t step;
        uint8_t frames = 0;
        uint8_t errors = 0;

        llp4_parser_init(&p, payload_buf, c->capacity, c->timeout_ms);

        /* The canonical fixture always carries the per-byte steps (compact
         * frames and chunked streams are flattened with dt_ms = 0). */
        TEST_ASSERT_NOT_NULL_MESSAGE(c->steps, c->id);
        for (step = 0; step < c->step_count; step++) {
            uint8_t byte;
            int rc;

            now = (uint16_t)(now + c->steps[step].dt_ms);
            byte = c->steps[step].byte;

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

/* KAT normativo (SPEC-v4 §4): CRC16-CCITT-FALSE over the check string
 * "123456789" is 0x29B1. This anchors both CRC builds (`LLP4_CRC_TABLE` 1 and
 * 0) to the polynomial of the norm, not just to each other. */
void test_v4_crc_kat_check_string(void)
{
    static const uint8_t kat[] = "123456789";
    const size_t len = sizeof(kat) - 1u;

    TEST_ASSERT_EQUAL_UINT8(9, (uint8_t)len);
    TEST_ASSERT_EQUAL_HEX16(0x29B1, llp4_crc16(kat, len));
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

/* Registry: a NULL buffer with a non-zero capacity used to store the payload
 * through the null pointer (SIGSEGV). The guard forces capacity 0, so a frame
 * with LEN > 0 is rejected with LLP4_ERR_LEN before any byte is written. */
void test_v4_parser_init_null_buffer_forces_zero_capacity(void)
{
    static const uint8_t frame3[] = {0xAA, 0x55, 0x03, 0x00,
                                     0x6F, 0x6B, 0x0A, 0x08, 0xDB};
    static const uint8_t empty_frame[] = {0xAA, 0x55, 0x00, 0x00, 0x23, 0xB3};
    llp4_parser_t p;
    size_t i;
    int rc = LLP4_EV_NONE;

    llp4_parser_init(&p, NULL, 16, LLP4_DEFAULT_TIMEOUT_MS);
    TEST_ASSERT_EQUAL_UINT8(0, p.capacity);

    for (i = 0; i < sizeof(frame3); i++) {
        rc = llp4_parser_feed(&p, frame3[i], 0);
        TEST_ASSERT_NOT_EQUAL(LLP4_EV_FRAME, rc);
        if (i == 2) { /* LEN byte: rejected before any payload store */
            TEST_ASSERT_EQUAL_INT(-LLP4_ERR_LEN, rc);
            TEST_ASSERT_EQUAL_UINT8(LLP4_ERR_LEN, p.error);
            TEST_ASSERT_EQUAL_UINT8(0, p.index);
        }
    }

    /* The parser is still usable: an empty payload is the only valid frame. */
    for (i = 0; i < sizeof(empty_frame); i++) {
        rc = llp4_parser_feed(&p, empty_frame[i], 0);
    }
    TEST_ASSERT_EQUAL_INT(LLP4_EV_FRAME, rc);
    TEST_ASSERT_EQUAL_UINT8(0, p.length);
}

/* Registry: timeout_ms accepted 0 (any inter-byte gap times out, so no
 * multi-byte frame can arrive) and values above the 16-bit wrap-safe limit.
 * init clamps both. */
void test_v4_parser_init_clamps_timeout(void)
{
    static const uint8_t frame3[] = {0xAA, 0x55, 0x03, 0x00,
                                     0x6F, 0x6B, 0x0A, 0x08, 0xDB};
    llp4_parser_t p;
    uint8_t buf[8];
    size_t i;
    int rc = LLP4_EV_NONE;

    TEST_ASSERT_EQUAL_UINT32(32767u, (uint32_t)LLP4_TIMEOUT_MAX_MS);

    llp4_parser_init(&p, buf, sizeof(buf), 0);
    TEST_ASSERT_EQUAL_UINT16(LLP4_DEFAULT_TIMEOUT_MS, p.timeout_ms);

    llp4_parser_init(&p, buf, sizeof(buf), 60000);
    TEST_ASSERT_EQUAL_UINT16(LLP4_TIMEOUT_MAX_MS, p.timeout_ms);

    llp4_parser_init(&p, buf, sizeof(buf), 500); /* in range: untouched */
    TEST_ASSERT_EQUAL_UINT16(500, p.timeout_ms);

    /* Behavioural check: with the clamped default, 1 ms per byte is fine. */
    llp4_parser_init(&p, buf, sizeof(buf), 0);
    for (i = 0; i < sizeof(frame3); i++) {
        rc = llp4_parser_feed(&p, frame3[i], (uint16_t)i);
    }
    TEST_ASSERT_EQUAL_INT(LLP4_EV_FRAME, rc);
    TEST_ASSERT_EQUAL_UINT8(3, p.length);
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

/* Registry (L-M4): a stray 0xAA followed by a gap is a half magic, and the
 * inter-byte timeout applies to WAIT_MAGIC2 too. The offending byte is
 * re-examined as a magic, so the link recovers on the next frame. */
void test_v4_timeout_while_waiting_for_magic2(void)
{
    static const uint8_t good[] = {0xAA, 0x55, 0x03, 0x00,
                                   0x6F, 0x6B, 0x0A, 0x08, 0xDB};
    llp4_parser_t p;
    uint8_t buf[8];
    uint16_t now = 0;
    size_t i;
    int rc = LLP4_EV_NONE;

    llp4_parser_init(&p, buf, sizeof(buf), 100);
    TEST_ASSERT_EQUAL_INT(LLP4_EV_NONE, llp4_parser_feed(&p, 0xAA, now));
    TEST_ASSERT_EQUAL_UINT8(LLP4_ST_WAIT_MAGIC2, p.state);

    /* 101 ms later the half magic is stale: 0x55 is not a magic, so the
     * timeout is reported and the parser returns to WAIT_MAGIC1. */
    now = 101;
    TEST_ASSERT_EQUAL_INT(-LLP4_ERR_TIMEOUT, llp4_parser_feed(&p, 0x55, now));
    TEST_ASSERT_EQUAL_UINT8(LLP4_ERR_TIMEOUT, p.error);
    TEST_ASSERT_EQUAL_UINT8(LLP4_ST_WAIT_MAGIC1, p.state);

    /* Recovery: the next good frame is delivered at a normal cadence. */
    for (i = 0; i < sizeof(good); i++) {
        now = (uint16_t)(now + 1u);
        rc = llp4_parser_feed(&p, good[i], now);
        if (i + 1u < sizeof(good)) {
            TEST_ASSERT_EQUAL_INT(LLP4_EV_NONE, rc);
        }
    }
    TEST_ASSERT_EQUAL_INT(LLP4_EV_FRAME, rc);
    TEST_ASSERT_EQUAL_UINT8(3, p.length);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(good + 4, buf, 3);

    /* A timing-out 0xAA is re-examined as a new magic instead of being
     * discarded: the following 0x55 completes a fresh frame header. */
    llp4_parser_init(&p, buf, sizeof(buf), 100);
    TEST_ASSERT_EQUAL_INT(LLP4_EV_NONE, llp4_parser_feed(&p, 0xAA, 0));
    TEST_ASSERT_EQUAL_INT(-LLP4_ERR_TIMEOUT, llp4_parser_feed(&p, 0xAA, 200));
    TEST_ASSERT_EQUAL_UINT8(LLP4_ST_WAIT_MAGIC2, p.state);

    now = 200;
    for (i = 1; i < sizeof(good); i++) { /* 0xAA is already in the parser */
        now = (uint16_t)(now + 1u);
        rc = llp4_parser_feed(&p, good[i], now);
    }
    TEST_ASSERT_EQUAL_INT(LLP4_EV_FRAME, rc);
    TEST_ASSERT_EQUAL_UINT8(3, p.length);
}
