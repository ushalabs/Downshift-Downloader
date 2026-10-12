#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace downshift {

// Time/byte inputs make decisions deterministic in tests; production uses steady_clock.
// Changes apply only to future assignments. Existing ranges always drain normally.
class AdaptiveController final {
public:
    AdaptiveController(std::uint64_t total, std::uint32_t maximum, bool automatic)
        : maximum_(maximum), automatic_(automatic) {
        if (maximum == 0 || maximum > 16) throw std::invalid_argument("Invalid connection limit.");
        const std::uint32_t initial = total <= 1024 * 1024 ? 1 : total < 32 * 1024 * 1024 ? 2 : 4;
        target_ = automatic ? std::min(initial, maximum) : maximum;
    }
    [[nodiscard]] std::uint32_t target() const noexcept { return target_; }
    [[nodiscard]] std::uint32_t changes() const noexcept { return changes_; }

    void observe(double now, std::uint64_t bytes, bool fullPool, bool resourceBusy = false) {
        if (!automatic_) return;
        if (!std::isfinite(now) || now < lastTime_ || bytes < lastBytes_) throw std::invalid_argument("Non-monotonic tuning sample.");
        // A tail, draining pool, or startup cannot be compared to a fully occupied pool.
        if (!fullPool) { lastTime_ = now; lastBytes_ = bytes; return; }
        const auto elapsed = now - lastTime_;
        if (elapsed < 1.0) return;
        const auto rate = static_cast<double>(bytes - lastBytes_) / elapsed;
        lastTime_ = now; lastBytes_ = bytes;
        if (resourceBusy) { reduce(); return; }
        if (settle_ != 0) { --settle_; return; }
        if (trial_) {
            trial_ = false;
            if (rate >= reference_ * 1.10 && rate > 0) {
                reference_ = rate; // Keep an expansion only with at least 10% measured gain.
            } else {
                setTarget(previous_); exploring_ = false; settle_ = 1; return;
            }
        } else if (reference_ > 0 && rate < reference_ * 0.75) {
            if (++slow_ >= 2) { reduce(); return; }
            return;
        } else {
            slow_ = 0;
            // A substantial capacity change allows a fresh trial after a plateau.
            if (!exploring_ && reference_ > 0 && rate > reference_ * 1.40) exploring_ = true;
            reference_ = reference_ == 0 ? rate : reference_ * 0.8 + rate * 0.2;
        }
        if (exploring_ && reference_ > 0 && target_ < maximum_) {
            previous_ = target_;
            setTarget(std::min(maximum_, target_ * 2));
            trial_ = true; settle_ = 1; // One full window to settle newly opened connections.
        }
    }

    [[nodiscard]] std::uint64_t chunkSize(double aggregateRate, double firstByteSeconds) const noexcept {
        constexpr std::uint64_t minimum = 256 * 1024, maximum = 16 * 1024 * 1024;
        if (!std::isfinite(aggregateRate) || aggregateRate <= 0) return 4 * 1024 * 1024;
        const auto seconds = std::clamp(0.5 + 4 * std::max(0.0, firstByteSeconds), 0.5, 2.0);
        const auto bytes = std::clamp(aggregateRate / target_ * seconds,
                                      static_cast<double>(minimum), static_cast<double>(maximum));
        return std::min(maximum, (static_cast<std::uint64_t>(bytes) + minimum - 1) / minimum * minimum);
    }
private:
    void setTarget(std::uint32_t value) noexcept { if (target_ != value) { target_ = value; ++changes_; } }
    void reduce() noexcept {
        setTarget(std::max(1U, target_ / 2));
        reference_ = 0; slow_ = 0; trial_ = false; exploring_ = false; settle_ = 1;
    }
    std::uint32_t maximum_, target_ = 1, previous_ = 1, changes_ = 0, settle_ = 0, slow_ = 0;
    bool automatic_, trial_ = false, exploring_ = true;
    double lastTime_ = 0, reference_ = 0;
    std::uint64_t lastBytes_ = 0;
};

} // namespace downshift
