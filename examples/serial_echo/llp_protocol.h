/**
 * llp_protocol.h — LLP (Layered Link Protocol) v3.0.0
 *
 * Single-header C implementation compatible with LLP Java library v3.0.0.
 * Designed for Arduino IDE and any C99-compatible embedded environment.
 *
 * Wire format (transport frame):
 *   [0xAA][0x55][LEN_L][LEN_H][PAYLOAD...][CRC_L][CRC_H]
 *
 * Byte stuffing: every 0xAA byte in LEN, PAYLOAD or CRC is written as 0xAA 0x00.
 * An unexpected 0xAA 0x55 sequence inside a frame signals a resync event.
 *
 * Payload format (layer chain):
 *   [LAYER_ID][META_LEN][METADATA...] ... [0x00][RAW APPLICATION DATA]
 *
 * Layer ID rules:
 *   0x00       -> FinalNode: no more layers, raw bytes follow
 *   0x01-0x7F  -> Passthrough layer: metadata can be skipped, payload is unchanged
 *   0x80-0xFE  -> Transform layer: payload was modified (encrypt/compress), cannot skip
 *   0xFF       -> Reserved
 *
 * META_LEN encoding:
 *   0-254   -> 1 byte (direct value)
 *   255+    -> 3 bytes: 0xFF followed by 2 bytes big-endian
 *
 * MIGRATION FROM v2.x:
 *   - llp_build_frame() signature changed: removed type, id, version parameters.
 *     Use llp_build_final_payload() to wrap raw data, then pass to llp_build_frame().
 *   - Output buffer must be sized with LLP_MAX_FRAME_SIZE(payload_len) due to stuffing.
 *   - llp_parser_t no longer exposes frame.type / frame.id / frame.version.
 *   - After a successful parse, use llp_get_final_payload() or llp_find_layer() to
 *     navigate the received layer chain.
 */

#ifndef LLP_PROTOCOL_H
#define LLP_PROTOCOL_H

#include <stdint.h>
#include <string.h>

// =============================================================================
// CONFIG
// =============================================================================

#define LLP_MAGIC_1           ((uint8_t)0xAA)
#define LLP_MAGIC_2           ((uint8_t)0x55)

/** Maximum payload (layer chain) size in bytes. Reduce to 64 for AVR boards. */
#ifndef LLP_MAX_PAYLOAD
#define LLP_MAX_PAYLOAD       128
#endif

/** Inter-byte timeout in milliseconds before the parser resets. */
#ifndef LLP_FRAME_TIMEOUT_MS
#define LLP_FRAME_TIMEOUT_MS  2000
#endif

/**
 * Worst-case output buffer size for llp_build_frame().
 * Every byte in LEN, PAYLOAD and CRC can be stuffed, doubling its cost.
 *   Magic(2) + StuffedLen(4) + StuffedPayload(n*2) + StuffedCRC(4)
 */
#define LLP_MAX_FRAME_SIZE(payload_len) (2u + 4u + ((payload_len) * 2u) + 4u)

// =============================================================================
// LAYER ID HELPERS
// =============================================================================

#define LLP_LAYER_ID_FINAL       ((uint8_t)0x00)
#define LLP_LAYER_ID_RESERVED    ((uint8_t)0xFF)

/** Returns 1 if the layer ID is passthrough (safe to skip without a handler). */
#define LLP_LAYER_IS_PASSTHROUGH(id)  ((id) >= 0x01 && (id) <= 0x7F)

/** Returns 1 if the layer ID signals a payload transformation (cannot skip). */
#define LLP_LAYER_IS_TRANSFORM(id)    ((id) >= 0x80 && (id) <= 0xFE)

/** Returns 1 if this is the FinalNode marker (end of the layer chain). */
#define LLP_LAYER_IS_FINAL(id)        ((id) == LLP_LAYER_ID_FINAL)

// =============================================================================
// ERROR CODES
// =============================================================================

typedef enum {
    LLP_ERR_OK              = 0x00,
    LLP_ERR_CHECKSUM        = 0x01,
    LLP_ERR_PAYLOAD_LEN     = 0x02,
    LLP_ERR_TIMEOUT         = 0x03,
    LLP_ERR_SYNC            = 0x04,
    LLP_ERR_BUFFER_FULL     = 0x05,
    LLP_ERR_TRANSFORM_LAYER = 0x06,
    LLP_ERR_MALFORMED_LAYER = 0x07
} llp_error_t;

