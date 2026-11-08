#include <cstdio>

#include "hardware/i2c.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#include "bme280.hpp"
#include "command.hpp"
#include "config.hpp"
#include "i2c_scan.hpp"
#include "sampler.hpp"

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

// The watchdog is the safety net: if anything ever wedges (USB stack, I2C
// peripheral, a stuck bus) the chip resets and re-enumerates USB, instead of
// vanishing from the bus until someone pulls the cable. It stays fixed because
// the sample loop kicks it while waiting, so a long sample interval cannot trip
// it and a wedge still resets within 6 s.
constexpr uint32_t kWatchdogTimeoutMs = 6000;

constexpr unsigned int kLineCapacity = 64;

monitor::Config g_config;

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

// Prints one probe result at a time: nothing accumulates into a fixed buffer,
// so the scan cannot overrun and fault the core.
void reportScan(i2c_inst_t *i2c) {
    uint8_t found[i2cscan::kProbedAddressCount];
    const size_t count = i2cscan::scan(i2c, found, i2cscan::kProbedAddressCount, watchdog_update);

    std::printf("I2C scan:");
    for (size_t i = 0; i < count; ++i) {
        std::printf(" %02X", found[i]);
    }
    if (count == 0) {
        std::printf(" (none)");
    }
    std::printf("\r\n");
    std::fflush(stdout);
}

void printHelp() {
    std::printf(
        "commands:\r\n"
        "  get                             show current settings\r\n"
        "  set interval=<100..3600000>     sample interval in ms\r\n"
        "  set decimals=<0..6>             digits after the decimal point\r\n"
        "  set average=<1..64>             moving average over raw ADC values\r\n"
        "  set osr.t|osr.p|osr.h=<1|2|4|8|16>  oversampling per channel\r\n"
        "  set filter=<0..4>               IIR filter (0=off,1=x2,2=x4,3=x8,4=x16)\r\n"
        "  set mode=normal|forced          conversion mode\r\n"
        "  set json=true|false             emit samples as one JSON object per line\r\n"
        "  ping                            liveness check\r\n");
    std::fflush(stdout);
}

void printConfig() {
    char buf[128];
    std::printf("%s\r\n", monitor::describe(g_config, buf, sizeof(buf)));
    std::fflush(stdout);
}

// One sample, either as a JSON object (machine readable, consumed by the
// collector service) or as aligned columns (human readable). Both honour the
// decimals setting; JSON numbers stay valid at decimals=0 because %.*f then
// prints a bare integer.
void printSample(const bme280::Reading &r, const bme280::RawData &avg,
                 unsigned int sample_count) {
    const int d = g_config.decimals;
    if (g_config.json) {
        std::printf("{\"temperature_c\":%.*f,\"pressure_hpa\":%.*f,\"humidity_rh\":%.*f,"
                    "\"dew_point_c\":%.*f,\"absolute_humidity_gm3\":%.*f,"
                    "\"adc_t\":%u,\"adc_p\":%u,\"adc_h\":%u,\"samples\":%u}\r\n",
                    d, r.temperature_c,
                    d, r.pressure_pa / 100.0f,
                    d, r.humidity_rh,
                    d, bme280::dewPoint(r.temperature_c, r.humidity_rh),
                    d, bme280::absoluteHumidity(r.temperature_c, r.humidity_rh),
                    avg.temperature, avg.pressure, avg.humidity, sample_count);
    } else {
        std::printf("T=%*.*f C  P=%*.*f hPa  RH=%*.*f %%  |  dew=%*.*f C  AH=%*.*f g/m3  (adc T=%u P=%u H=%u n=%u)\r\n",
                    d + 5, d, r.temperature_c,
                    d + 7, d, r.pressure_pa / 100.0f,
                    d + 4, d, r.humidity_rh,
                    d + 4, d, bme280::dewPoint(r.temperature_c, r.humidity_rh),
                    d + 3, d, bme280::absoluteHumidity(r.temperature_c, r.humidity_rh),
                    avg.temperature, avg.pressure, avg.humidity, sample_count);
    }
    std::fflush(stdout);
}

