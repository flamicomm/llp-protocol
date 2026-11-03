# Protocol Specification — LLP v3.1.0

> **Status:** v3.1.0 is the **implemented** version. The next wire format is
> specified in [`SPEC-v4.md`](SPEC-v4.md) (draft), with the rationale in
> [`DECISIONS.md`](DECISIONS.md). v4 replaces the 2-byte length + layer chain
> with `MAGIC + LEN(1) + CHANNEL(1)`; stuffing, magic and CRC are unchanged.

## Wire Format

```
[0xAA][0x55][LEN_L][LEN_H][PAYLOAD...][CRC_L][CRC_H]
```

- **MAGIC**: 0xAA 0x55 (synchronization marker)
- **LEN**: Payload length in little-endian (0–512 bytes, configurable via `LLP_MAX_PAYLOAD`)
- **PAYLOAD**: Layer chain containing application data
- **CRC16**: CRC16-CCITT (poly=0x1021, init=0xFFFF, no reflection) over MAGIC+LEN+PAYLOAD

### Byte Stuffing

Every 0xAA byte in LEN, PAYLOAD, or CRC is escaped as `0xAA 0x00`.  
An unexpected `0xAA 0x55` sequence inside a frame signals a **resync event** (error recovery).

## Layer Chain Format

The payload contains a chain of layers, terminated by a FinalNode:

```
[LAYER_ID][META_LEN][METADATA...]...[0x00][RAW APPLICATION DATA]
```

- **0x00** → FinalNode: end of chain, raw bytes follow
- **0x01–0x7F** → Passthrough layer: metadata can be skipped, payload unchanged
- **0x80–0xFE** → Transform layer: payload was modified (encrypt/compress), cannot skip
- **0xFF** → Reserved

### META_LEN Encoding

- **0–254**: 1 byte (direct value)
- **255+**: 3 bytes: `0xFF` followed by 2 bytes big-endian (e.g., `0xFF 0x01 0x00` = 256)

## Parser State Machine

```
WAIT_MAGIC1 ──0xAA──→ WAIT_MAGIC2 ──0x55──→ READ_LEN_L
    │                     │  0xAA               │
    │   other bytes       │  other bytes        ↓
    └─────────────────────┴──────────→ READ_LEN_H
                                              ↓
                                         READ_PAYLOAD (LEN > 0)
                                         READ_CRC_L (LEN == 0)
                                              ↓
                                         READ_CRC_H ──validate──→ FRAME OK (return 1)
                                              │
                                              └──invalid──→ ERROR (return -1)

Timeout: any byte arriving >LLP_FRAME_TIMEOUT_MS after the previous byte resets the parser.
Escape: 0xAA inside frame data → 0xAA 0x00 (escaped byte); 0xAA 0x55 → resync.
Invalid escape: 0xAA followed by any byte other than 0x00 or 0x55 → SYNC_ERROR.
```

## Error Codes

```c
LLP_ERR_OK              = 0x00  // No error
LLP_ERR_CHECKSUM        = 0x01  // CRC mismatch
LLP_ERR_PAYLOAD_LEN     = 0x02  // Payload exceeds LLP_MAX_PAYLOAD
LLP_ERR_TIMEOUT         = 0x03  // Inter-byte timeout exceeded
LLP_ERR_SYNC            = 0x04  // Invalid escape or resync
LLP_ERR_BUFFER_FULL     = 0x05  // Buffer overflow
LLP_ERR_TRANSFORM_LAYER = 0x06  // Cannot traverse transform layer
LLP_ERR_MALFORMED_LAYER = 0x07  // Malformed layer chain
```

---

**Document:** Protocol specification v3.1.0  
**Date:** 2026-05-17  
**Author:** EnzoLeonel