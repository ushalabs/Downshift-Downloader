#include "FileWriter.h"
#include "core/DownloadEngine/DownloadEngine.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <bcrypt.h>
#include <winioctl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <vector>

namespace downshift {
namespace {
struct Timer {
    double& seconds;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ~Timer() { seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); }
};
[[noreturn]] void fileError(const char* message) {
    throw DownloadError(DownloadErrorCode::File, message);
}
void seek(HANDLE handle, std::uint64_t offset) {
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) fileError("File offset exceeds the supported 64-bit size.");
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN)) fileError("Unable to seek in the partial file.");
}
bool invalidFilename(std::wstring name) {
    if (name.empty() || name.back() == L'.' || name.back() == L' ' || name.size() > 240) return true;
    for (const auto c : name) if (c < 32 || std::wstring_view(L"<>:\"/\\|?*").find(c) != std::wstring_view::npos) return true;
    name = name.substr(0, name.find(L'.'));
    for (auto& c : name) if (c >= L'a' && c <= L'z') c -= L'a' - L'A';
    return name == L"CON" || name == L"PRN" || name == L"AUX" || name == L"NUL" ||
        (name.size() == 4 && (name.starts_with(L"COM") || name.starts_with(L"LPT")) &&
         ((name[3] >= L'1' && name[3] <= L'9') || name[3] == L'\u00b9' || name[3] == L'\u00b2' || name[3] == L'\u00b3'));
}
void cryptoCheck(NTSTATUS status) {
    if (status < 0) throw DownloadError(DownloadErrorCode::Integrity, "Unable to calculate the file checksum.");
}
struct Sha256 {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<unsigned char> object;
    ~Sha256() {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    }
};
} // namespace

FileWriter::FileWriter(const std::filesystem::path& destination, bool reopen)
    : FileWriter(destination, reopen ? FileOpenMode::ReopenPartial : FileOpenMode::CreatePartial) {}

std::filesystem::path FileWriter::validatedDestination(const std::filesystem::path& destination) {
    if (invalidFilename(destination.filename().wstring())) fileError("Choose a valid Windows destination filename.");
    std::filesystem::path absolute;
    try { absolute = std::filesystem::absolute(destination).lexically_normal(); }
    catch (const std::filesystem::filesystem_error&) { fileError("Unable to resolve the destination path."); }
    const auto path = absolute.wstring();
    if (path.starts_with(L"\\\\.\\") || path.starts_with(L"\\\\?\\") || path.size() > 240) fileError("Device paths and excessively long destination paths are not supported.");
    return absolute;
}

FileWriter::FileWriter(const std::filesystem::path& destination, FileOpenMode mode) {
    destination_ = validatedDestination(destination);
    const auto path = destination_.wstring();
    if (mode != FileOpenMode::InspectCompleted && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) fileError("The destination already exists; it will not be overwritten.");
    auto partial = destination_;
    if (mode != FileOpenMode::InspectCompleted) partial += L".part";
    const auto handle = CreateFileW(partial.c_str(), mode == FileOpenMode::InspectCompleted ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr,
        mode == FileOpenMode::CreatePartial ? CREATE_NEW : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) fileError("Cannot create the partial file. Check permissions, disk space, and existing .part files.");
    handle_ = handle;
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info) || info.nNumberOfLinks != 1 ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) {
        CloseHandle(handle); handle_ = nullptr;
        fileError("The partial must be a regular file with no links or reparse points.");
    }
}

FileWriter::~FileWriter() { if (handle_) CloseHandle(handle_); }

void FileWriter::preallocate(std::uint64_t bytes, bool sparse) {
    Timer timer{metrics_.allocationSeconds};
    if (!handle_) fileError("The partial file is closed.");
    seek(handle_, bytes);
    if (sparse) {
        DWORD returned = 0;
        if (!DeviceIoControl(handle_, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr)) fileError("The filesystem does not support sparse partial files.");
    } else if (bytes != 0) {
        FILE_ALLOCATION_INFO allocation{};
        allocation.AllocationSize.QuadPart = static_cast<LONGLONG>(bytes);
        if (!SetFileInformationByHandle(handle_, FileAllocationInfo, &allocation, sizeof(allocation))) fileError("Unable to reserve disk space for the partial file.");
    }
    if (!SetEndOfFile(handle_)) fileError("Unable to set the partial file size.");
}

