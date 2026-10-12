#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>

namespace downshift {
struct ByteRange {
    std::uint64_t first;
    std::uint64_t last;
    [[nodiscard]] std::uint64_t size() const noexcept { return last - first + 1; }
};

// Fixed chunk policy for Phase 2. Lazy allocation bounds memory independently of file size.
// The engine assigns each range once; completed handles take the next range.
class SegmentScheduler final {
public:
    SegmentScheduler(std::uint64_t total, std::uint64_t chunk) : total_(total), chunk_(chunk) {
        if (chunk == 0) throw std::invalid_argument("Chunk size must be positive.");
    }
    [[nodiscard]] std::optional<ByteRange> next() noexcept {
        if (next_ == total_) return std::nullopt;
        const auto count = (total_ - next_ < chunk_) ? total_ - next_ : chunk_;
        const ByteRange range{next_, next_ + count - 1};
        next_ += count;
        return range;
    }
private:
    std::uint64_t total_, chunk_, next_ = 0;
};
} // namespace downshift
