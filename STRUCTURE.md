# Project Structure — LLP wire 4.0.0

Header-only C99 implementation of the LLP 4.0.0 wire format, the optional Ring
helper and the optional Profile R1 (reliable command channel), plus the vendored
conformance corpus and the checks that gate them.

## Directory tree

```
llp-protocol/
├── platformio.ini                  # native test env + cross-compile envs
├── library.properties              # Arduino Library Manager metadata
│
├── include/
│   ├── llp_v4.h                    # wire 4.0.0: parser + streaming encoder + CRC
│   ├── llp_v4_ring.h               # optional SPSC byte ring (ISR <-> main loop)
│   └── llp_v4_rel.h                # optional Profile R1: sender + receiver
│
├── src/
│   ├── llp_v4_check.c              # compile smoke test + static asserts (all envs)
│   └── main.cpp                    # PlatformIO entry for the cross envs
│
├── test/
│   ├── test_main.c                 # Unity runner: registers the 47 cases
│   ├── test_v4.c                   # core behaviour + the two corpus tables
│   ├── test_v4_ring.c              # ring helper
│   ├── test_v4_rel.c               # Profile R1 state machines + traces T1–T9
│   └── v4_vectors_generated.h      # GENERATED fixture (do not edit)
│
├── spec/vectors/v4/                # vendored llp-spec corpus (170 cases)
│   ├── encode.json  decode.json  errors.json  timing.json
│   ├── channels.json  payload.json  streams.json  reliability.json
│   ├── MANIFEST.sha256             # upstream hashes of the JSON files
│   └── FIXTURE.sha256              # hash of test/v4_vectors_generated.h
│
├── scripts/
│   ├── sync_corpus.sh              # vendor the corpus + rebuild the fixture
│   ├── check_corpus.py             # manifest, fixture pin, byte-for-byte regen
│   ├── check_robustness.sh         # fuzzer + noise channel
│   ├── check_examples.sh           # AVR cross-build + host self-test
│   ├── measure_v4.py               # RAM/flash table, budgets, README check
│   ├── platformio_coverage.py      # coverage wiring for `pio test -e test`
│   ├── llp_v4.py                   # vendored Python reference (from llp-spec)
│   └── llp_v4_rel.py               # Python counterpart of Profile R1
│
├── tools/
│   ├── fuzz_v4.c                   # property fuzzer (ASan + UBSan)
│   └── noise_report.c              # noise matrix + false-accept measurement
│
├── examples/
│   ├── v4/avr_baremetal/main.c     # ATmega328P, UART interrupt rings
│   ├── v4/host_posix/host_posix.c  # POSIX fd / TCP adapter + --selftest
│   └── {minimal_uart,request_response,serial_echo}/   # legacy v3 sketches
│
├── docs/
│   ├── SPEC-v4.md                  # pointer to the normative text in llp-spec
│   ├── CONFORMANCE.md              # corpus, pins, counts, Profile R1 gap (L-I1b)
│   ├── INTEGRATION.md              # wiring, sizing, RS-485, threat model
│   ├── RELIABILITY.md              # Profile R1 state machines (C side)
│   ├── ROBUSTNESS.md               # measured noise behaviour
│   ├── DECISIONS.md                # ADR log
│   ├── WHY-LLP.md                  # comparison with COBS/SLIP/HDLC/Modbus
│   └── PROTOCOL.md                 # v3.1.0 text, historical only
│
├── .github/workflows/
│   ├── platformio.yml              # corpus pins, tests, budget, robustness
│   └── arduino-publish.yml         # Library Manager release
│
├── AGENTS.md  README.md  TESTING.md  STRUCTURE.md  CHANGELOG.md  BUGS.md
└── LICENSE  .gitignore
```

## File purposes

| Path | Purpose |
|---|---|
| `include/llp_v4.h` | Parser (`llp4_parser_*`), streaming encoder (`llp4_tx_*`, `llp4_encode`), CRC16-CCITT, frame sizing |
| `include/llp_v4_ring.h` | 8-bit-index SPSC byte ring for the ISR↔loop handoff |
| `include/llp_v4_rel.h` | Profile R1 sender (stop-and-wait, retransmission, exhaustion) and receiver (dedup, HELLO) |
| `src/llp_v4_check.c` | Compiles the header on every target and holds the static asserts for parser/`rel_tx`/`rel_rx` size |
| `test/test_v4.c` | Core behaviour plus the generated fixture: every corpus row is an assertion |
| `test/v4_vectors_generated.h` | Emitted by `llp-spec/tools/build_vectors.py --emit-c`; pinned by `FIXTURE.sha256` |
| `spec/vectors/v4/` | Byte copy of `llp-spec/vectors/`; the source of truth is the spec repo |
| `scripts/sync_corpus.sh` | Single way to refresh the corpus and the fixture |
| `scripts/check_corpus.py` | Offline pins + byte-for-byte regeneration when `../llp-spec` exists |
| `scripts/measure_v4.py` | RAM/flash per target, hard budgets, README table sync |
| `tools/fuzz_v4.c` | Property fuzzer, four distributions, five invariants |
| `tools/noise_report.c` | Noise scenarios and the false-accept run behind `docs/ROBUSTNESS.md` |
| `examples/v4/` | The two reference integrations built by `scripts/check_examples.sh` |
| `examples/{minimal_uart,request_response,serial_echo}/` | v3.1.0 sketches kept for history; they include `llp_protocol.h` and are **not** built by the v4 checks |
| `docs/PROTOCOL.md` | v3.1.0 specification, historical; the v4 norm lives in `llp-spec` |

## Test summary

| Suite | Cases | Focus |
|---|---:|---|
| `test/test_v4.c` | 15 | Core behaviour + 60 encode and 101 decode corpus rows |
| `test/test_v4_ring.c` | 6 | Ring helper |
| `test/test_v4_rel.c` | 26 | Profile R1 state machines + traces T1–T9 |
| **Total** | **47** | `grep -c 'RUN_TEST(' test/test_main.c` |

Counts and how to derive them: [`TESTING.md`](TESTING.md) and
[`docs/CONFORMANCE.md`](docs/CONFORMANCE.md).

## Where things run

| Check | Command | Environment |
|---|---|---|
| Unit + corpus | `pio test -e test` | host (native, Unity) |
| Corpus pins | `python3 scripts/check_corpus.py` | host, offline-safe |
| Robustness | `sh scripts/check_robustness.sh` | host, ASan + UBSan |
| Examples | `sh scripts/check_examples.sh` | `avr-gcc` + host |
| Budget | `python3 scripts/measure_v4.py --budget` | AVR + host toolchains |
| Cross-compile | `pio run -e arduino_uno` (and the other envs) | AVR/ESP/STM32 toolchains |
