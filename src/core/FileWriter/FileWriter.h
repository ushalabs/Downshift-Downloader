#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace downshift {
struct FileMetrics {
    std::uint64_t writeBytes = 0;
    std::uint64_t writeCalls = 0, checksumReadBytes = 0, checksumReadCalls = 0;
    double writeSeconds = 0, allocationSeconds = 0, checksumSeconds = 0, flushSeconds = 0;
};
enum class FileOpenMode { CreatePartial, ReopenPartial, InspectCompleted };

// Owned by one engine worker; positional writes are serialized on that worker.
class FileWriter final {
public:
    explicit FileWriter(const std::filesystem::path& destination, bool reopen = false);
    FileWriter(const std::filesystem::path& destination, FileOpenMode mode);
    [[nodiscard]] static std::filesystem::path validatedDestination(const std::filesystem::path& destination);
    ~FileWriter();
    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;
    void preallocate(std::uint64_t bytes, bool sparse = false);
    void reset();
    void writeAt(std::uint64_t offset, const char* data, std::size_t bytes);
    [[nodiscard]] std::uint64_t size() const;
    [[nodiscard]] std::string sha256();
    [[nodiscard]] std::string sha256Range(std::uint64_t offset, std::uint64_t bytes);
    [[nodiscard]] std::string identity() const;
    void flush();
    [[nodiscard]] const FileMetrics& metrics() const noexcept { return metrics_; }
    // Flush and rename the open file by handle, without replacing an existing destination.
    void commit(std::uint64_t expectedSize, const std::optional<std::string>& expectedSha256 = {});
private:
    void* handle_ = nullptr;
    std::filesystem::path destination_;
    FileMetrics metrics_;
    // Reused bounded heap storage; large verification buffers never occupy the thread stack.
    std::vector<unsigned char> checksumBuffer_;
};

} // namespace downshift
