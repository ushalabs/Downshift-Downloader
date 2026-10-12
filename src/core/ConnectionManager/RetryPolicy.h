#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>

namespace downshift {
// Bounded exponential backoff with caller-supplied jitter. Server delays are a
// lower bound; an excessive Retry-After refuses retry rather than violating it.
inline std::optional<std::uint32_t> retryDelay(std::uint32_t attempt, std::uint32_t baseMs,
    std::uint32_t maximumMs, std::uint64_t serverSeconds, std::uint32_t random) noexcept {
    if (serverSeconds > maximumMs / 1000) return std::nullopt;
    const auto exponential = std::min<std::uint64_t>(maximumMs, static_cast<std::uint64_t>(baseMs) << std::min(attempt, 16U));
    const auto jittered = exponential / 2 + random % (exponential / 2 + 1);
    return static_cast<std::uint32_t>(std::max(jittered, serverSeconds * 1000));
}
} // namespace downshift
