# Testing Guide — LLP Protocol 3.1.0

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
90 Tests 0 Failures 0 Ignored OK
```

## Test Suites

### Original Tests (35 tests)

| Test File | Count | Focus |
|-----------|-------|-------|
| `test_parser_init.c` | 5 | Parser startup, state reset, stats |
| `test_frame_builder.c` | 11 | Frame construction, stuffing, roundtrip |
| `test_parser_process.c` | 11 | Byte parsing, errors, recovery, timeouts |
| `test_crc.c` | 8 | CRC16-CCITT, error detection, determinism |

### Spec Conformance Tests (55 test functions, ~199 vectors)

Based on the [LLP Specification v3.1.0](https://github.com/flamicomm/llp-spec) test vectors.

| Test File | Category | Count | Vectors |
|-----------|----------|-------|---------|
| `test_spec_vectors.c` | Transport Valid Encode | 1 | 31 |
| `test_spec_vectors.c` | Transport Valid Decode | 1 | 31 |
| `test_spec_vectors.c` | Transport Valid Stream | 3 | 3 |
| `test_spec_vectors.c` | Transport CRC | 1 | 28 |
| `test_spec_vectors.c` | Transport Stuffing | 2 | 8 |
| `test_spec_vectors.c` | Transport Resync | 8 | 8 |
| `test_spec_vectors.c` | Transport Truncation | 8 | 7+2 |
| `test_spec_vectors.c` | Transport Timeout | 3 | 3 |
| `test_spec_vectors.c` | Layers Passthrough | 4 | 15+15+2 |
| `test_spec_vectors.c` | Layers Transform | 2 | 6+7 |
| `test_spec_vectors.c` | Layers Malformed | 4 | 4 |
| `test_spec_vectors.c` | Layers Traversal | 5 | 5 |
| `test_spec_vectors.c` | Parser Incremental | 3 | 3 |
| `test_spec_vectors.c` | Parser Fragmented | 3 | 3 |
| `test_spec_vectors.c` | Parser Recovery | 4 | 4 |

### Cross-Language Test Generator

The `tools/cross_test_generate.c` standalone program generates binary frames
from known test vectors and verifies interoperability with the Java implementation.

Compile and run:
```bash
gcc -std=c99 -I include -o /tmp/llp_cross_gen tools/cross_test_generate.c
/tmp/llp_cross_gen [output_dir]
```

## Hardware Tests (30 test cases)

Run the automated test suite against an Arduino Nano connected via USB:

```bash
# Build, upload, and test with defaults
python3 test/hardware_test/test_hardware.py

# Specify a different port
python3 test/hardware_test/test_hardware.py --port /dev/ttyUSB1

# Skip build/upload if firmware is already loaded
python3 test/hardware_test/test_hardware.py --no-upload
```

The hardware test suite validates the library on real hardware:
- **Valid frame parsing**: empty, single bytes, multi-byte, stuffed bytes
- **Error detection**: CRC mismatch, invalid escapes, unstuffed magic bytes
- **Noise recovery**: frames arriving after garbage bytes
- **Sequential frames**: multiple frames without reset
- **Zero-copy API**: `llp_get_final_payload_ptr()` correctness
- **Long payloads**: up to 32 bytes of application data

Expected output:
```
Results: 30/30 passed — ALL TESTS PASSED
```

## Coverage

The CI pipeline generates code coverage reports using GCOV/LCOV and uploads them to Codecov.
Coverage data is collected automatically on every push and pull request.

### Local Coverage (optional)

Requires `lcov`:
```bash
# Ubuntu/Debian
sudo apt-get install lcov

# Run tests (coverage flags are already in platformio.ini)
platformio test -e test

# Generate HTML report
mkdir -p coverage
lcov --capture --directory .pio/build/test --output-file coverage/lcov.info
lcov --remove coverage/lcov.info '/usr/*' '*/libdeps/*' '*/Unity/*' --output-file coverage/lcov.info
genhtml coverage/lcov.info --output-directory coverage/html
```

## Known Issues

See [BUGS.md](BUGS.md) for known issues discovered through spec conformance testing.

## Debugging

```bash
platformio test -e test -vvv              # Extra verbose with compiler output
platformio test -e test --filter test_crc  # Run specific test file
pio test --verbose -e test                 # Alternative syntax
```