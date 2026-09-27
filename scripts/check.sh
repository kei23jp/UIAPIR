#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
ARDUINO_CLI=${ARDUINO_CLI:-arduino-cli}
FQBN=${UIAPIR_FQBN:-UIAP:ch32v:CH32V00x_EVT:pnum=CH32V003V1DOT4}
BUILD_ROOT=${BUILD_ROOT:-"$ROOT/build/arduino"}

if [ "${SKIP_HOST_TESTS:-0}" != "1" ]; then
    sh "$ROOT/tests/run.sh"
fi

if ! command -v "$ARDUINO_CLI" >/dev/null 2>&1; then
    echo "Arduino CLI was not found: $ARDUINO_CLI" >&2
    exit 1
fi

compile_group() {
    group=$1
    sketch_root=$2

    for sketch in "$sketch_root"/*; do
        [ -d "$sketch" ] || continue
        name=$(basename "$sketch")
        # HT6 is a PlatformIO project, not a sketch: `pio run` builds it.
        [ -f "$sketch/$name.ino" ] || continue
        echo "Compiling $group/$name"
        "$ARDUINO_CLI" compile \
            --fqbn "$FQBN" \
            --library "$ROOT" \
            --build-path "$BUILD_ROOT/$group-$name" \
            "$sketch"
    done
}

# One sketch built with extra compiler definitions, for the build switches.
compile_with_flags() {
    label=$1
    sketch=$2
    flags=$3
    echo "Compiling $label ($flags)"
    "$ARDUINO_CLI" compile \
        --fqbn "$FQBN" \
        --library "$ROOT" \
        --build-path "$BUILD_ROOT/$label" \
        --build-property "compiler.cpp.extra_flags=$flags" \
        "$sketch"
}

mkdir -p "$BUILD_ROOT"
compile_group example "$ROOT/examples"
compile_group hardware-test "$ROOT/extras/hardware-tests"

# Direction switches. A transmit-only build swaps TIM2 for SysTick in the
# envelope timing and a receive-only one drops TIM1, so both compile paths
# through UIAPIR.cpp have to build, not just the header.
compile_with_flags tx-only-SendProtocols "$ROOT/examples/SendProtocols" "-DUIAPIR_ENABLE_RX=0"
compile_with_flags tx-only-HT4_TransmitTiming "$ROOT/extras/hardware-tests/HT4_TransmitTiming" "-DUIAPIR_ENABLE_RX=0"
compile_with_flags rx-only-Receive "$ROOT/examples/Receive" "-DUIAPIR_ENABLE_TX=0"
compile_with_flags rx-only-HT5_ReceiveDump "$ROOT/extras/hardware-tests/HT5_ReceiveDump" "-DUIAPIR_ENABLE_TX=0"

# HT6 is the two-board test and needs PlatformIO rather than arduino-cli.
# Skipped when pio is not installed, because most contributors will not have
# it and the rest of the suite is complete without it.
PIO=${PIO:-pio}
if command -v "$PIO" >/dev/null 2>&1; then
    echo "Compiling hardware-test/HT6_TwoBoard (PlatformIO)"
    (cd "$ROOT/extras/hardware-tests/HT6_TwoBoard" && "$PIO" run)
else
    echo "Skipping hardware-test/HT6_TwoBoard: PlatformIO was not found: $PIO"
fi

echo "All UIAPIR checks passed"
