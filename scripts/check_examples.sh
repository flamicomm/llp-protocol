#!/bin/sh
# Build (and where possible run) the v4 reference examples.
#
# Used by CI; also useful locally:
#   sh scripts/check_examples.sh
#
# The AVR example is compiled with avr-gcc from PlatformIO if it is installed,
# otherwise from PATH. The host example is built and its self-test is run.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

OUT_DIR=${TMPDIR:-/tmp}/llp_examples
mkdir -p "$OUT_DIR"

find_avr_gcc() {
    if command -v avr-gcc >/dev/null 2>&1; then
        command -v avr-gcc
        return 0
    fi
    for candidate in "$HOME"/.platformio/packages/toolchain-atmelavr/bin/avr-gcc; do
        if [ -x "$candidate" ]; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

echo "== host_posix =="
cc -Os -std=c99 -Wall -Wextra -I include \
    examples/v4/host_posix/host_posix.c -o "$OUT_DIR/host_posix"
"$OUT_DIR/host_posix" --selftest

echo "== avr_baremetal =="
if AVR_GCC=$(find_avr_gcc); then
    "$AVR_GCC" -mmcu=atmega328p -Os -std=c99 -Wall -Wextra -I include \
        examples/v4/avr_baremetal/main.c -o "$OUT_DIR/avr_baremetal.elf"
    AVR_SIZE=$(dirname "$AVR_GCC")/avr-size
    if [ -x "$AVR_SIZE" ]; then
        "$AVR_SIZE" --format=avr --mcu=atmega328p "$OUT_DIR/avr_baremetal.elf" |
            sed -n '3,6p'
    fi
else
    echo "avr-gcc not found: skipping the AVR example"
fi

echo "examples OK"
