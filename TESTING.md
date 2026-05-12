# Testing Guide — LLP Protocol v3.0.0

## Prerequisites

```bash
pip install platformio
```

## Run All Tests

```bash
platformio test -e test -v
```

Expected output:
```
test_parser_init     [PASSED]
test_frame_builder   [PASSED]
test_parser_process  [PASSED]
test_crc             [PASSED]
```

Total: **35 tests**

## Test Suites

### test_parser_init.c (5 tests)
| Test | What it verifies |
|------|-----------------|
| `test_init_sets_state_wait_magic1` | State after init is WAIT_MAGIC1 |
| `test_init_clears_all_fields` | All struct fields properly zeroed |
| `test_init_clears_frame_payload_len` | Frame payload length is 0 |
| `test_can_be_reinitialized` | Calling init again resets everything |
| `test_init_resets_stats` | frames_ok/error/timeouts start at 0 |

### test_frame_builder.c (11 tests)
| Test | What it verifies |
|------|-----------------|
| `test_build_final_payload_empty` | Empty data -> `[0x00]` |
| `test_build_final_payload_with_data` | Data wrapped correctly |
| `test_build_final_payload_small_buffer` | Returns 0 when buffer too small |
| `test_build_frame_magic_bytes` | Frame starts with 0xAA 0x55 |
| `test_build_frame_payload_length` | Length field correct LE |
| `test_build_frame_roundtrip` | Build -> parse -> extract matches |
| `test_build_frame_small_buffer` | Returns 0 for tiny output buffer |
| `test_build_frame_oversized_payload` | Returns 0 when payload > LLP_MAX_PAYLOAD |
| `test_build_frame_stuffing` | 0xAA in payload is stuffed/unstuffed |
| `test_build_frame_deterministic` | Same input -> same output |
| `test_build_frame_empty_payload_chain` | FinalNode-only frame roundtrips |

### test_parser_process.c (11 tests)
| Test | What it verifies |
|------|-----------------|
| `test_process_incomplete_frame_returns_zero` | Partial frame returns 0 |
| `test_process_complete_frame_returns_one` | Complete frame returns 1 |
| `test_process_extracts_correct_data` | Received data matches sent data |
| `test_process_crc_error_returns_negative_one` | Corrupted CRC returns -1 |
| `test_process_timeout_returns_negative_one` | Timeout returns -1 |
| `test_process_recovers_from_noise` | Random bytes before frame |
| `test_process_multiple_frames` | 3 consecutive frames parsed OK |
| `test_process_increments_error_counter` | OK and error counters tracked |
| `test_process_stuffed_byte` | 0xAA in payload handles correctly |
| `test_process_sync_error_on_aa55_inside_frame` | 0xAA 0x55 mid-frame detected |
| `test_process_payload_len_error` | Oversized payload length rejected |

### test_crc.c (8 tests)
| Test | What it verifies |
|------|-----------------|
| `test_crc16_known_values` | "123456789" -> 0x29B1 (CCITT standard) |
| `test_crc16_differs_for_different_payloads` | Different data -> different CRC |
| `test_crc16_detects_bit_flip` | Single bit flip changes CRC |
| `test_crc16_detects_burst_error` | Multi-bit errors detected |
| `test_crc16_deterministic` | Same data -> same CRC always |
| `test_crc16_update_chain` | Sequential updates match batch |
| `test_crc16_order_matters` | "AB" != "BA" |
| `test_crc16_empty` | Zero-length input -> 0xFFFF |

## Debugging

```bash
platformio test -e test -vvv              # Extra verbose with compiler output
platformio test -e test --filter test_crc  # Run specific test file
pio test --verbose -e test                 # Alternative syntax
```
