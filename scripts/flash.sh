#!/usr/bin/env bash
# Flashes the firmware onto an attached Pico 2.
#
# Handles both board states:
#   * BOOTSEL mode (RPI-RP2 / RP2350 volume present) -> load directly.
#   * application mode -> ask the running firmware to reboot into BOOTSEL.
#
# On macOS copying the UF2 to the mass-storage volume frequently hangs, so this
# script always goes through picotool over USB.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UF2="${UF2:-$ROOT/firmware/build/bme280_monitor.uf2}"
PICOTOOL="${PICOTOOL:-$HOME/toolchain/picotool-build/picotool}"

if [ ! -f "$UF2" ]; then
    echo "no firmware at $UF2 - run scripts/build.sh first" >&2
    exit 1
fi
if [ ! -x "$PICOTOOL" ]; then
    echo "picotool not found at $PICOTOOL - run scripts/setup-toolchain.sh first" >&2
    exit 1
fi

bootsel_mode() {
    [ -d /Volumes/RPI-RP2 ] || [ -d /Volumes/RP2350 ]
}

if ! bootsel_mode; then
    echo "asking the running firmware to reboot into BOOTSEL..."
    if ! "$PICOTOOL" reboot -f -u >/dev/null 2>&1; then
        echo "no device answered. Hold BOOTSEL while (re)plugging the board, then re-run." >&2
        exit 1
    fi
    for _ in $(seq 1 60); do
        bootsel_mode && break
        sleep 0.25
    done
fi

if ! bootsel_mode; then
    echo "board did not enter BOOTSEL mode. Hold BOOTSEL while (re)plugging, then re-run." >&2
    exit 1
fi

"$PICOTOOL" load "$UF2"
"$PICOTOOL" verify "$UF2"
"$PICOTOOL" reboot
echo "flashed and rebooted. Run scripts/monitor.py to watch readings."
