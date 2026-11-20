# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- `test/test_v4.c`: normative CRC KAT — `llp4_crc16("123456789") == 0x29B1`
  (SPEC-v4 §4) — plus a `WAIT_MAGIC2` timeout case (`0xAA`, gap, `0x55` →
  `-LLP4_ERR_TIMEOUT`, recovery on the next frame).
- `[env:test_bits]` in `platformio.ini` and a CI step that run the whole suite
  with `LLP4_CRC_TABLE=0`, so the bitwise CRC branch is executed (and held to
  the same KAT). `scripts/check_robustness.sh` also fuzzes a bitwise build.
- `tools/fuzz_v4.c`: second campaign with random R1 command/ACK/HELLO
  interleavings and ring push/pop operations; invariants for delivery bytes,
  retransmission identity/bounds and FIFO order (three new properties).
- `test/test_v4_rel.c`: `test_rel_tx_exhausted_reports_once_per_backoff` pins
  exactly one `EXHAUSTED` event per `BACKOFF_MS` under 1 ms polling, with no
  behaviour change.
- `examples/v4/host_posix`: `serve_fd` injects `CLOCK_MONOTONIC` time instead of
  a constant `0`, so the inter-byte timeout actually fires.
- `examples/v4/avr_baremetal`: `ticks_read()` takes the 16-bit tick snapshot
  with interrupts disabled (`SREG` save/restore); the pattern is documented in
  `docs/INTEGRATION.md`.
- `scripts/llp_v4.py --self-test` and `scripts/llp_v4_rel.py --self-test`: pin
  the init guards of the Python reference (timeout clamp, `max_retries` clamp,
  R1 timer clamps) so it cannot drift from the C headers. CI runs both.
- `llp4_rel_tx_on_ack_frame()` (`llp_v4_rel.h`): applies the §3 MUST "ignore an
  ACK frame whose length is not 1" (plus the channel-2 check) before releasing
  the window, so the rule no longer lives only in the caller's dispatch.
- `scripts/sync_corpus.sh` and `scripts/check_corpus.py`: the conformance corpus
  is vendored from `llp-spec` (`spec/vectors/v4/*.json` plus its
  `MANIFEST.sha256`) and the emitted fixture is pinned by `FIXTURE.sha256`.
  `check_corpus.py` verifies both offline and, when `../llp-spec` is present,
  proves that `build_vectors.py --emit-c` reproduces the committed header byte
  for byte.
- `src/llp_v4_check.c`: compile-time asserts for the Profile R1 state
  (`llp4_rel_tx_t` 76 B on AVR, `llp4_rel_rx_t` 6 B) next to the parser assert.
- `docs/INTEGRATION.md`: threat model — CRC16 is a noise check, not
  authentication, and R1 deduplication is not anti-replay.
- `scripts/llp_v4_rel.py`: Python counterpart of the profile (vendored from the
  llp-spec reference), used by serial tools.
- `llp_v4_rel.h`: optional **Profile R1**, a reliable command channel on top of
  the v4 framing (sequence number, ACK, duplicate suppression, retransmission).
  Normative text: llp-spec `docs/RELIABILITY.md`. Wire layout pinned by the new
  encode vectors `enc-rel-*`; behaviour covered by `test/test_v4_rel.c`
  (traces T1–T8).

### Changed
- Counts in README/TESTING/STRUCTURE/ROBUSTNESS regenerated from the commands:
  **50** test cases per environment (`test_v4.c` 17, `test_v4_ring.c` 6,
  `test_v4_rel.c` 27) and the fuzzer gate (100 000 core + 20 000 R1/ring with
  the table CRC; 20 000 + 20 000 with the bitwise CRC). Corpus counts are
  unchanged: 170 cases, 161 emitted, 60 encode + 101 decode rows.
