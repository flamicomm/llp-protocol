# Protocol Specification — LLP v4.0.0 (wire format)

**Status:** current (v4.0.0). Supersedes [`PROTOCOL.md`](PROTOCOL.md) (v3.1.0).
**Scope:** the bytes on the wire and the observable behaviour of a conformant
parser. The C/Java/Python APIs are described in §11 as guidance, not as wire.

The reference implementation of this document is
[`scripts/llp_v4.py`](../scripts/llp_v4.py), and the normative test vectors are
under [`spec/vectors/v4/`](../spec/vectors/v4/) (see
[`CONFORMANCE.md`](CONFORMANCE.md)).

---

## 1. Scope and goals

LLP is a framing layer for **byte-oriented links with bit errors**: UART, RS-485,
RF (433 MHz, LoRa), CAN, Bluetooth, or anything that delivers bytes and can
corrupt, drop or invent them.

Goals, in order:

1. **No corrupted frame reaches the application.** A frame is delivered only
   after its CRC validates.
2. **Fast resynchronisation.** After noise, the parser must find the next frame
   in a bounded number of bytes.
3. **Predictable cost.** O(1) work per byte, no dynamic memory, no unbounded
   loops.
4. **Small on 8-bit MCUs.** Fits an ATmega328P with a few hundred bytes of RAM.
5. **Multiplexing.** Applications can route messages by channel.

Non-goals (deliberately out of scope):

- Retransmission, acknowledgements, ordering, flow control. Those are higher
  layers (§9).
- Encryption, compression, fragmentation of messages larger than 255 bytes.
- Self-describing payloads. The payload is opaque bytes; the application owns
  its format.

## 2. Terminology

| Term | Meaning |
|---|---|
| Frame | One complete LLP unit: magic, header, payload, CRC |
| Payload | Application bytes carried by a frame (0–255 bytes) |
| Channel | 1-byte application routing tag |
| Stuffing | Escaping of `0xAA` so the magic cannot appear inside a frame |
| Resync | Abandoning the current frame because a new magic was seen |
| Capacity | Maximum payload a parser instance accepts |

## 3. Wire format

```
 offset  0      1      2      3        4 .. 3+LEN      4+LEN  5+LEN
        +------+------+------+------+----------------+------+------+
        | 0xAA | 0x55 | LEN  | CHAN |    PAYLOAD     | CRC_L| CRC_H|
        +------+------+------+------+----------------+------+------+
          magic (never stuffed)     LEN bytes           little-endian
```

| Field | Size | Description |
|---|---:|---|
| `MAGIC` | 2 | `0xAA 0x55`, never stuffed, never escaped |
| `LEN` | 1 | Payload length, `0..255` |
| `CHANNEL` | 1 | Application routing tag, `0..255` |
| `PAYLOAD` | `LEN` | Opaque application bytes |
| `CRC16` | 2 | CRC16-CCITT-FALSE over `MAGIC + LEN + CHANNEL + PAYLOAD`, little-endian |

All fields from `LEN` onwards are subject to byte stuffing (§5).

### Sizes

| | Bytes |
|---|---:|
| Minimum frame (empty payload, nothing stuffed) | 6 |
| Common frame with `n` bytes and no `0xAA` | `6 + n` |
| Worst case (every byte after the magic is `0xAA`) | `10 + 2n` |

A sender that needs to reserve space before emitting a frame uses
`llp_frame_size(n) = 10 + 2n` (worst case).

### Examples

```
empty payload, channel 0     : AA 55 00 00 23 B3
"ok\n", channel 0            : AA 55 03 00 6F 6B 0A 08 DB
"G1 X10\n", channel 1        : AA 55 07 01 47 31 20 58 31 30 0A E4 27
payload AA 55, channel 0     : AA 55 02 00 AA 00 55 D2 4A
payload 0xAA x8, channel 0   : AA 55 08 00 AA 00 AA 00 AA 00 AA 00 AA 00 AA 00 AA 00 AA 00 B6 BB
```

(The same bytes are checked by the conformance vectors.)

## 4. CRC

```
polynomial : 0x1021
initial    : 0xFFFF
reflection : none
final XOR  : none
coverage   : MAGIC[0], MAGIC[1], LEN, CHANNEL, PAYLOAD[0..LEN-1]
transmitted: CRC_L then CRC_H (little-endian)
```

This is the usual “CRC-16/CCITT-FALSE” (also called `CRC-16/XMODEM` family
constants). Implementations are free to use a bitwise loop, a 16-entry nibble
table or a 256-entry table: the wire result is identical.

