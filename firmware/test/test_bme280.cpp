// Host unit tests for the BME280 driver and compensation math.
// Built with the host compiler against the simulated-I2C stubs, so the real
// driver code paths (register addresses, calibration parsing, output ordering,
// status polling) are exercised without hardware.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "bme280.hpp"
#include "bme280_math.hpp"
#include "command.hpp"
#include "config.hpp"
#include "hardware/i2c.h"
#include "i2c_scan.hpp"
#include "sampler.hpp"

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const std::string &what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

void checkNear(double actual, double expected, double tol, const std::string &what) {
    ++g_checks;
    if (std::fabs(actual - expected) > tol) {
        ++g_failures;
        std::printf("  FAIL: %s (got %.6f, want %.6f +/- %.6f)\n", what.c_str(),
                    actual, expected, tol);
    }
}

// Index of the last write to `reg` in the stub's write log, or SIZE_MAX.
size_t lastWriteIndexOf(const std::vector<uint8_t> &order, uint8_t reg) {
    for (size_t i = order.size(); i > 0; --i) {
        if (order[i - 1] == reg) {
            return i - 1;
        }
    }
    return SIZE_MAX;
}

// Calibration captured from the real module on the Pico 2.
bme280::Calibration groundTruthCal() {
    bme280::Calibration cal{};
    cal.t1 = 28651;
    cal.t2 = 26756;
    cal.t3 = 50;
    cal.p1 = 37438;
    cal.p2 = -10736;
    cal.p3 = 3024;
    cal.p4 = 10578;
    cal.p5 = -225;
    cal.p6 = -7;
    cal.p7 = 9900;
    cal.p8 = -10230;
    cal.p9 = 4285;
    cal.h1 = 75;
    cal.h2 = 373;
    cal.h3 = 0;
    cal.h4 = 292;
    cal.h5 = 50;
    cal.h6 = 30;
    return cal;
}

// Ground-truth ADC values from the same module.
constexpr uint32_t kAdcT = 529370;
constexpr uint32_t kAdcP = 275403;
constexpr uint32_t kAdcH = 27985;

// --- compensation math -----------------------------------------------------

void testCompensationAgainstGroundTruth() {
    std::printf("testCompensationAgainstGroundTruth\n");
    bme280::Calibration cal = groundTruthCal();
    bme280::RawData raw{kAdcT, kAdcP, kAdcH};

    // Values independently computed from the Bosch BME280_driver reference.
    check(bme280::compensateTemperature(raw, cal) == 2263, "temperature centi-deg C");
    check(cal.t_fine == 115882, "t_fine");
    check(bme280::compensatePressure(raw, cal) == 10032860u, "pressure Pa x100");
    check(bme280::compensateHumidity(raw, cal) == 54010u, "humidity %RH x1024");

    const bme280::Reading r = bme280::compensate(raw, cal);
    checkNear(r.temperature_c, 22.63, 0.02, "reading temperature C");
    checkNear(r.pressure_pa / 100.0, 1003.29, 0.05, "reading pressure hPa");
    checkNear(r.humidity_rh, 52.74, 0.05, "reading humidity %RH");
}

void testCompensationClamps() {
    std::printf("testCompensationClamps\n");
    bme280::Calibration cal = groundTruthCal();

    // Saturated ADC must clamp to the sensor's documented range rather than
    // return the nonsense values an unclamped formula produces.
    bme280::RawData saturated{0xFFFFF, 0xFFFFF, 0xFFFF};
    const int32_t t = bme280::compensateTemperature(saturated, cal);
    check(t >= -4000 && t <= 8500, "temperature clamped to -40..85 C");
    const uint32_t p = bme280::compensatePressure(saturated, cal);
    check(p >= 3000000u && p <= 11000000u, "pressure clamped to 300..1100 hPa");
    const uint32_t h = bme280::compensateHumidity(saturated, cal);
    check(h <= 102400u, "humidity clamped to <=100 %RH");

    // Zero P1 would divide by zero; the reference returns the minimum.
    bme280::Calibration broken = groundTruthCal();
    broken.p1 = 0;
    check(bme280::compensatePressure(saturated, broken) == 3000000u, "zero P1 handled");
}

