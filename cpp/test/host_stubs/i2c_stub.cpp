#include "hardware/i2c.h"

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#ifndef PICO_ERROR_GENERIC
#define PICO_ERROR_GENERIC (-1)
#endif

namespace {

constexpr uint8_t kSimAddress = 0x76;

struct Sim {
    std::map<uint8_t, uint8_t> regs;
    uint8_t pointer = 0;
    int measuring_cycles = 0;
    bool bus_stuck = false;
    std::vector<uint8_t> written;
};

Sim g_sim;

void loadGroundTruthCalibration() {
    // Calibration bank captured from the real module attached to the Pico 2:
    // T1=28651 T2=26756 T3=50
    // P1..P9 = 37438,-10736,3024,10578,-225,-7,9900,-10230,4285
    // H1=75 H2=373 H3=0 H4=292 H5=50 H6=30
    const std::pair<uint8_t, uint8_t> cal[] = {
        {0x88, 0xEB}, {0x89, 0x6F}, {0x8A, 0x84}, {0x8B, 0x68},
        {0x8C, 0x32}, {0x8D, 0x00}, {0x8E, 0x3E}, {0x8F, 0x92},
        {0x90, 0x10}, {0x91, 0xD6}, {0x92, 0xD0}, {0x93, 0x0B},
        {0x94, 0x52}, {0x95, 0x29}, {0x96, 0x1F}, {0x97, 0xFF},
        {0x98, 0xF9}, {0x99, 0xFF}, {0x9A, 0xAC}, {0x9B, 0x26},
        {0x9C, 0x0A}, {0x9D, 0xD8}, {0x9E, 0xBD}, {0x9F, 0x10},
        {0xA1, 0x4B}, {0xE1, 0x75}, {0xE2, 0x01}, {0xE3, 0x00},
        {0xE4, 0x12}, {0xE5, 0x24}, {0xE6, 0x03}, {0xE7, 0x1E},
    };
    for (const auto &kv : cal) {
        g_sim.regs[kv.first] = kv.second;
    }
}

}  // namespace

i2c_inst_t g_i2c0;
i2c_inst_t *i2c0 = &g_i2c0;

void simReset() {
    g_sim = Sim{};
    g_sim.regs[0xD0] = 0x60;  // chip ID
    g_sim.regs[0xF3] = 0x00;  // status: not measuring
    loadGroundTruthCalibration();
}

void simSetChipId(uint8_t id) { g_sim.regs[0xD0] = id; }

void simSetAdc(uint32_t adc_t, uint32_t adc_p, uint32_t adc_h) {
    g_sim.regs[0xF7] = static_cast<uint8_t>(adc_p >> 12);
    g_sim.regs[0xF8] = static_cast<uint8_t>(adc_p >> 4);
    g_sim.regs[0xF9] = static_cast<uint8_t>(adc_p << 4);
    g_sim.regs[0xFA] = static_cast<uint8_t>(adc_t >> 12);
    g_sim.regs[0xFB] = static_cast<uint8_t>(adc_t >> 4);
    g_sim.regs[0xFC] = static_cast<uint8_t>(adc_t << 4);
    g_sim.regs[0xFD] = static_cast<uint8_t>(adc_h >> 8);
    g_sim.regs[0xFE] = static_cast<uint8_t>(adc_h & 0xFF);
}

void simSetMeasuringCycles(int cycles) { g_sim.measuring_cycles = cycles; }

void simSetBusStuck(bool stuck) { g_sim.bus_stuck = stuck; }

uint8_t simReg(uint8_t addr) {
    auto it = g_sim.regs.find(addr);
    return it == g_sim.regs.end() ? 0 : it->second;
}

std::vector<uint8_t> simWrittenRegisters() { return g_sim.written; }

int i2c_write_blocking(i2c_inst_t *i2c, uint8_t addr, const uint8_t *data,
                       size_t len, bool nostop) {
    (void)nostop;
    if (g_sim.bus_stuck) {
        return PICO_ERROR_TIMEOUT;
    }
    if (i2c != i2c0) {
        return PICO_ERROR_GENERIC;
    }
    if (addr != kSimAddress) {
        return PICO_ERROR_GENERIC;  // NACK
    }
    if (len == 0) {
        return 0;  // address probe only
    }
    // First byte is the register pointer; remaining bytes are data writes.
    g_sim.pointer = data[0];
    for (size_t i = 1; i < len; ++i) {
        g_sim.regs[static_cast<uint8_t>(g_sim.pointer + i - 1)] = data[i];
        g_sim.written.push_back(data[i]);
    }
    return static_cast<int>(len);
}

int i2c_read_blocking(i2c_inst_t *i2c, uint8_t addr, uint8_t *data,
                      size_t len, bool nostop) {
    (void)nostop;
    if (g_sim.bus_stuck) {
        return PICO_ERROR_TIMEOUT;
    }
    if (i2c != i2c0 || addr != kSimAddress) {
        return PICO_ERROR_GENERIC;
    }
    for (size_t i = 0; i < len; ++i) {
        const uint8_t reg = static_cast<uint8_t>(g_sim.pointer + i);
        uint8_t value = 0;
        auto it = g_sim.regs.find(reg);
        if (it != g_sim.regs.end()) {
            value = it->second;
        }
        if (reg == 0xF3 && g_sim.measuring_cycles > 0) {
            value |= 0x03;
            if (i + 1 == len) {
                --g_sim.measuring_cycles;
            }
        }
        data[i] = value;
    }
    g_sim.pointer = static_cast<uint8_t>(g_sim.pointer + static_cast<int>(len));
    return static_cast<int>(len);
}

// Bounded variants used by the driver. The simulation never actually blocks, so
// a "timeout" is modelled by the stuck-bus flag.
int i2c_write_timeout_us(i2c_inst_t *i2c, uint8_t addr, const uint8_t *data,
                         size_t len, bool nostop, uint32_t timeout_us) {
    (void)timeout_us;
    return i2c_write_blocking(i2c, addr, data, len, nostop);
}

int i2c_read_timeout_us(i2c_inst_t *i2c, uint8_t addr, uint8_t *data,
                        size_t len, bool nostop, uint32_t timeout_us) {
    (void)timeout_us;
    return i2c_read_blocking(i2c, addr, data, len, nostop);
}
