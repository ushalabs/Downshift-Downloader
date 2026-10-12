#pragma once

#include <cstdint>
#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>

namespace downshift {

enum class DownloadErrorCode { InvalidRequest, Http, Protocol, Integrity, Network, Timeout, Tls, File, Cancelled, Paused, ResumeUnsafe };

class DownloadError final : public std::runtime_error {
public:
    DownloadError(DownloadErrorCode code, const std::string& message)
        : std::runtime_error(message), code_(code) {}
    [[nodiscard]] DownloadErrorCode code() const noexcept { return code_; }
private:
    DownloadErrorCode code_;
};

enum class ExistingDownload { New, Resume, Restart };
class DownloadControl final {
public:
    void requestPause() noexcept { paused_.store(true); }
    [[nodiscard]] bool pauseRequested() const noexcept { return paused_.load(); }
private:
    std::atomic_bool paused_ = false;
};

struct DownloadOptions {
    std::string url;
    std::filesystem::path destination;
    std::optional<std::string> expectedSha256;
    std::string requiredEtag; // Optional browser preflight binding; never bypasses validation.
    std::string requiredResolvedUrl;
    std::optional<std::uint64_t> requiredTotal;
    std::filesystem::path caBundle; // Optional explicit trust bundle; TLS verification stays enabled.
    long connectTimeoutMs = 10'000;
    long transferTimeoutMs = 0; // Zero uses the stall timeout instead of a whole-transfer deadline.
    std::uint32_t maxConnections = 16;
    std::uint64_t chunkBytes = 4 * 1024 * 1024;
    bool automaticConnections = true;
    bool adaptiveChunks = true;
    ExistingDownload existing = ExistingDownload::New;
    std::uint32_t maxRetries = 3; // Per request; excludes the initial attempt.
    std::uint32_t retryBaseMs = 500, retryMaxWaitMs = 60'000;
    DownloadControl* control = nullptr; // Must outlive the worker call.
    // Worker-only pacing hook; called for validated bytes before writing. Exceptions stop safely.
    std::function<void(std::size_t, std::stop_token)> beforeWrite;
    // Runs on the owning worker at most four times/second, plus stage boundaries.
    // Observer exceptions disable further notifications without failing the download.
    std::function<void(const struct DownloadProgress&)> onProgress;
};

enum class DownloadStage { Probe, Downloading, Finalizing, Completed, Paused };
struct DownloadProgress {
    DownloadStage stage = DownloadStage::Probe;
    std::uint64_t downloadedBytes = 0, networkBytes = 0, verifiedBytes = 0;
    std::optional<std::uint64_t> totalBytes;
    double elapsedSeconds = 0, instantaneousBytesPerSecond = 0, smoothedBytesPerSecond = 0;
    double averageBytesPerSecond = 0;
    std::optional<double> etaSeconds;
    std::uint32_t activeRanges = 0, targetConnections = 1;
    std::uint64_t chunkBytes = 0;
    std::uint64_t recoveredBytes = 0;
    bool resumable = false;
};

struct DownloadMetrics {
    double totalSeconds = 0, transferSeconds = 0, averageBytesPerSecond = 0, peakBytesPerSecond = 0;
    double cpuSeconds = 0, writeSeconds = 0, allocationSeconds = 0, checksumSeconds = 0, flushSeconds = 0;
    double connectionSetupSeconds = 0, firstByteSeconds = 0;
    double checkpointSeconds = 0;
    std::uint64_t writeCalls = 0, checksumReadBytes = 0, checksumReadCalls = 0, checkpoints = 0;
    std::uint64_t peakWorkingSetBytes = 0, networkBytes = 0, writeBytes = 0, connectionsOpened = 0;
    std::uint64_t smallestRangeBytes = 0, largestRangeBytes = 0;
    std::uint32_t peakActiveRanges = 0, initialConnections = 1, finalConnections = 1, tuningChanges = 0;
    bool observerFailed = false;
    bool resourceMetricsAvailable = false;
    std::uint64_t recoveredBytes = 0;
    std::uint32_t retries = 0;
    bool cleanupPending = false;
};

struct DownloadResult {
    std::uint64_t bytes = 0;
    std::uint64_t completedRanges = 0;
    bool segmented = false;
    bool fellBack = false;
    std::string suggestedFilename;
    DownloadMetrics metrics;
};

class DownloadEngine final {
public:
    struct Inspection { std::string etag, resolvedUrl; std::uint64_t total=0; };
    // Bounded public-resource preflight, no file creation or authentication context.
    [[nodiscard]] Inspection inspect(const std::string& url, std::stop_token stop={}) const;
    // Blocking worker API. Call off the GUI thread. CurlRuntime must outlive this call.
    // Failure leaves a .part file for inspection, never a completed destination.
    [[nodiscard]] DownloadResult download(const DownloadOptions& options,
                                         std::stop_token stop = {}) const;
};

} // namespace downshift