void FileWriter::reset() {
    if (!handle_) fileError("The partial file is closed.");
    seek(handle_, 0);
    if (!SetEndOfFile(handle_)) fileError("Unable to reset the partial file for streaming fallback.");
}

void FileWriter::writeAt(std::uint64_t offset, const char* data, std::size_t bytes) {
    Timer timer{metrics_.writeSeconds};
    if (!handle_) fileError("The partial file is closed.");
    const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (offset > maximum || bytes > maximum - offset) fileError("File write exceeds the supported 64-bit size.");
    seek(handle_, offset);
    while (bytes != 0) {
        const auto count = static_cast<DWORD>(std::min<std::size_t>(bytes, std::numeric_limits<DWORD>::max()));
        DWORD written = 0;
        if (!WriteFile(handle_, data, count, &written, nullptr) || written == 0) fileError("Partial-file write failed. Check disk space and permissions.");
        data += written;
        bytes -= written;
        metrics_.writeBytes += written;
    }
}

std::uint64_t FileWriter::size() const {
    LARGE_INTEGER value{};
    if (!handle_ || !GetFileSizeEx(handle_, &value) || value.QuadPart < 0) fileError("Unable to inspect the partial file size.");
    return static_cast<std::uint64_t>(value.QuadPart);
}

std::string FileWriter::sha256() {
    return sha256Range(0, size());
}

std::string FileWriter::identity() const {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle_, &info)) fileError("Unable to identify the partial file.");
    return std::to_string(info.dwVolumeSerialNumber) + ":" + std::to_string(info.nFileIndexHigh) + ":" + std::to_string(info.nFileIndexLow);
}

void FileWriter::flush() {
    Timer timer{metrics_.flushSeconds};
    if (!FlushFileBuffers(handle_)) fileError("Unable to flush the partial data to disk.");
}

std::string FileWriter::sha256Range(std::uint64_t offset, std::uint64_t bytes) {
    Timer timer{metrics_.checksumSeconds};
    if (offset > size() || bytes > size() - offset) fileError("Cannot hash bytes outside the partial file.");
    Sha256 state;
    cryptoCheck(BCryptOpenAlgorithmProvider(&state.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
    DWORD objectBytes = 0, returned = 0;
    cryptoCheck(BCryptGetProperty(state.algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes), &returned, 0));
    state.object.resize(objectBytes);
    cryptoCheck(BCryptCreateHash(state.algorithm, &state.hash, state.object.data(), objectBytes, nullptr, 0, 0));
    seek(handle_, offset);
    std::array<unsigned char, 64 * 1024> buffer{};
    while (bytes != 0) {
        DWORD received = 0;
        if (!ReadFile(handle_, buffer.data(), static_cast<DWORD>(std::min<std::uint64_t>(bytes, buffer.size())), &received, nullptr) || received == 0) fileError("Unable to read the partial file for checksum verification.");
        bytes -= received;
        cryptoCheck(BCryptHashData(state.hash, buffer.data(), received, 0));
    }
    std::array<unsigned char, 32> digest{};
    cryptoCheck(BCryptFinishHash(state.hash, digest.data(), static_cast<ULONG>(digest.size()), 0));
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const auto byte : digest) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}

void FileWriter::commit(std::uint64_t expectedSize, const std::optional<std::string>& expectedSha256) {
    if (size() != expectedSize) throw DownloadError(DownloadErrorCode::Integrity, "The completed file size does not match the transfer.");
    if (expectedSha256 && sha256() != *expectedSha256) throw DownloadError(DownloadErrorCode::Integrity, "SHA-256 verification failed; the partial file was not finalized.");
    flush();
    const auto name = destination_.wstring();
    const auto nameBytes = name.size() * sizeof(wchar_t);
    std::vector<unsigned char> buffer(sizeof(FILE_RENAME_INFO) + nameBytes, 0);
    auto* info = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    info->ReplaceIfExists = FALSE;
    info->RootDirectory = nullptr;
    info->FileNameLength = static_cast<DWORD>(nameBytes);
    std::memcpy(info->FileName, name.data(), nameBytes);
    if (!SetFileInformationByHandle(handle_, FileRenameInfo, info, static_cast<DWORD>(buffer.size()))) fileError("Cannot finalize the file. The destination may exist or lack rename permission.");
    CloseHandle(handle_);
    handle_ = nullptr;
}

} // namespace downshift
