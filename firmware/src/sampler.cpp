#include "sampler.hpp"

namespace monitor {

void RawAverager::reset(size_t window) {
    if (window < 1) {
        window = 1;
    }
    if (window > kMaxWindow) {
        window = kMaxWindow;
    }
    window_ = window;
    head_ = 0;
    count_ = 0;
}

void RawAverager::push(const bme280::RawData &raw) {
    items_[head_] = raw;
    head_ = (head_ + 1) % window_;
    if (count_ < window_) {
        ++count_;
    }
}

bme280::RawData RawAverager::average() const {
    bme280::RawData out{};
    if (count_ == 0) {
        return out;
    }

    // 64-bit accumulators: 64 samples of a 20-bit ADC fit comfortably, and
    // rounding half up keeps the result unbiased.
    uint64_t t = 0, p = 0, h = 0;
    size_t t_n = 0, p_n = 0, h_n = 0;
    for (size_t i = 0; i < count_; ++i) {
        const bme280::RawData &s = items_[i];
        if (s.temperature != 0) {
            t += s.temperature;
            ++t_n;
        }
        if (s.pressure != 0) {
            p += s.pressure;
            ++p_n;
        }
        if (s.humidity != 0) {
            h += s.humidity;
            ++h_n;
        }
    }
    if (t_n) {
        out.temperature = static_cast<uint32_t>((t + t_n / 2) / t_n);
    }
    if (p_n) {
        out.pressure = static_cast<uint32_t>((p + p_n / 2) / p_n);
    }
    if (h_n) {
        out.humidity = static_cast<uint32_t>((h + h_n / 2) / h_n);
    }
    return out;
}

}  // namespace monitor
