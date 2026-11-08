#ifndef HOST_STUB_HARDWARE_I2C_H
#define HOST_STUB_HARDWARE_I2C_H

// Host-side stub of pico-sdk's hardware/i2c.h, backed by a simulated BME280
// register file. Lets the real driver code (register addresses, calibration
// parsing, output ordering, status polling) be unit-tested without hardware.
#include <cstdint>
#include <cstddef>
#include <vector>

struct i2c_inst_t {
    int dummy = 0;
};

extern i2c_inst_t *i2c0;

// pico-sdk's error code for a transfer that did not complete in time.
constexpr int PICO_ERROR_TIMEOUT = -1;

// Simulation control, manipulated by the tests.
void simReset();
void simSetChipId(uint8_t id);
void simSetAdc(uint32_t adc_t, uint32_t adc_p, uint32_t adc_h);
// Number of consecutive status reads that report "measuring" before clearing.
void simSetMeasuringCycles(int cycles);
// Make every transfer fail as if the bus were stuck low.
void simSetBusStuck(bool stuck);
uint8_t simReg(uint8_t addr);
std::vector<uint8_t> simWrittenRegisters();
// Register addresses in write order; the only reliable way to assert that one
// register was written before another.
std::vector<uint8_t> simWriteOrder();

int i2c_write_blocking(i2c_inst_t *i2c, uint8_t addr, const uint8_t *data,
                       size_t len, bool nostop);
int i2c_read_blocking(i2c_inst_t *i2c, uint8_t addr, uint8_t *data,
                      size_t len, bool nostop);
// Bounded variants: identical behaviour, but they report PICO_ERROR_TIMEOUT
// instead of waiting forever when the bus is stuck.
int i2c_write_timeout_us(i2c_inst_t *i2c, uint8_t addr, const uint8_t *data,
                         size_t len, bool nostop, uint32_t timeout_us);
int i2c_read_timeout_us(i2c_inst_t *i2c, uint8_t addr, uint8_t *data,
                        size_t len, bool nostop, uint32_t timeout_us);

#endif  // HOST_STUB_HARDWARE_I2C_H
