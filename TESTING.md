# Testing Guide — LLP wire 4.0.0

Everything runs on the host (PlatformIO `native` environment) plus two support
checks: the AVR example is cross-compiled with `avr-gcc` and the memory budget is
measured with the same toolchain. No hardware is needed.

## Prerequisites

```bash
pip install platformio
# optional, for the AVR example and the memory budget:
#   avr-gcc/avr-size come from the PlatformIO AVR toolchain (installed on first
#   `pio run -e arduino_uno`) or from your distro (gcc-avr / avr-libc).
# optional, for coverage: lcov
```

## Gate

One command runs everything CI runs:

```bash
pio test -e test && \
python3 scripts/check_corpus.py && \
sh scripts/check_robustness.sh && \
sh scripts/check_examples.sh && \
python3 scripts/measure_v4.py --budget
```

## Unit and conformance suite

```bash
pio test -e test          # 42 test cases
pio test -e test -v       # with the Unity output of every case
pio test -e test --filter test_v4_rel   # one suite
```

Current result: **42 test cases, 42 succeeded**. The count is derived, not
remembered:

```bash
grep -c 'RUN_TEST(' test/test_main.c                              # 42 registered
grep -c '^void test_' test/test_v4.c test/test_v4_ring.c test/test_v4_rel.c   # 13, 5, 24
```

| Suite | Tests | Focus |
|---|---:|---|
| `test/test_v4.c` | 13 | Core behaviour + both corpus tables (`llp4_vec_encode[]`, `llp4_vec_decode[]`) |
| `test/test_v4_ring.c` | 5 | SPSC ring helper |
| `test/test_v4_rel.c` | 24 | Profile R1 state machines + conformance traces T1–T9 |

`test_v4.c` walks the generated fixture row by row: each encode row checks the
streaming writer and the buffer wrapper against the expected frame, each decode
row feeds its per-byte steps (with `dt_ms`) and asserts the delivered frames, the
error codes in order, and — with `LLP4_ENABLE_STATS=1`, which `platformio.ini`
sets — the timeout and error counters. Two pins at the top of those tests
(`60` encode, `101` decode) make a truncated fixture fail loudly instead of
passing vacuously.

What the fixture covers, why 161 of 170 cases, and the nine Profile R1 cases that
`--emit-c` does not emit yet: [`docs/CONFORMANCE.md`](docs/CONFORMANCE.md).

## Corpus and fixture pins

```bash
python3 scripts/check_corpus.py      # manifest + fixture hash (+ regeneration if ../llp-spec exists)
sh scripts/sync_corpus.sh            # re-vendor from ../llp-spec and rebuild the fixture
```

`check_corpus.py` is offline-safe: without a `llp-spec` checkout it verifies the
vendored manifest and `FIXTURE.sha256`, and prints a note instead of failing. With
`../llp-spec` present it also proves that `build_vectors.py --emit-c` reproduces
the committed header byte for byte.

## Robustness

```bash
sh scripts/check_robustness.sh
```

Two gates, both built with `-fsanitize=address,undefined`:

- **Property fuzzer** (`tools/fuzz_v4.c`): four input distributions, five
  invariants, 100 000 inputs per seed.
- **Noise channel** (`tools/noise_report.c`): bit flips, drops, inserts and bursts
  over 400 frames per scenario, plus a false-accept run over pure random bytes.
  The table in `docs/ROBUSTNESS.md` must match the measurement, so a regression in
  either direction fails the script.

## Examples

```bash
sh scripts/check_examples.sh
```

Builds `examples/v4/avr_baremetal` with `avr-gcc` (`avr-size` prints the footprint)
and compiles/runs `examples/v4/host_posix --selftest` (roundtrips + noise).

## Memory footprint

```bash
python3 scripts/measure_v4.py            # print the table
python3 scripts/measure_v4.py --budget   # CI gate: AVR parser/tx/rel_*/flash
python3 scripts/measure_v4.py --check-readme --toolchains avr,host
```

Budgets (ATmega328P, `micro` profile, stats off): parser ≤ 20 B, tx ≤ 8 B,
`rel_tx` ≤ 76 B, `rel_rx` ≤ 6 B, core flash ≤ 800 B. `src/llp_v4_check.c` carries
the compile-time twin of the RAM budgets, so the AVR cross-build fails as soon as
the AVR struct grows past one of them.

## Cross-compilation smoke

```bash
pio run -e arduino_uno     # compiles src/llp_v4_check.c for every environment
pio run -e esp8266
```

`src/llp_v4_check.c` is a compile smoke test: it instantiates the state and calls
the API, it is not an application.

## Coverage

`platformio.ini` passes `-coverage` and loads `scripts/platformio_coverage.py`, so
`pio test -e test` leaves `.gcda` files in `.pio/build/test`.

```bash
lcov --capture --directory .pio/build/test --output-file coverage/lcov.info \
     --ignore-errors inconsistent
lcov --remove coverage/lcov.info '*/libdeps/*' '*/Unity/*' \
     --output-file coverage/lcov.info --ignore-errors inconsistent,unused
genhtml coverage/lcov.info --output-directory coverage/html
```

`coverage/` is gitignored; CI uploads `coverage/lcov.info` to Codecov.

## Debugging

```bash
pio test -e test -vvv               # compiler and link lines
pio test -e test --filter test_v4_rel   # one suite by source file name
```

A failing vector names its `id` in the assertion message (`TEST_ASSERT_*_MESSAGE`
uses `c->id`), so the message points at the JSON case in `spec/vectors/v4/`.
Do not edit the fixture or the JSON by hand: see `docs/CONFORMANCE.md`,
"When a case fails".
