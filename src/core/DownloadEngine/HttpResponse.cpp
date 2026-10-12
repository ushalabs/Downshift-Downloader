#include "HttpResponse.h"
#include "DownloadEngine.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <limits>

namespace downshift {
namespace {
std::string_view trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r' || value.front() == '\n')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r' || value.back() == '\n')) value.remove_suffix(1);
    return value;
}
std::string lower(std::string_view value) {
    std::string result(value);
    for (auto& c : result) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return result;
}
std::optional<std::uint64_t> number(std::string_view value) {
    if (value.empty()) return std::nullopt;
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
        result > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return std::nullopt;
    return result;
}
void malformed() { throw DownloadError(DownloadErrorCode::Protocol, "Invalid or ambiguous HTTP headers."); }
int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
std::string decode(std::string_view value) {
    std::string result;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%') {
            if (i + 2 >= value.size() || hex(value[i + 1]) < 0 || hex(value[i + 2]) < 0) return {};
            result += static_cast<char>((hex(value[i + 1]) << 4) | hex(value[i + 2]));
            i += 2;
        } else result += value[i];
    }
    return result;
}
std::string safeName(std::string value) {
    if (value.empty() || value.size() > 1024 ||
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0) == 0) return "download";
    const auto slash = value.find_last_of("/\\");
    if (slash != std::string::npos) value.erase(0, slash + 1);
    for (auto& c : value) if (static_cast<unsigned char>(c) < 32 || c == 127 || std::string_view("<>:\"|?*").find(c) != std::string_view::npos) c = '_';
    while (!value.empty() && (value.back() == '.' || value.back() == ' ')) value.pop_back();
    if (value.empty() || value.size() > 180) return "download";
    auto base = lower(value.substr(0, value.find('.')));
    if (base == "con" || base == "prn" || base == "aux" || base == "nul" ||
        (base.size() == 4 && (base.starts_with("com") || base.starts_with("lpt")) && base[3] >= '1' && base[3] <= '9')) return "download";
    return value;
}
} // namespace

std::optional<ContentRange> parseContentRange(std::string_view text) {
    text = trim(text);
    if (text.size() < 6 || lower(text.substr(0, 6)) != "bytes ") return std::nullopt;
    text.remove_prefix(6);
    const auto slash = text.find('/');
    if (slash == std::string_view::npos) return std::nullopt;
    const auto total = number(text.substr(slash + 1));
    if (!total) return std::nullopt;
    const auto span = text.substr(0, slash);
    if (span == "*") return ContentRange{0, 0, *total, true};
    const auto dash = span.find('-');
    if (dash == std::string_view::npos) return std::nullopt;
    const auto first = number(span.substr(0, dash));
    const auto last = number(span.substr(dash + 1));
    if (!first || !last || *first > *last || *last >= *total) return std::nullopt;
    return ContentRange{*first, *last, *total, false};
}

bool strongEtag(std::string_view text) noexcept {
    if (text.size() < 2 || text.front() != '"' || text.back() != '"') return false;
    for (std::size_t i = 1; i + 1 < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x21 || c == 0x22 || c == 0x7f) return false;
    }
    return true;
}

std::string suggestedFilename(std::string_view disposition) {
    std::string ordinary, extended;
    auto position = disposition.find(';');
    while (position != std::string_view::npos) {
        disposition.remove_prefix(position + 1);
        disposition = trim(disposition);
        const auto equal = disposition.find('=');
        if (equal == std::string_view::npos) break;
        const auto key = lower(trim(disposition.substr(0, equal)));
        disposition.remove_prefix(equal + 1);
        std::string value;
        std::size_t consumed = 0;
        if (disposition.starts_with('"')) {
            consumed = 1;
            bool closed = false;
            for (; consumed < disposition.size(); ++consumed) {
                char c = disposition[consumed];
                if (c == '"') { ++consumed; closed = true; break; }
                if (c == '\\' && consumed + 1 < disposition.size()) c = disposition[++consumed];
                value += c;
            }
            if (!closed) return "download";
        } else {
            consumed = disposition.find(';');
            if (consumed == std::string_view::npos) consumed = disposition.size();
            value = trim(disposition.substr(0, consumed));
        }
        if (key == "filename") ordinary = value;
        if (key == "filename*") {
            const auto firstQuote = value.find('\'');
            const auto secondQuote = firstQuote == std::string::npos ? firstQuote : value.find('\'', firstQuote + 1);
            if (firstQuote != std::string::npos && secondQuote != std::string::npos && lower(value.substr(0, firstQuote)) == "utf-8") extended = decode(std::string_view(value).substr(secondQuote + 1));
        }
        disposition.remove_prefix(consumed);
        position = disposition.find(';');
    }
    return safeName(extended.empty() ? ordinary : extended);
}

void HeaderCollector::accept(std::string_view line) {
    bytes_ += line.size();
    if (bytes_ > 256 * 1024 || line.size() > 64 * 1024) malformed();
    line = trim(line);
    if (line.starts_with("HTTP/")) {
        const auto space = line.find(' ');
        if (space == std::string_view::npos || space + 4 > line.size()) malformed();
        const auto code = number(line.substr(space + 1, 3));
        if (!code || *code < 100 || *code > 599 || (space + 4 < line.size() && line[space + 4] != ' ')) malformed();
        response = {};
        response.status = static_cast<int>(*code);
        etagSeen_ = encodingSeen_ = transferSeen_ = false;
        return;
    }
    if (response.complete) return; // Trailers cannot redefine accepted representation metadata.
    if (response.status == 0) malformed();
    if (line.empty()) { response.complete = true; return; }
    const auto colon = line.find(':');
    if (colon == std::string_view::npos) malformed();
    const auto key = lower(line.substr(0, colon));
    const auto value = trim(line.substr(colon + 1));
    if (key == "last-modified") {
        if (!response.lastModified.empty() && response.lastModified != value) malformed();
        response.lastModified = value;
    }
    for (const auto c : value) if (static_cast<unsigned char>(c) < 32 && c != '\t') malformed();
    if (key == "content-length") {
        const auto length = number(value);
        if (!length || response.length) malformed();
        response.length = length;
    } else if (key == "content-range") {
        if (response.rangePresent) malformed();
        response.rangePresent = true;
        response.range = parseContentRange(value);
    } else if (key == "etag") {
        if (etagSeen_) malformed();
        etagSeen_ = true; response.etag = value;
    } else if (key == "content-encoding") {
        if (encodingSeen_) malformed();
        encodingSeen_ = true; response.encoding = lower(value);
    } else if (key == "transfer-encoding") {
        if (transferSeen_) malformed();
        transferSeen_ = true; response.transferEncoding = lower(value);
    } else if (key == "content-type") response.contentType = lower(value);
    else if (key == "content-disposition") response.disposition = value;
}

} // namespace downshift
