#include <cstdio>

#include "hardware/i2c.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#include "bme280.hpp"

// Raspberry Pi Pico 2 (RP2350) + BME280 on I2C0.
//
// RP2350 pad mapping (verified against hardware_regs/io_bank0.h):
//   GP4 -> I2C0_SDA, GP5 -> I2C0_SCL
// The original MicroPython scripts used scl=GP4/sda=GP5, which RP2350 rejects
// ("bad SCL pin"); the swapped wiring below is what actually clocks the bus.
namespace {

constexpr uint8_t kSdaPin = 4;
constexpr uint8_t kSclPin = 5;
constexpr uint32_t kI2cBaud = 100000;
constexpr uint8_t kI2cAddress = 0x76;

constexpr uint8_t kLedPin = PICO_DEFAULT_LED_PIN;

constexpr uint32_t kSampleIntervalMs = 2000;

// The watchdog is the safety net: if anything ever wedges (USB stack, I2C
// peripheral, a stuck bus) the chip resets and re-enumerates USB, instead of
// vanishing from the bus until someone pulls the cable.
constexpr uint32_t kWatchdogTimeoutMs = 6000;

void initLed() {
    gpio_init(kLedPin);
    gpio_set_dir(kLedPin, GPIO_OUT);
    gpio_put(kLedPin, 0);
}

void ledPattern(uint32_t halfPeriodMs, uint32_t cycles) {
    for (uint32_t i = 0; i < cycles; ++i) {
        gpio_put(kLedPin, 1);
        sleep_ms(halfPeriodMs);
        watchdog_update();
        gpio_put(kLedPin, 0);
        sleep_ms(halfPeriodMs);
        watchdog_update();
    }
}

// One printf per probe result: nothing accumulates into a fixed buffer, so the
// scan cannot overrun and fault the core.
void reportScan(i2c_inst_t *i2c) {
    std::printf("I2C scan:");
    std::fflush(stdout);
    int found = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
        watchdog_update();
        // Bounded probe: an unpowered or miswired sensor can hold the bus low,
        // and an untimed probe would then hang the scan forever.
        if (i2c_write_timeout_us(i2c, addr, nullptr, 0, false, 20000) >= 0) {
            std::printf(" %02X", addr);
            std::fflush(stdout);
            ++found;
        }
    }
    if (found == 0) {
        std::printf(" (none)");
    }
    std::printf("\r\n");
    std::fflush(stdout);
}

}  // namespace

int main() {
    // A watchdog reset means the previous run wedged; report it on the next
    // boot so the failure is not invisible.
    const bool watchdogReset = watchdog_enable_caused_reboot();
    watchdog_enable(kWatchdogTimeoutMs, false);

    stdio_init_all();
    initLed();

    std::printf("\r\n=== BME280 C++ monitor (Pico 2 / RP2350) ===\r\n");
    if (watchdogReset) {
        std::printf("NOTE: the previous run was reset by the watchdog (it had wedged).\r\n");
    }
    std::printf("I2C0 sda=GP%d scl=GP%d addr=0x%02X\r\n", kSdaPin, kSclPin, kI2cAddress);
    std::fflush(stdout);

    // Three quick blinks prove the program is executing, independent of USB.
    ledPattern(120, 3);

    i2c_init(i2c0, kI2cBaud);
    gpio_set_function(kSdaPin, GPIO_FUNC_I2C);
    gpio_set_function(kSclPin, GPIO_FUNC_I2C);
    gpio_pull_up(kSdaPin);
    gpio_pull_up(kSclPin);

    reportScan(i2c0);

    bme280::Sensor sensor(i2c0, kI2cAddress);

    // The sensor may be absent or still settling; retry rather than halt. A
    // slow LED heartbeat means "no sensor", so the state is readable without a
    // serial console.
    while (!sensor.begin()) {
        std::printf("BME280 not ready at 0x%02X (check VIN/GND and SDA=GP4 SCL=GP5), retrying...\r\n",
                    kI2cAddress);
        std::fflush(stdout);
        ledPattern(500, 2);
        watchdog_update();
    }

    const bme280::Calibration &cal = sensor.calibration();
    std::printf("Calibration: T1=%u T2=%d T3=%d\r\n", cal.t1, cal.t2, cal.t3);
    std::printf("  P1=%u P2=%d P3=%d P4=%d P5=%d P6=%d P7=%d P8=%d P9=%d\r\n",
                cal.p1, cal.p2, cal.p3, cal.p4, cal.p5, cal.p6, cal.p7, cal.p8, cal.p9);
    std::printf("  H1=%u H2=%d H3=%d H4=%d H5=%d H6=%d\r\n",
                cal.h1, cal.h2, cal.h3, cal.h4, cal.h5, cal.h6);
    std::fflush(stdout);

    uint32_t consecutive_failures = 0;
    while (true) {
        watchdog_update();

        bme280::RawData raw;
        if (sensor.read(&raw)) {
            consecutive_failures = 0;
            const bme280::Reading r = bme280::compensate(raw, sensor.calibration());
            std::printf("T=%7.2f C  P=%9.2f hPa  RH=%6.2f %%  |  dew=%6.2f C  AH=%5.2f g/m3  (adc T=%u P=%u H=%u)\r\n",
                        r.temperature_c, r.pressure_pa / 100.0f, r.humidity_rh,
                        bme280::dewPoint(r.temperature_c, r.humidity_rh),
                        bme280::absoluteHumidity(r.temperature_c, r.humidity_rh),
                        raw.temperature, raw.pressure, raw.humidity);
            std::fflush(stdout);
            // One short blink per good sample.
            gpio_put(kLedPin, 1);
            sleep_ms(60);
            gpio_put(kLedPin, 0);
        } else {
            ++consecutive_failures;
            std::printf("I2C read failed (%u in a row)\r\n", consecutive_failures);
            std::fflush(stdout);
            if (consecutive_failures >= 10) {
                std::printf("Too many failures; re-initialising sensor.\r\n");
                std::fflush(stdout);
                consecutive_failures = 0;
                while (!sensor.begin()) {
                    watchdog_update();
                    sleep_ms(2000);
                }
            }
        }
        sleep_ms(kSampleIntervalMs);
    }
}
