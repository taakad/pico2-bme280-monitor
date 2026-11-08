#ifndef MONITOR_CONFIG_HPP
#define MONITOR_CONFIG_HPP

#include <cstddef>
#include <cstdint>

// Runtime-tunable measurement settings. Deliberately free of pico-sdk headers
// so the parser can be unit-tested on a host compiler.
namespace monitor {

struct Config {
    uint32_t interval_ms = 2000;  // time between samples
    uint8_t decimals = 2;         // digits after the decimal point in output
    uint8_t average = 1;          // moving-average window over raw ADC values
    // Oversampling factors, stored as the human value (1, 2, 4, 8, 16).
    uint8_t osr_t = 1;
    uint8_t osr_p = 1;
    uint8_t osr_h = 1;
    uint8_t filter = 0;  // IIR filter: 0=off, 1=x2, 2=x4, 3=x8, 4=x16
    bool forced = false;  // false = normal (continuous) mode
    // true emits each sample as one JSON object per line, which is what the
    // collector service consumes; false keeps the human-readable column format.
    bool json = false;
};

enum class ParseResult {
    Ok,
    UnknownKey,
    BadValue,
};

// Applies "key=value" to cfg. On failure cfg is left untouched and *message
// describes the problem. Keys use dotted form for the oversampling trio, e.g.
// "osr.p=4".
ParseResult applySetting(Config *cfg, const char *key, const char *value,
                         const char **message);

// Writes a machine-readable snapshot ("interval=2000 decimals=2 ...") into buf.
// Returns the buffer for chaining.
const char *describe(const Config &cfg, char *buf, size_t size);

// Maps the human oversampling value to the BME280 3-bit field. Returns 0 for
// an invalid value.
uint8_t osrField(uint8_t osr);

}  // namespace monitor

#endif  // MONITOR_CONFIG_HPP
