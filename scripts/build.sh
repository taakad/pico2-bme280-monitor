#!/usr/bin/env bash
# Configures and builds the firmware. Requires the toolchain from
# scripts/setup-toolchain.sh.
set -euo pipefail

# macOS: Xcode のライセンス未同意だとシステムツール (git/cmake/c++) が動かない
if [ -d /Library/Developer/CommandLineTools ] && [ -z "${DEVELOPER_DIR:-}" ]; then
    export DEVELOPER_DIR=/Library/Developer/CommandLineTools
fi


ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIRMWARE="$ROOT/firmware"
BUILD="$FIRMWARE/build"
BOARD="${PICO_BOARD:-pico2}"

TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-$HOME/toolchain}"
: "${PICO_SDK_PATH:=$TOOLCHAIN_DIR/pico-sdk}"
if [ -z "${PICO_TOOLCHAIN_PATH:-}" ]; then
    # setup-toolchain.sh の出力名は OS/arch 依存なので、インストール済みディレクトリを探す
    PICO_TOOLCHAIN_PATH="$(find "$TOOLCHAIN_DIR" -maxdepth 1 -type d \
        -name 'arm-gnu-toolchain-*-arm-none-eabi' 2>/dev/null | sort | tail -1)"
    PICO_TOOLCHAIN_PATH="${PICO_TOOLCHAIN_PATH:-$TOOLCHAIN_DIR/arm-gnu-toolchain-13.2.Rel1-darwin-arm64-arm-none-eabi}"
fi
export PICO_SDK_PATH PICO_TOOLCHAIN_PATH

if [ ! -f "$PICO_SDK_PATH/pico_sdk_init.cmake" ]; then
    echo "pico-sdk not found at $PICO_SDK_PATH - run scripts/setup-toolchain.sh first" >&2
    exit 1
fi
if [ ! -x "$PICO_TOOLCHAIN_PATH/bin/arm-none-eabi-gcc" ]; then
    echo "Arm toolchain not found at $PICO_TOOLCHAIN_PATH - run scripts/setup-toolchain.sh first" >&2
    exit 1
fi

CMAKE="$(command -v cmake || command -v /opt/homebrew/bin/cmake || true)"
[ -n "$CMAKE" ] || { echo "cmake not found - install it (brew install cmake / apt install cmake)" >&2; exit 1; }

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
