/*
 * llp_v4.h — LLP wire format 4.0.0 (header-only, C99)
 *
 *     [0xAA][0x55][LEN][CHANNEL][PAYLOAD...][CRC_L][CRC_H]
 *
 * Normative specification: docs/SPEC-v4.md
 * Conformance vectors:     spec/vectors/v4/  (see docs/CONFORMANCE.md)
 *
 * Design rules this file follows (see docs/SPEC-v4.md §6):
 *   - nothing reaches the application before the CRC validates;
 *   - O(1) work per byte, no dynamic memory, no recursion, no VLAs;
 *   - one parser instance per link, fed from a single context (main or ISR);
 *   - the caller injects time (16-bit milliseconds, wrap-safe).
 *
 * Payload storage is supplied by the caller (`llp4_parser_init`), so the parser
 * itself is ~17 bytes: the application's line buffer is reused. The encoder
 * streams through a write callback, so no frame buffer is needed either.
 */

#ifndef LLP_V4_H
#define LLP_V4_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Configuration                                                             */
/* ------------------------------------------------------------------------- */

/**
 * Default inter-byte timeout in milliseconds (max 32767). The comparison is
 * strictly greater-than, so a timeout of 0 means "any inter-byte gap times
 * out": always pass a real value.
 */
#ifndef LLP4_DEFAULT_TIMEOUT_MS
#define LLP4_DEFAULT_TIMEOUT_MS 2000
#endif

/** Enable parser statistics (+6 bytes on 16-bit platforms). */
#ifndef LLP4_ENABLE_STATS
#define LLP4_ENABLE_STATS 0
#endif

/**
 * CRC implementation: 1 = 16-nibble table (32 bytes of flash, on AVR kept in
 * program memory), 0 = bitwise (smallest flash, slowest).
 */
#ifndef LLP4_CRC_TABLE
#define LLP4_CRC_TABLE 1
#endif

/* ------------------------------------------------------------------------- */
/* Constants                                                                 */
/* ------------------------------------------------------------------------- */

#define LLP4_MAGIC_1 0xAAu
#define LLP4_MAGIC_2 0x55u

/* Error codes (stable, part of the wire semantics). */
#define LLP4_OK 0
#define LLP4_ERR_CRC 1
#define LLP4_ERR_LEN 2
#define LLP4_ERR_TIMEOUT 3
#define LLP4_ERR_SYNC 4

/* Feed results. Errors are returned as negative values (-LLP4_ERR_*). */
#define LLP4_EV_NONE 0
#define LLP4_EV_FRAME 1

/* Parser states (kept > WAIT_MAGIC2 so `state > LLP4_ST_WAIT_MAGIC2` means
 * "inside a frame, escape processing applies"). */
#define LLP4_ST_WAIT_MAGIC1 0u
#define LLP4_ST_WAIT_MAGIC2 1u
#define LLP4_ST_READ_LEN 2u
#define LLP4_ST_READ_CHANNEL 3u
#define LLP4_ST_READ_PAYLOAD 4u
#define LLP4_ST_READ_CRC_L 5u
#define LLP4_ST_READ_CRC_H 6u

/* ------------------------------------------------------------------------- */
/* CRC16-CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection)               */
/* ------------------------------------------------------------------------- */

