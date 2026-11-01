// Host unit tests for the BME280 driver and compensation math.
// Built with the host compiler against the simulated-I2C stubs, so the real
// driver code paths (register addresses, calibration parsing, output ordering,
// status polling) are exercised without hardware.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "bme280.hpp"
#include "bme280_math.hpp"
#include "hardware/i2c.h"
#include "i2c_scan.hpp"

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

    const std::vector<uint8_t> writes = simWrittenRegisters();
    auto it_hum = std::find(writes.begin(), writes.end(), ctrl_hum);
    auto it_meas = std::find(writes.begin(), writes.end(), ctrl_meas);
    check(it_hum != writes.end() && it_meas != writes.end() && it_hum < it_meas,
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

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
