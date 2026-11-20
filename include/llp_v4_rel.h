/*
 * llp_v4_rel.h — LLP v4 Profile R1: reliable command channel (header-only, C99)
 *
 * Normative specification: llp-spec docs/RELIABILITY.md.
 *
 *   COMMAND (channel 1):  [SEQ][DATA...]
 *   ACK     (channel 2):  [SEQ]
 *
 * This header implements only the two state machines; framing, CRC and
 * stuffing stay in llp_v4.h:
 *
 *   - sender   (llp4_rel_tx_t): stop-and-wait window of one, retransmission of
 *     a byte-identical frame after a timeout, exhaustion reporting;
 *   - receiver (llp4_rel_rx_t): duplicate suppression so a lost ACK can never
 *     cause a command to be delivered twice.
 *
 * Design rules (same as llp_v4.h):
 *   - O(1) work per event, no dynamic memory, no recursion, no VLAs;
 *   - the caller injects time (16-bit milliseconds, wrap-safe);
 *   - the caller owns every buffer, and nothing is copied behind its back.
 *
 * The header is self-contained: a receiver that only deduplicates never pulls
 * in the sender code (static inline, unused functions cost nothing).
 */

#ifndef LLP4_REL_H
#define LLP4_REL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Profile constants                                                         */
/* ------------------------------------------------------------------------- */

#define LLP4_REL_PROFILE_VERSION 1u

/** Command frames (sender -> receiver). */
#define LLP4_REL_CH_COMMAND 1u

/** Acknowledgement frames (receiver -> sender). */
#define LLP4_REL_CH_ACK 2u

/** Session marker (sender -> receiver): the duplicate-suppression state must be
 * reset before the first command of a new session. A rebooted sender restarts
 * its SEQ, and without this marker the first command after a reboot could
 * collide with the receiver's `last_seq` and be discarded. */
#define LLP4_REL_CH_HELLO 3u

/** Maximum DATA bytes per command frame (payload = SEQ + DATA <= 64). */
#define LLP4_REL_MAX_DATA 63u

/** Maximum command payload, including the sequence byte. */
#define LLP4_REL_MAX_PAYLOAD (1u + LLP4_REL_MAX_DATA)

/** Recommended timers (RELIABILITY.md §5.4). */
#define LLP4_REL_DEFAULT_ACK_TIMEOUT_MS 50u
#define LLP4_REL_DEFAULT_MAX_RETRIES 5u
#define LLP4_REL_DEFAULT_BACKOFF_MS 1000u

/** Normative range of MAX_RETRIES (RELIABILITY.md §5.4): 1..10. */
#define LLP4_REL_MIN_RETRIES 1u
#define LLP4_REL_MAX_RETRIES 10u

/** Maximum timer value: half of the 16-bit range, so the wrap-safe comparison
 * stays unambiguous (same rule as LLP4_TIMEOUT_MAX_MS in llp_v4.h). */
#define LLP4_REL_MAX_TIMER_MS 32767u

/* ------------------------------------------------------------------------- */
/* Sender                                                                    */
/* ------------------------------------------------------------------------- */

/** Service results. */
#define LLP4_REL_TX_IDLE 0      /**< nothing to do (window idle or waiting)   */
#define LLP4_REL_TX_SEND 1      /**< transmit (or retransmit) the payload now  */
#define LLP4_REL_TX_EXHAUSTED 2 /**< retries spent: report it to the app       */

typedef struct {
    uint8_t payload[LLP4_REL_MAX_PAYLOAD]; /**< [SEQ][DATA...], ready to encode */
    uint8_t len;                           /**< payload length; 0 when idle     */
    uint8_t seq;                           /**< SEQ of the outstanding frame    */
    uint8_t active;                        /**< 1 = unacknowledged frame        */
    uint8_t exhausted;                     /**< 1 = retries spent, in backoff   */
    uint8_t retries;                       /**< retransmissions already sent    */
    uint16_t sent_ms;                      /**< time of the last transmission   */
    uint16_t timeout_ms;                   /**< ACK timeout                     */
    uint16_t backoff_ms;                   /**< poll period after exhaustion    */
    uint8_t max_retries;                   /**< fast retransmissions allowed    */
} llp4_rel_tx_t;