- API contract guards, init-only and constant-time (CNC-030 L-M3):
  `llp4_parser_init()` forces `cap = 0` when the buffer is `NULL` (a frame with
  `LEN > 0` is rejected with `LLP4_ERR_LEN` instead of storing through the null
  pointer) and clamps the inter-byte timeout (`0` → default, `> 32767` →
  `LLP4_TIMEOUT_MAX_MS`); `llp4_ring_init()` disables the ring on
  `buf == NULL` or `size < 2` (capacity 0, push/pop no-ops) instead of wrapping
  `size - 1` to 255 and writing out of bounds; `llp4_rel_tx_init()` clamps
  `max_retries` to the normative 1–10 (a `0` used to exhaust with zero
  retransmissions) and the R1 timers to 32767. Hot paths unchanged.
- `platformio.ini`: dropped the dead v3 flags (`LLP_MAX_PAYLOAD`,
  `LLP_FRAME_TIMEOUT_MS`, `LLP_ENABLE_STATS`) from every environment; only the
  real `LLP4_*` switches remain. The v4 payload capacity is a runtime
  `uint8_t cap`, not a compile-time size.
- `scripts/llp_v4.py` / `scripts/llp_v4_rel.py`: the Python reference applies the
  same clamps as the C headers (`clamp_timeout()`, `_clamp_retries()`,
  `_clamp_timer()`), so both languages answer the same for `timeout_ms = 0`,
  `timeout_ms > 32767`, `max_retries = 0` and `max_retries > 10`. The wire is
  unchanged: the corpus and the emitted fixture are byte-identical.
- The Unity suite now walks the canonical corpus of `llp-spec` instead of a local
  29-case set: 60 encode and 101 decode rows (161 of the 170 cases; the nine
  `rel-*` wire cases are not emitted by `--emit-c` yet, tracked as **L-I1b** in
  `llp-spec`). The two counts are pinned inside the test so a truncated fixture
  fails loudly. See `docs/CONFORMANCE.md`.
- `docs/RELIABILITY.md`: `llp4_rel_tx_t` is 76 B on AVR (78 B on a 64-bit host,
  where `uint16_t` is aligned); `scripts/measure_v4.py` gates the AVR figure.
- `README.md`, `TESTING.md` and `STRUCTURE.md` rebuilt on v4 numbers: 50 test
  cases (17 + 6 + 27, the current count after the L-M4 anchors) and counts
  derivable from a command instead of the stale v3.1.0 "90 tests" table.

### Removed
- `scripts/gen_vectors_v4.py`: duplicate generator. The single generator is
  `llp-spec/tools/build_vectors.py`, invoked through `scripts/sync_corpus.sh`.

### Planned
- RS485 multi-node example
- LoRa integration example
- Advanced retransmission patterns
- Fragmentation support for large payloads
- Hardware CRC acceleration (STM32, etc.)

## [4.0.0] - 2026-11-03

**Wire format 4.0.0 — breaking change.** The frame keeps the magic (`AA 55`),
the byte stuffing and the CRC16-CCITT, but replaces the 2-byte length and the
layer chain with a fixed 6-byte header: `MAGIC + LEN(1) + CHANNEL(1)`.
v4 is **not** wire compatible with v3.1.0; the v3 implementation is preserved in
branch `feature/v3.1.0` (commit `422ff17`) and there is no dual-format mode.

### Breaking
- Frame header is now `MAGIC + LEN(1) + CHANNEL(1)`; the layer chain
  (FinalNode / passthrough / transform) is gone. Routing uses `CHANNEL`.
- The parser takes a caller-owned payload buffer:
  `llp4_parser_init(&p, buf, cap, timeout_ms)`. Capacity is per instance, and
  `LEN` above it is rejected before any byte is stored.
- The encoder streams through a write callback (`llp4_tx_begin/byte/end`); the
  old `llp_build_final_payload()` + `llp_build_frame()` pair is gone.
- Public header is `include/llp_v4.h`. `include/llp_protocol.h` (v3) and its
  test suite were removed from the tree; they live in branch `feature/v3.1.0`.

### Added
- **Wire format 4.0.0 specification** (`docs/SPEC-v4.md`): frame layout, parser
  state machine, error semantics, timeout rules and the normative invariants
  (no side effects before the CRC, O(1) work per byte, no dynamic memory).
