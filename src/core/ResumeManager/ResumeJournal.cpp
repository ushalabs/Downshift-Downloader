#include "ResumeJournal.h"
#include "core/DownloadEngine/DownloadEngine.h"
#include "core/DownloadEngine/HttpResponse.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dpapi.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <memory>

namespace downshift {
namespace {
constexpr std::size_t maximumBytes = 32 * 1024 * 1024, maximumRanges = 100'000;
[[noreturn]] void invalid() { throw DownloadError(DownloadErrorCode::ResumeUnsafe, "Recovery metadata is invalid or unavailable. Use an explicit restart to archive the old partial."); }
[[noreturn]] void fileFail() { throw DownloadError(DownloadErrorCode::File, "Unable to safely read or save recovery metadata."); }
[[noreturn]] void replaceFail(DWORD error) {
    throw DownloadError(DownloadErrorCode::File, "Unable to replace recovery metadata. Windows error " + std::to_string(error) + ".");
}
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct LocalMemory { void operator()(unsigned char* p) const noexcept { LocalFree(p); } };
using LocalBytes = std::unique_ptr<unsigned char, LocalMemory>;
std::uint64_t now() { return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()); }
bool present(const std::filesystem::path& path) {
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    const auto error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return false;
    fileFail();
}
void regular(HANDLE handle) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info) || info.nNumberOfLinks != 1 ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) invalid();
}
void rename(HANDLE handle, const std::filesystem::path& path) {
    const auto name = path.wstring();
    const auto bytes = name.size() * sizeof(wchar_t);
    std::vector<unsigned char> buffer(sizeof(FILE_RENAME_INFO) + bytes, 0);
    auto* info = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    info->ReplaceIfExists = FALSE;
    info->FileNameLength = static_cast<DWORD>(bytes);
    std::memcpy(info->FileName, name.data(), bytes);
    if (!SetFileInformationByHandle(handle, FileRenameInfo, info, static_cast<DWORD>(buffer.size()))) fileFail();
}
void integer(std::vector<unsigned char>& data, std::uint64_t n) {
    for (unsigned i = 0; i < 8; ++i) data.push_back(static_cast<unsigned char>(n >> (8 * i)));
}
void text(std::vector<unsigned char>& data, const std::string& value) {
    integer(data, value.size()); data.insert(data.end(), value.begin(), value.end());
}
struct Reader {
    const unsigned char* data; std::size_t size, offset = 0;
    std::uint64_t integer() {
        if (size - offset < 8) invalid();
        std::uint64_t n = 0;
        for (unsigned i = 0; i < 8; ++i) n |= static_cast<std::uint64_t>(data[offset++]) << (8 * i);
        return n;
    }
    std::string text() {
        const auto n = integer();
        if (n > 32 * 1024 || n > size - offset) invalid();
        std::string value(reinterpret_cast<const char*>(data + offset), static_cast<std::size_t>(n));
        offset += static_cast<std::size_t>(n);
        if (value.find('\0') != std::string::npos) invalid();
        return value;
    }
};
bool digest(const std::string& s) {
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
} // namespace

std::string ResumeJournal::newId() {
    std::array<unsigned char, 16> bytes{};
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) fileFail();
    std::string result;
    for (auto b : bytes) { result += "0123456789abcdef"[b >> 4]; result += "0123456789abcdef"[b & 15]; }
    return result;
}
std::string ResumeJournal::pathText(const std::filesystem::path& path) {
    const auto s = std::filesystem::absolute(path).lexically_normal().u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}
