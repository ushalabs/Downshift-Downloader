#include "DownloadEngine.h"
#include "HttpResponse.h"
#include "core/FileWriter/FileWriter.h"
#include "core/SegmentScheduler/SegmentScheduler.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <curl/curl.h>

#include <algorithm>
#include <exception>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

namespace downshift {
namespace {
struct EasyDeleter { void operator()(CURL* p) const noexcept { curl_easy_cleanup(p); } };
struct ListDeleter { void operator()(curl_slist* p) const noexcept { curl_slist_free_all(p); } };
struct UrlDeleter { void operator()(CURLU* p) const noexcept { curl_url_cleanup(p); } };
struct StringDeleter { void operator()(char* p) const noexcept { curl_free(p); } };
using Easy = std::unique_ptr<CURL, EasyDeleter>;
using List = std::unique_ptr<curl_slist, ListDeleter>;

[[noreturn]] void fail(DownloadErrorCode code, const char* message) { throw DownloadError(code, message); }
void multiCheck(CURLMcode code) {
    if (code != CURLM_OK) fail(DownloadErrorCode::Network, "The networking event loop failed.");
}
template<class T> void option(CURL* easy, CURLoption name, T value) {
    if (curl_easy_setopt(easy, name, value) != CURLE_OK) fail(DownloadErrorCode::Network, "Unable to configure the transfer.");
}
std::string utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
std::string validateUrl(const std::string& text) {
    if (text.empty() || text.size() > 32 * 1024 || text.find('\0') != std::string::npos) fail(DownloadErrorCode::InvalidRequest, "A valid HTTP or HTTPS URL is required.");
    std::unique_ptr<CURLU, UrlDeleter> url(curl_url());
    if (!url || curl_url_set(url.get(), CURLUPART_URL, text.c_str(), CURLU_DISALLOW_USER) != CURLUE_OK) fail(DownloadErrorCode::InvalidRequest, "A valid HTTP or HTTPS URL without embedded credentials is required.");
    char* raw = nullptr;
    if (curl_url_get(url.get(), CURLUPART_SCHEME, &raw, 0) != CURLUE_OK) fail(DownloadErrorCode::InvalidRequest, "The URL needs an HTTP or HTTPS scheme.");
    std::unique_ptr<char, StringDeleter> scheme(raw);
    if (std::string_view(raw) != "http" && std::string_view(raw) != "https") fail(DownloadErrorCode::InvalidRequest, "Only HTTP and HTTPS downloads are supported.");
    return raw;
}
void curlFailure(CURLcode code) {
    if (code == CURLE_OK) return;
    auto category = DownloadErrorCode::Network;
    if (code == CURLE_OPERATION_TIMEDOUT) category = DownloadErrorCode::Timeout;
    else if (code == CURLE_PARTIAL_FILE) category = DownloadErrorCode::Integrity;
    else if (code == CURLE_PEER_FAILED_VERIFICATION || code == CURLE_SSL_CONNECT_ERROR || code == CURLE_SSL_CACERT_BADFILE || code == CURLE_SSL_CERTPROBLEM) category = DownloadErrorCode::Tls;
    else if (code == CURLE_UNSUPPORTED_PROTOCOL || code == CURLE_URL_MALFORMAT || code == CURLE_LOGIN_DENIED) category = DownloadErrorCode::InvalidRequest;
    throw DownloadError(category, std::string("Transfer failed: ") + curl_easy_strerror(code));
}
void httpFailure(int status) {
    throw DownloadError(DownloadErrorCode::Http, "The server returned HTTP " + std::to_string(status) + ".");
}
bool identity(const HttpResponse& response) {
    return (response.encoding.empty() || response.encoding == "identity") &&
        (response.transferEncoding.empty() || response.transferEncoding == "chunked") &&
        !(response.length && !response.transferEncoding.empty()) &&
        !response.contentType.starts_with("multipart/byteranges");
}

enum class Kind { Probe, Segment, Stream };
struct Transfer {
    Easy easy{curl_easy_init()};
    List headers;
    HeaderCollector collector;
    Kind kind;
    FileWriter* writer;
    std::optional<ByteRange> assigned;
    std::optional<std::uint64_t> expectedTotal;
    std::string etag;
    std::uint64_t received = 0;
    bool probeRejected = false;
    bool rangeUnsupported = false;
    bool accepted = false;
    std::exception_ptr exception;
    CURLcode result = CURLE_OK;
    std::stop_token stop;

