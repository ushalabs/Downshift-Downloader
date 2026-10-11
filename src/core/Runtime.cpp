#include "core/Runtime.h"

#include <curl/curl.h>
#include <sqlite3.h>

#include <cstring>
#include <memory>
#include <stdexcept>

namespace downshift {
namespace {

struct MultiDeleter {
    void operator()(CURLM* handle) const noexcept { curl_multi_cleanup(handle); }
};
struct DatabaseDeleter {
    void operator()(sqlite3* database) const noexcept { sqlite3_close(database); }
};
struct StatementDeleter {
    void operator()(sqlite3_stmt* statement) const noexcept { sqlite3_finalize(statement); }
};

bool hasProtocol(const curl_version_info_data& info, const char* expected) {
    if (info.protocols == nullptr) {
        return false;
    }
    for (const char* const* protocol = info.protocols; *protocol != nullptr; ++protocol) {
        if (std::strcmp(*protocol, expected) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

CurlRuntime::CurlRuntime() {
    const auto result = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (result != CURLE_OK) {
        throw std::runtime_error("Unable to initialize libcurl.");
    }
}

CurlRuntime::~CurlRuntime() { curl_global_cleanup(); }

DependencyInfo checkDependencies() {
    const auto* info = curl_version_info(CURLVERSION_NOW);
    if (info == nullptr || info->version == nullptr ||
        (info->features & CURL_VERSION_SSL) == 0 ||
        (info->features & CURL_VERSION_HTTP2) == 0 ||
        !hasProtocol(*info, "http") || !hasProtocol(*info, "https")) {
        throw std::runtime_error("libcurl must provide HTTP, HTTPS, TLS, and HTTP/2.");
    }

    std::unique_ptr<CURLM, MultiDeleter> multi(curl_multi_init());
    if (!multi) {
        throw std::runtime_error("Unable to create a libcurl multi handle.");
    }

    if (sqlite3_threadsafe() == 0 || sqlite3_libversion_number() != SQLITE_VERSION_NUMBER) {
        throw std::runtime_error("SQLite runtime must match the headers and support threads.");
    }

    sqlite3* rawDatabase = nullptr;
    const auto opened = sqlite3_open_v2(":memory:", &rawDatabase,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    std::unique_ptr<sqlite3, DatabaseDeleter> database(rawDatabase);
    if (opened != SQLITE_OK) {
        throw std::runtime_error("Unable to open an in-memory SQLite database.");
    }

    // Exercise writing and reading, rather than merely checking a version string.
    if (sqlite3_exec(database.get(),
            "CREATE TABLE foundation (value INTEGER NOT NULL);"
            "INSERT INTO foundation VALUES (4294967297);", nullptr, nullptr, nullptr) != SQLITE_OK) {
        throw std::runtime_error("SQLite foundation write check failed.");
    }
    sqlite3_stmt* rawStatement = nullptr;
    const auto prepared = sqlite3_prepare_v2(database.get(),
        "SELECT value FROM foundation", -1, &rawStatement, nullptr);
    std::unique_ptr<sqlite3_stmt, StatementDeleter> statement(rawStatement);
    if (prepared != SQLITE_OK || sqlite3_step(statement.get()) != SQLITE_ROW ||
        sqlite3_column_int64(statement.get(), 0) != 4294967297LL) {
        throw std::runtime_error("SQLite foundation 64-bit read check failed.");
    }

    return {info->version, sqlite3_libversion(),
        (info->features & CURL_VERSION_HTTP2) != 0,
        (info->features & CURL_VERSION_HTTP3) != 0};
}

} // namespace downshift
