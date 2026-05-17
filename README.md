# LLP Protocol — Lightweight Link Protocol

**Header-only C library** for embedded communication. Ultra-lightweight (~500B), robust (CRC16-CCITT, byte stuffing, timeouts), and extensible (layer chain with metadata).

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![PlatformIO CI](https://github.com/EnzoLeonel/llp-protocol/actions/workflows/platformio.yml/badge.svg)](https://github.com/EnzoLeonel/llp-protocol/actions/workflows/platformio.yml)
[![C Standard](https://img.shields.io/badge/C-99-blue)](https://en.wikipedia.org/wiki/C99)
[![Spec v3.1.0](https://img.shields.io/badge/Spec-v3.1.0-blue)](https://github.com/flamicomm/llp-spec)

---

## Table of Contents

1. [Overview](#overview)
2. [Wire Format](#wire-format)
3. [Layer Chain](#layer-chain)
4. [API Reference](#api-reference)
5. [Quick Start](#quick-start)
6. [Examples](#examples)
7. [Testing](#testing)
8. [Project Structure](#project-structure)

---

## Overview

LLP (Layered Link Protocol) is a transport-level framing protocol designed for **microcontrollers and embedded systems**. It provides reliable byte-stream framing with error detection and extensible payload routing.

**Use cases:** UART, RF (433MHz, LoRa), RS485, CAN, Bluetooth, any byte-oriented medium.

**Key features:**
- **CRC16-CCITT** error detection on every frame
- **Byte stuffing** prevents magic sequence (`0xAA 0x55`) from appearing in payload
- **Timeout protection** (default 2000ms) detects truncated frames
- **Layer chain** for metadata routing (passthrough/transform layers)
- **Automatic resync** after errors or noise
- **Header-only**: single `#include "llp_protocol.h"` — no dependencies

### Conformance

This implementation conforms to the [LLP Specification v3.1.0](https://github.com/flamicomm/llp-spec) with **90 passing tests** including all 199 official test vectors.

---

## Wire Format

```
+--------+--------+--------+--------+------------------+--------+--------+
| MAGIC1 | MAGIC2 | LEN_L  | LEN_H  | PAYLOAD (stuffed)| CRC_L  | CRC_H  |
|  0xAA  |  0x55  |  [N]   |  [N]   |  layer chain     |  [N]   |  [N]   |
+--------+--------+--------+--------+------------------+--------+--------+
```

| Field | Size | Description |
|-------|------|-------------|
| `MAGIC1` | 1 | Frame start: `0xAA` (never stuffed) |
| `MAGIC2` | 1 | Frame start: `0x55` (never stuffed) |
| `LEN_L` | 1 | Payload length (low byte), little-endian, stuffed |
| `LEN_H` | 1 | Payload length (high byte), little-endian, stuffed |
| `PAYLOAD` | N | Layer chain bytes, stuffed |
| `CRC_L` | 1 | CRC16-CCITT low byte, stuffed |
| `CRC_H` | 1 | CRC16-CCITT high byte, stuffed |

### Byte Stuffing

Every `0xAA` byte in LEN, PAYLOAD, or CRC is escaped as `0xAA 0x00`.  
An unexpected `0xAA 0x55` inside a frame signals a **resync event** (error recovery).

### CRC Coverage

CRC is computed over **unstuffed** bytes: `MAGIC1 + MAGIC2 + LEN_L + LEN_H + PAYLOAD`.

### Worst-Case Frame Size

```
LLP_MAX_FRAME_SIZE(n) = 2 + 4 + (n * 2) + 4 = 8 + n * 2
```

Where `n` is the layer chain length. With `LLP_MAX_PAYLOAD=128`, worst case is 264 bytes.

---

## Layer Chain

The payload contains an ordered sequence of layer headers followed by raw application data:

```
[LAYER_ID][META_LEN][METADATA...]...[0x00][RAW DATA]
```

| ID Range | Type | Description |
|----------|------|-------------|
| `0x00` | **FinalNode** | End of chain; remaining bytes are raw application data |
| `0x01–0x7F` | **Passthrough** | Metadata can be skipped; payload is unchanged |
| `0x80–0xFE` | **Transform** | Payload was transformed (encrypted/compressed); cannot skip |
| `0xFF` | **Reserved** | Unknown layer ID |

### Meta Length Encoding

- **0–254**: 1 byte (direct value)
- **255+**: 3 bytes: `0xFF` + big-endian high/low

---

## API Reference

### Initialization

```c
llp_parser_t parser;
llp_parser_init(&parser);
```

### Processing

```c
int result = llp_parser_process_byte(&parser, byte, millis());

// Returns:
//   1  → Frame complete, use parser.frame.payload
//   0  → Incomplete (more bytes needed)
//  -1  → Error (check parser.error_code)
```

### Extracting Data

```c
// Build layer chain with FinalNode
uint8_t payload[LLP_MAX_PAYLOAD];
size_t payload_len = llp_build_final_payload(payload, sizeof(payload),
                                              data, data_len);

// Build transport frame
uint8_t frame[LLP_MAX_FRAME_SIZE(payload_len)];
size_t frame_len = llp_build_frame(frame, sizeof(frame),
                                    payload, payload_len);

// Parse: extract layer chain
int result = llp_parser_process_byte(&parser, byte, millis());

// Parse: extract raw application data (skip all layer headers)
uint8_t out[LLP_MAX_PAYLOAD];
int out_len = llp_get_final_payload(&parser.frame, out, sizeof(out));
// Returns: bytes of raw data, or -1 if malformed (no FinalNode)
```

### Error Codes

```c
LLP_ERR_OK              = 0x00  // No error
LLP_ERR_CHECKSUM        = 0x01  // CRC mismatch
LLP_ERR_PAYLOAD_LEN     = 0x02  // Length > LLP_MAX_PAYLOAD
LLP_ERR_TIMEOUT         = 0x03  // Inter-byte timeout exceeded
LLP_ERR_SYNC            = 0x04  // Invalid escape or resync
LLP_ERR_BUFFER_FULL     = 0x05  // Internal buffer overflow
LLP_ERR_TRANSFORM_LAYER = 0x06  // Cannot traverse transform layer
LLP_ERR_MALFORMED_LAYER = 0x07  // Malformed layer chain
```

### Statistics

```c
uint32_t frames_ok, frames_error, timeouts;
llp_get_stats(&parser, &frames_ok, &frames_error, &timeouts);
llp_reset_stats(&parser);
```

---

## Quick Start

### 1. Include the library

```c
#include "llp_protocol.h"
```

### 2. Initialize parser

```c
llp_parser_t parser;
llp_parser_init(&parser);
```

### 3. Process incoming bytes

```c
void loop() {
    while (Serial.available()) {
        uint8_t byte = Serial.read();
        int result = llp_parser_process_byte(&parser, byte, millis());

        if (result == 1) {
            // Frame received successfully
            uint8_t data[LLP_MAX_PAYLOAD];
            int len = llp_get_final_payload(&parser.frame, data, sizeof(data));
            if (len > 0) {
                // data contains raw application payload
            }
        } else if (result == -1) {
            // Error: check parser.error_code
        }
    }
}
```

### 4. Send a response (optional)

```c
void send_response(const uint8_t* data, uint16_t len) {
    uint8_t payload[LLP_MAX_PAYLOAD];
    size_t payload_len = llp_build_final_payload(payload, sizeof(payload),
                                                  data, len);

    uint8_t frame[LLP_MAX_FRAME_SIZE(payload_len)];
    size_t frame_len = llp_build_frame(frame, sizeof(frame),
                                       payload, (uint16_t)payload_len);

    Serial.write(frame, frame_len);
}
```

---

## Examples

### Minimal UART Echo

Located at `examples/minimal_uart/minimal_uart.ino`:
- Receives frames via Serial
- Echoes received data back

### Request-Response with Retries

Located at `examples/request_response/request_response.ino`:
- Sends commands with ID and waits for ACK
- Retries up to 3 times on timeout
- Manages up to 5 pending requests

---

## Testing

### Run all tests

```bash
platformio test -e test -v
```

**Expected output:** `90 Tests 0 Failures 0 Ignored OK`

### Test categories

| Suite | Tests | Description |
|-------|-------|-------------|
| `test_parser_init.c` | 5 | Parser initialization and reset |
| `test_frame_builder.c` | 11 | Frame construction and byte stuffing |
| `test_parser_process.c` | 11 | Byte-by-byte parsing and error handling |
| `test_crc.c` | 8 | CRC16-CCITT validation |
| `test_spec_vectors.c` | 55 | Spec conformance (199 vectors total) |

### Spec vector categories

- **Transport**: valid, crc, stuffing, resync, truncation, timeout
- **Layers**: passthrough, transform, malformed, traversal
- **Parser**: incremental, fragmented, recovery

### Cross-language testing

```bash
gcc -std=c99 -I include -o /tmp/llp_cross_gen tools/cross_test_generate.c
/tmp/llp_cross_gen /tmp/llp_cross
```

---

## Project Structure

```
llp-protocol/
├── include/
│   └── llp_protocol.h          # Single-header library (~470 lines)
├── src/
│   └── llp_protocol.c           # Standalone compilation verification
├── test/
│   ├── test_main.c             # Test runner (90 tests)
│   ├── test_spec_common.h      # Spec test utilities
│   ├── test_spec_vectors.c      # 199 spec conformance vectors
│   ├── test_parser_init.c       # 5 tests
│   ├── test_frame_builder.c     # 11 tests
│   ├── test_parser_process.c    # 11 tests
│   └── test_crc.c               # 8 tests
├── tools/
│   └── cross_test_generate.c    # Cross-language test vector generator
├── examples/
│   ├── minimal_uart/            # Basic UART echo
│   └── request_response/         # Request-response with retries
├── docs/
│   └── PROTOCOL.md              # Protocol specification
├── platformio.ini                # Multi-platform build config
├── library.properties            # Arduino Library Manager metadata
├── CHANGELOG.md
├── README.md
├── STRUCTURE.md
├── TESTING.md
├── BUGS.md
└── LICENSE
```

---

## Configuration

Override defaults with build flags:

```bash
-DLLP_MAX_PAYLOAD=256       # Default: 128
-DLLP_FRAME_TIMEOUT_MS=3000 # Default: 2000
```

---

## License

MIT — See [LICENSE](LICENSE)