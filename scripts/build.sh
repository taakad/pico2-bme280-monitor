#!/usr/bin/env bash
# Configures and builds the firmware. Requires the toolchain from
# scripts/setup-toolchain.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIRMWARE="$ROOT/firmware"
BUILD="$FIRMWARE/build"
BOARD="${PICO_BOARD:-pico2}"

: "${PICO_SDK_PATH:=$HOME/toolchain/pico-sdk}"
: "${PICO_TOOLCHAIN_PATH:=$HOME/toolchain/arm-gnu-toolchain-13.2.Rel1-darwin-arm64-arm-none-eabi}"
export PICO_SDK_PATH PICO_TOOLCHAIN_PATH

if [ ! -f "$PICO_SDK_PATH/pico_sdk_init.cmake" ]; then
    echo "pico-sdk not found at $PICO_SDK_PATH - run scripts/setup-toolchain.sh first" >&2
    exit 1
fi
if [ ! -x "$PICO_TOOLCHAIN_PATH/bin/arm-none-eabi-gcc" ]; then
    echo "Arm toolchain not found at $PICO_TOOLCHAIN_PATH - run scripts/setup-toolchain.sh first" >&2
    exit 1
fi

CMAKE="$(command -v cmake || command -v /opt/homebrew/bin/cmake)"

# A cache left over from a moved or renamed checkout cannot be reused.
if [ -f "$BUILD/CMakeCache.txt" ] &&
   ! grep -q "CMAKE_HOME_DIRECTORY:INTERNAL=$FIRMWARE" "$BUILD/CMakeCache.txt"; then
    echo "discarding stale cmake cache in $BUILD"
    rm -rf "$BUILD"
fi

"$CMAKE" -B "$BUILD" -S "$FIRMWARE" -DPICO_BOARD="$BOARD"
"$CMAKE" --build "$BUILD" -j

echo
echo "built: $BUILD/bme280_monitor.uf2"
ls -l "$BUILD/bme280_monitor.uf2"
