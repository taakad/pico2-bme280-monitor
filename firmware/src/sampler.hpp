#ifndef MONITOR_SAMPLER_HPP
#define MONITOR_SAMPLER_HPP

#include <cstddef>
#include <cstdint>

#include "bme280_math.hpp"

// Moving average over raw ADC samples. Averaging the ADC values (rather than
// the compensated physical values) matches the sensor's noise behaviour and
// keeps the compensation math untouched.
namespace monitor {

class RawAverager {
public:
    static constexpr size_t kMaxWindow = 64;

    // window == 1 disables averaging.
    void reset(size_t window);
    size_t window() const { return window_; }

    void push(const bme280::RawData &raw);
    bool empty() const { return count_ == 0; }
    size_t count() const { return count_; }

    // Averages only the channels that have samples, so a channel the sensor
    // never filled cannot drag the result toward zero.
    bme280::RawData average() const;

private:
    bme280::RawData items_[kMaxWindow]{};
    size_t window_ = 1;
    size_t head_ = 0;
    size_t count_ = 0;
};

}  // namespace monitor

#endif  // MONITOR_SAMPLER_HPP
