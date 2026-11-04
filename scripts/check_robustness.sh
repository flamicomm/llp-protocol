#!/bin/sh
# Robustness checks for the v4 core:
#   1. property fuzzer (with ASan/UBSan when available), deterministic seeds;
#   2. noise-channel report and its consistency with docs/ROBUSTNESS.md.
#
# Used by CI; also:
#   sh scripts/check_robustness.sh

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

OUT_DIR=${TMPDIR:-/tmp}/llp_robustness
mkdir -p "$OUT_DIR"

FUZZ_ITERS=${FUZZ_ITERS:-100000}
FUZZ_SEED=${FUZZ_SEED:-1}

echo "== property fuzzer ($FUZZ_ITERS inputs, seed $FUZZ_SEED) =="
if cc -fsanitize=address,undefined -fno-sanitize-recover=all -O1 -g -std=c99 \
      -Wall -Wextra -I include tools/fuzz_v4.c -o "$OUT_DIR/fuzz_v4" 2>/dev/null; then
    echo "(AddressSanitizer + UBSan enabled)"
else
    echo "(sanitizers unavailable: plain build)"
    cc -O2 -std=c99 -Wall -Wextra -I include tools/fuzz_v4.c -o "$OUT_DIR/fuzz_v4"
fi
"$OUT_DIR/fuzz_v4" "$FUZZ_SEED" "$FUZZ_ITERS"

echo "== noise channel =="
cc -O2 -std=c99 -Wall -Wextra -I include tools/noise_report.c -o "$OUT_DIR/noise_report"
"$OUT_DIR/noise_report"
"$OUT_DIR/noise_report" --check-doc

if command -v clang >/dev/null 2>&1; then
    echo "== libFuzzer (clang) =="
    clang -DLLP4_FUZZ_MAIN -fsanitize=fuzzer,address -O1 -g -I include \
        tools/fuzz_v4.c -o "$OUT_DIR/fuzz_libfuzzer"
    "$OUT_DIR/fuzz_libfuzzer" -runs=20000 -max_len=512 -print_final_stats=1 2>&1 |
        tail -3
fi

echo "robustness OK"
