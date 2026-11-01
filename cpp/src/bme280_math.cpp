#include "bme280_math.hpp"

#include <cmath>

namespace bme280 {
namespace {

// Magnus / Tetens constants for the 0..50 C range.
constexpr float kMagnusA = 6.1094f;
constexpr float kMagnusB = 17.625f;
constexpr float kMagnusC = 243.04f;
// 216.74 = (M_w / R_v) * 100, converts hPa and kelvin to g/m^3.
constexpr float kAbsHumidityFactor = 216.74f;

}  // namespace

// Bosch BME280_driver compensate_temperature(), 32-bit fixed point.
// Returns temperature in centi-degrees Celsius.
int32_t compensateTemperature(const RawData &raw, Calibration &cal) {
    int32_t var1 = static_cast<int32_t>((raw.temperature / 8) -
                                        (static_cast<int32_t>(cal.t1) * 2));
    var1 = (var1 * static_cast<int32_t>(cal.t2)) / 2048;
    int32_t var2 = static_cast<int32_t>((raw.temperature / 16) - static_cast<int32_t>(cal.t1));
    var2 = (((var2 * var2) / 4096) * static_cast<int32_t>(cal.t3)) / 16384;
    cal.t_fine = var1 + var2;
    int32_t temperature = (cal.t_fine * 5 + 128) / 256;

    if (temperature < -4000) {
        temperature = -4000;
    } else if (temperature > 8500) {
        temperature = 8500;
    }
    return temperature;
}

// Bosch BME280_driver compensate_pressure(), 64-bit fixed point.
// Returns pressure in Pascals x 100.
uint32_t compensatePressure(const RawData &raw, const Calibration &cal) {
    int64_t var1 = static_cast<int64_t>(cal.t_fine) - 128000;
    int64_t var2 = var1 * var1 * static_cast<int64_t>(cal.p6);
    var2 = var2 + ((var1 * static_cast<int64_t>(cal.p5)) * 131072);
    var2 = var2 + (static_cast<int64_t>(cal.p4) * 34359738368LL);
    var1 = ((var1 * var1 * static_cast<int64_t>(cal.p3)) / 256) +
           ((var1 * (static_cast<int64_t>(cal.p2) * 4096)));
    const int64_t var3 = 140737488355328LL;
    var1 = (var3 + var1) * static_cast<int64_t>(cal.p1) / 8589934592LL;

    uint32_t pressure;
    if (var1 != 0) {
        int64_t var4 = 1048576 - static_cast<int64_t>(raw.pressure);
        var4 = (((var4 * INT64_C(2147483648)) - var2) * 3125) / var1;
        var1 = (static_cast<int64_t>(cal.p9) * (var4 / 8192) * (var4 / 8192)) / 33554432;
        var2 = (static_cast<int64_t>(cal.p8) * var4) / 524288;
        var4 = ((var4 + var1 + var2) / 256) + (static_cast<int64_t>(cal.p7) * 16);
        pressure = static_cast<uint32_t>(((var4 / 2) * 100) / 128);

        if (pressure < 3000000) {
            pressure = 3000000;
        } else if (pressure > 11000000) {
            pressure = 11000000;
        }
    } else {
        pressure = 3000000;
    }
    return pressure;
}

// Bosch BME280_driver compensate_humidity(). Returns %RH x 1024.
uint32_t compensateHumidity(const RawData &raw, const Calibration &cal) {
    int32_t var1 = cal.t_fine - static_cast<int32_t>(76800);
    int32_t var2 = static_cast<int32_t>(raw.humidity * 16384);
    int32_t var3 = static_cast<int32_t>(cal.h4) * 1048576;
    int32_t var4 = static_cast<int32_t>(cal.h5) * var1;
    int32_t var5 = (((var2 - var3) - var4) + static_cast<int32_t>(16384)) / 32768;
    var2 = (var1 * static_cast<int32_t>(cal.h6)) / 1024;
    var3 = (var1 * static_cast<int32_t>(cal.h3)) / 2048;
    var4 = ((var2 * (var3 + static_cast<int32_t>(32768))) / 1024) +
           static_cast<int32_t>(2097152);
    var2 = ((var4 * static_cast<int32_t>(cal.h2)) + 8192) / 16384;
    var3 = var5 * var2;
    var4 = ((var3 / 32768) * (var3 / 32768)) / 128;
    var5 = var3 - ((var4 * static_cast<int32_t>(cal.h1)) / 16);
    var5 = (var5 < 0) ? 0 : var5;
    var5 = (var5 > 419430400) ? 419430400 : var5;
    uint32_t humidity = static_cast<uint32_t>(var5 / 4096);

    if (humidity > 102400) {
        humidity = 102400;
    }
    return humidity;
}

Reading compensate(const RawData &raw, Calibration &cal) {
    Reading out;
    // Order matters: pressure and humidity both consume t_fine.
    out.temperature_c = static_cast<float>(compensateTemperature(raw, cal)) / 100.0f;
    out.pressure_pa = static_cast<float>(compensatePressure(raw, cal)) / 100.0f;
    out.humidity_rh = static_cast<float>(compensateHumidity(raw, cal)) / 1024.0f;
    return out;
}

float saturationVapourPressure(float temp_c) {
    return kMagnusA * std::exp(kMagnusB * temp_c / (kMagnusC + temp_c));
}

float actualVapourPressure(float temp_c, float rh) {
    return saturationVapourPressure(temp_c) * rh / 100.0f;
}

float absoluteHumidity(float temp_c, float rh) {
    const float vp_hpa = actualVapourPressure(temp_c, rh);
    return kAbsHumidityFactor * vp_hpa / (temp_c + 273.15f);
}

float dewPoint(float temp_c, float rh) {
    if (rh <= 0.0f) {
        return -100.0f;
    }
    const float gamma = std::log(rh / 100.0f) + (kMagnusB * temp_c) / (kMagnusC + temp_c);
    return (kMagnusC * gamma) / (kMagnusB - gamma);
}

}  // namespace bme280
