#include "core/DownloadEngine/DownloadEngine.h"
#include "core/Runtime.h"

#include <charconv>
#include <iostream>
#include <iomanip>
#include <locale>
#include <string_view>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace {
downshift::DownloadControl control;
BOOL WINAPI consoleControl(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT) { control.requestPause(); return TRUE; }
    return FALSE;
}
std::uint64_t positiveInteger(const wchar_t* input) {
    const auto text = std::filesystem::path(input).string();
    std::uint64_t number = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || number == 0)
        throw downshift::DownloadError(downshift::DownloadErrorCode::InvalidRequest, "A positive integer option is required.");
    return number;
}
void progressJson(const downshift::DownloadProgress& p) {
    std::cerr << std::setprecision(17) << "{\"stage\":" << static_cast<int>(p.stage)
        << ",\"downloaded_bytes\":" << p.downloadedBytes << ",\"network_bytes\":" << p.networkBytes
        << ",\"verified_bytes\":" << p.verifiedBytes << ",\"elapsed_seconds\":" << p.elapsedSeconds
        << ",\"instant_bps\":" << p.instantaneousBytesPerSecond << ",\"smoothed_bps\":" << p.smoothedBytesPerSecond
        << ",\"average_bps\":" << p.averageBytesPerSecond << ",\"active_ranges\":" << p.activeRanges
        << ",\"target_connections\":" << p.targetConnections << ",\"chunk_bytes\":" << p.chunkBytes << ",\"recovered_bytes\":" << p.recoveredBytes
        << ",\"resumable\":" << (p.resumable ? "true" : "false")
        << ",\"total_bytes\":";
    if (p.totalBytes) std::cerr << *p.totalBytes; else std::cerr << "null";
    std::cerr << ",\"eta_seconds\":";
    if (p.etaSeconds) std::cerr << *p.etaSeconds; else std::cerr << "null";
    std::cerr << "}\n";
}
void resultJson(const downshift::DownloadResult& r) {
    const auto& m = r.metrics;
    std::cout << std::setprecision(17) << std::boolalpha
        << "{\"bytes\":" << r.bytes << ",\"segmented\":" << r.segmented << ",\"fallback\":" << r.fellBack
        << ",\"ranges\":" << r.completedRanges << ",\"total_seconds\":" << m.totalSeconds
        << ",\"transfer_seconds\":" << m.transferSeconds << ",\"average_bps\":" << m.averageBytesPerSecond
        << ",\"peak_bps\":" << m.peakBytesPerSecond << ",\"cpu_seconds\":" << m.cpuSeconds
        << ",\"peak_working_set_bytes\":" << m.peakWorkingSetBytes << ",\"write_bytes\":" << m.writeBytes
        << ",\"write_seconds\":" << m.writeSeconds << ",\"allocation_seconds\":" << m.allocationSeconds
        << ",\"checksum_seconds\":" << m.checksumSeconds << ",\"flush_seconds\":" << m.flushSeconds
        << ",\"connection_setup_seconds\":" << m.connectionSetupSeconds << ",\"first_byte_seconds\":" << m.firstByteSeconds
        << ",\"connections_opened\":" << m.connectionsOpened << ",\"network_bytes\":" << m.networkBytes
        << ",\"smallest_range_bytes\":" << m.smallestRangeBytes << ",\"largest_range_bytes\":" << m.largestRangeBytes
        << ",\"peak_active_ranges\":" << m.peakActiveRanges << ",\"initial_connections\":" << m.initialConnections
        << ",\"final_connections\":" << m.finalConnections << ",\"tuning_changes\":" << m.tuningChanges
        << ",\"resource_metrics_available\":" << m.resourceMetricsAvailable << ",\"observer_failed\":" << m.observerFailed
        << ",\"recovered_bytes\":" << m.recoveredBytes << ",\"retries\":" << m.retries << ",\"cleanup_pending\":" << m.cleanupPending << "}\n";
}
} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: DownshiftDownload.exe URL DESTINATION [options]\n"
            "  --connections auto|1..16  (default: auto)\n"
            "  --chunk-kib N            fixed chunks; default is adaptive 256 KiB..16 MiB\n"
            "  --sha256 HEX  --ca-bundle FILE  --timeout-ms N\n"
            "  --progress               show live speed and progress\n"
            "  --resume | --restart     reuse verified ranges or archive old partials\n"
            "  --retries 0..10          default: 3 per request, with bounded backoff\n"
            "  --pause-after-mib N      development pause checkpoint; Ctrl+C also pauses\n"
            "  --json  --progress-json  machine-readable result/progress\n";
        return argc == 2 && std::wstring_view(argv[1]) == L"--help" ? 0 : 2;
    }
    try {
        downshift::DownloadOptions options;
        options.control = &control;
        if (!SetConsoleCtrlHandler(consoleControl, TRUE))
            throw downshift::DownloadError(downshift::DownloadErrorCode::File, "Cannot install the console pause handler.");
        std::cout.imbue(std::locale::classic()); std::cerr.imbue(std::locale::classic());
        bool json = false, progress = false, jsonProgress = false;
        std::uint64_t pauseAfter = 0;
        const auto encoded = std::filesystem::path(argv[1]).u8string();
        options.url.assign(reinterpret_cast<const char*>(encoded.data()), encoded.size());
        options.destination = argv[2];
        for (int i = 3; i < argc; ++i) {
            const std::wstring_view flag(argv[i]);
            if (flag == L"--json") { json = true; continue; }
            if (flag == L"--progress") { progress = true; continue; }
            if (flag == L"--progress-json") { jsonProgress = true; continue; }
            if (flag == L"--resume" || flag == L"--restart") {
                if (options.existing != downshift::ExistingDownload::New)
                    throw downshift::DownloadError(downshift::DownloadErrorCode::InvalidRequest, "Choose only one of --resume and --restart.");
                options.existing = flag == L"--resume" ? downshift::ExistingDownload::Resume : downshift::ExistingDownload::Restart;
                continue;
            }
            if (++i >= argc) throw downshift::DownloadError(downshift::DownloadErrorCode::InvalidRequest, "A command option is missing its value.");
            if (flag == L"--ca-bundle") options.caBundle = argv[i];
            else if (flag == L"--sha256") {
                const auto value = std::filesystem::path(argv[i]).u8string();
                options.expectedSha256 = std::string(reinterpret_cast<const char*>(value.data()), value.size());
            } else if (flag == L"--timeout-ms") {
                const auto value = positiveInteger(argv[i]);
                if (value > 2'147'483'647) throw downshift::DownloadError(downshift::DownloadErrorCode::InvalidRequest, "Timeout is too large.");
                options.transferTimeoutMs = static_cast<long>(value);
            } else if (flag == L"--retries") {
                const auto value = std::wstring_view(argv[i]) == L"0" ? 0 : positiveInteger(argv[i]);
                if (value > 10) throw downshift::DownloadError(downshift::DownloadErrorCode::InvalidRequest, "Retries must be between 0 and 10.");
                options.maxRetries = static_cast<std::uint32_t>(value);
            } else if (flag == L"--pause-after-mib") {
                const auto value = positiveInteger(argv[i]);
                if (value > 1'000'000'000) throw downshift::DownloadError(downshift::DownloadErrorCode::InvalidRequest, "Pause limit is too large.");
                pauseAfter = value * 1048576;
            } else if (flag == L"--connections") {
                if (std::wstring_view(argv[i]) == L"auto") { options.automaticConnections = true; options.maxConnections = 16; }
                else {
                    const auto count = positiveInteger(argv[i]);
                    if (count > 16) throw downshift::DownloadError(downshift::DownloadErrorCode::InvalidRequest, "Connections must be between 1 and 16.");
                    options.automaticConnections = false; options.maxConnections = static_cast<std::uint32_t>(count);
                }
            } else if (flag == L"--chunk-kib") {
                const auto value = positiveInteger(argv[i]);
                if (value > 1024 * 1024) throw downshift::DownloadError(downshift::DownloadErrorCode::InvalidRequest, "Chunk size is too large.");
                options.chunkBytes = value * 1024; options.adaptiveChunks = false;
            } else throw downshift::DownloadError(downshift::DownloadErrorCode::InvalidRequest, "Unknown command option.");
        }
        if (jsonProgress || progress || pauseAfter) options.onProgress = [=](const downshift::DownloadProgress& p) {
            if (pauseAfter && p.stage == downshift::DownloadStage::Downloading &&
                (p.verifiedBytes >= pauseAfter || (!p.resumable && p.downloadedBytes >= pauseAfter))) control.requestPause();
            if (jsonProgress) { progressJson(p); return; }
            if (!progress) return;
            std::cerr << std::fixed << std::setprecision(2) << p.downloadedBytes / 1048576.0 << " MiB, "
                << p.smoothedBytesPerSecond / 1048576.0 << " MiB/s, " << p.activeRanges << " active ranges";
            if (p.etaSeconds) std::cerr << ", ETA " << *p.etaSeconds << " s";
            std::cerr << '\n';
        };
        downshift::CurlRuntime runtime;
        const auto result = downshift::DownloadEngine{}.download(options);
        if (json) { resultJson(result); return 0; }
        std::cout << "Completed " << result.bytes << " bytes; mode=" << (result.segmented ? "segmented" : "streaming")
                  << "; ranges=" << result.completedRanges << "; fallback=" << (result.fellBack ? "yes" : "no")
                  << "; average=" << std::fixed << std::setprecision(2) << result.metrics.averageBytesPerSecond / 1048576.0 << " MiB/s\n";
        return 0;
    } catch (const downshift::DownloadError& error) {
        std::cerr << "Downshift: " << error.what() << " [category=" << static_cast<int>(error.code()) << "]\n";
        return error.code() == downshift::DownloadErrorCode::Paused ? 3 : 1;
    } catch (const std::exception&) {
        std::cerr << "Downshift: an unexpected local error prevented the download.\n";
        return 1;
    }
}