The CRC is computed over the **unescaped** field values, i.e. before stuffing.

## 5. Byte stuffing

After the magic, every `0xAA` byte on the wire must be written as `0xAA 0x00`:

| Wire bytes | Meaning |
|---|---|
| `0xAA 0x00` | literal `0xAA` in the current field |
| `0xAA 0x55` | **resync**: a new frame starts here |
| `0xAA 0xXX` (other) | **sync error**: the current frame is discarded |

Consequences:

- The payload may contain any byte sequence, including `AA 55`.
- A frame’s worst-case wire size doubles (§3).
- The parser detects a lost/new frame boundary without waiting for the timeout.

Stuffing applies to `LEN`, `CHANNEL`, `PAYLOAD` and `CRC16`, never to the magic
itself. While the parser is still matching the magic (`WAIT_MAGIC1`/`WAIT_MAGIC2`)
there is no escaping at all: stray bytes are ignored and no error is reported.

## 6. Parser

### State machine

```
           0xAA            0x55
WAIT_MAGIC1 ───► WAIT_MAGIC2 ───► READ_LEN ──► READ_CHANNEL ──► READ_PAYLOAD
     ▲                │ other            │            │                │
     │                └──────► WAIT_MAGIC1│            │  LEN == 0      │
     │                                    ▼            ▼                ▼
     │                            LEN > capacity?  READ_CRC_L ◄─────────┘
     │                                    │              │
     │                                    ▼              ▼
     └────────────────────────────── error        READ_CRC_H ──► frame ok
                                                       │
                                                       └──► error (CRC)
```

| State | On byte | Effect |
|---|---|---|
| `WAIT_MAGIC1` | `0xAA` | → `WAIT_MAGIC2` |
| | other | ignored (garbage tolerance) |
| `WAIT_MAGIC2` | `0x55` | start frame: `READ_LEN`, reset CRC |
| | `0xAA` | stay (covers `AA AA 55`) |
| | other | → `WAIT_MAGIC1` |
| `READ_LEN` | `> capacity` | **`LLP_ERR_LEN`**, discard frame |
| | else | store `LEN`, update CRC → `READ_CHANNEL` |
| `READ_CHANNEL` | any | store `CHANNEL`, update CRC → `READ_PAYLOAD` or `READ_CRC_L` |
| `READ_PAYLOAD` | any | store byte, update CRC, when `LEN` bytes read → `READ_CRC_L` |
| `READ_CRC_L` | any | store low CRC byte → `READ_CRC_H` |
| `READ_CRC_H` | any | compare; equal → **frame delivered**, else **`LLP_ERR_CRC`** |

Escape handling (states `READ_*` only):

- `0xAA` → remember “escape pending” and consume nothing else.
- Next byte `0x00` → the current field byte is a literal `0xAA`.
- Next byte `0x55` → **resync**: report `LLP_ERR_SYNC`, discard the partial
  frame, and start a new frame (the magic has already been consumed).
- Next byte anything else → **`LLP_ERR_SYNC`**, discard the partial frame,
  return to `WAIT_MAGIC1` (the offending byte is consumed).

A frame with `LEN == 0` is valid and carries no payload.

### Normative invariants

1. **No side effects before CRC validation.** No payload byte may be acted upon
   before `READ_CRC_H` accepts the frame.
2. **O(1) work per byte.** No loop bounded by payload content; the only loops
   allowed are the fixed 8-bit CRC step and the (bounded) capacity check.
3. **No dynamic memory**, no VLAs, no recursion.
4. **Bounded memory.** A parser instance never buffers more than `capacity`
   payload bytes.
5. **Early length rejection.** `LEN` is validated as soon as it is read, before
   any payload byte is stored.
6. **Reentrancy.** One parser instance per link, fed from a single context (main
   loop *or* ISR, never both). Distinct instances are independent.
7. **Time is injected.** The parser never reads a clock; the caller passes the
   current millisecond count on every byte.

## 7. Errors

| Code | Value | Cause | Recovery |
|---|---:|---|---|
| `LLP_OK` | 0 | — | — |
| `LLP_ERR_CRC` | 1 | CRC mismatch after a complete frame | frame discarded, `WAIT_MAGIC1` |
| `LLP_ERR_LEN` | 2 | `LEN` greater than the instance capacity | frame discarded, `WAIT_MAGIC1` |
| `LLP_ERR_TIMEOUT` | 3 | inter-byte gap > `timeout_ms` | frame discarded, `WAIT_MAGIC1`; the byte is re-examined as a possible magic |
| `LLP_ERR_SYNC` | 4 | bad escape sequence, or a new magic inside a frame | frame discarded (`AA 55` case starts the new frame) |

