#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>

namespace downshift {

enum class DownloadErrorCode { InvalidRequest, Http, Protocol, Integrity, Network, Timeout, Tls, File, Cancelled };

class DownloadError final : public std::runtime_error {
public:
    DownloadError(DownloadErrorCode code, const std::string& message)
        : std::runtime_error(message), code_(code) {}
    [[nodiscard]] DownloadErrorCode code() const noexcept { return code_; }
private:
    DownloadErrorCode code_;
};

struct DownloadOptions {
    std::string url;
    std::filesystem::path destination;
    std::optional<std::string> expectedSha256;
    std::filesystem::path caBundle; // Optional explicit trust bundle; TLS verification stays enabled.
    long connectTimeoutMs = 10'000;
    long transferTimeoutMs = 0; // Zero uses the stall timeout instead of a whole-transfer deadline.
    std::uint32_t maxConnections = 4;
    std::uint64_t chunkBytes = 4 * 1024 * 1024;
};

struct DownloadResult {
    std::uint64_t bytes = 0;
    std::uint64_t completedRanges = 0;
    bool segmented = false;
    bool fellBack = false;
    std::string suggestedFilename;
};

class DownloadEngine final {
public:
    // Blocking worker API. Call off the GUI thread. CurlRuntime must outlive this call.
    // Failure leaves a .part file for inspection, never a completed destination.
    [[nodiscard]] DownloadResult download(const DownloadOptions& options,
                                         std::stop_token stop = {}) const;
};

} // namespace downshift