    Transfer(Kind value, FileWriter* file, std::stop_token token) : kind(value), writer(file), stop(token) {
        if (!easy) fail(DownloadErrorCode::Network, "Unable to create a transfer handle.");
    }

    void validateHeaders() {
        const auto& h = collector.response;
        if (h.status < 200) return;
        if (h.status >= 300 && h.status < 400 && kind != Kind::Segment) return;
        if (kind == Kind::Probe) {
            if (h.status == 200 || (h.status == 416 && h.range && h.range->unsatisfied && h.range->total == 0)) {
                probeRejected = true; return;
            }
            if (h.status != 206) httpFailure(h.status);
            if (!identity(h) || !h.range || h.range->unsatisfied || h.range->first != 0 || h.range->last != 0 ||
                (h.length && *h.length != 1)) { probeRejected = true; return; }
        } else if (kind == Kind::Segment) {
            if (h.status == 200) {
                if (h.etag != etag) fail(DownloadErrorCode::Integrity, "The remote file changed during the download.");
                rangeUnsupported = true; return;
            }
            if (h.status != 206) httpFailure(h.status);
            if (!identity(h) || !h.range || h.range->unsatisfied || h.range->first != assigned->first ||
                h.range->last != assigned->last || h.range->total != *expectedTotal ||
                (h.length && *h.length != assigned->size())) fail(DownloadErrorCode::Integrity, "The server returned an incorrect byte range.");
            if (h.etag != etag) fail(DownloadErrorCode::Integrity, "The remote file version does not match the verified probe.");
        } else {
            if (h.status != 200) httpFailure(h.status);
            if (!identity(h) || h.rangePresent) fail(DownloadErrorCode::Protocol, "A complete, untransformed response is required for streaming.");
            if (!etag.empty() && h.etag != etag) fail(DownloadErrorCode::Integrity, "The remote file changed before streaming.");
        }
        accepted = true;
    }

    static std::size_t onHeader(char* data, std::size_t size, std::size_t count, void* context) noexcept {
        auto& self = *static_cast<Transfer*>(context);
        try {
            if (count > std::numeric_limits<std::size_t>::max() / (size ? size : 1)) fail(DownloadErrorCode::Protocol, "HTTP header size overflow.");
            const auto bytes = size * count;
            const std::string_view line(data, bytes);
            if (line.starts_with("HTTP/")) self.accepted = false;
            self.collector.accept(line);
            if (self.collector.response.complete) self.validateHeaders();
            if (self.probeRejected || self.rangeUnsupported) return CURL_WRITEFUNC_ERROR;
            return bytes;
        } catch (...) { self.exception = std::current_exception(); return CURL_WRITEFUNC_ERROR; }
    }

    static std::size_t onBody(char* data, std::size_t size, std::size_t count, void* context) noexcept {
        auto& self = *static_cast<Transfer*>(context);
        try {
            if (count > std::numeric_limits<std::size_t>::max() / (size ? size : 1)) fail(DownloadErrorCode::Integrity, "Response body size overflow.");
            const auto bytes = size * count;
            const auto& h = self.collector.response;
            if (h.status >= 300 && h.status < 400 && self.kind != Kind::Segment) return bytes;
            if (!self.accepted) fail(DownloadErrorCode::Protocol, "Response data arrived without validated headers.");
            const auto bound = self.kind == Kind::Probe ? std::optional<std::uint64_t>(1) :
                self.kind == Kind::Segment ? std::optional<std::uint64_t>(self.assigned->size()) : h.length;
            if ((bound && (self.received > *bound || bytes > *bound - self.received)) ||
                bytes > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) - self.received) fail(DownloadErrorCode::Integrity, "The response exceeded its expected byte count.");
            if (self.writer) self.writer->writeAt((self.assigned ? self.assigned->first : 0) + self.received, data, bytes);
            self.received += bytes;
            return bytes;
        } catch (...) { self.exception = std::current_exception(); return CURL_WRITEFUNC_ERROR; }
    }

