#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>
#include <utility>

namespace downshift {
struct ByteRange {
    std::uint64_t first;
    std::uint64_t last;
    [[nodiscard]] std::uint64_t size() const noexcept { return last - first + 1; }
};

// Lazy allocation bounds memory independently of file size; sizing affects only unassigned bytes.
// The engine assigns each range once; completed handles take the next range.
class SegmentScheduler final {
public:
    SegmentScheduler(std::uint64_t total, std::uint64_t chunk, std::vector<ByteRange> completed = {})
        : total_(total), chunk_(chunk), remaining_(total), completed_(std::move(completed)) {
        if (chunk == 0) throw std::invalid_argument("Chunk size must be positive.");
        std::uint64_t end = 0;
        for (const auto& r : completed_) {
            if (r.first < end || r.last < r.first || r.last >= total) throw std::invalid_argument("Invalid completed ranges.");
            end = r.last + 1; remaining_ -= r.size();
        }
    }
    void setChunkSize(std::uint64_t chunk) {
        if (chunk == 0) throw std::invalid_argument("Chunk size must be positive.");
        chunk_ = chunk;
    }
    [[nodiscard]] std::uint64_t remaining() const noexcept { return remaining_; }
    [[nodiscard]] std::optional<ByteRange> next() noexcept {
        while (completedIndex_ < completed_.size() && next_ == completed_[completedIndex_].first)
            next_ = completed_[completedIndex_++].last + 1;
        if (next_ == total_) return std::nullopt;
        const auto end = completedIndex_ < completed_.size() ? completed_[completedIndex_].first : total_;
        const auto count = std::min(end - next_, chunk_);
        const ByteRange range{next_, next_ + count - 1};
        next_ += count;
        remaining_ -= count;
        return range;
    }
private:
    std::uint64_t total_, chunk_, next_ = 0;
    std::uint64_t remaining_;
    std::vector<ByteRange> completed_;
    std::size_t completedIndex_ = 0;
};
} // namespace downshift