Errors are **events**, not fatal states: after reporting one, the parser is
always ready to receive the next frame.

## 8. Timeout

- Measured between consecutive bytes, from the second byte of a frame onwards.
  No timeout is reported while in `WAIT_MAGIC1` (an idle link is normal).
- Default: **2000 ms**, configurable at compile time or per instance.
- Comparison is **strictly greater than** `timeout_ms`.
- `timestamp` arithmetic must be wrap-safe:

  ```c
  if ((uint16_t)(now_ms - last_byte_time) > timeout_ms) { /* timeout */ }
  ```

  With a 16-bit timestamp the maximum supported timeout is **32767 ms**.
- On timeout the offending byte is re-examined as a potential `0xAA` magic, so a
  frame that starts right after a gap is not lost.

## 9. Channels and layering

`CHANNEL` is opaque to the protocol. Recommended allocation:

| Channel | Suggested use |
|---:|---|
| `0` | default / legacy single-stream traffic |
| `1` | commands |
| `2` | telemetry |
| `3` | logs / diagnostics |
| `4..239` | application-defined |
| `240..255` | reserved for future LLP layers |

Reliability features (sequence numbers, ACK, retransmission) are **not** part of
the wire format. They belong to an application layer or a future channel
convention, so that the base framing stays minimal.

## 10. Relationship with v3.1.0

| | v3.1.0 | v4.0.0 |
|---|---|---|
| Header | `MAGIC + LEN(2) + layered payload` | `MAGIC + LEN(1) + CHANNEL(1)` |
| Minimum frame | 7 bytes | 6 bytes |
| Payload layout | layer chain with metadata | opaque bytes |
| Multiplexing | layer IDs with metadata | `CHANNEL` byte |
| Stuffing / CRC / magic | `0xAA` escape, CRC16-CCITT, `AA 55` | **unchanged** |
| Parser states | 7 | 7 |

v4 is **not wire compatible** with v3.1.0; there is no dual-format mode. The v3
implementation remains available as tag `v3.1.0`.

Rationale for each change is recorded in [`DECISIONS.md`](DECISIONS.md).

## 11. Reference API (informative)

The API below is what the C library implements. Other languages mirror it.

```c
/* Parser: payload storage is supplied by the caller (low-RAM profile) or
   embedded in the struct (compatibility profile). */
typedef struct llp4_parser llp4_parser_t;

void           llp4_parser_init(llp4_parser_t *p, uint8_t *buf, uint8_t cap,
                                uint16_t timeout_ms);
int            llp4_parser_feed(llp4_parser_t *p, uint8_t byte, uint16_t now_ms);
const uint8_t *llp4_parser_payload(const llp4_parser_t *p, uint8_t *len_out);
uint8_t        llp4_parser_channel(const llp4_parser_t *p);

/* Encoder: streaming, no frame buffer required. */
size_t llp4_frame_size(uint8_t payload_len);
typedef void (*llp4_write_fn)(void *ctx, uint8_t byte);
void   llp4_tx_begin(llp4_tx_t *tx, uint8_t channel, uint8_t len,
                     llp4_write_fn write, void *ctx);
void   llp4_tx_byte(llp4_tx_t *tx, uint8_t byte);
void   llp4_tx_end(llp4_tx_t *tx);
```

`llp4_parser_feed()` returns `LLP4_EV_FRAME` (1) when a frame is delivered,
`LLP4_EV_NONE` (0) otherwise, or `-LLP4_ERR_*` on error (`p->error` holds the
same code). The `llp4_` prefix is temporary: the header ships as
`include/llp_v4.h` while v3 remains the default; at release it becomes
`llp_protocol.h` with unprefixed names.

Memory profiles:

| Profile | Parser RAM | Notes |
|---|---:|---|
| `full` (payload 128 embedded) | 141 B | ESP32 / ARM / host |
| `mid` (payload 64 embedded) | 77 B | current AVR builds |
| `micro` (external payload) | ~17 B | ATmega328P / ATtiny |

## 12. Conformance

An implementation is conformant when it passes every vector in
[`spec/vectors/v4/`](../spec/vectors/v4/) — see
[`CONFORMANCE.md`](CONFORMANCE.md) for the schema and the checklist.

---

**Document:** wire format v4.0.0
**Supersedes:** [`PROTOCOL.md`](PROTOCOL.md) (v3.1.0)
**Decisions:** [`DECISIONS.md`](DECISIONS.md)
**Vectors:** [`spec/vectors/v4/`](../spec/vectors/v4/)
