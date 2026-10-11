#include "core/Runtime.h"
#include <exception>
#include <iostream>

int main() {
    try {
        // Repeat initialization and cleanup to catch lifecycle problems.
        for (int iteration = 0; iteration < 3; ++iteration) {
            downshift::CurlRuntime runtime;
            const auto info = downshift::checkDependencies();
            if (info.curlVersion.empty() || info.sqliteVersion.empty() || !info.http2) {
                std::cerr << "Missing required dependency capabilities.\n";
                return 1;
            }
            std::cout << "libcurl " << info.curlVersion << ", SQLite " << info.sqliteVersion
                      << ", HTTP/2 available, HTTP/3 "
                      << (info.http3 ? "available" : "not enabled") << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
