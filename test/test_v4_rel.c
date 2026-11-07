/* LLP v4 Profile R1 (llp_v4_rel.h): unit tests and conformance traces.
 *
 * The traces T1..T8 come from llp-spec docs/RELIABILITY.md §11; they are the
 * behavioural contract of the profile. The last test round-trips the profile
 * payloads through the real wire format (llp_v4.h) so the two headers cannot
 * drift apart.
 */

#include <string.h>

#include <unity.h>

#include "llp_v4.h"
#include "llp_v4_rel.h"

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static uint8_t wire[300];
static size_t wire_len;

static void wire_sink(void *ctx, uint8_t byte)
{
    (void)ctx;
    if (wire_len < sizeof(wire)) {
        wire[wire_len++] = byte;
    }
}

static size_t encode_frame(uint8_t channel, const uint8_t *payload, uint8_t len)
{
    llp4_tx_t tx;
    uint8_t i;

    wire_len = 0;
    llp4_tx_begin(&tx, channel, len, wire_sink, NULL);
    for (i = 0; i < len; i++) {
        llp4_tx_byte(&tx, payload[i]);
    }
    llp4_tx_end(&tx);
    return wire_len;
}

/* Decodes a single frame and returns the channel (payload copied to `out`). */
static uint8_t decode_frame(const uint8_t *bytes, size_t n, uint8_t *out,
                            uint8_t *out_len)
{
    static uint8_t buf[64];
    llp4_parser_t p;
    size_t i;

    llp4_parser_init(&p, buf, sizeof(buf), 2000);
    for (i = 0; i < n; i++) {
        int rc = llp4_parser_feed(&p, bytes[i], (uint16_t)i);
        if (rc == LLP4_EV_FRAME) {
            memcpy(out, buf, p.length);
            *out_len = p.length;
            return p.channel;
        }
        if (rc < 0) {
            return 0xFFu;
        }
    }
    return 0xFFu;
}

/* Sends the outstanding tx payload on the command channel. */
static size_t transmit(llp4_rel_tx_t *tx)
{
    uint8_t len = 0;
    const uint8_t *payload = llp4_rel_tx_payload(tx, &len);
    return encode_frame(LLP4_REL_CH_COMMAND, payload, len);
}

/* Builds an ACK frame for `seq`. */
static size_t ack_frame(uint8_t seq)
{
    return encode_frame(LLP4_REL_CH_ACK, &seq, 1);
}

/* ------------------------------------------------------------------------- */
/* Sender                                                                    */
/* ------------------------------------------------------------------------- */

void test_rel_tx_starts_idle(void)
{
    llp4_rel_tx_t tx;
    uint8_t len = 0xFF;

    llp4_rel_tx_init(&tx, 50, 5, 1000);
    TEST_ASSERT_EQUAL_UINT8(0, tx.active);
    TEST_ASSERT_EQUAL_UINT16(50, tx.timeout_ms);
    TEST_ASSERT_EQUAL_UINT8(5, tx.max_retries);
    TEST_ASSERT_EQUAL_INT(LLP4_REL_TX_IDLE, llp4_rel_tx_service(&tx, 0));
    TEST_ASSERT_NOT_NULL(llp4_rel_tx_payload(&tx, &len));
    TEST_ASSERT_EQUAL_UINT8(0, len);
}

void test_rel_tx_defaults(void)
{
    llp4_rel_tx_t tx;

    llp4_rel_tx_init(&tx, 0, 3, 0);
    TEST_ASSERT_EQUAL_UINT16(LLP4_REL_DEFAULT_ACK_TIMEOUT_MS, tx.timeout_ms);
    TEST_ASSERT_EQUAL_UINT16(LLP4_REL_DEFAULT_BACKOFF_MS, tx.backoff_ms);
}

void test_rel_tx_start_builds_payload(void)
{
    llp4_rel_tx_t tx;
    static const uint8_t data[] = "G1 X0\n";
    const uint8_t *payload;
    uint8_t len = 0;

    llp4_rel_tx_init(&tx, 50, 5, 1000);
    TEST_ASSERT_EQUAL_INT(1, llp4_rel_tx_start(&tx, 7, data, sizeof(data) - 1, 100));

    payload = llp4_rel_tx_payload(&tx, &len);
    TEST_ASSERT_EQUAL_UINT8(1 + sizeof(data) - 1, len);
    TEST_ASSERT_EQUAL_UINT8(7, payload[0]);
    TEST_ASSERT_EQUAL_MEMORY(data, payload + 1, sizeof(data) - 1);
    TEST_ASSERT_EQUAL_UINT8(7, tx.seq);
}

