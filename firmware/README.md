# BME280 C++ firmware (Raspberry Pi Pico 2)

pico-sdk C++ replacement for the MicroPython BME280 scripts. Compensation math
follows the Bosch `BME280_driver` reference implementation.

The repository root has the user-facing guide (setup, flashing, monitoring,
roadmap). This file documents the firmware itself. See [../README.md](../README.md).

## Wiring

| BME280 | Pico 2 |
|--------|--------|
| VIN    | 3V3(OUT) |
| GND    | GND |
| SDA    | GP4 |
| SCL    | GP5 |

On RP2350 the I2C0 pads differ from RP2040: GP4 is I2C0_SDA and GP5 is
I2C0_SCL (verified against `hardware_regs/io_bank0.h`). The old MicroPython
scripts used `scl=Pin(4), sda=Pin(5)`, which MicroPython rejects on Pico 2 with
`ValueError: bad SCL pin`. The board's LED is GP25.

## Build

From the repository root, prefer the wrapper scripts:

```sh
scripts/setup-toolchain.sh   # one-off: Arm toolchain + pico-sdk + picotool
scripts/build.sh             # -> firmware/build/bme280_monitor.uf2
scripts/flash.sh             # flash + verify + reboot an attached board
```

Or drive cmake directly:

```sh
export PICO_SDK_PATH=$HOME/toolchain/pico-sdk
export PICO_TOOLCHAIN_PATH=$HOME/toolchain/arm-gnu-toolchain-13.2.Rel1-darwin-arm64-arm-none-eabi
cmake -B build -DPICO_BOARD=pico2
cmake --build build -j
```

Produces `build/bme280_monitor.uf2`.

### Flashing

Hold BOOTSEL while plugging in USB, then copy the UF2 to the `RPI-RP2` volume.
On macOS that copy can hang; `scripts/flash.sh` (picotool over USB) is reliable:

```sh
picotool reboot -f -u          # running firmware -> BOOTSEL
picotool load build/bme280_monitor.uf2
picotool verify build/bme280_monitor.uf2
picotool reboot
```

picotool needs libusb support to see devices; `scripts/setup-toolchain.sh` builds
it with the right flags. Manually:

```sh
cmake -B picotool-build <pico-sdk>/tools/picotool \
    -DPICO_SDK_PATH=$HOME/toolchain/pico-sdk \
    -DLIBUSB_INCLUDE_DIR=/opt/homebrew/include/libusb-1.0 \
    -DLIBUSB_LIBRARIES=/opt/homebrew/lib/libusb-1.0.dylib
```

## Host tests

The compensation math and the driver's register handling are tested on the host
with a simulated BME280 register file seeded from calibration data captured off
the real module:

```sh
scripts/test.sh
```

Equivalent manual command:

```sh
c++ -std=c++17 -Wall -Wextra -Isrc -Itest/host_stubs \
    test/test_bme280.cpp test/host_stubs/i2c_stub.cpp \
    src/bme280.cpp src/bme280_math.cpp src/i2c_scan.cpp -o /tmp/bme280_test
/tmp/bme280_test
```

## Output

`printf` goes over USB CDC, so the board appears as a serial device
(`PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=3000` lets the firmware wait for the
host to attach before the first lines are emitted):

```
=== BME280 C++ monitor (Pico 2 / RP2350) ===
I2C0 sda=GP4 scl=GP5 addr=0x76
I2C scan: 76
Calibration: T1=28651 T2=26756 T3=50
  P1=37438 P2=-10736 P3=3024 P4=10578 P5=-225 P6=-7 P7=9900 P8=-10230 P9=4285
  H1=75 H2=373 H3=0 H4=292 H5=50 H6=30
T=  23.77 C  P=  1002.95 hPa  RH= 49.83 %  |  dew= 12.68 C  AH=10.68 g/m3  (adc T=532928 P=276784 H=27471)
```

## Diagnostics

The firmware reports its state without a serial console, and cannot wedge the
board silently:

| LED (GP25) | meaning |
|------------|---------|
| three fast blinks at boot | program is running |
| slow heartbeat | no BME280 answering at 0x76 |
| one short blink per sample | good reading |

Every I2C transfer is bounded (`kBusTimeoutUs = 50000`), because pico-sdk's
untimed blocking calls wait forever on a stuck bus. A 6 s watchdog resets the
chip if anything wedges; the next boot then prints
`NOTE: the previous run was reset by the watchdog`.

Two traps worth knowing about, both of which bit this project:

* `i2c_write_blocking(i2c, addr, nullptr, 0, false)` transmits nothing —
  pico-sdk's write loop body never runs for `len == 0` — and returns success
  for every address. Use a one-byte read to probe a bus.
* Accumulating with `n += snprintf(buf + n, sizeof(buf) - n, ...)` is unsafe:
  `snprintf` returns the would-be length, so once `n > sizeof(buf)` the size
  argument underflows and snprintf writes arbitrarily far past the buffer. On
  the Pico that faults the core, which drops the USB device off the bus with no
  way back but a power cycle.
