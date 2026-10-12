#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace downshift {
struct SpeedSample {
    double instantaneous = 0, smoothed = 0, average = 0, peak = 0, elapsed = 0;
};

class SpeedTracker final {
public:
    [[nodiscard]] std::optional<SpeedSample> sample(std::uint64_t bytes, double seconds, bool force = false) {
        if (!std::isfinite(seconds) || seconds < lastTime_ || bytes < lastBytes_)
            throw std::invalid_argument("Non-monotonic speed sample.");
        const auto interval = seconds - lastTime_;
        if (interval <= 0 || (!force && interval < 0.25)) return std::nullopt;
        const auto rate = static_cast<double>(bytes - lastBytes_) / interval;
        const auto alpha = 1 - std::exp(-interval / 2.0);
        smoothed_ = initialized_ ? smoothed_ + alpha * (rate - smoothed_) : rate;
        initialized_ = true;
        if (interval >= 0.25 && rate > 0) {
            peak_ = fullPositiveWindow_ ? std::max(peak_, rate) : rate;
            fullPositiveWindow_ = true;
        } else if (!fullPositiveWindow_) peak_ = std::max(peak_, rate);
        lastTime_ = seconds; lastBytes_ = bytes;
        return SpeedSample{rate, smoothed_, static_cast<double>(bytes) / seconds, peak_, seconds};
    }
    [[nodiscard]] double smoothed() const noexcept { return smoothed_; }
    [[nodiscard]] double peak() const noexcept { return peak_; }
private:
    double lastTime_ = 0, smoothed_ = 0, peak_ = 0;
    std::uint64_t lastBytes_ = 0;
    bool initialized_ = false;
    bool fullPositiveWindow_ = false;
};
} // namespace downshift