ResumeJournal::ResumeJournal(const std::filesystem::path& destination)
    : destination_(std::filesystem::absolute(destination).lexically_normal()), path_(destination_) {
    path_ += L".part.resume";
    auto lease = destination_; lease += L".part.lock";
    auto handle = CreateFileW(lease.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw DownloadError(DownloadErrorCode::File, "Another worker owns this download, or its recovery lock is unavailable.");
    try { regular(handle); } catch (...) { CloseHandle(handle); throw; }
    lease_ = handle;
}
ResumeJournal::~ResumeJournal() { if (lease_) CloseHandle(lease_); }
bool ResumeJournal::exists() const { return present(path_); }

void ResumeJournal::validate(const RecoveryRecord& record) {
    for (const auto* value : {&record.id, &record.url, &record.resolvedUrl, &record.destination, &record.fileIdentity, &record.etag, &record.lastModified, &record.expectedSha256, &record.finalSha256})
        if (value->size() > 32 * 1024 || value->find('\0') != std::string::npos) invalid();
    if (record.id.size() != 32 || record.url.empty() || record.destination.empty() || record.fileIdentity.empty() ||
        record.completed.size() > maximumRanges || record.total > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        record.status > RecoveryStatus::Finalizing || (!record.expectedSha256.empty() && !digest(record.expectedSha256)) ||
        (!record.finalSha256.empty() && !digest(record.finalSha256)) ||
        (record.resumable && (!strongEtag(record.etag) || record.resolvedUrl.empty() || record.total == 0)) ||
        (!record.resumable && !record.completed.empty())) invalid();
    std::uint64_t next = 0;
    for (const auto& r : record.completed) {
        if (r.range.first < next || r.range.last < r.range.first || r.range.last >= record.total || !digest(r.sha256)) invalid();
        next = r.range.last + 1;
    }
}
RecoveryRecord ResumeJournal::load() const {
    Handle file{CreateFileW(path_.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (file.value == INVALID_HANDLE_VALUE) invalid();
    regular(file.value);
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file.value, &length) || length.QuadPart <= 0 || length.QuadPart > maximumBytes) invalid();
    std::vector<unsigned char> encrypted(static_cast<std::size_t>(length.QuadPart));
    DWORD read = 0;
    if (!ReadFile(file.value, encrypted.data(), static_cast<DWORD>(encrypted.size()), &read, nullptr) || read != encrypted.size()) invalid();
    DATA_BLOB input{read, encrypted.data()}, output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) invalid();
    LocalBytes memory(output.pbData);
    if (output.cbData > maximumBytes) invalid();
    Reader reader{output.pbData, output.cbData};
    if (reader.integer() != 0x44534a4e4c000002ULL) invalid(); // Versioned format.
    RecoveryRecord r;
    r.id = reader.text(); r.url = reader.text(); r.resolvedUrl = reader.text(); r.destination = reader.text();
    r.fileIdentity = reader.text(); r.etag = reader.text(); r.lastModified = reader.text(); r.expectedSha256 = reader.text();
    r.finalSha256 = reader.text();
    r.total = reader.integer(); r.created = reader.integer(); r.updated = reader.integer();
    const auto capable = reader.integer(); if (capable > 1) invalid(); r.resumable = capable != 0;
    r.status = static_cast<RecoveryStatus>(reader.integer());
    const auto count = reader.integer(); if (count > maximumRanges) invalid();
    for (std::uint64_t i = 0; i < count; ++i) r.completed.push_back({{reader.integer(), reader.integer()}, reader.text()});
    if (reader.offset != reader.size) invalid();
    validate(r);
    return r;
}
void ResumeJournal::save(RecoveryRecord& r) {
    validate(r);
    r.updated = now(); if (r.created == 0) r.created = r.updated;
    std::vector<unsigned char> data;
    integer(data, 0x44534a4e4c000002ULL);
    for (const auto* value : {&r.id, &r.url, &r.resolvedUrl, &r.destination, &r.fileIdentity, &r.etag, &r.lastModified, &r.expectedSha256, &r.finalSha256}) text(data, *value);
    for (auto value : {r.total, r.created, r.updated, static_cast<std::uint64_t>(r.resumable), static_cast<std::uint64_t>(r.status), static_cast<std::uint64_t>(r.completed.size())}) integer(data, value);
    for (const auto& range : r.completed) { integer(data, range.range.first); integer(data, range.range.last); text(data, range.sha256); }
    if (data.size() > maximumBytes - 4096) fileFail();
    DATA_BLOB input{static_cast<DWORD>(data.size()), data.data()}, output{};
    if (!CryptProtectData(&input, L"Downshift recovery", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) fileFail();
    LocalBytes memory(output.pbData);
    SecureZeroMemory(data.data(), data.size());
    const auto temp = path_.parent_path() / (L".downshift-" + std::filesystem::path(newId()).wstring() + L".tmp");
    try {
        {
            Handle file{CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_HIDDEN, nullptr)};
            if (file.value == INVALID_HANDLE_VALUE) fileFail();
            DWORD written = 0;
            if (!WriteFile(file.value, output.pbData, output.cbData, &written, nullptr) || written != output.cbData || !FlushFileBuffers(file.value)) fileFail();
        }
        // A short-lived reader/filter can deny replacement after our flushed
        // temporary handle closes. Keep the old journal intact while retrying.
        for (unsigned attempt = 0;; ++attempt) {
            if (MoveFileExW(temp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) break;
            const auto error = GetLastError();
            if (attempt == 8 || (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION)) replaceFail(error);
            Sleep(25); // At most 200 ms; no permission/attribute changes or deletion.
        }
    } catch (...) { DeleteFileW(temp.c_str()); throw; }
}
bool ResumeJournal::remove() noexcept { return DeleteFileW(path_.c_str()) != 0 || GetLastError() == ERROR_FILE_NOT_FOUND; }
void ResumeJournal::archivePrevious() {
    if (present(destination_)) throw DownloadError(DownloadErrorCode::File, "The destination already exists; it will not be overwritten.");
    auto partial = destination_; partial += L".part";
    Handle file, metadata;
    if (present(partial)) {
        file.value = CreateFileW(partial.c_str(), GENERIC_READ | DELETE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file.value == INVALID_HANDLE_VALUE) fileFail(); regular(file.value);
    }
    if (present(path_)) {
        metadata.value = CreateFileW(path_.c_str(), GENERIC_READ | DELETE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (metadata.value == INVALID_HANDLE_VALUE) fileFail(); regular(metadata.value);
    }
    const auto archived = destination_.parent_path() / (L"Downshift.previous-" + std::filesystem::path(newId()).wstring() + L".part");
    if (file.value != INVALID_HANDLE_VALUE) rename(file.value, archived);
    try {
        if (metadata.value != INVALID_HANDLE_VALUE) { auto target = archived; target += L".resume"; rename(metadata.value, target); }
    } catch (...) {
        if (file.value != INVALID_HANDLE_VALUE) { try { rename(file.value, partial); } catch (...) {} }
        throw;
    }
}
} // namespace downshift