    static int onProgress(void* context, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept {
        return static_cast<Transfer*>(context)->stop.stop_requested() ? 1 : 0;
    }

    void configure(const DownloadOptions& options, const std::string& url, bool follow) {
        const auto scheme = validateUrl(url);
        option(easy.get(), CURLOPT_URL, url.c_str());
        option(easy.get(), CURLOPT_PROTOCOLS_STR, "http,https");
        option(easy.get(), CURLOPT_REDIR_PROTOCOLS_STR, scheme == "https" ? "https" : "http,https");
        option(easy.get(), CURLOPT_DISALLOW_USERNAME_IN_URL, 1L);
        option(easy.get(), CURLOPT_NETRC, static_cast<long>(CURL_NETRC_IGNORED));
        option(easy.get(), CURLOPT_FOLLOWLOCATION, follow ? 1L : 0L);
        // A proxy's CONNECT response is tunnel setup, not the downloaded representation.
        option(easy.get(), CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L);
        option(easy.get(), CURLOPT_MAXREDIRS, 5L);
        option(easy.get(), CURLOPT_SSL_VERIFYPEER, 1L);
        option(easy.get(), CURLOPT_SSL_VERIFYHOST, 2L);
        if (!options.caBundle.empty()) {
            const auto bundle = utf8(options.caBundle);
            option(easy.get(), CURLOPT_CAINFO, bundle.c_str());
        }
        option(easy.get(), CURLOPT_ACCEPT_ENCODING, "identity");
        option(easy.get(), CURLOPT_HTTP_CONTENT_DECODING, 0L);
        option(easy.get(), CURLOPT_HTTP_TRANSFER_DECODING, 1L);
        option(easy.get(), CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_2TLS));
        option(easy.get(), CURLOPT_USERAGENT, "Downshift");
        option(easy.get(), CURLOPT_NOSIGNAL, 1L);
        option(easy.get(), CURLOPT_CONNECTTIMEOUT_MS, options.connectTimeoutMs);
        option(easy.get(), CURLOPT_TIMEOUT_MS, kind == Kind::Probe && options.transferTimeoutMs == 0 ? 10'000L : options.transferTimeoutMs);
        option(easy.get(), CURLOPT_LOW_SPEED_LIMIT, 1L);
        option(easy.get(), CURLOPT_LOW_SPEED_TIME, 30L);
        option(easy.get(), CURLOPT_HEADERFUNCTION, &Transfer::onHeader);
        option(easy.get(), CURLOPT_HEADERDATA, this);
        option(easy.get(), CURLOPT_WRITEFUNCTION, &Transfer::onBody);
        option(easy.get(), CURLOPT_WRITEDATA, this);
        option(easy.get(), CURLOPT_XFERINFOFUNCTION, &Transfer::onProgress);
        option(easy.get(), CURLOPT_XFERINFODATA, this);
        option(easy.get(), CURLOPT_NOPROGRESS, 0L);
        if (kind == Kind::Probe) option(easy.get(), CURLOPT_RANGE, "0-0");
        if (assigned) {
            const auto range = std::to_string(assigned->first) + "-" + std::to_string(assigned->last);
            option(easy.get(), CURLOPT_RANGE, range.c_str());
        }
        if (!etag.empty()) {
            const auto line = "If-Match: " + etag;
            auto* list = curl_slist_append(nullptr, line.c_str());
            if (!list) fail(DownloadErrorCode::Network, "Unable to allocate transfer headers.");
            headers.reset(list);
            option(easy.get(), CURLOPT_HTTPHEADER, headers.get());
        }
    }

    void verify() const {
        if (stop.stop_requested()) fail(DownloadErrorCode::Cancelled, "The download was cancelled.");
        if (exception) std::rethrow_exception(exception);
        if (probeRejected || rangeUnsupported) return;
        curlFailure(result);
        if (!accepted || !collector.response.complete) httpFailure(collector.response.status);
        const auto expected = kind == Kind::Probe ? std::optional<std::uint64_t>(1) :
            kind == Kind::Segment ? std::optional<std::uint64_t>(assigned->size()) : collector.response.length;
        if (expected && received != *expected) fail(DownloadErrorCode::Integrity, "The response ended before all expected bytes arrived.");
    }
};

class MultiSession final {
public:
    explicit MultiSession(std::uint32_t connections) : handle_(curl_multi_init()) {
        if (!handle_) fail(DownloadErrorCode::Network, "Unable to create a networking event loop.");
        try {
            multiCheck(curl_multi_setopt(handle_, CURLMOPT_MAX_TOTAL_CONNECTIONS, static_cast<long>(connections)));
            multiCheck(curl_multi_setopt(handle_, CURLMOPT_MAX_HOST_CONNECTIONS, static_cast<long>(connections)));
        } catch (...) { curl_multi_cleanup(handle_); throw; }
    }
    ~MultiSession() { clear(); curl_multi_cleanup(handle_); }
    MultiSession(const MultiSession&) = delete;
    MultiSession& operator=(const MultiSession&) = delete;
    void add(std::unique_ptr<Transfer> transfer) {
        // Allocate vector storage before attaching the easy handle (exception-safe ownership).
        active_.push_back(std::move(transfer));
        try { multiCheck(curl_multi_add_handle(handle_, active_.back()->easy.get())); }
        catch (...) { active_.pop_back(); throw; }
    }
    void clear() noexcept {
        for (const auto& transfer : active_) curl_multi_remove_handle(handle_, transfer->easy.get());
        active_.clear();
        int queued = 0;
        while (curl_multi_info_read(handle_, &queued)) {} // Remove obsolete completion messages.
    }
    [[nodiscard]] std::size_t count() const noexcept { return active_.size(); }
    void perform(std::stop_token stop) {
        if (stop.stop_requested()) fail(DownloadErrorCode::Cancelled, "The download was cancelled.");
        int running = 0;
        multiCheck(curl_multi_perform(handle_, &running));
    }
    void poll() { multiCheck(curl_multi_poll(handle_, nullptr, 0, 100, nullptr)); }
    std::vector<std::unique_ptr<Transfer>> completed() {
        std::vector<std::unique_ptr<Transfer>> result;
        int queued = 0;
        while (auto* message = curl_multi_info_read(handle_, &queued)) {
            if (message->msg != CURLMSG_DONE) continue;
            const auto found = std::find_if(active_.begin(), active_.end(), [message](const auto& transfer) { return transfer->easy.get() == message->easy_handle; });
            if (found == active_.end()) fail(DownloadErrorCode::Network, "An unknown transfer completed.");
            (*found)->result = message->data.result;
            // Reserve result storage before removing the attached handle.
            result.reserve(result.size() + 1);
            multiCheck(curl_multi_remove_handle(handle_, (*found)->easy.get()));
            result.push_back(std::move(*found));
            active_.erase(found);
        }
        return result;
    }
    std::unique_ptr<Transfer> run(std::unique_ptr<Transfer> transfer, std::stop_token stop) {
        add(std::move(transfer));
        for (;;) {
            perform(stop);
            auto done = completed();
            if (!done.empty()) { done.front()->verify(); return std::move(done.front()); }
            poll();
        }
    }
private:
    CURLM* handle_;
    std::vector<std::unique_ptr<Transfer>> active_;
};

} // namespace

DownloadResult DownloadEngine::download(const DownloadOptions& options, std::stop_token stop) const {
    validateUrl(options.url);
    if (options.destination.empty() || options.connectTimeoutMs <= 0 || options.transferTimeoutMs < 0 ||
        options.maxConnections == 0 || options.maxConnections > 16 || options.chunkBytes == 0 ||
        options.chunkBytes > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) fail(DownloadErrorCode::InvalidRequest, "Invalid download destination, limits, or timeouts.");
    auto expectedHash = options.expectedSha256;
    if (expectedHash) {
        if (expectedHash->size() != 64) fail(DownloadErrorCode::InvalidRequest, "SHA-256 must contain exactly 64 hexadecimal digits.");
        for (auto& c : *expectedHash) {
            if (c >= 'A' && c <= 'F') c = static_cast<char>(c + ('a' - 'A'));
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) fail(DownloadErrorCode::InvalidRequest, "SHA-256 must contain exactly 64 hexadecimal digits.");
        }
    }
    if (stop.stop_requested()) fail(DownloadErrorCode::Cancelled, "The download was cancelled.");
    FileWriter writer(options.destination);
    MultiSession session(options.maxConnections);
    auto probe = std::make_unique<Transfer>(Kind::Probe, nullptr, stop);
    probe->configure(options, options.url, true);
    probe = session.run(std::move(probe), stop);
    char* effective = nullptr;
    if (curl_easy_getinfo(probe->easy.get(), CURLINFO_EFFECTIVE_URL, &effective) != CURLE_OK || !effective) fail(DownloadErrorCode::Network, "Unable to resolve the download URL.");
    const std::string resolved(effective);
    validateUrl(resolved);
    const auto metadata = probe->collector.response;
    const auto etag = strongEtag(metadata.etag) ? metadata.etag : std::string{};
    DownloadResult result;
    result.suggestedFilename = suggestedFilename(metadata.disposition);

