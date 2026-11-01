#ifndef BME280_MATH_HPP
#define BME280_MATH_HPP

#include <cstdint>

// Pure, hardware-independent BME280 math. Split out from the I2C driver so it
// can be unit-tested on a host compiler.
namespace bme280 {

struct Calibration {
    uint16_t t1;
    int16_t  t2, t3;
    uint16_t p1;
    int16_t  p2, p3, p4, p5, p6, p7, p8, p9;
    uint8_t  h1;
    int16_t  h2;
    int8_t   h3;
    int16_t  h4, h5;
    int8_t   h6;
    // Linearization state, valid after compensateTemperature().
    int32_t  t_fine = 0;
};

struct RawData {
    uint32_t temperature = 0;  // 20-bit
    uint32_t pressure    = 0;  // 20-bit
    uint32_t humidity    = 0;  // 16-bit
};

struct Reading {
    float temperature_c = 0.0f;  // degrees Celsius
    float pressure_pa   = 0.0f;  // Pascals
    float humidity_rh   = 0.0f;  // percent relative humidity
};

// Bosch BME280_driver reference compensation. Updates cal.t_fine, which the
// pressure and humidity compensation both consume, so temperature must run
// first.
int32_t  compensateTemperature(const RawData &raw, Calibration &cal);   // centi-deg C
uint32_t compensatePressure(const RawData &raw, const Calibration &cal);  // Pa x 100
uint32_t compensateHumidity(const RawData &raw, const Calibration &cal);  // %RH x 1024

// Convenience wrapper applying all three in the correct order.
Reading compensate(const RawData &raw, Calibration &cal);

// Derived quantities (Magnus/Tetens). Constants require exp(), not 10**x.
float saturationVapourPressure(float temp_c);             // hPa
float actualVapourPressure(float temp_c, float rh);       // hPa
float absoluteHumidity(float temp_c, float rh);           // g/m^3
float dewPoint(float temp_c, float rh);                   // degrees Celsius

}  // namespace bme280

#endif  // BME280_MATH_HPP