void test_rel_tx_start_rejects_busy_window(void)
{
    llp4_rel_tx_t tx;

    llp4_rel_tx_init(&tx, 50, 5, 1000);
    TEST_ASSERT_EQUAL_INT(1, llp4_rel_tx_start(&tx, 1, (const uint8_t *)"a", 1, 0));
    TEST_ASSERT_EQUAL_INT(0, llp4_rel_tx_start(&tx, 2, (const uint8_t *)"b", 1, 0));
    TEST_ASSERT_EQUAL_UINT8(1, tx.seq);
}

void test_rel_tx_start_rejects_oversized_data(void)
{
    static uint8_t data[LLP4_REL_MAX_DATA + 1];
    llp4_rel_tx_t tx;

    memset(data, 'x', sizeof(data));
    llp4_rel_tx_init(&tx, 50, 5, 1000);
    TEST_ASSERT_EQUAL_INT(0, llp4_rel_tx_start(&tx, 0, data, sizeof(data), 0));
    TEST_ASSERT_EQUAL_UINT8(0, tx.active);

    TEST_ASSERT_EQUAL_INT(1, llp4_rel_tx_start(&tx, 0, data, LLP4_REL_MAX_DATA, 0));
    TEST_ASSERT_EQUAL_UINT8(LLP4_REL_MAX_PAYLOAD, tx.len);
}

void test_rel_tx_retransmits_identical_payload_after_timeout(void)
{
    llp4_rel_tx_t tx;
    uint8_t before[LLP4_REL_MAX_PAYLOAD];
    uint8_t before_len = 0;
    uint8_t after_len = 0;
    const uint8_t *before_ptr;
    const uint8_t *after;

    llp4_rel_tx_init(&tx, 50, 5, 1000);
    llp4_rel_tx_start(&tx, 9, (const uint8_t *)"G1 X0\n", 6, 1000);
    before_ptr = llp4_rel_tx_payload(&tx, &before_len);
    memcpy(before, before_ptr, before_len);
    TEST_ASSERT_EQUAL_UINT8(7, before_len);
    TEST_ASSERT_EQUAL_INT(LLP4_REL_TX_IDLE, llp4_rel_tx_service(&tx, 1049));
    TEST_ASSERT_EQUAL_INT(LLP4_REL_TX_SEND, llp4_rel_tx_service(&tx, 1050));
    TEST_ASSERT_EQUAL_UINT8(1, tx.retries);

    after = llp4_rel_tx_payload(&tx, &after_len);
    TEST_ASSERT_EQUAL_UINT8(before_len, after_len);
    TEST_ASSERT_EQUAL_MEMORY(before, after, before_len);
}

void test_rel_tx_exhausts_and_backs_off(void)
{
    llp4_rel_tx_t tx;
    int result = LLP4_REL_TX_IDLE;
    uint16_t now = 0;
    uint8_t i;

    llp4_rel_tx_init(&tx, 50, 2, 1000);
    llp4_rel_tx_start(&tx, 1, (const uint8_t *)"x", 1, now);

    for (i = 0; i < 2; i++) {
        now = (uint16_t)(now + 50);
        TEST_ASSERT_EQUAL_INT(LLP4_REL_TX_SEND, llp4_rel_tx_service(&tx, now));
    }

    now = (uint16_t)(now + 50);
    result = llp4_rel_tx_service(&tx, now);
    TEST_ASSERT_EQUAL_INT(LLP4_REL_TX_EXHAUSTED, result);

    /* Backoff: nothing happens before backoff_ms. */
    TEST_ASSERT_EQUAL_INT(LLP4_REL_TX_IDLE, llp4_rel_tx_service(&tx, (uint16_t)(now + 999)));
    TEST_ASSERT_EQUAL_INT(LLP4_REL_TX_EXHAUSTED, llp4_rel_tx_service(&tx, (uint16_t)(now + 1000)));
}

void test_rel_tx_ack_releases_window(void)
{
    llp4_rel_tx_t tx;

    llp4_rel_tx_init(&tx, 50, 5, 1000);
    llp4_rel_tx_start(&tx, 7, (const uint8_t *)"x", 1, 0);

    TEST_ASSERT_EQUAL_INT(0, llp4_rel_tx_on_ack(&tx, 6));   /* stale */
    TEST_ASSERT_EQUAL_UINT8(1, tx.active);
    TEST_ASSERT_EQUAL_INT(1, llp4_rel_tx_on_ack(&tx, 7));
    TEST_ASSERT_EQUAL_UINT8(0, tx.active);
    TEST_ASSERT_EQUAL_INT(0, llp4_rel_tx_on_ack(&tx, 7));   /* idempotent */
}

