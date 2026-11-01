#!/usr/bin/env bash
# Installs the toolchain needed to build the firmware: Arm GNU Toolchain,
# pico-sdk and picotool. Everything lands under $TOOLCHAIN_DIR (default
# ~/toolchain) so nothing is written to system locations.
#
# Usage: scripts/setup-toolchain.sh [--dir DIR]
set -euo pipefail

# macOS: Xcode のライセンス未同意だとシステムツール (git/cmake/c++) が動かない
if [ -d /Library/Developer/CommandLineTools ] && [ -z "${DEVELOPER_DIR:-}" ]; then
    export DEVELOPER_DIR=/Library/Developer/CommandLineTools
fi


TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-$HOME/toolchain}"
ARM_RELEASE="13.2.Rel1"
SDK_TAG="2.1.1"

# --skip-picotool: ビルドに不要な picotool / gh の取得を省略する (CI 向け)
SKIP_PICOTOOL=0
while [ $# -gt 0 ]; do
    case "$1" in
        --dir) TOOLCHAIN_DIR="$2"; shift 2 ;;
        --skip-picotool) SKIP_PICOTOOL=1; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

case "$(uname -s)" in
    Darwin) OS=darwin ;;
    Linux)  OS=linux ;;
    *) echo "unsupported OS: $(uname -s)" >&2; exit 1 ;;
esac
CPU="$(uname -m)"          # Arm/gh の配布名にそのまま使う表記
case "$CPU" in
    arm64|aarch64) ARCH=arm64 ;;
    x86_64)        ARCH=x64 ;;
    *) echo "unsupported arch: $CPU" >&2; exit 1 ;;
esac

mkdir -p "$TOOLCHAIN_DIR"
cd "$TOOLCHAIN_DIR"

# --- Arm GNU Toolchain -------------------------------------------------------
# 展開後のディレクトリ名は配布ファイル名と同じ (macOS: darwin-arm64 / Linux: x86_64 など)
case "$OS" in
    darwin) ARM_NAME="arm-gnu-toolchain-${ARM_RELEASE}-darwin-arm64-arm-none-eabi" ;;
    linux)  ARM_NAME="arm-gnu-toolchain-${ARM_RELEASE}-${CPU}-arm-none-eabi" ;;
esac
ARM_DIR="$TOOLCHAIN_DIR/$ARM_NAME"
if [ ! -x "$ARM_DIR/bin/arm-none-eabi-gcc" ]; then
    echo "downloading Arm GNU Toolchain ${ARM_RELEASE} (${OS}-${ARCH})"
    case "$OS" in
        # Arm の配布ファイル名は OS/arch 表記が独自: macOS は "darwin-arm64"、
        # Linux は uname -m と同じ "x86_64" / "aarch64" (x64 とは書かない)。
        darwin) URL="https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_RELEASE}/binrel/arm-gnu-toolchain-${ARM_RELEASE}-darwin-arm64-arm-none-eabi.tar.xz" ;;
        linux)  URL="https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_RELEASE}/binrel/arm-gnu-toolchain-${ARM_RELEASE}-${CPU}-arm-none-eabi.tar.xz" ;;
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
if [ "$SKIP_PICOTOOL" = 1 ]; then
    echo "skipping picotool (--skip-picotool)"
elif [ ! -x "$PICO_TOOL" ]; then
    echo "building picotool (needs libusb: brew install libusb / apt install libusb-1.0-0-dev)"
    if [ ! -d "$SDK_DIR/tools/picotool" ]; then
        git clone --depth 1 https://github.com/raspberrypi/picotool.git "$TOOLCHAIN_DIR/picotool-src"
        PICOTOOL_SRC="$TOOLCHAIN_DIR/picotool-src"
    else
        PICOTOOL_SRC="$SDK_DIR/tools/picotool"
    fi
    case "$OS" in
        darwin) LIBUSB_INC="${LIBUSB_INCLUDE_DIR:-/opt/homebrew/include/libusb-1.0}"
                LIBUSB_LIB="${LIBUSB_LIBRARIES:-/opt/homebrew/lib/libusb-1.0.dylib}" ;;
        linux)  LIBUSB_INC="${LIBUSB_INCLUDE_DIR:-/usr/include/libusb-1.0}"
                LIBUSB_LIB="${LIBUSB_LIBRARIES:-/usr/lib/$(uname -m)-linux-gnu/libusb-1.0.so}" ;;
    esac
    cmake -B "$TOOLCHAIN_DIR/picotool-build" "$PICOTOOL_SRC" \
        -DPICO_SDK_PATH="$SDK_DIR" \
        -DLIBUSB_INCLUDE_DIR="$LIBUSB_INC" \
        -DLIBUSB_LIBRARIES="$LIBUSB_LIB"
    cmake --build "$TOOLCHAIN_DIR/picotool-build" -j
fi
[ "$SKIP_PICOTOOL" = 1 ] || echo "picotool: $("$PICO_TOOL" version 2>/dev/null | head -1)"

# --- gh CLI (optional, used by scripts/publish.sh) ---------------------------
GH_BIN="$TOOLCHAIN_DIR/gh"
if [ "$SKIP_PICOTOOL" = 1 ]; then
    :
elif ! command -v gh >/dev/null 2>&1 && [ ! -x "$GH_BIN" ]; then
    echo "installing gh CLI (needed only for scripts/publish.sh)"
    GH_VER="2.103.3"
    case "$OS" in
        darwin) GH_URL="https://github.com/cli/cli/releases/download/v${GH_VER}/gh_${GH_VER}_macOS_${ARCH}.zip" ;;
        # gh の配布はアーキテクチャ表記が Arm と違う (amd64 / arm64)
        linux)  case "$CPU" in x86_64) GH_ARCH=amd64 ;; *) GH_ARCH="$CPU" ;; esac
                GH_URL="https://github.com/cli/cli/releases/download/v${GH_VER}/gh_${GH_VER}_linux_${GH_ARCH}.tar.gz" ;;
    esac
    if curl -fL --retry 3 -o "$TOOLCHAIN_DIR/gh-download" "$GH_URL"; then
        mkdir -p "$GH_BIN"
        case "$OS" in
            darwin) unzip -q -o "$TOOLCHAIN_DIR/gh-download" -d "$GH_BIN" ;;
            linux)  tar xzf "$TOOLCHAIN_DIR/gh-download" -C "$GH_BIN" --strip-components=1 ;;
        esac
        rm -f "$TOOLCHAIN_DIR/gh-download"
    else
        echo "gh download failed - install it yourself (brew install gh) if you need publish.sh" >&2
    fi
fi
GH_EXE="$(find "$GH_BIN" -type f -name gh 2>/dev/null | head -1 || true)"
[ -n "$GH_EXE" ] && echo "gh: $("$GH_EXE" --version 2>/dev/null | head -1)"

cat <<EOF

Done. Export these before building:

  export PICO_SDK_PATH="$SDK_DIR"
  export PICO_TOOLCHAIN_PATH="$ARM_DIR"

Then: scripts/build.sh && scripts/flash.sh
EOF
