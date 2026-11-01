#!/usr/bin/env bash
# Installs the toolchain needed to build the firmware: Arm GNU Toolchain,
# pico-sdk and picotool. Everything lands under $TOOLCHAIN_DIR (default
# ~/toolchain) so nothing is written to system locations.
#
# Usage: scripts/setup-toolchain.sh [--dir DIR]
set -euo pipefail

TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-$HOME/toolchain}"
ARM_RELEASE="13.2.Rel1"
SDK_TAG="2.1.1"

while [ $# -gt 0 ]; do
    case "$1" in
        --dir) TOOLCHAIN_DIR="$2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

case "$(uname -s)" in
    Darwin) OS=darwin ;;
    Linux)  OS=linux ;;
    *) echo "unsupported OS: $(uname -s)" >&2; exit 1 ;;
esac
case "$(uname -m)" in
    arm64|aarch64) ARCH=arm64 ;;
    x86_64)        ARCH=x64 ;;
    *) echo "unsupported arch: $(uname -m)" >&2; exit 1 ;;
esac

mkdir -p "$TOOLCHAIN_DIR"
cd "$TOOLCHAIN_DIR"

# --- Arm GNU Toolchain -------------------------------------------------------
ARM_DIR="$TOOLCHAIN_DIR/arm-gnu-toolchain-${ARM_RELEASE}-${OS}-${ARCH}-arm-none-eabi"
if [ ! -x "$ARM_DIR/bin/arm-none-eabi-gcc" ]; then
    echo "downloading Arm GNU Toolchain ${ARM_RELEASE} (${OS}-${ARCH})"
    case "$OS" in
        darwin) URL="https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_RELEASE}/binrel/arm-gnu-toolchain-${ARM_RELEASE}-${OS}-arm64-arm-none-eabi.tar.xz" ;;
        linux)  URL="https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_RELEASE}/binrel/arm-gnu-toolchain-${ARM_RELEASE}-${OS}-aarch64-arm-none-eabi.tar.xz" ;;
    esac
    curl -fL --retry 3 -o arm-gnu.tar.xz "$URL"
    tar xf arm-gnu.tar.xz
    rm -f arm-gnu.tar.xz
fi
[ -x "$ARM_DIR/bin/arm-none-eabi-gcc" ] || { echo "Arm toolchain missing at $ARM_DIR" >&2; exit 1; }
echo "arm-none-eabi-gcc: $("$ARM_DIR/bin/arm-none-eabi-gcc" --version | head -1)"

# --- pico-sdk ----------------------------------------------------------------
SDK_DIR="$TOOLCHAIN_DIR/pico-sdk"
if [ ! -f "$SDK_DIR/pico_sdk_init.cmake" ]; then
    echo "downloading pico-sdk ${SDK_TAG}"
    git clone --depth 1 --branch "$SDK_TAG" --recurse-submodules --shallow-submodules \
        https://github.com/raspberrypi/pico-sdk.git "$SDK_DIR"
fi
echo "pico-sdk: $SDK_DIR"

# --- cmake / picotool --------------------------------------------------------
if ! command -v cmake >/dev/null 2>&1; then
    echo "cmake not found. Install it (brew install cmake, or apt install cmake) and re-run." >&2
    exit 1
fi

PICO_TOOL="$TOOLCHAIN_DIR/picotool-build/picotool"
if [ ! -x "$PICO_TOOL" ]; then
    echo "building picotool (needs libusb: brew install libusb / apt install libusb-1.0-0-dev)"
    if [ ! -d "$SDK_DIR/tools/picotool" ]; then
        git clone --depth 1 https://github.com/raspberrypi/picotool.git "$TOOLCHAIN_DIR/picotool-src"
        PICOTOOL_SRC="$TOOLCHAIN_DIR/picotool-src"
    else
        PICOTOOL_SRC="$SDK_DIR/tools/picotool"
    fi
    cmake -B "$TOOLCHAIN_DIR/picotool-build" "$PICOTOOL_SRC" \
        -DPICO_SDK_PATH="$SDK_DIR" \
        -DLIBUSB_INCLUDE_DIR="${LIBUSB_INCLUDE_DIR:-/opt/homebrew/include/libusb-1.0}" \
        -DLIBUSB_LIBRARIES="${LIBUSB_LIBRARIES:-/opt/homebrew/lib/libusb-1.0.dylib}"
    cmake --build "$TOOLCHAIN_DIR/picotool-build" -j
fi
echo "picotool: $("$PICO_TOOL" version 2>/dev/null | head -1)"

cat <<EOF

Done. Export these before building:

  export PICO_SDK_PATH="$SDK_DIR"
  export PICO_TOOLCHAIN_PATH="$ARM_DIR"

Then: scripts/build.sh && scripts/flash.sh
EOF
