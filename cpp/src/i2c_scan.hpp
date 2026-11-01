#ifndef I2C_SCAN_HPP
#define I2C_SCAN_HPP

#include <cstddef>
#include <cstdint>

#include "hardware/i2c.h"

namespace i2cscan {

// 0x08..0x77 inclusive.
constexpr size_t kProbedAddressCount = 112;

// Probes every 7-bit address and returns how many acknowledged, writing the
// acknowledging addresses into `found` (at most `capacity` of them).
//
// `kick` is called before each probe; the firmware passes watchdog_update() so
// a full scan can never outlive the watchdog.
size_t scan(i2c_inst_t *i2c, uint8_t *found, size_t capacity, void (*kick)());

}  // namespace i2cscan

#endif  // I2C_SCAN_HPP
