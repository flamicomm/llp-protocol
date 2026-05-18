# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [3.0.0] - 2026-05-17

### Changed
- **Breaking**: Wire format v3.0.0 — removed TYPE and ID fields, added layer chain payload
- **Breaking**: `llp_build_frame()` signature changed: removed type, id, version parameters
- Byte stuffing now covers LEN, PAYLOAD, and CRC fields (every 0xAA → 0xAA 0x00)
- 0xAA 0x55 inside a frame now triggers resync (replaces old TYPE/ID-based design)
- Layer chain format replaces flat payload: FinalNode, Passthrough, Transform layers
- Extended META_LEN encoding: 0xFF prefix for meta lengths ≥ 255

### Added
- `llp_build_final_payload()` helper to wrap raw data with FinalNode
- `llp_find_layer()` to search for specific layers in the chain
- `llp_get_final_payload()` to extract raw application data from the chain
- `LLP_LAYER_IS_PASSTHROUGH()`, `LLP_LAYER_IS_TRANSFORM()`, `LLP_LAYER_IS_FINAL()` macros
- `LLP_ERR_TRANSFORM_LAYER` and `LLP_ERR_MALFORMED_LAYER` error codes
- Parser statistics: `frames_ok`, `frames_error`, `timeouts`
- `llp_get_stats()` and `llp_reset_stats()` functions

### Fixed
- Optimistic resync on timeout: 0xAA byte after timeout immediately enters WAIT_MAGIC2
- CRC now covers MAGIC bytes (0xAA, 0x55) in addition to LEN and PAYLOAD

### Removed
- TYPE, ID, VERSION fields from wire format
- `llp_build_frame()` old signature with type/id/version parameters

## [1.0.0] - 2026-03-30

### Added
- Initial release of LLP Protocol
- Core parser with state machine
- CRC16-CCITT checksum validation
- Support for 0-512 byte payload
- Examples for Arduino UART
- Complete documentation and README

### Features
- RF-robust synchronization (consecutive magic bytes handling)
- Timeout protection (2 seconds configurable)
- Defensive NULL pointer validation
- Direct payload storage (optimized for small RAM)
- Statistics tracking (frames_ok, frames_error, timeouts)

---

## [Unreleased]

### Planned
- RS485 multi-node example
- LoRa integration example
- Advanced retransmission patterns
- Fragmentation support for large payloads
- Hardware CRC acceleration (STM32, etc.)
- Fix: timeout handling should update last_byte_time for correct subsequent behavior