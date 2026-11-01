# BME280 C++ firmware (Raspberry Pi Pico 2)

pico-sdk C++ replacement for the MicroPython BME280 scripts. Compensation math
follows the Bosch `BME280_driver` reference implementation.

## Wiring

| BME280 | Pico 2 |
|--------|--------|
| VIN    | 3V3(OUT) |
| GND    | GND |
| SDA    | GP4 |
| SCL    | GP5 |

On RP2350 the I2C0 pad functions are swapped relative to RP2040: SDA=GP4,
SCL=GP5. The old MicroPython scripts used `scl=Pin(4), sda=Pin(5)`, which
MicroPython rejects on Pico 2 with `ValueError: bad SCL pin`.

## Build

```sh
export PICO_SDK_PATH=$HOME/toolchain/pico-sdk
export PICO_TOOLCHAIN_PATH=$HOME/toolchain/arm-gnu-toolchain-13.2.Rel1-darwin-arm64-arm-none-eabi
cmake -B build -DPICO_BOARD=pico2
cmake --build build -j
```

Produces `build/bme280_monitor.uf2`. Hold BOOTSEL while plugging in USB, then
copy the UF2 to the `RPI-RP2` volume.

## Host tests

The compensation math and the driver's register handling are tested on the host
with a simulated BME280 register file seeded from calibration data captured off
the real module:

```sh
c++ -std=c++17 -Wall -Wextra -Isrc -Itest/host_stubs \
    test/test_bme280.cpp test/host_stubs/i2c_stub.cpp \
    src/bme280.cpp src/bme280_math.cpp -o /tmp/bme280_test
/tmp/bme280_test
```

## Output

```
T=  22.64 C  P=  1003.29 hPa  RH= 52.74 %  |  dew= 12.86 C  AH=20.10 g/m3  (adc T=529370 P=275403 H=27985)
```