void testDerivedQuantities() {
    std::printf("testDerivedQuantities\n");
    // The original Python used pow(10, .) with exp() constants, which gave
    // 133.69 hPa at 20 C instead of 23.33 hPa.
    checkNear(bme280::saturationVapourPressure(20.0f), 23.3344, 0.001, "sat VP at 20 C");
    checkNear(bme280::saturationVapourPressure(0.0f), 6.1094, 0.0001, "sat VP at 0 C");
    checkNear(bme280::actualVapourPressure(20.0f, 50.0f), 11.6672, 0.001, "actual VP");
    checkNear(bme280::absoluteHumidity(20.0f, 50.0f), 8.6261, 0.001, "absolute humidity");
    checkNear(bme280::dewPoint(20.0f, 50.0f), 9.2611, 0.001, "dew point");
    // RH = 100 %% means dew point equals air temperature.
    checkNear(bme280::dewPoint(22.63f, 100.0f), 22.63f, 0.01, "dew point at saturation");
}

// --- driver / register map -------------------------------------------------

void testCalibrationParsing() {
    std::printf("testCalibrationParsing\n");
    simReset();
    simSetAdc(kAdcT, kAdcP, kAdcH);
    bme280::Sensor sensor(i2c0, 0x76);
    check(sensor.begin(), "begin() succeeds on simulated sensor");

    const bme280::Calibration &cal = sensor.calibration();
    const bme280::Calibration want = groundTruthCal();
    check(cal.t1 == want.t1 && cal.t2 == want.t2 && cal.t3 == want.t3, "T1..T3 parsed");
    check(cal.p1 == want.p1 && cal.p2 == want.p2 && cal.p3 == want.p3 &&
          cal.p4 == want.p4 && cal.p5 == want.p5 && cal.p6 == want.p6 &&
          cal.p7 == want.p7 && cal.p8 == want.p8 && cal.p9 == want.p9,
          "P1..P9 parsed from 0x8E..0x9F");
    // H4/H5 packing is the part the original code got wrong entirely.
    check(cal.h1 == want.h1, "H1 from 0xA1");
    check(cal.h2 == want.h2 && cal.h3 == want.h3, "H2/H3 parsed");
    check(cal.h4 == want.h4, "H4 packed from 0xE4/0xE5 low nibble");
    check(cal.h5 == want.h5, "H5 packed from 0xE5 high nibble/0xE6");
    check(cal.h6 == want.h6, "H6 from 0xE7");
}

void testReadOrderingAndValues() {
    std::printf("testReadOrderingAndValues\n");
    simReset();
    simSetAdc(kAdcT, kAdcP, kAdcH);
    bme280::Sensor sensor(i2c0, 0x76);
    check(sensor.begin(), "begin()");

    bme280::RawData raw{};
    check(sensor.read(&raw), "read() succeeds");
    // 0xF7 is PRESSURE first; swapping T and P was a critical bug in the
    // original code.
    check(raw.pressure == kAdcP, "pressure decoded from 0xF7..0xF9");
    check(raw.temperature == kAdcT, "temperature decoded from 0xFA..0xFC");
    check(raw.humidity == kAdcH, "humidity decoded from 0xFD..0xFE");

    const bme280::Reading r = bme280::compensate(raw, sensor.calibration());
    checkNear(r.temperature_c, 22.63, 0.02, "end-to-end temperature");
    checkNear(r.pressure_pa / 100.0, 1003.29, 0.05, "end-to-end pressure");
    checkNear(r.humidity_rh, 52.74, 0.05, "end-to-end humidity");
}

void testStatusPolling() {
    std::printf("testStatusPolling\n");
    simReset();
    simSetAdc(kAdcT, kAdcP, kAdcH);
    bme280::Sensor sensor(i2c0, 0x76);
    check(sensor.begin(), "begin()");

    // Sensor reports "measuring" for the first three status reads; read() must
    // wait rather than sample a mid-conversion (stale/saturated) value.
    simSetMeasuringCycles(3);
    bme280::RawData raw{};
    check(sensor.read(&raw), "read() waits through measuring cycles");
    check(raw.temperature == kAdcT, "value valid after waiting");
}

