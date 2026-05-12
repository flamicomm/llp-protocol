# Project Structure — LLP Protocol v3.0.0

## Directory Tree

```
llp-protocol/
│
├── platformio.ini                    # PlatformIO configuration
│
├── src/
│   └── llp_protocol.c                # Source (includes header-only lib)
│
├── include/
│   └── llp_protocol.h                # Public API (single-header library)
│
├── test/
│   ├── test_parser_init.c            # 5 tests — Parser initialization
│   ├── test_frame_builder.c          # 11 tests — Frame construction
│   ├── test_parser_process.c         # 11 tests — Byte-by-byte processing
│   └── test_crc.c                    # 8 tests — CRC16-CCITT validation
│
├── lib/                              # External libraries (auto-installed)
│   └── (Unity via PIO lib-deps)
│
├── examples/
│   ├── minimal_uart/
│   │   └── minimal_uart.ino          # Minimal UART echo example
│   └── request_response/
│       └── request_response.ino      # Request-response with retries
│
├── docs/
│   └── PROTOCOL.md                   # Protocol specification
│
├── .github/
│   └── workflows/
│       └── platformio.yml            # CI/CD via GitHub Actions
│
├── .gitignore
├── CHANGELOG.md
├── LICENSE
├── README.md
├── STRUCTURE.md                      # This file
├── TESTING.md                        # Testing guide
└── library.properties                # Arduino Library Manager metadata
```

## File Purposes

| File | Purpose |
|------|---------|
| `platformio.ini` | PlatformIO build/test configuration |
| `src/llp_protocol.c` | Verifies header compiles standalone |
| `include/llp_protocol.h` | Full protocol implementation (header-only) |
| `test/test_parser_init.c` | Parser initialization tests |
| `test/test_frame_builder.c` | Frame building and roundtrip tests |
| `test/test_parser_process.c` | Byte processing, error handling |
| `test/test_crc.c` | CRC16-CCITT validation tests |
| `.github/workflows/platformio.yml` | Automated test execution on push/PR |

## Test Summary

| Test File | Tests | Focus |
|-----------|-------|-------|
| `test_parser_init.c` | 5 | Parser startup, state reset, stats |
| `test_frame_builder.c` | 11 | Frame construction, stuffing, roundtrip |
| `test_parser_process.c` | 11 | Byte parsing, errors, recovery, timeouts |
| `test_crc.c` | 8 | CRC16-CCITT, error detection, determinism |
| **Total** | **35** | |

## Running Tests

```bash
platformio test -e test -v         # All tests verbose
platformio test -e test --filter test_crc  # Specific suite
```

## Build Environments

```bash
platformio run -e arduino_uno      # Arduino UNO
platformio run -e arduino_nano     # Arduino Nano
platformio run -e arduino_mega     # Arduino Mega
platformio run -e esp8266          # ESP8266
platformio run -e esp32            # ESP32
platformio run -e stm32f103        # STM32 (Nucleo)
platformio run -e attiny85         # ATtiny85
```