#if LLP4_CRC_TABLE
#if defined(__AVR__)
#include <avr/pgmspace.h>
static const uint16_t llp4_crc_nibble[16] PROGMEM = {
#else
static const uint16_t llp4_crc_nibble[16] = {
#endif
    0x0000u, 0x1021u, 0x2042u, 0x3063u, 0x4084u, 0x50A5u, 0x60C6u, 0x70E7u,
    0x8108u, 0x9129u, 0xA14Au, 0xB16Bu, 0xC18Cu, 0xD1ADu, 0xE1CEu, 0xF1EFu};

#if defined(__AVR__)
#define LLP4_CRC_NIBBLE(i) ((uint16_t)pgm_read_word(&llp4_crc_nibble[(i)]))
#else
#define LLP4_CRC_NIBBLE(i) (llp4_crc_nibble[(i)])
#endif

/** Update the CRC with one byte (table implementation). */
static inline uint16_t llp4_crc16_update(uint16_t crc, uint8_t byte)
{
    crc ^= (uint16_t)byte << 8;
    crc = (uint16_t)(crc << 4) ^ LLP4_CRC_NIBBLE((crc >> 12) & 0x0Fu);
    crc = (uint16_t)(crc << 4) ^ LLP4_CRC_NIBBLE((crc >> 12) & 0x0Fu);
    return crc;
}
#else
/** Update the CRC with one byte (bitwise implementation). */
static inline uint16_t llp4_crc16_update(uint16_t crc, uint8_t byte)
{
    uint8_t i;
    crc ^= (uint16_t)byte << 8;
    for (i = 0; i < 8; i++) {
        crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                              : (uint16_t)(crc << 1);
    }
    return crc;
}
#endif

/** CRC state after the magic: frames start from this seed. */
static inline uint16_t llp4_crc_seed(void)
{
    return llp4_crc16_update(llp4_crc16_update(0xFFFFu, LLP4_MAGIC_1),
                             LLP4_MAGIC_2);
}

/** Convenience: CRC over a whole buffer (magic + body + CRC frame excluded). */
static inline uint16_t llp4_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    size_t i;
    for (i = 0; i < len; i++) {
        crc = llp4_crc16_update(crc, data[i]);
    }
    return crc;
}

/* ------------------------------------------------------------------------- */
/* Parser                                                                    */
/* ------------------------------------------------------------------------- */

typedef struct {
    uint8_t *payload;   /**< caller-owned payload buffer (may be NULL if cap 0) */
    uint8_t capacity;   /**< bytes available in `payload` */
    uint8_t length;     /**< payload length of the last delivered frame */
    uint8_t channel;    /**< channel of the last delivered frame */
    uint8_t index;      /**< payload bytes stored so far */
    uint8_t state;      /**< LLP4_ST_* */
    uint8_t escape_pending;
    uint8_t error;      /**< last error code (LLP4_OK if none) */
    uint16_t crc_calculated;
    uint16_t crc_received;
    uint16_t last_byte_time;
    uint16_t timeout_ms;
#if LLP4_ENABLE_STATS
    uint16_t frames_ok;
    uint16_t frames_error;
    uint16_t timeouts;
#endif
} llp4_parser_t;

/** Drop the frame in progress and return to WAIT_MAGIC1. */
static inline void llp4_parser_discard(llp4_parser_t *p)
{
    p->state = LLP4_ST_WAIT_MAGIC1;
    p->escape_pending = 0;
    p->index = 0;
    p->length = 0;
    p->channel = 0;
}

/** Reset the parser state; configuration (buffer, capacity, timeout) is kept. */
static inline void llp4_parser_reset(llp4_parser_t *p)
{
    llp4_parser_discard(p);
    p->crc_calculated = llp4_crc_seed();
    p->crc_received = 0;
    p->last_byte_time = 0;
    p->error = LLP4_OK;
}

/**
 * Configure a parser.
 *
 * @param buf        payload storage owned by the caller (or NULL when cap == 0)
 * @param cap        payload capacity, 0..255
 * @param timeout_ms inter-byte timeout, 1..32767 (0 disables via immediate
 *                   comparison semantics; use LLP4_DEFAULT_TIMEOUT_MS)
 */
static inline void llp4_parser_init(llp4_parser_t *p, uint8_t *buf, uint8_t cap,
                                    uint16_t timeout_ms)
{
    p->payload = buf;
    p->capacity = cap;
    p->timeout_ms = timeout_ms;
#if LLP4_ENABLE_STATS
    p->frames_ok = 0;
    p->frames_error = 0;
    p->timeouts = 0;
#endif
    llp4_parser_reset(p);
}

/** Start a new frame right after the magic has been consumed. */
static inline void llp4_parser_begin(llp4_parser_t *p)
{
    p->state = LLP4_ST_READ_LEN;
    p->escape_pending = 0;
    p->length = 0;
    p->channel = 0;
    p->index = 0;
    p->crc_calculated = llp4_crc_seed();
}

static inline int llp4_parser_fail(llp4_parser_t *p, uint8_t code)
{
    p->error = code;
#if LLP4_ENABLE_STATS
    p->frames_error++;
    if (code == LLP4_ERR_TIMEOUT) {
        p->timeouts++;
    }
#endif
    llp4_parser_discard(p);
    return -(int)code;
}