void test_rel_tx_abort_releases_window(void)
{
    llp4_rel_tx_t tx;

    llp4_rel_tx_init(&tx, 50, 5, 1000);
    llp4_rel_tx_start(&tx, 7, (const uint8_t *)"x", 1, 0);
    llp4_rel_tx_abort(&tx);
    TEST_ASSERT_EQUAL_UINT8(0, tx.active);
}

void test_rel_tx_time_wraps_at_16_bits(void)
{
    llp4_rel_tx_t tx;

    llp4_rel_tx_init(&tx, 50, 5, 1000);
    llp4_rel_tx_start(&tx, 1, (const uint8_t *)"x", 1, 0xFFF0u);

    TEST_ASSERT_EQUAL_INT(LLP4_REL_TX_IDLE, llp4_rel_tx_service(&tx, (uint16_t)0xFFF0u + 49));
    TEST_ASSERT_EQUAL_INT(LLP4_REL_TX_SEND, llp4_rel_tx_service(&tx, (uint16_t)(0xFFF0u + 50)));
}

/* ------------------------------------------------------------------------- */
/* Receiver                                                                  */
/* ------------------------------------------------------------------------- */

void test_rel_rx_first_frame_and_duplicate(void)
{
    llp4_rel_rx_t rx;

    llp4_rel_rx_init(&rx);
    TEST_ASSERT_EQUAL_INT(1, llp4_rel_rx_accept(&rx, 3));
    TEST_ASSERT_EQUAL_INT(0, llp4_rel_rx_accept(&rx, 3));
    TEST_ASSERT_EQUAL_UINT16(1, rx.delivered);
    TEST_ASSERT_EQUAL_UINT16(1, rx.duplicates);
    TEST_ASSERT_EQUAL_UINT8(3, rx.last_seq);
}

void test_rel_rx_sequence_wrap(void)
{
    llp4_rel_rx_t rx;

    llp4_rel_rx_init(&rx);
    TEST_ASSERT_EQUAL_INT(1, llp4_rel_rx_accept(&rx, 255));
    TEST_ASSERT_EQUAL_INT(1, llp4_rel_rx_accept(&rx, 0));
    TEST_ASSERT_EQUAL_INT(1, llp4_rel_rx_accept(&rx, 1));
    TEST_ASSERT_EQUAL_UINT16(3, rx.delivered);
    TEST_ASSERT_EQUAL_UINT16(0, rx.duplicates);
}

void test_rel_rx_sequence_jump_is_accepted(void)
{
    llp4_rel_rx_t rx;

    llp4_rel_rx_init(&rx);
    TEST_ASSERT_EQUAL_INT(1, llp4_rel_rx_accept(&rx, 10));
    TEST_ASSERT_EQUAL_INT(1, llp4_rel_rx_accept(&rx, 200));
    TEST_ASSERT_EQUAL_UINT16(2, rx.delivered);
}

/* ------------------------------------------------------------------------- */
/* Confirming traces (RELIABILITY.md §11)                                    */
/* ------------------------------------------------------------------------- */

/* T1: send, accept, ACK -> delivered once, no retransmission. */
void test_rel_trace_t1_clean_delivery(void)
{
    llp4_rel_tx_t tx;
    llp4_rel_rx_t rx;
    uint16_t now = 100;

    llp4_rel_tx_init(&tx, 50, 5, 1000);
    llp4_rel_rx_init(&rx);

    TEST_ASSERT_EQUAL_INT(1, llp4_rel_tx_start(&tx, 7, (const uint8_t *)"G1 X0\n", 6, now));
    TEST_ASSERT_EQUAL_INT(1, llp4_rel_rx_accept(&rx, 7));
    TEST_ASSERT_EQUAL_INT(1, llp4_rel_tx_on_ack(&tx, 7));
    TEST_ASSERT_EQUAL_INT(0, tx.retries);
    TEST_ASSERT_EQUAL_UINT16(1, rx.delivered);
}