void testConfigurationWrites() {
    std::printf("testConfigurationWrites\n");
    simReset();
    bme280::Sensor sensor(i2c0, 0x76);
    check(sensor.begin(), "begin()");

    // ctrl_hum (0xF2) must be written before ctrl_meas (0xF4) or the humidity
    // oversampling never latches.
    const uint8_t ctrl_hum = simReg(0xF2);
    const uint8_t ctrl_meas = simReg(0xF4);
    check(ctrl_hum == 0x01, "ctrl_hum written with H x1");
    check((ctrl_meas & 0x03) == 0x03, "ctrl_meas sets normal mode");
    check(((ctrl_meas >> 2) & 0x07) == 0x01, "ctrl_meas sets P x1");
    check(((ctrl_meas >> 5) & 0x07) == 0x01, "ctrl_meas sets T x1");

    // Assert on the register addresses actually written, not on the values:
    // searching values can match unrelated writes and prove nothing about order.
    const std::vector<uint8_t> order = simWriteOrder();
    const size_t hum = lastWriteIndexOf(order, 0xF2);
    const size_t meas = lastWriteIndexOf(order, 0xF4);
    check(hum != SIZE_MAX && meas != SIZE_MAX && hum < meas,
          "ctrl_hum written before ctrl_meas");
}

void testChipIdRejected() {
    std::printf("testChipIdRejected\n");
    simReset();
    simSetChipId(0x41);  // BMP280 id, not BME280
    bme280::Sensor sensor(i2c0, 0x76);
    check(!sensor.begin(), "begin() rejects wrong chip id");
    check(!sensor.probe(), "probe() rejects wrong chip id");

    simReset();
    check(sensor.probe(), "probe() accepts BME280 chip id");
}

void testMissingSensorRejected() {
    std::printf("testMissingSensorRejected\n");
    simReset();
    bme280::Sensor sensor(i2c0, 0x77);  // nothing ACKs at this address
    check(!sensor.begin(), "begin() fails when device NACKs");
    bme280::RawData raw{};
    check(!sensor.read(&raw), "read() fails when device NACKs");
}

// A stuck bus (unpowered sensor holding SDA/SCL low) must fail every transfer
// instead of blocking forever, so main() can keep its watchdog fed and retry.
void testStuckBusFailsFast() {
    std::printf("testStuckBusFailsFast\n");
    simReset();
    simSetBusStuck(true);
    bme280::Sensor sensor(i2c0, 0x76);
    check(!sensor.begin(), "begin() fails on a stuck bus");
    bme280::RawData raw{};
    check(!sensor.read(&raw), "read() fails on a stuck bus");
    simSetBusStuck(false);
    check(sensor.begin(), "begin() recovers once the bus is free");
}

// pico-sdk's i2c_write_blocking() transmits nothing when len == 0 and reports
// success, so probing with a zero-length write makes every address look
// present. The scan must use a transfer that really exercises the address
// phase, and must not report addresses that never acknowledged.
void testScanOnlyReportsAcknowledgingAddresses() {
    std::printf("testScanOnlyReportsAcknowledgingAddresses\n");
    simReset();

    uint8_t found[i2cscan::kProbedAddressCount] = {};
    const size_t count = i2cscan::scan(i2c0, found, i2cscan::kProbedAddressCount, nullptr);
    check(count == 1, "scan finds exactly the simulated device");
    check(count == 1 && found[0] == 0x76, "scan reports address 0x76");

    // A scan into a smaller buffer must stay in bounds and still count all hits.
    uint8_t small[1] = {};
    const size_t total = i2cscan::scan(i2c0, small, 1, nullptr);
    check(total == 1, "scan counts hits even when the buffer is full");
    check(small[0] == 0x76, "scan fills the buffer before stopping storing");

    // With nothing on the bus the scan must report an empty result rather than
    // the 112 addresses a zero-length-write probe would have claimed.
    simSetBusStuck(true);
    const size_t none = i2cscan::scan(i2c0, found, i2cscan::kProbedAddressCount, nullptr);
    check(none == 0, "scan reports no addresses on a stuck bus");
    simSetBusStuck(false);
}

void testApplyConfigRegisters() {
    std::printf("testApplyConfigRegisters\n");
    simReset();
    bme280::Sensor sensor(i2c0, 0x76);
    check(sensor.begin(), "begin()");

    monitor::Config cfg;
    cfg.osr_t = 4;
    cfg.osr_p = 16;
    cfg.osr_h = 2;
    cfg.filter = 2;   // x8
    cfg.forced = true;
    sensor.applyConfig(cfg);

    // ctrl_hum (0xF2) written before ctrl_meas (0xF4), with the 3-bit encodings.
    const uint8_t ctrl_hum = simReg(0xF2);
    const uint8_t ctrl_meas = simReg(0xF4);
    const uint8_t config_reg = simReg(0xF5);
    check(ctrl_hum == 0x02, "applyConfig writes H x2 to ctrl_hum");
    check(((ctrl_meas >> 5) & 0x07) == 0x03, "applyConfig sets T x4");
    check(((ctrl_meas >> 2) & 0x07) == 0x05, "applyConfig sets P x16");
    check((ctrl_meas & 0x03) == 0x01, "applyConfig sets forced mode");
    check(((config_reg >> 2) & 0x07) == 0x02, "applyConfig sets IIR x8");

    // Look at the last pair of writes: applyConfig runs after begin(), so the
    // ordering that matters is the one from this call.
    const std::vector<uint8_t> order = simWriteOrder();
    const size_t hum = lastWriteIndexOf(order, 0xF2);
    const size_t meas = lastWriteIndexOf(order, 0xF4);
    check(hum != SIZE_MAX && meas != SIZE_MAX && hum < meas,
          "applyConfig writes ctrl_hum before ctrl_meas");
}