// =============================================================================
// CRC16-CCITT
// =============================================================================

static inline uint16_t llp_crc16_update(uint16_t crc, uint8_t data) {
    crc ^= (uint16_t)data << 8;
    for (int i = 0; i < 8; i++) {
        crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

// =============================================================================
// DATA STRUCTURES
// =============================================================================

typedef struct {
    uint8_t  payload[LLP_MAX_PAYLOAD];
    uint16_t payload_len;
    uint16_t crc;
} llp_frame_t;

typedef struct {
    uint8_t  layer_id;
    uint16_t meta_offset;
    uint16_t meta_len;
    uint16_t payload_offset;
    uint16_t payload_len;
} llp_layer_info_t;

// =============================================================================
// PARSER STATE MACHINE
// =============================================================================

typedef enum {
    LLP_STATE_WAIT_MAGIC1,
    LLP_STATE_WAIT_MAGIC2,
    LLP_STATE_READ_LEN_L,
    LLP_STATE_READ_LEN_H,
    LLP_STATE_READ_PAYLOAD,
    LLP_STATE_READ_CRC_L,
    LLP_STATE_READ_CRC_H
} llp_parser_state_t;

typedef struct {
    llp_parser_state_t state;

    uint8_t  escape_pending;

    uint16_t payload_idx;
    uint16_t crc_received;
    uint16_t crc_calculated;

    unsigned long last_byte_time;

    llp_frame_t frame;
    uint8_t     error_code;

    uint32_t frames_ok;
    uint32_t frames_error;
    uint32_t timeouts;
} llp_parser_t;

// =============================================================================
// PARSER — INTERNAL HELPERS
// =============================================================================

static inline void _llp_parser_reset(llp_parser_t* p) {
    p->state          = LLP_STATE_WAIT_MAGIC1;
    p->escape_pending = 0;
    p->payload_idx    = 0;
    p->crc_calculated = 0xFFFF;
}

// =============================================================================
// PARSER — PUBLIC API
// =============================================================================

static inline void llp_parser_init(llp_parser_t* p) {
    _llp_parser_reset(p);
    p->crc_received      = 0;
    p->last_byte_time    = 0;
    p->error_code        = LLP_ERR_OK;
    p->frames_ok         = 0;
    p->frames_error      = 0;
    p->timeouts          = 0;
    p->frame.payload_len = 0;
}

static inline int llp_parser_process_byte(llp_parser_t* p, uint8_t byte,
                                           unsigned long current_ms) {
    if (p->state != LLP_STATE_WAIT_MAGIC1) {
        if (current_ms - p->last_byte_time > LLP_FRAME_TIMEOUT_MS) {
            p->error_code = LLP_ERR_TIMEOUT;
            p->timeouts++;
            _llp_parser_reset(p);
            p->last_byte_time = current_ms;
            if (byte == LLP_MAGIC_1) p->state = LLP_STATE_WAIT_MAGIC2;
            return -1;
        }
    }
    p->last_byte_time = current_ms;

    if (p->state != LLP_STATE_WAIT_MAGIC1 &&
        p->state != LLP_STATE_WAIT_MAGIC2) {

        if (p->escape_pending) {
            p->escape_pending = 0;

            if (byte == LLP_MAGIC_2) {
                p->error_code = LLP_ERR_SYNC;
                p->frames_error++;

                p->crc_calculated = 0xFFFF;
                p->crc_calculated = llp_crc16_update(p->crc_calculated, LLP_MAGIC_1);
                p->crc_calculated = llp_crc16_update(p->crc_calculated, LLP_MAGIC_2);
                p->payload_idx    = 0;
                p->state          = LLP_STATE_READ_LEN_L;
                return -1;

            } else if (byte == 0x00) {
                byte = LLP_MAGIC_1;

            } else {
                p->error_code = LLP_ERR_SYNC;
                p->frames_error++;
                _llp_parser_reset(p);
                return -1;
            }

        } else if (byte == LLP_MAGIC_1) {
            p->escape_pending = 1;
            return 0;
        }
    }

    switch (p->state) {

        case LLP_STATE_WAIT_MAGIC1:
            if (byte == LLP_MAGIC_1) {
                p->state = LLP_STATE_WAIT_MAGIC2;
            }
            break;

        case LLP_STATE_WAIT_MAGIC2:
            if (byte == LLP_MAGIC_2) {
                p->crc_calculated = 0xFFFF;
                p->crc_calculated = llp_crc16_update(p->crc_calculated, LLP_MAGIC_1);
                p->crc_calculated = llp_crc16_update(p->crc_calculated, LLP_MAGIC_2);
                p->state = LLP_STATE_READ_LEN_L;
            } else if (byte == LLP_MAGIC_1) {
                p->state = LLP_STATE_WAIT_MAGIC2;
            } else {
                p->state = LLP_STATE_WAIT_MAGIC1;
            }
            break;

        case LLP_STATE_READ_LEN_L:
            p->frame.payload_len = byte;
            p->crc_calculated    = llp_crc16_update(p->crc_calculated, byte);
            p->state             = LLP_STATE_READ_LEN_H;
            break;

        case LLP_STATE_READ_LEN_H:
            p->frame.payload_len |= ((uint16_t)byte << 8);
            p->crc_calculated     = llp_crc16_update(p->crc_calculated, byte);

            if (p->frame.payload_len > LLP_MAX_PAYLOAD) {
                p->error_code = LLP_ERR_PAYLOAD_LEN;
                p->frames_error++;
                _llp_parser_reset(p);
                return -1;
            }

            p->payload_idx = 0;
            p->state = (p->frame.payload_len == 0)
                       ? LLP_STATE_READ_CRC_L
                       : LLP_STATE_READ_PAYLOAD;
            break;

        case LLP_STATE_READ_PAYLOAD:
            p->frame.payload[p->payload_idx++] = byte;
            p->crc_calculated = llp_crc16_update(p->crc_calculated, byte);
            if (p->payload_idx == p->frame.payload_len) {
                p->state = LLP_STATE_READ_CRC_L;
            }
            break;

        case LLP_STATE_READ_CRC_L:
            p->crc_received = byte;
            p->state        = LLP_STATE_READ_CRC_H;
            break;

        case LLP_STATE_READ_CRC_H:
            p->crc_received |= ((uint16_t)byte << 8);

            if (p->crc_received != p->crc_calculated) {
                p->error_code = LLP_ERR_CHECKSUM;
                p->frames_error++;
                _llp_parser_reset(p);
                return -1;
            }

            p->frame.crc = p->crc_calculated;
            p->frames_ok++;
            _llp_parser_reset(p);
            return 1;

        default:
            _llp_parser_reset(p);
            break;
    }

    return 0;
}

// =============================================================================
// LAYER CHAIN HELPERS — INTERNAL
// =============================================================================

static inline uint8_t _llp_read_meta_len(const uint8_t* buf, uint16_t buf_len,
                                          uint16_t offset, uint16_t* out_meta_len) {
    if (offset >= buf_len) return 0;

    if (buf[offset] < 0xFF) {
        *out_meta_len = buf[offset];
        return 1;
    }

    if ((uint16_t)(offset + 3) > buf_len) return 0;
    *out_meta_len = ((uint16_t)buf[offset + 1] << 8) | buf[offset + 2];
    return 3;
}

// =============================================================================
// LAYER CHAIN HELPERS — PUBLIC API
// =============================================================================

static inline int llp_find_layer(const llp_frame_t* frame, uint8_t target_id,
                                  llp_layer_info_t* out) {
    const uint8_t* buf   = frame->payload;
    const uint16_t total = frame->payload_len;
    uint16_t pos = 0;

    while (pos < total) {
        uint8_t layer_id = buf[pos++];

        if (LLP_LAYER_IS_FINAL(layer_id)) return 0;

        uint16_t meta_len  = 0;
        uint8_t  len_bytes = _llp_read_meta_len(buf, total, pos, &meta_len);
        if (len_bytes == 0)           return -1;
        pos += len_bytes;
        if (pos + meta_len > total)   return -1;

        if (layer_id == target_id) {
            if (out) {
                out->layer_id       = layer_id;
                out->meta_offset    = pos;
                out->meta_len       = meta_len;
                out->payload_offset = pos + meta_len;
                out->payload_len    = total - (pos + meta_len);
            }
            return 1;
        }

        if (LLP_LAYER_IS_TRANSFORM(layer_id)) return -1;

        pos += meta_len;
    }

    return 0;
}

static inline int llp_get_final_payload(const llp_frame_t* frame,
                                         uint8_t* out_buf, uint16_t out_buf_size) {
    const uint8_t* buf   = frame->payload;
    const uint16_t total = frame->payload_len;
    uint16_t pos = 0;

    while (pos < total) {
        uint8_t layer_id = buf[pos++];

        if (LLP_LAYER_IS_FINAL(layer_id)) {
            uint16_t raw_len = total - pos;
            if (raw_len > out_buf_size) return -1;
            if (raw_len > 0) memcpy(out_buf, &buf[pos], raw_len);
            return (int)raw_len;
        }

        uint16_t meta_len  = 0;
        uint8_t  len_bytes = _llp_read_meta_len(buf, total, pos, &meta_len);
        if (len_bytes == 0)           return -1;
        pos += len_bytes;
        if (pos + meta_len > total)   return -1;

        if (LLP_LAYER_IS_TRANSFORM(layer_id)) return -1;

        pos += meta_len;
    }

    return -1;
}

// =============================================================================
// FRAME BUILDER — INTERNAL
// =============================================================================

static inline void _llp_write_stuffed(uint8_t* buf, size_t* idx,
                                       uint8_t byte, uint16_t* crc) {
    if (crc) *crc = llp_crc16_update(*crc, byte);
    buf[(*idx)++] = byte;
    if (byte == LLP_MAGIC_1) {
        buf[(*idx)++] = 0x00;
    }
}

// =============================================================================
// FRAME BUILDER — PUBLIC API
// =============================================================================

static inline size_t llp_build_final_payload(uint8_t* out_buf, size_t out_buf_size,
                                               const uint8_t* raw_data, uint16_t raw_len) {
    if (out_buf_size < (size_t)raw_len + 1u) return 0;
    out_buf[0] = LLP_LAYER_ID_FINAL;
    if (raw_len > 0 && raw_data != NULL) {
        memcpy(&out_buf[1], raw_data, raw_len);
    }
    return (size_t)raw_len + 1u;
}

static inline size_t llp_build_frame(uint8_t* out_buf, size_t out_buf_size,
                                       const uint8_t* llp_payload, uint16_t llp_payload_len) {
    if (llp_payload_len > LLP_MAX_PAYLOAD) return 0;

    size_t worst_case = LLP_MAX_FRAME_SIZE(llp_payload_len);
    if (out_buf_size < worst_case) return 0;

    size_t   idx = 0;
    uint16_t crc = 0xFFFF;

    out_buf[idx++] = LLP_MAGIC_1;
    out_buf[idx++] = LLP_MAGIC_2;
    crc = llp_crc16_update(crc, LLP_MAGIC_1);
    crc = llp_crc16_update(crc, LLP_MAGIC_2);

    _llp_write_stuffed(out_buf, &idx, (uint8_t)(llp_payload_len & 0xFF), &crc);
    _llp_write_stuffed(out_buf, &idx, (uint8_t)(llp_payload_len >> 8),   &crc);

    for (uint16_t i = 0; i < llp_payload_len; i++) {
        _llp_write_stuffed(out_buf, &idx, llp_payload[i], &crc);
    }

    _llp_write_stuffed(out_buf, &idx, (uint8_t)(crc & 0xFF), NULL);
    _llp_write_stuffed(out_buf, &idx, (uint8_t)(crc >> 8),   NULL);

    return idx;
}

// =============================================================================
// STATISTICS
// =============================================================================

static inline void llp_get_stats(const llp_parser_t* p,
                                   uint32_t* frames_ok,
                                   uint32_t* frames_error,
                                   uint32_t* timeouts) {
    if (frames_ok)    *frames_ok    = p->frames_ok;
    if (frames_error) *frames_error = p->frames_error;
    if (timeouts)     *timeouts     = p->timeouts;
}

static inline void llp_reset_stats(llp_parser_t* p) {
    p->frames_ok    = 0;
    p->frames_error = 0;
    p->timeouts     = 0;
}

#endif /* LLP_PROTOCOL_H */
