#pragma once

#include "core/SegmentScheduler/SegmentScheduler.h"
#include <filesystem>
#include <string>
#include <vector>

namespace downshift {
enum class RecoveryStatus : std::uint64_t { Active, Paused, Failed, Finalizing };
struct SavedRange { ByteRange range; std::string sha256; };
struct RecoveryRecord {
    std::string id, url, resolvedUrl, destination, fileIdentity, etag, lastModified, expectedSha256, finalSha256;
    std::uint64_t total = 0, created = 0, updated = 0;
    bool resumable = false;
    RecoveryStatus status = RecoveryStatus::Active;
    std::vector<SavedRange> completed;
};

// User-scoped DPAPI encryption; data is flushed before this ledger is published.
// One exclusive lease serializes creation, resume, restart and metadata replacement.
class ResumeJournal final {
public:
    explicit ResumeJournal(const std::filesystem::path& destination);
    ~ResumeJournal();
    ResumeJournal(const ResumeJournal&) = delete;
    ResumeJournal& operator=(const ResumeJournal&) = delete;
    [[nodiscard]] bool exists() const;
    [[nodiscard]] RecoveryRecord load() const;
    void save(RecoveryRecord& record);
    [[nodiscard]] bool remove() noexcept;
    // Explicit restart archives old partial/ledger; it never silently overwrites them.
    void archivePrevious();
    [[nodiscard]] static std::string newId();
    [[nodiscard]] static std::string pathText(const std::filesystem::path& path);
    static void validate(const RecoveryRecord& record);
private:
    std::filesystem::path destination_, path_;
    void* lease_ = nullptr;
};
} // namespace downshift
