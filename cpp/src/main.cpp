#include <cstdio>

#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include "bme280.hpp"

// Raspberry Pi Pico 2 (RP2350) + BME280 on I2C0.
//
// NOTE: on RP2350 the I2C0 pad functions are swapped relative to RP2040. The
// working wiring is SDA=GP4, SCL=GP5. The old MicroPython scripts assumed
// scl=GP4/sda=GP5, which MicroPython rejects on Pico 2 with "bad SCL pin".
namespace {

constexpr uint8_t kSdaPin = 4;
constexpr uint8_t kSclPin = 5;
constexpr uint32_t kI2cBaud = 100000;
constexpr uint8_t kI2cAddress = 0x76;

constexpr uint32_t kSampleIntervalMs = 2000;

void reportScan(i2c_inst_t *i2c) {
    char line[96];
    int n = std::snprintf(line, sizeof(line), "I2C scan:");
    int found = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
        if (i2c_write_blocking(i2c, addr, nullptr, 0, false) >= 0) {
            n += std::snprintf(line + n, sizeof(line) - n, " %02X", addr);
            ++found;
        }
    }
    n += std::snprintf(line + n, sizeof(line) - n, "%s", found ? "" : " (none)");
    line[n++] = '\r';
    line[n++] = '\n';
    std::fwrite(line, 1, static_cast<size_t>(n), stdout);
}

}  // namespace

int main() {
    stdio_init_all();
    // Give the host time to open the CDC serial port before the first output.
    sleep_ms(1500);

    printf("BME280 C++ monitor (Pico 2 / RP2350)\r\n");
    printf("I2C0 sda=GP%d scl=GP%d addr=0x%02X\r\n", kSdaPin, kSclPin, kI2cAddress);

    i2c_init(i2c0, kI2cBaud);
    gpio_set_function(kSdaPin, GPIO_FUNC_I2C);
    gpio_set_function(kSclPin, GPIO_FUNC_I2C);
    gpio_pull_up(kSdaPin);
    gpio_pull_up(kSclPin);

    reportScan(i2c0);

    bme280::Sensor sensor(i2c0, kI2cAddress);

    // The sensor may be absent or still settling; retry rather than halting so
    // a re-plugged sensor recovers on its own.
    while (!sensor.begin()) {
        printf("BME280 not ready at 0x%02X, retrying...\r\n", kI2cAddress);
        sleep_ms(2000);
    }

    const bme280::Calibration &cal = sensor.calibration();
    printf("Calibration: T1=%u T2=%d T3=%d\r\n", cal.t1, cal.t2, cal.t3);
    printf("  P1=%u P2=%d P3=%d P4=%d P5=%d P6=%d P7=%d P8=%d P9=%d\r\n",
           cal.p1, cal.p2, cal.p3, cal.p4, cal.p5, cal.p6, cal.p7, cal.p8, cal.p9);
    printf("  H1=%u H2=%d H3=%d H4=%d H5=%d H6=%d\r\n",
           cal.h1, cal.h2, cal.h3, cal.h4, cal.h5, cal.h6);

    uint32_t consecutive_failures = 0;
    while (true) {
        bme280::RawData raw;
        if (sensor.read(&raw)) {
            consecutive_failures = 0;
            const bme280::Reading r = bme280::compensate(raw, sensor.calibration());
            printf("T=%7.2f C  P=%9.2f hPa  RH=%6.2f %%  |  dew=%6.2f C  AH=%5.2f g/m3  (adc T=%u P=%u H=%u)\r\n",
                   r.temperature_c, r.pressure_pa / 100.0f, r.humidity_rh,
                   bme280::dewPoint(r.temperature_c, r.humidity_rh),
                   bme280::absoluteHumidity(r.temperature_c, r.humidity_rh),
                   raw.temperature, raw.pressure, raw.humidity);
        } else {
            ++consecutive_failures;
            printf("I2C read failed (%u in a row)\r\n", consecutive_failures);
            if (consecutive_failures >= 10) {
                printf("Too many failures; re-initialising sensor.\r\n");
                consecutive_failures = 0;
                while (!sensor.begin()) {
                    sleep_ms(2000);
                }
            }
        }
        sleep_ms(kSampleIntervalMs);
    }
}
