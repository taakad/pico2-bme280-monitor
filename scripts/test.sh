#!/usr/bin/env bash
# Runs the host unit tests: the compensation math and the driver's register
# handling are exercised against a simulated BME280, no hardware needed.
set -euo pipefail

# macOS: Xcode のライセンス未同意だとシステムツール (git/cmake/c++) が動かない
if [ -d /Library/Developer/CommandLineTools ] && [ -z "${DEVELOPER_DIR:-}" ]; then
    export DEVELOPER_DIR=/Library/Developer/CommandLineTools
fi


ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIRMWARE="$ROOT/firmware"

CXX="${CXX:-c++}"
# macOS ships a `c++` shim inside the Xcode toolchain; fall back to the Command
# Line Tools copy when the Xcode license has not been accepted.
if ! "$CXX" --version >/dev/null 2>&1; then
    DEVELOPER_DIR=/Library/Developer/CommandLineTools "$CXX" --version >/dev/null 2>&1 &&
        CXX="env DEVELOPER_DIR=/Library/Developer/CommandLineTools $CXX"
fi

# shellcheck disable=SC2086
$CXX -std=c++17 -Wall -Wextra -Werror \
    -I"$FIRMWARE/src" -I"$FIRMWARE/test/host_stubs" \
    "$FIRMWARE/test/test_bme280.cpp" \
    "$FIRMWARE/test/host_stubs/i2c_stub.cpp" \
    "$FIRMWARE/src/bme280.cpp" \
    "$FIRMWARE/src/bme280_math.cpp" \
    "$FIRMWARE/src/i2c_scan.cpp" \
    "$FIRMWARE/src/config.cpp" \
    "$FIRMWARE/src/command.cpp" \
    "$FIRMWARE/src/sampler.cpp" \
    -o /tmp/bme280_test

/tmp/bme280_test