/**
 * Feed one byte.
 *
 * @param now_ms current time in milliseconds (16-bit; the caller may truncate a
 *               wider counter, the arithmetic is wrap-safe)
 * @return LLP4_EV_FRAME when a frame was delivered (payload/channel/length are
 *         valid), LLP4_EV_NONE otherwise, or -LLP4_ERR_* on error
 *         (`p->error` holds the same code).
 */
static inline int llp4_parser_feed(llp4_parser_t *p, uint8_t byte,
                                   uint16_t now_ms)
{
    p->error = LLP4_OK;

    /* Inter-byte timeout. No timeout while waiting for the magic: an idle link
     * is normal. On timeout the offending byte is re-examined as a magic. */
    if (p->state != LLP4_ST_WAIT_MAGIC1 &&
        (uint16_t)(now_ms - p->last_byte_time) > p->timeout_ms) {
        int rc = llp4_parser_fail(p, LLP4_ERR_TIMEOUT);
        p->last_byte_time = now_ms;
        if (byte == LLP4_MAGIC_1) {
            p->state = LLP4_ST_WAIT_MAGIC2;
        }
        return rc;
    }
    p->last_byte_time = now_ms;

    /* Escape processing, only inside a frame. */
    if (p->state > LLP4_ST_WAIT_MAGIC2) {
        if (p->escape_pending) {
            p->escape_pending = 0;
            if (byte == LLP4_MAGIC_2) {
                /* A new magic aborted the frame: report and start over. */
                llp4_parser_fail(p, LLP4_ERR_SYNC);
                llp4_parser_begin(p);
                return -LLP4_ERR_SYNC;
            }
            if (byte == 0x00u) {
                byte = LLP4_MAGIC_1; /* escaped literal */
            } else {
                return llp4_parser_fail(p, LLP4_ERR_SYNC);
            }
        } else if (byte == LLP4_MAGIC_1) {
            p->escape_pending = 1;
            return LLP4_EV_NONE;
        }
    }

    switch (p->state) {
    case LLP4_ST_WAIT_MAGIC1:
        if (byte == LLP4_MAGIC_1) {
            p->state = LLP4_ST_WAIT_MAGIC2;
        }
        break;

    case LLP4_ST_WAIT_MAGIC2:
        if (byte == LLP4_MAGIC_2) {
            llp4_parser_begin(p);
        } else if (byte != LLP4_MAGIC_1) {
            p->state = LLP4_ST_WAIT_MAGIC1;
        }
        break;

    case LLP4_ST_READ_LEN:
        if (byte > p->capacity) {
            return llp4_parser_fail(p, LLP4_ERR_LEN);
        }
        p->length = byte;
        p->crc_calculated = llp4_crc16_update(p->crc_calculated, byte);
        p->state = LLP4_ST_READ_CHANNEL;
        break;

    case LLP4_ST_READ_CHANNEL:
        p->channel = byte;
        p->crc_calculated = llp4_crc16_update(p->crc_calculated, byte);
        p->state = (p->length == 0) ? LLP4_ST_READ_CRC_L : LLP4_ST_READ_PAYLOAD;
        break;

    case LLP4_ST_READ_PAYLOAD:
        p->payload[p->index++] = byte;
        p->crc_calculated = llp4_crc16_update(p->crc_calculated, byte);
        if (p->index == p->length) {
            p->state = LLP4_ST_READ_CRC_L;
        }
        break;

    case LLP4_ST_READ_CRC_L:
        p->crc_received = byte;
        p->state = LLP4_ST_READ_CRC_H;
        break;

    case LLP4_ST_READ_CRC_H:
        p->crc_received |= (uint16_t)byte << 8;
        if (p->crc_received != p->crc_calculated) {
            return llp4_parser_fail(p, LLP4_ERR_CRC);
        }
        p->state = LLP4_ST_WAIT_MAGIC1;
#if LLP4_ENABLE_STATS
        p->frames_ok++;
#endif
        return LLP4_EV_FRAME;

    default:
        llp4_parser_discard(p);
        break;
    }

    return LLP4_EV_NONE;
}

/** Payload of the last delivered frame (length written to `len_out`). */
static inline const uint8_t *llp4_parser_payload(const llp4_parser_t *p,
                                                 uint8_t *len_out)
{
    if (len_out != NULL) {
        *len_out = p->length;
    }
    return p->payload;
}

