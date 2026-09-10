#!/usr/bin/env sh
# Host tests. Nothing here touches Arduino or the CH32 SPL: the protocol
# decoders and the capture state machine are deliberately free of hardware
# dependencies so they can be exercised on a desktop compiler.
set -eu

CXX=${CXX:-c++}
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
OUT=${OUT:-"$ROOT/.test-build/host"}
FLAGS="-std=c++14 -Wall -Wextra -Werror -Isrc"

mkdir -p "$OUT"
cd "$ROOT"

"$CXX" $FLAGS tests/test_protocols.cpp src/UIAPIRProtocol.cpp -o "$OUT/protocol_tests"
"$OUT/protocol_tests"
echo "UIAPIR protocol tests passed"

"$CXX" $FLAGS tests/test_capture.cpp src/UIAPIRCapture.cpp src/UIAPIRProtocol.cpp \
    -o "$OUT/capture_tests"
"$OUT/capture_tests"

# Header/API compile check. Arduino.h is a minimal test stub; hardware calls
# remain in UIAPIR.cpp and are covered by extras/hardware-tests.
"$CXX" $FLAGS -Itests -c tests/test_api.cpp -o "$OUT/api_tests.o"
echo "UIAPIR API compile check passed"

"$CXX" $FLAGS -Itests tests/test_link.cpp src/UIAPIRProtocol.cpp -o "$OUT/link_tests"
"$OUT/link_tests"

# Protocol selection. Every UIAPIR_ENABLE_* combination has to compile clean,
# which is more than a formality: -Werror turns a helper left behind by a
# switched-off decoder into a build failure, and that is exactly the mistake
# these guards invite. RAW-only (all three off) is a legitimate build.
NEC_ONLY="-DUIAPIR_ENABLE_AEHA=0 -DUIAPIR_ENABLE_SONY=0"
AEHA_ONLY="-DUIAPIR_ENABLE_NEC=0 -DUIAPIR_ENABLE_SONY=0"
SONY_ONLY="-DUIAPIR_ENABLE_NEC=0 -DUIAPIR_ENABLE_AEHA=0"
RAW_ONLY="-DUIAPIR_ENABLE_NEC=0 -DUIAPIR_ENABLE_AEHA=0 -DUIAPIR_ENABLE_SONY=0"

for sel in "$NEC_ONLY" "$AEHA_ONLY" "$SONY_ONLY" "$RAW_ONLY" \
    "-DUIAPIR_ENABLE_NEC=0" "-DUIAPIR_ENABLE_AEHA=0" "-DUIAPIR_ENABLE_SONY=0"
do
    # shellcheck disable=SC2086
    "$CXX" $FLAGS $sel -c src/UIAPIRProtocol.cpp -o "$OUT/protocol_sel.o"
    # shellcheck disable=SC2086
    "$CXX" $FLAGS $sel -Itests -c tests/test_api.cpp -o "$OUT/api_sel.o"
    "$CXX" $FLAGS $sel -Itests tests/test_link.cpp src/UIAPIRProtocol.cpp -o "$OUT/link_sel"
    "$OUT/link_sel"
done
echo "UIAPIR protocol selection compile checks passed"

# Capacity must follow configured AEHA limits, and control timing must survive
# the supported fine-grained RAW quantisation as well as the default tick.
for config in \
    "-DUIAPIR_ENABLE_NEC=0 -DUIAPIR_ENABLE_SONY=0 -DUIAPIR_MAX_AEHA_BYTES=3 -DUIAPIR_RAW_BUFFER_SIZE=67" \
    "-DUIAPIR_ENABLE_NEC=0 -DUIAPIR_ENABLE_SONY=0 -DUIAPIR_MAX_AEHA_BYTES=32 -DUIAPIR_RAW_BUFFER_SIZE=515" \
    "-DUIAPIR_RAW_TICK_US=40"
do
    "$CXX" $FLAGS $config -Itests tests/test_link.cpp src/UIAPIRProtocol.cpp -o "$OUT/link_config"
    "$OUT/link_config"
done
