#pragma once

#include <string>

namespace downshift {

// Owns process-wide libcurl initialization. Construct before starting workers
// and destroy only after every easy/multi handle and worker has stopped.
class CurlRuntime final {
public:
    CurlRuntime();
    ~CurlRuntime();
    CurlRuntime(const CurlRuntime&) = delete;
    CurlRuntime& operator=(const CurlRuntime&) = delete;
    CurlRuntime(CurlRuntime&&) = delete;
    CurlRuntime& operator=(CurlRuntime&&) = delete;
};

struct DependencyInfo {
    std::string curlVersion;
    std::string sqliteVersion;
    bool http2;
    bool http3;
};

// Checks linked dependencies without downloading anything or opening a
// persistent database. Throws on a missing required runtime capability.
[[nodiscard]] DependencyInfo checkDependencies();

} // namespace downshift