    if (!probe->probeRejected && metadata.status == 206 && !etag.empty()) {
        const auto total = metadata.range->total;
        writer.preallocate(total);
        SegmentScheduler scheduler(total, options.chunkBytes);
        auto fill = [&] {
            while (session.count() < options.maxConnections) {
                const auto range = scheduler.next();
                if (!range) break;
                auto transfer = std::make_unique<Transfer>(Kind::Segment, &writer, stop);
                transfer->assigned = range;
                transfer->expectedTotal = total;
                transfer->etag = etag;
                transfer->configure(options, resolved, false);
                session.add(std::move(transfer));
            }
        };
        fill();
        bool unsupported = false;
        std::uint64_t verifiedBytes = 0;
        while (session.count() != 0) {
            session.perform(stop);
            auto done = session.completed();
            for (const auto& transfer : done) {
                transfer->verify();
                if (transfer->rangeUnsupported) { unsupported = true; continue; }
                verifiedBytes += transfer->received;
                ++result.completedRanges;
            }
            if (unsupported) { session.clear(); break; }
            fill();
            if (session.count() != 0 && done.empty()) session.poll();
        }
        if (!unsupported) {
            if (verifiedBytes != total) fail(DownloadErrorCode::Integrity, "Not all assigned ranges completed.");
            writer.commit(total, expectedHash);
            result.bytes = total;
            result.segmented = true;
            return result;
        }
        // All easy handles are detached before truncating previously written ranges.
        writer.reset();
        result.completedRanges = 0;
        result.fellBack = true;
    }

    auto stream = std::make_unique<Transfer>(Kind::Stream, &writer, stop);
    stream->etag = etag;
    stream->configure(options, resolved, true);
    stream = session.run(std::move(stream), stop);
    writer.commit(stream->received, expectedHash);
    result.bytes = stream->received;
    result.suggestedFilename = suggestedFilename(stream->collector.response.disposition);
    return result;
}

} // namespace downshift