/** Channel of the last delivered frame. */
static inline uint8_t llp4_parser_channel(const llp4_parser_t *p)
{
    return p->channel;
}

/* ------------------------------------------------------------------------- */
/* Encoder (streaming; no frame buffer)                                      */
/* ------------------------------------------------------------------------- */

/** Write-callback: emit one byte to the link. */
typedef void (*llp4_write_fn)(void *ctx, uint8_t byte);

typedef struct {
    llp4_write_fn write;
    void *ctx;
    uint16_t crc;
} llp4_tx_t;

/**
 * Worst-case on-wire size of a frame carrying `payload_len` bytes.
 * Also available as a macro so it can be used in constant expressions
 * (e.g. `typedef char fit[(LLP4_FRAME_SIZE(48) <= RING) ? 1 : -1];`).
 */
#define LLP4_FRAME_SIZE(payload_len) (10u + 2u * (size_t)(payload_len))

static inline size_t llp4_frame_size(uint8_t payload_len)
{
    return LLP4_FRAME_SIZE(payload_len);
}

static inline void llp4_tx_stuffed(llp4_tx_t *tx, uint8_t byte)
{
    if (byte == LLP4_MAGIC_1) {
        tx->write(tx->ctx, LLP4_MAGIC_1);
        tx->write(tx->ctx, 0x00u);
    } else {
        tx->write(tx->ctx, byte);
    }
}

/**
 * Begin a frame: emits the magic, the length and the channel.
 *
 * The caller must know the payload length up front (it is in the header) and
 * must then call `llp4_tx_byte` exactly `len` times followed by `llp4_tx_end`.
 */
static inline void llp4_tx_begin(llp4_tx_t *tx, uint8_t channel, uint8_t len,
                                 llp4_write_fn write, void *ctx)
{
    tx->write = write;
    tx->ctx = ctx;
    tx->crc = llp4_crc_seed();

    write(ctx, LLP4_MAGIC_1);
    write(ctx, LLP4_MAGIC_2);

    llp4_tx_stuffed(tx, len);
    tx->crc = llp4_crc16_update(tx->crc, len);

    llp4_tx_stuffed(tx, channel);
    tx->crc = llp4_crc16_update(tx->crc, channel);
}

/** Emit one payload byte (stuffed as needed, CRC updated). */
static inline void llp4_tx_byte(llp4_tx_t *tx, uint8_t byte)
{
    llp4_tx_stuffed(tx, byte);
    tx->crc = llp4_crc16_update(tx->crc, byte);
}

/** Emit the CRC and finish the frame. */
static inline void llp4_tx_end(llp4_tx_t *tx)
{
    llp4_tx_stuffed(tx, (uint8_t)(tx->crc & 0xFFu));
    llp4_tx_stuffed(tx, (uint8_t)(tx->crc >> 8));
}

/* Buffer sink used by llp4_encode. */
typedef struct {
    uint8_t *buf;
    size_t len;
    size_t cap;
} llp4_sink_t;

static inline void llp4_sink_byte(void *ctx, uint8_t byte)
{
    llp4_sink_t *sink = (llp4_sink_t *)ctx;
    if (sink->len < sink->cap) {
        sink->buf[sink->len++] = byte;
    }
}

/**
 * Encode one frame into a caller buffer.
 *
 * @param out_size buffer size; must be at least `llp4_frame_size(len)` because
 *                 the worst case is reserved. The return value is the number of
 *                 bytes actually written (0 on error).
 */
static inline size_t llp4_encode(uint8_t channel, const uint8_t *payload,
                                 uint8_t len, uint8_t *out, size_t out_size)
{
    llp4_tx_t tx;
    llp4_sink_t sink;
    uint8_t i;

    if ((out == NULL) || (out_size < llp4_frame_size(len)) ||
        ((payload == NULL) && (len > 0))) {
        return 0;
    }

    sink.buf = out;
    sink.len = 0;
    sink.cap = out_size;

    llp4_tx_begin(&tx, channel, len, llp4_sink_byte, &sink);
    for (i = 0; i < len; i++) {
        llp4_tx_byte(&tx, payload[i]);
    }
    llp4_tx_end(&tx);
    return sink.len;
}

#ifdef __cplusplus
}
#endif

#endif /* LLP_V4_H */
