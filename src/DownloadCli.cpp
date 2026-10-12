#include "core/DownloadEngine/DownloadEngine.h"
#include "core/Runtime.h"

#include <charconv>
#include <iostream>
#include <string_view>

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: DownshiftDownload.exe URL DESTINATION [--sha256 HEX] [--ca-bundle FILE] [--timeout-ms N]\n";
        return argc == 2 && std::wstring_view(argv[1]) == L"--help" ? 0 : 2;
    }
    try {
        downshift::DownloadOptions options;
        const auto encoded = std::filesystem::path(argv[1]).u8string();
        options.url.assign(reinterpret_cast<const char*>(encoded.data()), encoded.size());
        options.destination = argv[2];
        for (int i = 3; i < argc; ++i) {
            const std::wstring_view flag(argv[i]);
            if (++i >= argc) throw downshift::DownloadError(downshift::DownloadErrorCode::InvalidRequest, "A command option is missing its value.");
            if (flag == L"--ca-bundle") options.caBundle = argv[i];
            else if (flag == L"--sha256") {
                const auto value = std::filesystem::path(argv[i]).u8string();
                options.expectedSha256 = std::string(reinterpret_cast<const char*>(value.data()), value.size());
            } else if (flag == L"--timeout-ms") {
                const auto value = std::filesystem::path(argv[i]).string();
                long timeout = 0;
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), timeout);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || timeout <= 0) throw downshift::DownloadError(downshift::DownloadErrorCode::InvalidRequest, "Timeout must be a positive millisecond value.");
                options.transferTimeoutMs = timeout;
            } else throw downshift::DownloadError(downshift::DownloadErrorCode::InvalidRequest, "Unknown command option.");
        }
        downshift::CurlRuntime runtime;
        const auto result = downshift::DownloadEngine{}.download(options);
        std::cout << "Completed " << result.bytes << " bytes; mode=" << (result.segmented ? "segmented" : "streaming")
                  << "; ranges=" << result.completedRanges << "; fallback=" << (result.fellBack ? "yes" : "no") << '\n';
        return 0;
    } catch (const downshift::DownloadError& error) {
        std::cerr << "Downshift: " << error.what() << " [category=" << static_cast<int>(error.code()) << "]\n";
        return 1;
    } catch (const std::exception&) {
        std::cerr << "Downshift: an unexpected local error prevented the download.\n";
        return 1;
    }
}
