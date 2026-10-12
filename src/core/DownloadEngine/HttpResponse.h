#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace downshift {

struct ContentRange {
    std::uint64_t first = 0;
    std::uint64_t last = 0;
    std::uint64_t total = 0;
    bool unsatisfied = false;
};

[[nodiscard]] std::optional<ContentRange> parseContentRange(std::string_view text);
[[nodiscard]] bool strongEtag(std::string_view text) noexcept;
[[nodiscard]] std::string suggestedFilename(std::string_view disposition);

struct HttpResponse {
    int status = 0;
    bool complete = false;
    std::optional<std::uint64_t> length;
    std::optional<ContentRange> range;
    bool rangePresent = false;
    std::string etag;
    std::string encoding;
    std::string transferEncoding;
    std::string contentType;
    std::string disposition;
    std::string lastModified;
};

// Handles interim responses/redirects separately and ignores trailers after the body headers.
class HeaderCollector final {
public:
    void accept(std::string_view line);
    HttpResponse response;
private:
    std::size_t bytes_ = 0;
    bool etagSeen_ = false;
    bool encodingSeen_ = false;
    bool transferSeen_ = false;
};

} // namespace downshift
