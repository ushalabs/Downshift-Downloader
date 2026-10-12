#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "core/DownloadEngine/DownloadEngine.h"
#include "core/DownloadEngine/HttpResponse.h"
#include "core/ConnectionManager/AdaptiveController.h"
#include "core/ConnectionManager/SpeedTracker.h"
#include "core/ConnectionManager/RetryPolicy.h"
#include "core/ResumeManager/ResumeJournal.h"
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
    throw std::runtime_error("Expected rejection did not occur after " + std::to_string(checks) + " checks.");
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
void recovery(const std::filesystem::path& directory) {
    SegmentScheduler scheduler(100, 13, {{0, 9}, {30, 39}, {90, 99}});
    require(scheduler.remaining() == 70, "Recovered bytes were not excluded.");
    std::vector<bool> covered(100, false);
    for (auto r : {ByteRange{0, 9}, ByteRange{30, 39}, ByteRange{90, 99}})
        for (auto i = r.first; i <= r.last; ++i) covered[static_cast<std::size_t>(i)] = true;
    while (auto r = scheduler.next()) {
        scheduler.setChunkSize(7);
        for (auto i = r->first; i <= r->last; ++i) {
            require(!covered[static_cast<std::size_t>(i)], "Recovery overlapped a completed range.");
            covered[static_cast<std::size_t>(i)] = true;
        }
    }
    require(std::all_of(covered.begin(), covered.end(), [](bool c) { return c; }) && scheduler.remaining() == 0, "Recovery left a hole.");
    require(*retryDelay(0, 500, 60000, 0, 0) == 250, "Initial retry bound failed.");
    require(*retryDelay(2, 500, 60000, 0, 1000) == 2000, "Exponential retry bound failed.");
    require(*retryDelay(0, 500, 60000, 5, 0) == 5000, "Retry-After was ignored.");
    require(!retryDelay(0, 500, 60000, 61, 0), "Excessive server delay was shortened.");
    require(*retryDelay(100, 500, 60000, 0, 0) == 30000, "Retry cap failed.");
    const auto destination = directory / "recovery.bin";
    RecoveryRecord r;
    r.id = ResumeJournal::newId(); r.url = "https://example.test/data?token=PRIVATE_TOKEN";
    r.resolvedUrl = r.url; r.destination = ResumeJournal::pathText(destination);
    r.etag = "\"v1\""; r.lastModified = "Mon, 12 Oct 2026 00:00:00 GMT";
    r.resumable = true; r.total = 6;
    {
        ResumeJournal journal(destination);
        fails([&] { ResumeJournal competing(destination); }, DownloadErrorCode::File);
        { FileWriter writer(destination); writer.preallocate(6); writer.writeAt(3, "def", 3);
          r.fileIdentity = writer.identity(); r.completed = {{{3, 5}, writer.sha256Range(3, 3)}};
          writer.flush(); journal.save(r); }
        const auto loaded = journal.load();
        require(loaded.url == r.url && loaded.completed[0].sha256 == r.completed[0].sha256 && loaded.updated > 0, "Recovery roundtrip failed.");
        auto metadata = destination; metadata += ".part.resume";
        const auto ciphertext = contents(metadata);
        require(ciphertext.find("PRIVATE_TOKEN") == std::string::npos && ciphertext.find(r.etag) == std::string::npos, "Metadata was plaintext.");
        { FileWriter reopened(destination, true);
          require(reopened.identity() == r.fileIdentity && reopened.sha256Range(3, 3) == r.completed[0].sha256, "Reopened bytes/identity changed."); }
        auto invalid = r; invalid.completed.push_back({{5, 5}, std::string(64, '0')});
        fails([&] { journal.save(invalid); }, DownloadErrorCode::ResumeUnsafe);
        { std::fstream broken(metadata, std::ios::binary | std::ios::in | std::ios::out);
          require(broken.good(), "Unable to open encrypted fixture for mutation."); broken.put(static_cast<char>(0xff)); }
        fails([&] { (void)journal.load(); }, DownloadErrorCode::ResumeUnsafe);
        journal.archivePrevious();
        require(!std::filesystem::exists(destination.string() + ".part") && !journal.exists(), "Explicit restart did not archive old files.");
    }
    require(!std::filesystem::exists(destination.string() + ".part.lock"), "Recovery lock was not released.");
    const auto large = directory / "large-recovery.bin";
    constexpr std::uint64_t offset = 4294967296ULL + 17;
    std::string identity, hash;
    { FileWriter writer(large); writer.preallocate(offset + 3, true); writer.writeAt(offset, "end", 3);
      writer.flush(); identity = writer.identity(); hash = writer.sha256Range(offset, 3); }
    { FileWriter reopened(large, true);
      require(reopened.size() == offset + 3 && reopened.identity() == identity && reopened.sha256Range(offset, 3) == hash, "64-bit reopened range verification failed."); }
    SegmentScheduler largeScheduler(offset + 3, 1048576, {{offset, offset + 2}});
    std::uint64_t coveredBytes = 0;
    while (auto range = largeScheduler.next()) { require(range->last < offset, "64-bit recovery reused a saved range."); coveredBytes += range->size(); }
    require(coveredBytes == offset, "64-bit recovery lost unassigned bytes.");
}
} // namespace

int main(int argc, char** argv) {
    try {
        CurlRuntime runtime;
        if (argc == 3 && std::string_view(argv[1]) == "--inspect-recovery") {
            ResumeJournal journal(argv[2]); const auto record = journal.load();
            std::cout << "{\"total\":" << record.total << ",\"resumable\":" << (record.resumable ? "true" : "false")
                << ",\"status\":" << static_cast<int>(record.status) << ",\"ranges\":[";
            bool first = true;
            for (const auto& saved : record.completed) {
                if (!first) std::cout << ','; first = false;
                std::cout << '[' << saved.range.first << ',' << saved.range.last << ']';
            }
            std::cout << "]}\n"; return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "--rename-checkpoint") {
            ResumeJournal journal(argv[2]); auto record = journal.load();
            FileWriter writer(argv[2], true);
            if (!record.resumable) { record.total = writer.size(); record.finalSha256 = writer.sha256(); }
            record.status = RecoveryStatus::Finalizing; journal.save(record);
            writer.commit(record.total);
            return 0; // Simulate termination after rename, before ledger cleanup.
        }
        if (argc == 4 && (std::string_view(argv[1]) == "--pause-finalizing" || std::string_view(argv[1]) == "--pause-timer")) {
            DownloadOptions options; options.url = argv[2]; options.destination = argv[3];
            DownloadControl control; options.control = &control;
            options.onProgress = [&](const DownloadProgress& p) { if (p.stage == DownloadStage::Finalizing) control.requestPause(); };
            std::jthread timer;
            if (std::string_view(argv[1]) == "--pause-timer") timer = std::jthread([&] { std::this_thread::sleep_for(std::chrono::milliseconds(150)); control.requestPause(); });
            fails([&] { (void)DownloadEngine{}.download(options); }, DownloadErrorCode::Paused);
            return 0;
        }
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
            Workspace workspace; headers(); scheduling(); performance(); files(workspace.path); recovery(workspace.path);
            std::stop_source source; source.request_stop();
            DownloadOptions options; options.url = "http://127.0.0.1:1/file"; options.destination = workspace.path / "cancel.bin";
            fails([&] { (void)DownloadEngine{}.download(options, source.get_token()); }, DownloadErrorCode::Cancelled);
            require(!std::filesystem::exists(options.destination.string() + ".part"), "Pre-cancelled request created a partial.");
        }
        std::cout << checks << " engine checks passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