void testConfigSettingValidation() {
    std::printf("testConfigSettingValidation\n");
    monitor::Config cfg;
    const char *msg = nullptr;

    check(monitor::applySetting(&cfg, "interval", "500", &msg) == monitor::ParseResult::Ok,
          "interval=500 accepted");
    check(cfg.interval_ms == 500, "interval applied");

    check(monitor::applySetting(&cfg, "interval", "50", &msg) == monitor::ParseResult::BadValue,
          "interval below the floor rejected");
    check(cfg.interval_ms == 500, "rejected setting leaves config untouched");

    check(monitor::applySetting(&cfg, "decimals", "4", &msg) == monitor::ParseResult::Ok,
          "decimals=4 accepted");
    check(cfg.decimals == 4, "decimals applied");
    check(monitor::applySetting(&cfg, "decimals", "9", &msg) == monitor::ParseResult::BadValue,
          "decimals above 6 rejected");

    check(monitor::applySetting(&cfg, "osr.p", "16", &msg) == monitor::ParseResult::Ok,
          "osr.p=16 accepted");
    check(cfg.osr_p == 16, "osr.p applied");
    check(monitor::applySetting(&cfg, "osr.p", "3", &msg) == monitor::ParseResult::BadValue,
          "non-power-of-two osr rejected");
    check(monitor::applySetting(&cfg, "osr.p", "256", &msg) == monitor::ParseResult::BadValue,
          "oversized osr rejected without truncation");
    check(cfg.osr_p == 16, "osr.p unchanged after rejections");

    check(monitor::applySetting(&cfg, "average", "8", &msg) == monitor::ParseResult::Ok,
          "average=8 accepted");
    check(monitor::applySetting(&cfg, "average", "0", &msg) == monitor::ParseResult::BadValue,
          "average=0 rejected");
    check(monitor::applySetting(&cfg, "average", "65", &msg) == monitor::ParseResult::BadValue,
          "average above the window cap rejected");

    check(monitor::applySetting(&cfg, "mode", "forced", &msg) == monitor::ParseResult::Ok,
          "mode=forced accepted");
    check(cfg.forced, "mode applied");
    check(monitor::applySetting(&cfg, "mode", "sleep", &msg) == monitor::ParseResult::BadValue,
          "unknown mode rejected");

    check(monitor::applySetting(&cfg, "json", "true", &msg) == monitor::ParseResult::Ok,
          "json=true accepted");
    check(cfg.json, "json applied");
    check(monitor::applySetting(&cfg, "json", "0", &msg) == monitor::ParseResult::Ok,
          "json=0 accepted as false");
    check(!cfg.json, "json=0 clears the flag");
    check(monitor::applySetting(&cfg, "json", "maybe", &msg) == monitor::ParseResult::BadValue,
          "non-boolean json rejected");

    check(monitor::applySetting(&cfg, "nosuchkey", "1", &msg) == monitor::ParseResult::UnknownKey,
          "unknown key reported as such");
    check(monitor::applySetting(&cfg, "interval", "abc", &msg) == monitor::ParseResult::BadValue,
          "non-numeric value rejected");
    check(monitor::applySetting(&cfg, "interval", "1000x", &msg) == monitor::ParseResult::BadValue,
          "trailing garbage rejected");

    char buf[128];
    monitor::describe(cfg, buf, sizeof(buf));
    check(std::strstr(buf, "interval=500") != nullptr, "describe includes interval");
    check(std::strstr(buf, "mode=forced") != nullptr, "describe includes mode");
}

