#ifndef BME280_HPP
#define BME280_HPP

#include <cstddef>
#include <cstdint>

#include <hardware/i2c.h>

#include "bme280_math.hpp"

// BME280 driver for Raspberry Pi Pico 2 (RP2350) using pico-sdk.
namespace bme280 {

// Register map
constexpr uint8_t REG_CHIP_ID     = 0xD0;
constexpr uint8_t REG_RESET       = 0xE0;
constexpr uint8_t REG_HUM_MSB     = 0xFD;
constexpr uint8_t REG_PRESS_MSB   = 0xF7;
constexpr uint8_t REG_TEMP_MSB    = 0xFA;
constexpr uint8_t REG_STATUS      = 0xF3;
constexpr uint8_t REG_CTRL_HUM    = 0xF2;
constexpr uint8_t REG_CTRL_MEAS   = 0xF4;
constexpr uint8_t REG_CONFIG      = 0xF5;
constexpr uint8_t REG_CAL_TP      = 0x88;  // 26 bytes: 0x88..0xA1
constexpr uint8_t REG_CAL_H       = 0xE1;  // 7 bytes:  0xE1..0xE7
constexpr uint8_t CMD_SOFT_RESET  = 0xB6;
constexpr uint8_t CHIP_ID_BME280  = 0x60;

// Oversampling / mode encodings
constexpr uint8_t OSR_X1      = 0x01;
constexpr uint8_t MODE_FORCED = 0x01;
constexpr uint8_t MODE_NORMAL = 0x03;

class Sensor {
public:
    Sensor(i2c_inst_t *i2c, uint8_t address) : i2c_(i2c), address_(address) {}

    // Verifies chip ID, soft-resets, reads calibration and starts normal mode.
    bool begin();

    // Blocking read: waits for the in-flight conversion to finish, then reads
    // 0xF7..0xFE (pressure, temperature) and 0xFD..0xFE (humidity) in one burst.
    bool read(RawData *raw);

    const Calibration &calibration() const { return cal_; }
    // Non-const because compensation updates the t_fine linearization state.
    Calibration &calibration() { return cal_; }
    bool probe() const;

private:
    uint8_t readRegister(uint8_t reg);
    void writeRegister(uint8_t reg, uint8_t value);
    bool readRegisters(uint8_t reg, uint8_t *out, size_t len);
    bool readCalibration();

    i2c_inst_t *i2c_;
    uint8_t address_;
    Calibration cal_{};
};

}  // namespace bme280

#endif  // BME280_HPP