- **Decision log** (`docs/DECISIONS.md`, 10 ADRs) and the comparison against
  plain text, SLIP, COBS, HDLC/PPP and Modbus RTU (`docs/WHY-LLP.md`).
- **Conformance vectors** (`spec/vectors/v4/`): 29 cases (encode, decode,
  errors, timing) with a schema (`docs/CONFORMANCE.md`), a self-checking
  generator (`scripts/gen_vectors_v4.py`) and a Python reference implementation
  (`scripts/llp_v4.py`).
- **C implementation** (`include/llp_v4.h`): parser with external payload buffer
  (17 bytes of state on AVR), streaming encoder (no frame buffer), CRC16-CCITT
  with a 16-nibble table (flash on AVR) or bitwise, optional statistics, four
  error codes and wrap-safe 16-bit timestamps.
- **Ring helper** (`include/llp_v4_ring.h`): single-producer/single-consumer byte
  ring with 8-bit indices, used by the examples.
- **Transport adapter examples** (`examples/v4/`, built in CI):
  `avr_baremetal/` (UART interrupt rings, complete app in ~2 KB of flash) and
  `host_posix/` (file descriptor / TCP, with a `--selftest` that injects noise).
  Integration guide: `docs/INTEGRATION.md`.
- **Robustness measurements** (`docs/ROBUSTNESS.md`, `tools/noise_report.c`):
  deterministic integer-only noise model (bit flips, drops, inserts, bursts)
  over 400 frames plus a 16 MB pure-noise false-accept test. Measured: **0
  corrupt frames delivered**, 97–100 % of frames intact at 1e-3..1e-4 error
  rates, ~50–70 bytes to resync, **0 false accepts in 16 MB**.
- **Property fuzzer** (`tools/fuzz_v4.c`): four input distributions, five
  invariants, standalone or as a libFuzzer target. The CI gate runs 100 000
  inputs per seed with the table CRC and 20 000 with the bitwise CRC; the
  default standalone run is 200 000 (`./fuzz_v4 [seed] [iterations]`).
- **Measured memory footprint** (`scripts/measure_v4.py`): ATmega328P `micro`
  profile = 17 B parser + 6 B tx + 622 B core flash; 564 B with the bitwise CRC.
  ARM Cortex-M4: 20 B / 12 B / 360 B. The README table is generated by the
  script and checked in CI.

### Changed
- CI now runs the vector self-check, the C fixture freshness check, the AVR
  build, the size budget/table gate, the examples and the robustness suite.
- Byte stuffing, magic, CRC parameters and the timeout semantics are unchanged
  from v3.1.0 (see `docs/SPEC-v4.md` §10).

### Removed
- `include/llp_protocol.h`, `src/llp_protocol.c` and the v3 test suite
  (`test_parser_init`, `test_frame_builder`, `test_parser_process`, `test_crc`,
  `test_spec_vectors`). The protocol documentation moved to `docs/PROTOCOL.md`
  (historical) and `docs/SPEC-v4.md` (current).

## [3.1.0] - 2026-06-10

### Added
- `llp_get_final_payload_ptr()` zero-copy API to read final payload without extra buffer
- `LLP_ENABLE_STATS` compile-time flag to disable parser statistics (12 bytes RAM saving)

### Changed
- `llp_parser_state_t` changed from `enum` to `uint8_t` (saves 1–3 bytes RAM)
- `llp_get_final_payload()` refactored to delegate to `llp_get_final_payload_ptr()`, eliminating duplicate layer-traversal logic
- `last_byte_time` changed from `unsigned long` to `uint16_t` (saves 2 bytes RAM)
- Fields reordered in `llp_parser_t` to minimize padding in 32-bit platforms (saves up to 4 bytes)
- Parser statistics now disabled by default (`LLP_ENABLE_STATS=0`)

### Removed
- `crc` field from `llp_frame_t` (was written but never read, saves 2 bytes RAM)

### Fixed
- Timeout detection now uses correct `uint16_t` modular arithmetic for `last_byte_time`

### Added
- Hardware test suite: `test/hardware_test/` with Arduino sketch and Python test runner (30 tests)

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