void testCommandParsing() {
    std::printf("testCommandParsing\n");
    const char *key = nullptr;
    const char *value = nullptr;

    char line1[] = "set interval=1000\r\n";
    check(monitor::parseLine(line1, &key, &value) == monitor::Command::Set, "set parsed");
    check(std::strcmp(key, "interval") == 0, "key extracted");
    check(std::strcmp(value, "1000") == 0, "value extracted");

    char line2[] = "GET";
    check(monitor::parseLine(line2, &key, &value) == monitor::Command::Get, "get is case-insensitive");

    char line3[] = "ping";
    check(monitor::parseLine(line3, &key, &value) == monitor::Command::Ping, "ping parsed");

    char line4[] = "   ";
    check(monitor::parseLine(line4, &key, &value) == monitor::Command::None, "blank line ignored");

    char line5[] = "set interval\r\n";
    check(monitor::parseLine(line5, &key, &value) == monitor::Command::Bad, "set without = is bad");

    char line6[] = "set =5\r\n";
    check(monitor::parseLine(line6, &key, &value) == monitor::Command::Bad, "empty key is bad");

    char line7[] = "frobnicate\r\n";
    check(monitor::parseLine(line7, &key, &value) == monitor::Command::Bad, "unknown command is bad");

    char line8[] = "set mode=forced\r\n";
    check(monitor::parseLine(line8, &key, &value) == monitor::Command::Set, "string value parsed");
    check(std::strcmp(value, "forced") == 0, "string value preserved");
}

void testFeedCharLineBuffer() {
    std::printf("testFeedCharLineBuffer\n");
    char line[8];
    unsigned int length = 0;

    check(!monitor::feedChar('g', line, &length, sizeof(line)), "char buffered");
    check(!monitor::feedChar('e', line, &length, sizeof(line)), "char buffered");
    check(!monitor::feedChar('t', line, &length, sizeof(line)), "char buffered");
    check(monitor::feedChar('\n', line, &length, sizeof(line)), "newline completes a line");
    check(std::strcmp(line, "get") == 0, "line content correct");
    check(length == 0, "length reset after a complete line");

    check(!monitor::feedChar('\n', line, &length, sizeof(line)), "bare newline yields nothing");

    // An over-long line must be dropped whole, never executed as a prefix.
    const char *junk = "set interval=9999999999999999999\n";
    for (const char *p = junk; *p; ++p) {
        monitor::feedChar(*p, line, &length, sizeof(line));
    }
    check(line[0] == '\0', "over-long line discarded instead of truncated");
}

void testRawAverager() {
    std::printf("testRawAverager\n");
    monitor::RawAverager avg;
    avg.reset(1);
    bme280::RawData one{531000, 276000, 27000};
    avg.push(one);
    check(avg.average().temperature == 531000, "window 1 passes the sample through");

    avg.reset(3);
    bme280::RawData a{100, 200, 300};
    bme280::RawData b{200, 400, 600};
    bme280::RawData c{300, 600, 900};
    avg.push(a);
    avg.push(b);
    check(avg.average().temperature == 150, "partial window averages what it has");
    avg.push(c);
    const bme280::RawData full = avg.average();
    check(full.temperature == 200, "full window averages temperature");
    check(full.pressure == 400, "full window averages pressure");
    check(full.humidity == 600, "full window averages humidity");

    // Ring behaviour: the oldest sample must fall out.
    bme280::RawData d{900, 1800, 2700};
    avg.push(d);
    const bme280::RawData slid = avg.average();
    check(slid.temperature == 467, "window slides, oldest sample dropped");
    check(avg.count() == 3, "count capped at the window");

    // A channel the sensor never filled must not drag the average to zero.
    avg.reset(2);
    bme280::RawData nohum{100, 200, 0};
    avg.push(nohum);
    avg.push(nohum);
    check(avg.average().humidity == 0, "empty humidity stays empty");
    check(avg.average().temperature == 100, "filled channels unaffected");

    avg.reset(0);
    check(avg.window() == 1, "window 0 clamped to 1");
    avg.reset(9999);
    check(avg.window() == monitor::RawAverager::kMaxWindow, "oversized window clamped");
}

}  // namespace

int main() {
    testCompensationAgainstGroundTruth();
    testCompensationClamps();
    testDerivedQuantities();
    testCalibrationParsing();
    testReadOrderingAndValues();
    testStatusPolling();
    testConfigurationWrites();
    testChipIdRejected();
    testMissingSensorRejected();
    testStuckBusFailsFast();
    testScanOnlyReportsAcknowledgingAddresses();
    testApplyConfigRegisters();
    testConfigSettingValidation();
    testCommandParsing();
    testFeedCharLineBuffer();
    testRawAverager();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