/**
 * Clamp a timer to the range the 16-bit wrap-safe arithmetic supports.
 * `0` selects `fallback` (a zero timer would fire on every poll).
 */
static inline uint16_t llp4_rel_clamp_timer(uint16_t timer_ms,
                                            uint16_t fallback_ms)
{
    if (timer_ms == 0u) {
        return fallback_ms;
    }
    if (timer_ms > (uint16_t)LLP4_REL_MAX_TIMER_MS) {
        return (uint16_t)LLP4_REL_MAX_TIMER_MS;
    }
    return timer_ms;
}

/** Clamp MAX_RETRIES into the normative range 1..10 (RELIABILITY.md §5.4). */
static inline uint8_t llp4_rel_clamp_retries(uint8_t max_retries)
{
    if (max_retries < (uint8_t)LLP4_REL_MIN_RETRIES) {
        return (uint8_t)LLP4_REL_MIN_RETRIES;
    }
    if (max_retries > (uint8_t)LLP4_REL_MAX_RETRIES) {
        return (uint8_t)LLP4_REL_MAX_RETRIES;
    }
    return max_retries;
}

/**
 * Configure the sender.
 *
 * Out-of-contract configuration is clamped here, once, instead of silently
 * changing behaviour later (CNC-030 L-M3):
 *   - `max_retries` is clamped to the normative range 1..10. `0` would mean
 *     "exhaust after zero retransmissions", which the profile does not allow;
 *   - `timeout_ms` and `backoff_ms` of `0` select their defaults; values above
 *     32767 are clamped, because the wrap-safe comparison cannot represent
 *     them (a 65535 ms timer would never elapse).
 */
static inline void llp4_rel_tx_init(llp4_rel_tx_t *tx, uint16_t timeout_ms,
                                    uint8_t max_retries, uint16_t backoff_ms)
{
    tx->len = 0;
    tx->seq = 0;
    tx->active = 0;
    tx->exhausted = 0;
    tx->retries = 0;
    tx->sent_ms = 0;
    tx->timeout_ms = llp4_rel_clamp_timer(timeout_ms,
                                          LLP4_REL_DEFAULT_ACK_TIMEOUT_MS);
    tx->backoff_ms = llp4_rel_clamp_timer(backoff_ms,
                                          LLP4_REL_DEFAULT_BACKOFF_MS);
    tx->max_retries = llp4_rel_clamp_retries(max_retries);
}

/**
 * Load a command into the window and mark it ready.
 *
 * Returns 1 when the caller must transmit the payload now, 0 when the window is
 * busy (the caller must retry after the outstanding frame is acknowledged) or
 * when `data_len` does not fit. The payload is kept intact for retransmission.
 */
static inline int llp4_rel_tx_start(llp4_rel_tx_t *tx, uint8_t seq,
                                    const uint8_t *data, uint8_t data_len,
                                    uint16_t now_ms)
{
    uint8_t i;

    if (tx->active) {
        return 0;
    }
    if (data_len > LLP4_REL_MAX_DATA) {
        return 0;
    }
    if (data == NULL && data_len != 0) {
        return 0;
    }

    tx->payload[0] = seq;
    for (i = 0; i < data_len; i++) {
        tx->payload[1 + i] = data[i];
    }
    tx->len = (uint8_t)(data_len + 1u);
    tx->seq = seq;
    tx->retries = 0;
    tx->exhausted = 0;
    tx->sent_ms = now_ms;
    tx->active = 1;
    return 1;
}

/**
 * Drive the retransmission timer. Call it on every main-loop iteration.
 *
 * @return LLP4_REL_TX_SEND when the caller must transmit the payload again,
 *         LLP4_REL_TX_EXHAUSTED once per `backoff_ms` after the fast retries
 *         are spent (the caller reports it and decides what to do),
 *         LLP4_REL_TX_IDLE otherwise.
 */