/* T2: the command frame is lost; the retransmission delivers it once. */
void test_rel_trace_t2_lost_command(void)
{
    llp4_rel_tx_t tx;
    llp4_rel_rx_t rx;
    uint16_t now = 0;
    uint8_t attempts = 0;

    llp4_rel_tx_init(&tx, 50, 5, 1000);
    llp4_rel_rx_init(&rx);

    llp4_rel_tx_start(&tx, 11, (const uint8_t *)"G1 X1\n", 6, now);
    attempts++;                                  /* first transmission, lost */

    now = (uint16_t)(now + 50);
    TEST_ASSERT_EQUAL_INT(LLP4_REL_TX_SEND, llp4_rel_tx_service(&tx, now));
    attempts++;                                  /* retransmission arrives */

    TEST_ASSERT_EQUAL_INT(1, llp4_rel_rx_accept(&rx, 11));
    TEST_ASSERT_EQUAL_INT(1, llp4_rel_tx_on_ack(&tx, 11));
    TEST_ASSERT_EQUAL_UINT8(2, attempts);
    TEST_ASSERT_EQUAL_UINT16(1, rx.delivered);
    TEST_ASSERT_EQUAL_UINT16(0, rx.duplicates);
}

/* T3: the ACK is lost; the duplicate is discarded and acknowledged again. */
void test_rel_trace_t3_lost_ack(void)
{
    llp4_rel_tx_t tx;
    llp4_rel_rx_t rx;
    uint16_t now = 0;

    llp4_rel_tx_init(&tx, 50, 5, 1000);
    llp4_rel_rx_init(&rx);

    llp4_rel_tx_start(&tx, 21, (const uint8_t *)"G1 X2\n", 6, now);
    TEST_ASSERT_EQUAL_INT(1, llp4_rel_rx_accept(&rx, 21));
    /* ACK never arrives at the sender. */
    now = (uint16_t)(now + 50);
    TEST_ASSERT_EQUAL_INT(LLP4_REL_TX_SEND, llp4_rel_tx_service(&tx, now));

    /* Receiver sees the same SEQ again: discard and ACK again. */
    TEST_ASSERT_EQUAL_INT(0, llp4_rel_rx_accept(&rx, 21));
    TEST_ASSERT_EQUAL_UINT16(1, rx.delivered);
    TEST_ASSERT_EQUAL_UINT16(1, rx.duplicates);

    TEST_ASSERT_EQUAL_INT(1, llp4_rel_tx_on_ack(&tx, 21));
    TEST_ASSERT_EQUAL_UINT8(0, tx.active);
}

/* T4: a duplicate SEQ carrying different DATA must not be delivered again. */
void test_rel_trace_t4_duplicate_with_other_data(void)
{
    llp4_rel_rx_t rx;

    llp4_rel_rx_init(&rx);
    TEST_ASSERT_EQUAL_INT(1, llp4_rel_rx_accept(&rx, 33));
    TEST_ASSERT_EQUAL_INT(0, llp4_rel_rx_accept(&rx, 33));
    TEST_ASSERT_EQUAL_UINT16(1, rx.delivered);
    TEST_ASSERT_EQUAL_UINT16(1, rx.duplicates);
}

/* T5: 255 -> 0 wrap, both delivered. */
void test_rel_trace_t5_wrap(void)
{
    llp4_rel_rx_t rx;
    llp4_rel_tx_t tx;

    llp4_rel_rx_init(&rx);
    llp4_rel_tx_init(&tx, 50, 5, 1000);

    llp4_rel_tx_start(&tx, 255, (const uint8_t *)"a", 1, 0);
    TEST_ASSERT_EQUAL_INT(1, llp4_rel_rx_accept(&rx, 255));
    TEST_ASSERT_EQUAL_INT(1, llp4_rel_tx_on_ack(&tx, 255));

    llp4_rel_tx_start(&tx, 0, (const uint8_t *)"b", 1, 0);
    TEST_ASSERT_EQUAL_INT(1, llp4_rel_rx_accept(&rx, 0));
    TEST_ASSERT_EQUAL_UINT16(2, rx.delivered);
}

/* T6: never acknowledged -> the sender reports exhaustion, never delivery. */
void test_rel_trace_t6_exhaustion(void)
{
    llp4_rel_tx_t tx;
    uint16_t now = 0;
    int result = LLP4_REL_TX_IDLE;
    uint8_t i;

    llp4_rel_tx_init(&tx, 50, 3, 1000);
    llp4_rel_tx_start(&tx, 44, (const uint8_t *)"x", 1, now);

    for (i = 0; i < 3; i++) {
        now = (uint16_t)(now + 50);
        result = llp4_rel_tx_service(&tx, now);
        TEST_ASSERT_EQUAL_INT(LLP4_REL_TX_SEND, result);
    }
    now = (uint16_t)(now + 50);
    TEST_ASSERT_EQUAL_INT(LLP4_REL_TX_EXHAUSTED, llp4_rel_tx_service(&tx, now));
    TEST_ASSERT_EQUAL_UINT8(1, tx.active);   /* still unacknowledged */
}

