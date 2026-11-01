#include "bme280.hpp"

#include "hardware/i2c.h"
#include "pico/stdlib.h"

namespace bme280 {
namespace {

constexpr uint8_t kStatusMeasuringMask = 0x03;
// pico-sdk's plain blocking calls wait forever if the bus is stuck (missing
// pull-ups, wrong pins, unpowered sensor), which wedges the whole program.
// Every transfer is bounded instead.
constexpr uint kBusTimeoutUs = 50000;

}  // namespace

uint8_t Sensor::readRegister(uint8_t reg) {
    uint8_t value = 0;
    if (i2c_write_timeout_us(i2c_, address_, &reg, 1, true, kBusTimeoutUs) < 0) {
        return 0;
    }
    if (i2c_read_timeout_us(i2c_, address_, &value, 1, false, kBusTimeoutUs) < 0) {
        return 0;
    }
    return value;
}

void Sensor::writeRegister(uint8_t reg, uint8_t value) {
    const uint8_t payload[2] = {reg, value};
    i2c_write_timeout_us(i2c_, address_, payload, sizeof(payload), false, kBusTimeoutUs);
}

bool Sensor::readRegisters(uint8_t reg, uint8_t *out, size_t len) {
    if (i2c_write_timeout_us(i2c_, address_, &reg, 1, true, kBusTimeoutUs) < 0) {
        return false;
    }
    return i2c_read_timeout_us(i2c_, address_, out, static_cast<int>(len), false,
                               kBusTimeoutUs) >= 0;
}

bool Sensor::probe() const {
    uint8_t reg = REG_CHIP_ID;
    uint8_t id = 0;
    if (i2c_write_timeout_us(i2c_, address_, &reg, 1, true, kBusTimeoutUs) < 0) {
        return false;
    }
    if (i2c_read_timeout_us(i2c_, address_, &id, 1, false, kBusTimeoutUs) < 0) {
        return false;
    }
    return id == CHIP_ID_BME280;
}

bool Sensor::readCalibration() {
    // 0x88..0xA1 covers T1..P9 plus H1 at 0xA1 (26 bytes).
    uint8_t tp[26];
    if (!readRegisters(REG_CAL_TP, tp, sizeof(tp))) {
        return false;
    }
    cal_.t1 = static_cast<uint16_t>((tp[1] << 8) | tp[0]);
    cal_.t2 = static_cast<int16_t>((tp[3] << 8) | tp[2]);
    cal_.t3 = static_cast<int16_t>((tp[5] << 8) | tp[4]);
    cal_.p1 = static_cast<uint16_t>((tp[7] << 8) | tp[6]);
    cal_.p2 = static_cast<int16_t>((tp[9] << 8) | tp[8]);
    cal_.p3 = static_cast<int16_t>((tp[11] << 8) | tp[10]);
    cal_.p4 = static_cast<int16_t>((tp[13] << 8) | tp[12]);
    cal_.p5 = static_cast<int16_t>((tp[15] << 8) | tp[14]);
    cal_.p6 = static_cast<int16_t>((tp[17] << 8) | tp[16]);
    cal_.p7 = static_cast<int16_t>((tp[19] << 8) | tp[18]);
    cal_.p8 = static_cast<int16_t>((tp[21] << 8) | tp[20]);
    cal_.p9 = static_cast<int16_t>((tp[23] << 8) | tp[22]);
    cal_.h1 = tp[25];

    // 0xE1..0xE7 covers H2..H6.
    uint8_t h[7];
    if (!readRegisters(REG_CAL_H, h, sizeof(h))) {
        return false;
    }
    cal_.h2 = static_cast<int16_t>((h[1] << 8) | h[0]);
    cal_.h3 = static_cast<int8_t>(h[2]);
    // H4/H5 are split across 0xE4 and 0xE5.
    cal_.h4 = static_cast<int16_t>(static_cast<int8_t>(h[3]) * 16) | (h[4] & 0x0F);
    cal_.h5 = static_cast<int16_t>(static_cast<int8_t>(h[5]) * 16) | (h[4] >> 4);
    cal_.h6 = static_cast<int8_t>(h[6]);

    // A zeroed T1 or P1 means the calibration bank never answered: every
    // derived value would be garbage and P1 would divide by zero.
    return cal_.t1 != 0 && cal_.p1 != 0;
}

bool Sensor::begin() {
    if (readRegister(REG_CHIP_ID) != CHIP_ID_BME280) {
        return false;
    }

    writeRegister(REG_RESET, CMD_SOFT_RESET);
    sleep_ms(10);  // datasheet: >=1 ms after reset before further access

    if (!readCalibration()) {
        return false;
    }

    // ctrl_hum must be written before ctrl_meas for the setting to latch.
    writeRegister(REG_CTRL_HUM, OSR_X1);
    // config: standby 1000 ms, IIR filter x16.
    writeRegister(REG_CONFIG, 0xA0 | (0x04 << 2));
    // ctrl_meas: T x1, P x1, normal (continuous) mode.
    writeRegister(REG_CTRL_MEAS, (OSR_X1 << 5) | (OSR_X1 << 2) | MODE_NORMAL);

    sleep_ms(100);
    return true;
}

bool Sensor::read(RawData *raw) {
    if (raw == nullptr) {
        return false;
    }

    // Wait for any in-flight conversion to complete.
    bool done = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        if ((readRegister(REG_STATUS) & kStatusMeasuringMask) == 0) {
            done = true;
            break;
        }
        sleep_ms(1);
    }
    if (!done) {
        return false;
    }

    // Pressure first: 0xF7 press[3], 0xFA temp[3], 0xFD hum[2] are contiguous.
    uint8_t data[8];
    if (!readRegisters(REG_PRESS_MSB, data, sizeof(data))) {
        return false;
    }
    raw->pressure = (static_cast<uint32_t>(data[0]) << 12) |
                    (static_cast<uint32_t>(data[1]) << 4) |
                    (data[2] >> 4);
    raw->temperature = (static_cast<uint32_t>(data[3]) << 12) |
                       (static_cast<uint32_t>(data[4]) << 4) |
                       (data[5] >> 4);
    raw->humidity = (static_cast<uint32_t>(data[6]) << 8) | data[7];
    return true;
}

}  // namespace bme280