static inline int llp4_rel_tx_service(llp4_rel_tx_t *tx, uint16_t now_ms)
{
    if (!tx->active) {
        return LLP4_REL_TX_IDLE;
    }

    /* Exhausted: report once per backoff period, never claim delivery. */
    if (tx->exhausted) {
        if ((uint16_t)(now_ms - tx->sent_ms) < tx->backoff_ms) {
            return LLP4_REL_TX_IDLE;
        }
        tx->sent_ms = now_ms;
        return LLP4_REL_TX_EXHAUSTED;
    }

    /* The last fast transmission is out: wait one timeout, then report. */
    if (tx->retries >= tx->max_retries) {
        if ((uint16_t)(now_ms - tx->sent_ms) < tx->timeout_ms) {
            return LLP4_REL_TX_IDLE;
        }
        tx->exhausted = 1;
        tx->sent_ms = now_ms;
        return LLP4_REL_TX_EXHAUSTED;
    }

    if ((uint16_t)(now_ms - tx->sent_ms) < tx->timeout_ms) {
        return LLP4_REL_TX_IDLE;
    }

    tx->retries++;
    tx->sent_ms = now_ms;
    return LLP4_REL_TX_SEND;
}

/**
 * Feed an ACK. Returns 1 when it acknowledged the outstanding frame (the window
 * is released), 0 for a stale or unknown sequence number.
 */
static inline int llp4_rel_tx_on_ack(llp4_rel_tx_t *tx, uint8_t seq)
{
    if (!tx->active || seq != tx->seq) {
        return 0;
    }
    tx->active = 0;
    tx->exhausted = 0;
    return 1;
}

/**
 * Feed an ACK *frame*: applies the two checks of RELIABILITY.md §3 before
 * touching the window — the frame must be on `LLP4_REL_CH_ACK` and must carry
 * exactly one byte. An ACK frame whose length is not 1 MUST be ignored, and
 * this helper is where that rule lives so no caller has to restate it.
 *
 * Returns 1 when the ACK was valid and acknowledged the outstanding frame, 0
 * for a malformed/foreign frame or a stale sequence number (window unchanged).
 * A caller that already dispatched on channel and length may keep calling
 * `llp4_rel_tx_on_ack()` directly.
 */
static inline int llp4_rel_tx_on_ack_frame(llp4_rel_tx_t *tx, uint8_t channel,
                                           const uint8_t *payload, uint8_t len)
{
    if ((channel != (uint8_t)LLP4_REL_CH_ACK) || (len != 1u) ||
        (payload == NULL)) {
        return 0;
    }
    return llp4_rel_tx_on_ack(tx, payload[0]);
}

/** Release the window without a delivery (application abort). */
static inline void llp4_rel_tx_abort(llp4_rel_tx_t *tx)
{
    tx->active = 0;
    tx->exhausted = 0;
}

/** Payload to encode on LLP4_REL_CH_COMMAND (length written to `len_out`). */
static inline const uint8_t *llp4_rel_tx_payload(const llp4_rel_tx_t *tx,
                                                 uint8_t *len_out)
{
    if (len_out != NULL) {
        *len_out = tx->active ? tx->len : 0;
    }
    return tx->payload;
}

/* ------------------------------------------------------------------------- */
/* Receiver                                                                  */
/* ------------------------------------------------------------------------- */

typedef struct {
    uint8_t last_seq;    /**< SEQ of the last accepted frame                 */
    uint8_t have_seq;    /**< 0 until the first frame is accepted            */
    uint16_t delivered;  /**< frames delivered to the application (saturates) */
    uint16_t duplicates; /**< duplicates suppressed (saturates)              */
} llp4_rel_rx_t;

static inline void llp4_rel_rx_init(llp4_rel_rx_t *rx)
{
    rx->last_seq = 0;
    rx->have_seq = 0;
    rx->delivered = 0;
    rx->duplicates = 0;
}

/**
 * Classify a command frame by its sequence number.
 *
 * Returns 1 when `DATA` must be delivered to the application, 0 for a
 * retransmission (the frame must be ACKed again and the data discarded).
 *
 * IMPORTANT (RELIABILITY.md §6.1): the caller MUST verify that it can accept the
 * whole `DATA` *before* calling this function. If it cannot, it must not consume
 * the sequence number and must not send the ACK, so the sender's retransmission
 * retries later.
 */
static inline int llp4_rel_rx_accept(llp4_rel_rx_t *rx, uint8_t seq)
{
    if (rx->have_seq && seq == rx->last_seq) {
        if (rx->duplicates < 0xFFFFu) {
            rx->duplicates++;
        }
        return 0;
    }

    rx->last_seq = seq;
    rx->have_seq = 1;
    if (rx->delivered < 0xFFFFu) {
        rx->delivered++;
    }
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* LLP4_REL_H */