/* T7: an ACK with an unknown SEQ is ignored. */
void test_rel_trace_t7_unknown_ack(void)
{
    llp4_rel_tx_t tx;

    llp4_rel_tx_init(&tx, 50, 5, 1000);
    llp4_rel_tx_start(&tx, 5, (const uint8_t *)"x", 1, 0);
    TEST_ASSERT_EQUAL_INT(0, llp4_rel_tx_on_ack(&tx, 4));
    TEST_ASSERT_EQUAL_INT(0, llp4_rel_tx_on_ack(&tx, 6));
    TEST_ASSERT_EQUAL_UINT8(1, tx.active);
}

/* T8: a duplicate arriving after the sender gave up is still acknowledged and
       still not delivered twice. */
void test_rel_trace_t8_late_duplicate_after_exhaustion(void)
{
    llp4_rel_tx_t tx;
    llp4_rel_rx_t rx;
    uint16_t now = 0;
    uint8_t i;

    llp4_rel_tx_init(&tx, 50, 1, 1000);
    llp4_rel_rx_init(&rx);

    llp4_rel_tx_start(&tx, 60, (const uint8_t *)"x", 1, now);
    TEST_ASSERT_EQUAL_INT(1, llp4_rel_rx_accept(&rx, 60));
    now = (uint16_t)(now + 50);
    TEST_ASSERT_EQUAL_INT(LLP4_REL_TX_SEND, llp4_rel_tx_service(&tx, now)); /* retransmit */
    TEST_ASSERT_EQUAL_INT(0, llp4_rel_rx_accept(&rx, 60));                  /* duplicate */
    now = (uint16_t)(now + 50);
    TEST_ASSERT_EQUAL_INT(LLP4_REL_TX_EXHAUSTED, llp4_rel_tx_service(&tx, now));

    /* The receiver still answers duplicates, so a late retransmission cannot
       deadlock it (the caller always has an ACK to send). */
    for (i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_INT(0, llp4_rel_rx_accept(&rx, 60));
    }
    TEST_ASSERT_EQUAL_UINT16(1, rx.delivered);
    TEST_ASSERT_EQUAL_UINT16(4, rx.duplicates);
}

/* ------------------------------------------------------------------------- */
/* Wire integration (llp_v4.h)                                               */
/* ------------------------------------------------------------------------- */

void test_rel_payload_fits_one_frame(void)
{
    TEST_ASSERT_TRUE(LLP4_REL_MAX_PAYLOAD <= 64u);
    TEST_ASSERT_TRUE(LLP4_REL_MAX_PAYLOAD <= LLP4_REL_MAX_DATA + 1u);
}

void test_rel_roundtrip_command_and_ack(void)
{
    llp4_rel_tx_t tx;
    uint8_t out[64];
    uint8_t out_len = 0;
    uint8_t channel;
    size_t n;
    static const uint8_t data[] = "G1 X-3.0000 Y-46.0000 F100\n";

    llp4_rel_tx_init(&tx, 50, 5, 1000);
    TEST_ASSERT_EQUAL_INT(1, llp4_rel_tx_start(&tx, 0xAA, data, sizeof(data) - 1, 0));

    /* Command frame: channel 1, payload [SEQ][DATA]. SEQ 0xAA exercises stuffing. */
    n = transmit(&tx);
    channel = decode_frame(wire, n, out, &out_len);
    TEST_ASSERT_EQUAL_UINT8(LLP4_REL_CH_COMMAND, channel);
    TEST_ASSERT_EQUAL_UINT8(1 + sizeof(data) - 1, out_len);
    TEST_ASSERT_EQUAL_UINT8(0xAA, out[0]);
    TEST_ASSERT_EQUAL_MEMORY(data, out + 1, sizeof(data) - 1);

    /* ACK frame: channel 2, payload [SEQ]. */
    n = ack_frame(0xAA);
    channel = decode_frame(wire, n, out, &out_len);
    TEST_ASSERT_EQUAL_UINT8(LLP4_REL_CH_ACK, channel);
    TEST_ASSERT_EQUAL_UINT8(1, out_len);
    TEST_ASSERT_EQUAL_UINT8(0xAA, out[0]);
}