// Consumes every line waiting on USB without blocking the sample loop.
void pollCommands(bme280::Sensor *sensor) {
    static char line[kLineCapacity];
    static unsigned int length = 0;

    while (true) {
        const int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT || c == PICO_ERROR_NO_DATA) {
            return;
        }
        if (!monitor::feedChar(static_cast<char>(c), line, &length, kLineCapacity)) {
            continue;
        }

        const char *key = nullptr;
        const char *value = nullptr;
        switch (monitor::parseLine(line, &key, &value)) {
            case monitor::Command::None:
                break;
            case monitor::Command::Ping:
                std::printf("pong\r\n");
                std::fflush(stdout);
                break;
            case monitor::Command::Get:
                printConfig();
                break;
            case monitor::Command::Help:
                printHelp();
                break;
            case monitor::Command::Set: {
                const char *message = nullptr;
                if (monitor::applySetting(&g_config, key, value, &message) ==
                    monitor::ParseResult::Ok) {
                    std::printf("ok %s=%s\r\n", key, value);
                    sensor->applyConfig(g_config);
                } else {
                    std::printf("error: %s: %s=%s\r\n", message, key, value);
                }
                printConfig();
                std::fflush(stdout);
                break;
            }
            case monitor::Command::Bad:
                std::printf("error: unknown command (try 'help')\r\n");
                std::fflush(stdout);
                break;
        }
        watchdog_update();
    }
}

// Sleeps in short slices, kicking the watchdog and draining commands, so a
// `set interval=` or `set average=` takes effect promptly.
void interruptibleSleep(uint32_t ms, bme280::Sensor *sensor) {
    constexpr uint32_t kSliceMs = 50;
    uint32_t remaining = ms;
    while (remaining > 0) {
        const uint32_t slice = remaining < kSliceMs ? remaining : kSliceMs;
        sleep_ms(slice);
        remaining -= slice;
        watchdog_update();
        pollCommands(sensor);
    }
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
    printConfig();
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
    sensor.applyConfig(g_config);

    const bme280::Calibration &cal = sensor.calibration();
    std::printf("Calibration: T1=%u T2=%d T3=%d\r\n", cal.t1, cal.t2, cal.t3);
    std::printf("  P1=%u P2=%d P3=%d P4=%d P5=%d P6=%d P7=%d P8=%d P9=%d\r\n",
                cal.p1, cal.p2, cal.p3, cal.p4, cal.p5, cal.p6, cal.p7, cal.p8, cal.p9);
    std::printf("  H1=%u H2=%d H3=%d H4=%d H5=%d H6=%d\r\n",
                cal.h1, cal.h2, cal.h3, cal.h4, cal.h5, cal.h6);
    std::fflush(stdout);

    monitor::RawAverager averager;
    averager.reset(g_config.average);
    uint32_t applied_average = g_config.average;

    uint32_t consecutive_failures = 0;
    while (true) {
        watchdog_update();
        pollCommands(&sensor);

        if (applied_average != g_config.average) {
            averager.reset(g_config.average);
            applied_average = g_config.average;
        }

        bme280::RawData raw;
        if (sensor.read(&raw)) {
            consecutive_failures = 0;
            averager.push(raw);
            const bme280::RawData avg = averager.average();
            const bme280::Reading r = bme280::compensate(avg, sensor.calibration());

            // %.*f takes the precision from the config; pico_printf supports the
            // star form, so the decimals setting is honoured on the device.
            printSample(r, avg, static_cast<unsigned int>(averager.count()));

            if (g_config.forced) {
                sensor.triggerForced();
            }

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
                sensor.applyConfig(g_config);
            }
        }

        interruptibleSleep(g_config.interval_ms, &sensor);
    }
}
