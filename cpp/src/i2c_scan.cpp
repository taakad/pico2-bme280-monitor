#include "i2c_scan.hpp"

namespace i2cscan {
namespace {

// A probe must not take longer than this, or an unpowered or miswired device
// holding the bus low would stall the whole scan.
constexpr uint32_t kProbeTimeoutUs = 20000;

}  // namespace

size_t scan(i2c_inst_t *i2c, uint8_t *found, size_t capacity, void (*kick)()) {
    size_t count = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
        if (kick != nullptr) {
            kick();
        }

        // A zero-length write cannot be used as a probe: pico-sdk's write loop
        // body never executes for len == 0, so nothing is transmitted and the
        // call reports success for every address on the bus. A one-byte read
        // does exercise the address phase, and because it carries no register
        // pointer it cannot disturb a device's configuration.
        uint8_t probe = 0;
        if (i2c_read_timeout_us(i2c, addr, &probe, 1, false, kProbeTimeoutUs) >= 0) {
            if (found != nullptr && count < capacity) {
                found[count++] = addr;
            }
        }
    }
    return count;
}

}  // namespace i2cscan
