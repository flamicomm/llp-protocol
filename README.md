# LLP Protocol — Lightweight Link Protocol

**Header-only C library for embedded communication over noisy byte links.**
Wire format **4.0.0**: 6-byte fixed header, CRC16-CCITT, byte stuffing,
channels and timeouts. The parser needs **17 bytes of RAM** on an ATmega328P.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![PlatformIO CI](https://github.com/EnzoLeonel/llp-protocol/actions/workflows/platformio.yml/badge.svg)](https://github.com/EnzoLeonel/llp-protocol/actions/workflows/platformio.yml)
[![C Standard](https://img.shields.io/badge/C-99-blue)](https://en.wikipedia.org/wiki/C99)
[![Spec v4.0.0](https://img.shields.io/badge/Spec-v4.0.0-blue)](docs/SPEC-v4.md)
[![codecov](https://codecov.io/github/flamicomm/llp-protocol/graph/badge.svg?token=DGZMIPFEBE)](https://codecov.io/github/flamicomm/llp-protocol)

---

## Table of Contents

1. [Overview](#overview)
2. [Wire Format](#wire-format)
3. [Channels](#channels)
4. [API Reference](#api-reference)
5. [Memory Footprint](#memory-footprint)
6. [Quick Start](#quick-start)
7. [Examples](#examples)
8. [Testing](#testing)
9. [Project Structure](#project-structure)
10. [Migrating from v3](#migrating-from-v3)

---

## Overview

LLP is a transport-level framing protocol for **microcontrollers and embedded
systems**. It turns a byte stream (UART, RS-485, RF 433 MHz, LoRa, Bluetooth,
CAN, sockets) into validated messages.

**The one hard guarantee:** a frame reaches the application only if every byte
of it survived intact. A corrupted command is worse than a lost one — a CNC
that receives `X-90.5` instead of `X-10.5` cuts the wrong path.

**Key features:**

- **CRC16-CCITT** on every frame, checked before the payload is handed over.
- **Magic-based resynchronisation**: after noise, the next frame is found in a
  few dozen bytes (measured: ~50–70 bytes).
- **Byte stuffing** so the magic cannot appear inside a frame.
- **Channel byte** for routing without per-message overhead.
- **Inter-byte timeout** (default 2000 ms) for truncated frames.
- **O(1) work per byte**, no dynamic memory, no recursion.
- **Header-only**, zero dependencies: `#include "llp_v4.h"`.
- **Caller-owned buffers**: the parser writes into the application's buffer and
  the encoder streams through a write callback, so neither needs a
  frame-sized buffer.

**Why this and not COBS/SLIP/HDLC?** See [`docs/WHY-LLP.md`](docs/WHY-LLP.md).
**Integration guide:** [`docs/INTEGRATION.md`](docs/INTEGRATION.md).

### Conformance

This implementation conforms to the specification in
[`docs/SPEC-v4.md`](docs/SPEC-v4.md). The language-independent test vectors live
in [`spec/vectors/v4/`](spec/vectors/v4/) and are executed by the C test suite;
`docs/CONFORMANCE.md` documents the schema and the checklist.

Measured robustness (`docs/ROBUSTNESS.md`): **0 corrupted frames delivered** in
every noise scenario and **0 false accepts in 16 MB of pure random bytes**.

---

## Wire Format

```
 offset  0      1      2      3        4 .. 3+LEN      4+LEN  5+LEN
        +------+------+------+------+----------------+------+------+
        | 0xAA | 0x55 | LEN  | CHAN |    PAYLOAD     | CRC_L| CRC_H|
        +------+------+------+------+----------------+------+------+
          magic (never stuffed)     LEN bytes           little-endian
```

| Field | Size | Description |
|---|---:|---|
| `MAGIC` | 2 | `0xAA 0x55`, never stuffed |
| `LEN` | 1 | Payload length, `0..255` |
| `CHANNEL` | 1 | Application routing tag, `0..255` |
| `PAYLOAD` | `LEN` | Opaque application bytes |
| `CRC16` | 2 | CRC16-CCITT-FALSE over `MAGIC + LEN + CHANNEL + PAYLOAD` |

All fields from `LEN` onwards are subject to byte stuffing (`0xAA` becomes
`0xAA 0x00`). A frame with `LEN == 0` is valid.

| | Bytes |
|---|---:|
| Minimum frame (empty payload, nothing stuffed) | 6 |
| Common frame with `n` bytes and no `0xAA` | `6 + n` |
| Worst case (every byte after the magic is `0xAA`) | `10 + 2n` |

```
empty payload, channel 0     : AA 55 00 00 23 B3
"ok\n", channel 0            : AA 55 03 00 6F 6B 0A 08 DB
"G1 X10\n", channel 1        : AA 55 07 01 47 31 20 58 31 30 0A E4 27
payload AA 55, channel 0     : AA 55 02 00 AA 00 55 D2 4A
```

Full state machine, error semantics and invariants: [`docs/SPEC-v4.md`](docs/SPEC-v4.md).

## Channels

`CHANNEL` is opaque to the protocol. A convention that works well:

| Channel | Suggested use |
|---:|---|
| `0` | default / text command stream |
| `1` | binary commands |
| `2` | telemetry |
| `3` | logs |
| `4..239` | application-defined |
| `240..255` | reserved |

Reliability features (sequence numbers, ACK, retransmission) are **not** part of
the framing: they belong to the application or a channel convention.

## API Reference

### Parser

```c
#include "llp_v4.h"

static uint8_t payload[64];           /* caller-owned, capacity */
static llp4_parser_t parser;

llp4_parser_init(&parser, payload, sizeof(payload), 2000);

int rc = llp4_parser_feed(&parser, byte, now_ms);
if (rc == LLP4_EV_FRAME) {
    uint8_t len;
    const uint8_t *data = llp4_parser_payload(&parser, &len);
    handle(llp4_parser_channel(&parser), data, len);
} else if (rc < 0) {
    /* rc is -LLP4_ERR_*; parser.error holds the same code */
}
```

| Symbol | Meaning |
|---|---|
| `LLP4_EV_FRAME` / `LLP4_EV_NONE` | `1` / `0` |
| `LLP4_ERR_CRC` / `_LEN` / `_TIMEOUT` / `_SYNC` | `1..4` |
| `llp4_parser_reset()` | reset the frame state, keep the configuration |
| `llp4_parser_discard()` | drop the frame in progress |
| `llp4_parser_payload()` / `_channel()` | accessors for the last frame |

### Encoder

```c
static void sink(void *ctx, uint8_t byte) { link_write(byte); }

llp4_tx_t tx;
llp4_tx_begin(&tx, channel, len, sink, NULL);   /* magic + LEN + CHANNEL */
for (uint8_t i = 0; i < len; i++) {
    llp4_tx_byte(&tx, data[i]);                 /* stuffing + CRC */
}
llp4_tx_end(&tx);                               /* CRC */
```

`llp4_frame_size(len)` (macro form `LLP4_FRAME_SIZE(len)`) gives the worst-case
size, for reserving space in a ring before encoding. `llp4_encode()` is a
convenience wrapper when the caller does have a buffer.

### Ring helper (optional)

`llp_v4_ring.h` provides a single-producer/single-consumer byte ring with 8-bit
indices (atomic on 8-bit MCUs) for links up to 254 bytes: `llp4_ring_init`,
`_push`, `_pop`, `_count`, `_free`, `_capacity`.

### Configuration

| Macro | Default | Effect |
|---|---|---|
| `LLP4_DEFAULT_TIMEOUT_MS` | `2000` | Suggested inter-byte timeout |
| `LLP4_ENABLE_STATS` | `0` | Parser counters (+6 bytes) |
| `LLP4_CRC_TABLE` | `1` | 16-nibble CRC table (flash on AVR) vs bitwise |

## Memory Footprint

<!-- LLP4-SIZE-TABLE:BEGIN -->
| Target | Profile | `sizeof(parser)` | `sizeof(tx)` | Core flash |
|---|---|---:|---:|---:|
| ATmega328P | `micro` | 17 B | 6 B | 622 B |
| ATmega328P | `micro-bits` | 17 B | 6 B | 564 B |
| ATmega328P | `stats` | 23 B | 6 B | 622 B |
| x86-64 (host) | `micro` | 24 B | 24 B | 639 B |
| x86-64 (host) | `micro-bits` | 24 B | 24 B | 621 B |
| x86-64 (host) | `stats` | 32 B | 24 B | 638 B |

_Generated by `scripts/measure_v4.py`. "Core flash" is the code the v4 core adds over an empty build (encode + parse + CRC), not a full application._
<!-- LLP4-SIZE-TABLE:END -->

Regenerate with `python3 scripts/measure_v4.py --update-readme`. CI fails if this
table drifts, if the parser exceeds 20 B on AVR or if the core flash exceeds
800 B.

## Quick Start

```c
#include "llp_v4.h"

uint8_t payload[64];
llp4_parser_t parser;

void link_init(void) { llp4_parser_init(&parser, payload, sizeof payload, 2000); }

void link_rx_byte(uint8_t b, uint16_t now_ms) {
    if (llp4_parser_feed(&parser, b, now_ms) == LLP4_EV_FRAME) {
        uint8_t len;
        const uint8_t *data = llp4_parser_payload(&parser, &len);
        /* use data[0..len) here, and only here: it is a validated frame */
    }
}
```

Send: `llp4_encode(0, text, len, out, sizeof out)` or stream with `llp4_tx_*`.

## Examples

Located at [`examples/v4/`](examples/v4/) (built in CI):

- [`avr_baremetal/`](examples/v4/avr_baremetal/main.c) — ATmega328P with UART
  interrupt rings and a streaming sink; complete application in ~2 KB of flash.
- [`host_posix/`](examples/v4/host_posix/host_posix.c) — file descriptor / TCP
  adapter with `--selftest` (roundtrips + noise) used by CI.

Integration guide (context rules, sizing, RS-485, text-over-LLP):
[`docs/INTEGRATION.md`](docs/INTEGRATION.md).

## Testing

```bash
platformio test -e test -v          # 18 Unity tests (core + conformance + ring)
python3 scripts/gen_vectors_v4.py --check
sh scripts/check_robustness.sh      # property fuzzer + noise channel
sh scripts/check_examples.sh        # builds/runs the examples
```

| Suite | Tests | Description |
|---|---|---|
| `test_v4.c` | 13 | Core behaviour + all 29 conformance vectors |
| `test_v4_ring.c` | 5 | Ring helper |

Additional checks run by CI:

- **Vectors**: `scripts/gen_vectors_v4.py` self-checks every vector against the
  reference implementation and regenerates the C fixture (`test/v4_vectors_generated.h`).
- **Fuzzer** (`tools/fuzz_v4.c`): four input distributions, five invariants,
  AddressSanitizer + UBSan. 600 000 inputs across three seeds.
- **Noise channel** (`tools/noise_report.c`): bit flips, drops, inserts and
  bursts. Results in [`docs/ROBUSTNESS.md`](docs/ROBUSTNESS.md).
- **Memory footprint**: budget gate + README table check (`scripts/measure_v4.py`).
- **Examples**: `avr-gcc` compile + host self-test.

## Project Structure

```
llp-protocol/
├── include/
│   ├── llp_v4.h                # wire format 4.0.0 (parser + streaming encoder)
│   └── llp_v4_ring.h           # optional SPSC byte ring
├── docs/
│   ├── SPEC-v4.md              # normative specification
│   ├── DECISIONS.md            # ADR log
│   ├── WHY-LLP.md              # comparison with COBS/SLIP/HDLC/Modbus
│   ├── CONFORMANCE.md          # vector schema + checklist
│   ├── INTEGRATION.md          # how to wire it to a link
│   ├── ROBUSTNESS.md           # measured behaviour under noise
│   └── PROTOCOL.md             # v3.1.0 specification (historical)
├── examples/v4/                # AVR bare-metal + POSIX/TCP adapters
├── spec/vectors/v4/            # language-independent conformance vectors
├── scripts/                    # generators, measurements, checks
├── tools/                      # fuzzer + noise report
├── test/                       # Unity suite (18 tests)
└── src/                        # compile smoke test for every environment
```

## Migrating from v3

**v4 is not wire compatible with v3.** The v3.1.0 implementation is available at
tag `v3.1.0`; there is no dual-format mode.

| | v3.1.0 | v4.0.0 |
|---|---|---|
| Header | `MAGIC + LEN(2) + layer chain` | `MAGIC + LEN(1) + CHANNEL(1)` |
| Minimum frame | 7 bytes | 6 bytes |
| Payload | layer chain with metadata | opaque bytes |
| Routing | layer IDs + metadata | `CHANNEL` byte |
| Stuffing / CRC / magic | `0xAA` escape, CRC16-CCITT, `AA 55` | **unchanged** |
| Parser RAM (AVR) | 77 B (payload 64 embedded) | 17 B + caller buffer |
| Encoder | builds a frame in RAM | streams, no frame buffer |

API mapping:

| v3 | v4 |
|---|---|
| `llp_parser_init(&p)` | `llp4_parser_init(&p, buf, cap, timeout_ms)` |
| `llp_parser_process_byte(&p, b, now)` | `llp4_parser_feed(&p, b, now_ms)` |
| `llp_get_final_payload_ptr(&p.frame, &len)` | `llp4_parser_payload(&p, &len)` |
| `llp_build_final_payload()` + `llp_build_frame()` | `llp4_tx_begin/byte/end` or `llp4_encode()` |
| `p.error_code` | `p.error` (`LLP4_ERR_*`) |

A written migration guide per consumer lives in the consumers' repositories
(`grbl-llp`, `esp8266-llp-bridge`).

## License

MIT — see [LICENSE](LICENSE).

LLP Specification v4.0.0 — Copyright © 2026 Flamingo Communications

## Credits

- Grbl by Sungeun "Sonny" Jeon (@chamnit) — the CNC firmware this protocol was
  built for.
