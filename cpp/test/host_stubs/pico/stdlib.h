#ifndef HOST_STUB_PICO_STDLIB_H
#define HOST_STUB_PICO_STDLIB_H

// Host-side stub for the pico/stdlib.h symbols the driver uses. Sleeps are
// no-ops so the status-polling loop runs instantly under test.
#include <cstdint>

inline void sleep_ms(uint32_t) {}
inline void sleep_us(uint64_t) {}

#endif  // HOST_STUB_PICO_STDLIB_H
