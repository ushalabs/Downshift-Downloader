#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "core/DownloadEngine/DownloadEngine.h"
#include "core/DownloadEngine/HttpResponse.h"
#include "core/ConnectionManager/AdaptiveController.h"
#include "core/ConnectionManager/SpeedTracker.h"
#include "core/FileWriter/FileWriter.h"
#include "core/Runtime.h"
#include "core/SegmentScheduler/SegmentScheduler.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>

using namespace downshift;
namespace {
int checks = 0;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
    ++checks;
}
template<class F> void fails(F action, DownloadErrorCode expected) {
    try { action(); }
    catch (const DownloadError& error) { require(error.code() == expected, "Unexpected error category."); return; }
    throw std::runtime_error("Expected rejection did not occur.");
}
std::string contents(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
struct Workspace {
    std::filesystem::path path = std::filesystem::current_path() / ("engine-test-" + std::to_string(GetCurrentProcessId()));
    Workspace() { if (!std::filesystem::create_directory(path)) throw std::runtime_error("Test directory collision."); }
    ~Workspace() { std::error_code error; std::filesystem::remove_all(path, error); }
};
void headers() {
    for (auto text : {"bytes 0-0/1", "BYTES 4294967296-4294967300/4294967301", "bytes */0"})
        require(parseContentRange(text).has_value(), "Valid 64-bit range rejected.");
    for (auto text : {"", "bytes 0-1/1", "bytes 4-3/5", "bytes -1-2/5", "bytes 0-0/*",
                      "bytes 0-0/9223372036854775808", "bytes 0-0/1x", "items 0-0/1", "bytes 0-/3", "bytes 0-0/"})
        require(!parseContentRange(text), "Malformed range accepted.");
    require(strongEtag("\"\"") && strongEtag("\"version-1\""), "Strong ETag rejected.");
    for (auto text : {"", "W/\"x\"", "x", "\"a b\"", "\"a\"b\""}) require(!strongEtag(text), "Invalid ETag accepted.");
    HeaderCollector collector;
    for (auto line : {"HTTP/1.1 100 Continue\r\n", "\r\n", "HTTP/1.1 302 Found\r\n", "Content-Length: 25\r\n",
                      "\r\n", "HTTP/2 206\r\n", "Content-Length: 1\r\n", "Content-Range: bytes 0-0/4294967301\r\n",
                      "ETag: \"x\"\r\n", "\r\n", "Content-Length: 999\r\n", "ETag: \"trailer\"\r\n"}) collector.accept(line);
    require(collector.response.status == 206 && collector.response.complete && collector.response.length == 1 &&
            collector.response.range->total == 4294967301ULL && collector.response.etag == "\"x\"", "Response/trailer isolation failed.");
    for (auto name : {"Content-Length: 1\r\n", "Content-Range: bytes 0-0/2\r\n", "ETag: \"x\"\r\n",
                      "Content-Encoding: identity\r\n", "Transfer-Encoding: chunked\r\n"}) {
        HeaderCollector duplicate;
        duplicate.accept("HTTP/1.1 200 OK\r\n"); duplicate.accept(name);
        fails([&] { duplicate.accept(name); }, DownloadErrorCode::Protocol);
    }
    HeaderCollector invalid;
    fails([&] { invalid.accept("Content-Length: 1\r\n"); }, DownloadErrorCode::Protocol);
    invalid.accept("HTTP/1.1 200 OK\r\n");
    fails([&] { invalid.accept("Content-Length: -1\r\n"); }, DownloadErrorCode::Protocol);
    fails([&] { invalid.accept(std::string(65537, 'x')); }, DownloadErrorCode::Protocol);
    require(suggestedFilename("attachment; filename=\"../../safe.bin\"") == "safe.bin", "Filename traversal was not stripped.");
    require(suggestedFilename("attachment; filename=\"CON.txt\"") == "download", "Reserved filename accepted.");
    require(suggestedFilename("attachment; filename=old.txt; filename*=UTF-8''caf%C3%A9.txt") == "caf\xc3\xa9.txt", "UTF-8 filename decoding failed.");
    require(suggestedFilename("attachment; filename=\"a; b.txt\"") == "a; b.txt", "Quoted semicolon misparsed.");
    require(suggestedFilename("attachment; filename*=UTF-8''%FF") == "download", "Invalid UTF-8 accepted.");
    require(suggestedFilename("attachment; filename=\"file?.txt. \"") == "file_.txt", "Invalid Windows filename was not sanitized.");
}
void scheduling() {
    for (auto total : {0ULL, 1ULL, 16ULL, 17ULL, 4294967313ULL}) {
        SegmentScheduler scheduler(total, 1024 * 1024);
        std::uint64_t cursor = 0;
        while (const auto range = scheduler.next()) {
            require(range->first == cursor && range->last < total && range->size() <= 1024 * 1024, "Range overlap, gap, or overflow.");
            cursor += range->size();
        }
        require(cursor == total && !scheduler.next(), "Range coverage incomplete.");
    }
    SegmentScheduler dynamic(8'589'934'621ULL, 1);
    std::uint64_t cursor = 0, count = 0;
    while (dynamic.remaining()) {
        dynamic.setChunkSize(++count % 2 ? 257 * 1024 : 4 * 1024 * 1024);
        const auto range = dynamic.next();
        require(range && range->first == cursor, "Adaptive sizing introduced a gap/overlap.");
        cursor += range->size();
    }
    require(cursor == 8'589'934'621ULL, "Adaptive 64-bit coverage failed.");
}
void performance() {
    SpeedTracker speed;
    require(!speed.sample(100, 0.1), "Progress refresh was not bounded.");
    auto sample = speed.sample(500, 0.5);
    require(sample && sample->instantaneous == 1000 && sample->average == 1000, "Speed calculation failed.");
    sample = speed.sample(500, 0.75);
    require(sample && sample->instantaneous == 0 && sample->smoothed > 0 && sample->smoothed < 1000 && sample->peak == 1000, "Stall smoothing failed.");
    sample = speed.sample(501, 0.750001, true);
    require(sample && sample->peak == 1000, "Short final sample inflated the peak.");
    try { (void)speed.sample(499, 1); throw std::runtime_error("Byte counter regression accepted."); }
    catch (const std::invalid_argument&) { ++checks; }
    try { (void)speed.sample(600, 0.7); throw std::runtime_error("Time regression accepted."); }
    catch (const std::invalid_argument&) { ++checks; }
    AdaptiveController gain(100 * 1024 * 1024, 16, true);
    require(gain.target() == 4, "Large-file startup policy failed.");
    gain.observe(1, 100, true); require(gain.target() == 8, "Expansion trial missing.");
    gain.observe(2, 300, true); require(gain.target() == 8, "Warmup was not respected.");
    gain.observe(3, 500, true); require(gain.target() == 16, "Measured gain was not retained.");
    gain.observe(4, 900, true); gain.observe(5, 1300, true);
    require(gain.target() == 16, "Maximum connection limit failed.");
    gain.observe(6, 1400, true); gain.observe(7, 1500, true);
    require(gain.target() == 8, "Sustained throughput loss did not back off.");
    AdaptiveController plateau(100 * 1024 * 1024, 16, true);
    plateau.observe(1, 100, true); plateau.observe(2, 200, true); plateau.observe(3, 300, true);
    require(plateau.target() == 4 && plateau.changes() == 2, "Unhelpful expansion was not rolled back.");
    plateau.observe(4, 400, true); plateau.observe(5, 500, true);
    require(plateau.target() == 4, "Plateau caused connection oscillation.");
    plateau.observe(100, 10000, false);
    require(plateau.target() == 4, "Tail changed connection policy.");
    plateau.observe(101, 10200, true);
    require(plateau.target() == 8, "Changed capacity did not permit a new trial.");
    AdaptiveController resource(100 * 1024 * 1024, 16, true);
    resource.observe(1, 100, true, true);
    require(resource.target() == 2, "Disk pressure did not reduce concurrency.");
    AdaptiveController fixed(1, 16, false);
    fixed.observe(1, 0, true, true);
    require(fixed.target() == 16 && fixed.changes() == 0, "Manual mode was modified.");
    require(AdaptiveController(1, 16, true).target() == 1, "Tiny file opened excess connections.");
    require(gain.chunkSize(1, 0) == 256 * 1024 && gain.chunkSize(1e12, 1) == 16 * 1024 * 1024, "Chunk limits failed.");
    require(gain.chunkSize(8 * 1024 * 1024, 0.2) > gain.chunkSize(8 * 1024 * 1024, 0), "Latency was not considered in sizing.");
}
void files(const std::filesystem::path& directory) {
    const auto small = directory / "ordered.bin";
    {
        FileWriter writer(small); writer.preallocate(6);
        writer.writeAt(3, "def", 3); writer.writeAt(0, "abc", 3);
        writer.commit(6);
    }
    require(contents(small) == "abcdef" && !std::filesystem::exists(small.string() + ".part"), "Positional write/finalization failed.");
    fails([&] { FileWriter writer(small); }, DownloadErrorCode::File);
    require(contents(small) == "abcdef", "Existing destination was modified.");
    const auto hashFile = directory / "hash.bin";
    {
        FileWriter writer(hashFile); writer.writeAt(0, "abc", 3);
        require(writer.sha256() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 mismatch.");
        fails([&] { writer.commit(3, std::string(64, '0')); }, DownloadErrorCode::Integrity);
        fails([&] { writer.commit(4); }, DownloadErrorCode::Integrity);
        fails([&] { writer.writeAt(std::numeric_limits<std::uint64_t>::max(), "x", 1); }, DownloadErrorCode::File);
    }
    require(!std::filesystem::exists(hashFile) && contents(hashFile.string() + ".part") == "abc", "Failed checksum was finalized.");
    fails([&] { FileWriter writer(hashFile); }, DownloadErrorCode::File);
    require(contents(hashFile.string() + ".part") == "abc", "Existing partial was modified.");
    const auto raceFile = directory / "race.bin";
    {
        FileWriter writer(raceFile); writer.writeAt(0, "abc", 3);
        { std::ofstream existing(raceFile); existing << "KEEP"; }
        fails([&] { writer.commit(3); }, DownloadErrorCode::File);
    }
    require(contents(raceFile) == "KEEP", "Destination created during download was overwritten.");
    for (auto name : {"CON.txt", "NUL", "COM1.bin", "name.", "name ", "a:b.bin"})
        fails([&] { FileWriter writer(directory / name); }, DownloadErrorCode::File);
    const auto resetFile = directory / "reset.bin";
    { FileWriter writer(resetFile); writer.preallocate(4096); writer.writeAt(4000, "stale", 5);
      writer.reset(); writer.writeAt(0, "new", 3); writer.commit(3); }
    require(contents(resetFile) == "new", "Fallback retained stale bytes.");
    const auto large = directory / "large.bin";
    constexpr std::uint64_t offset = 4294967296ULL + 17;
    { FileWriter writer(large); writer.preallocate(offset + 3, true); writer.writeAt(offset, "end", 3); writer.commit(offset + 3); }
    require(std::filesystem::file_size(large) == offset + 3, "Large file size truncated to 32 bits.");
    std::ifstream stream(large, std::ios::binary); stream.seekg(static_cast<std::streamoff>(offset));
    char tail[3]{}; stream.read(tail, 3);
    require(std::string(tail, 3) == "end", "Write above 4 GB failed.");
}
} // namespace

int main(int argc, char** argv) {
    try {
        CurlRuntime runtime;
        // Integration fixture uses this mode to exercise cooperative cancellation on a worker.
        if (argc == 4 && std::string_view(argv[1]) == "--cancel") {
            std::stop_source source;
            std::jthread timer([&] { std::this_thread::sleep_for(std::chrono::milliseconds(150)); source.request_stop(); });
            DownloadOptions options; options.url = argv[2]; options.destination = argv[3];
            fails([&] { (void)DownloadEngine{}.download(options, source.get_token()); }, DownloadErrorCode::Cancelled);
            require(!std::filesystem::exists(options.destination), "Cancelled download was finalized.");
        } else if (argc == 4 && std::string_view(argv[1]) == "--observer-failure") {
            DownloadOptions options; options.url = argv[2]; options.destination = argv[3];
            options.onProgress = [](const DownloadProgress&) { throw std::runtime_error("Observer error."); };
            const auto result = DownloadEngine{}.download(options);
            require(result.metrics.observerFailed && std::filesystem::exists(options.destination), "Observer exception damaged download.");
        } else {
            Workspace workspace; headers(); scheduling(); performance(); files(workspace.path);
            std::stop_source source; source.request_stop();
            DownloadOptions options; options.url = "http://127.0.0.1:1/file"; options.destination = workspace.path / "cancel.bin";
            fails([&] { (void)DownloadEngine{}.download(options, source.get_token()); }, DownloadErrorCode::Cancelled);
            require(!std::filesystem::exists(options.destination.string() + ".part"), "Pre-cancelled request created a partial.");
        }
        std::cout << checks << " engine checks passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
