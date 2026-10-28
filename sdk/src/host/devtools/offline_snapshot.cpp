#include "offline_snapshot.h"
// Ported from experiments/devtools-stage0/resources/offline_snapshot.cpp.

#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <limits>
#include <set>
#include <utility>

namespace soui::sdk::detail::inspector_resources {
namespace {

[[noreturn]] void reject(failure code, const char* reason, DWORD error = 0) {
    throw resource_error(code, reason, error);
}

class held_handle final {
public:
    held_handle() noexcept = default;
    explicit held_handle(HANDLE value) noexcept : value_(value) {}
    ~held_handle() { reset(); }
    held_handle(const held_handle&) = delete;
    held_handle& operator=(const held_handle&) = delete;
    held_handle(held_handle&& other) noexcept : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE)) {}
    held_handle& operator=(held_handle&& other) noexcept {
        if (this != &other) { reset(); value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE); }
        return *this;
    }
    [[nodiscard]] HANDLE get() const noexcept { return value_; }
private:
    void reset() noexcept { if (value_ != INVALID_HANDLE_VALUE && value_) CloseHandle(value_); value_ = INVALID_HANDLE_VALUE; }
    HANDLE value_{INVALID_HANDLE_VALUE};
};

[[nodiscard]] std::string ascii_lower(std::string value) {
    for (auto& ch : value) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + ('a' - 'A'));
    return value;
}
[[nodiscard]] bool device_name(std::string value) {
    value = ascii_lower(value.substr(0, value.find('.')));
    return value == "con" || value == "prn" || value == "aux" || value == "nul"
        || (value.size() == 4 && (value.substr(0, 3) == "com" || value.substr(0, 3) == "lpt")
            && value[3] >= '1' && value[3] <= '9');
}
void validate_relative(std::string_view path) {
    if (path.empty() || path.size() > max_path_bytes) reject(failure::invalid_path, "relative-path-extent");
    std::size_t begin = 0, depth = 0;
    while (begin <= path.size()) {
        const auto end = path.find('/', begin);
        const auto segment = path.substr(begin, end == std::string_view::npos ? path.size() - begin : end - begin);
        if (++depth > max_path_depth || segment.empty() || segment.size() > max_segment_bytes
            || segment == "." || segment == ".." || segment.back() == '.' || device_name(std::string(segment)))
            reject(failure::invalid_path, "relative-segment");
        for (const unsigned char ch : segment) {
            if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
                || ch == '_' || ch == '-' || ch == '.')) reject(failure::invalid_path, "relative-character");
        }
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
}
[[nodiscard]] std::wstring native_relative(std::string_view path) {
    std::wstring result(path.begin(), path.end());
    std::replace(result.begin(), result.end(), L'/', L'\\'); return result;
}
[[nodiscard]] bool same_path(const std::wstring& first, const std::wstring& second) {
    return first.size() == second.size() && CompareStringOrdinal(first.data(), static_cast<int>(first.size()),
        second.data(), static_cast<int>(second.size()), TRUE) == CSTR_EQUAL;
}
[[nodiscard]] std::wstring approved_root_path(const std::filesystem::path& input) {
    std::wstring path = input.native();
    if (path.size() < 3 || path.size() > max_root_characters || path[1] != L':'
        || !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z'))
        || (path[2] != L'\\' && path[2] != L'/')) reject(failure::invalid_root, "absolute-local-root-required");
    std::replace(path.begin(), path.end(), L'/', L'\\');
    while (path.size() > 3 && path.back() == L'\\') path.pop_back();
    const std::wstring drive = path.substr(0, 3);
    const UINT drive_type = GetDriveTypeW(drive.c_str());
    if (drive_type != DRIVE_FIXED && drive_type != DRIVE_RAMDISK) reject(failure::invalid_root, "local-fixed-volume-required");
    std::size_t begin = 3, components = 1;
    while (begin < path.size()) {
        const auto end = path.find(L'\\', begin);
        const auto part = path.substr(begin, end == std::wstring::npos ? path.size() - begin : end - begin);
        if (part.empty() || part == L"." || part == L".." || part.back() == L'.' || part.back() == L' '
            || ++components > 64) reject(failure::invalid_root, "root-segment");
        for (const wchar_t ch : part)
            if (ch < 0x20 || ch == L':' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|' || ch == L'?' || ch == L'*')
                reject(failure::invalid_root, "root-character");
        if (end == std::wstring::npos) break;
        begin = end + 1;
    }
    return path;
}
[[nodiscard]] std::wstring final_path(HANDLE handle) {
    std::vector<wchar_t> text(4096);
    const DWORD count = GetFinalPathNameByHandleW(handle, text.data(), static_cast<DWORD>(text.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!count || count >= text.size()) reject(failure::identity, "final-path-unavailable", GetLastError());
    std::wstring result(text.data(), count);
    while (result.size() > 7 && result.back() == L'\\') result.pop_back();
    return result;
}
[[nodiscard]] BY_HANDLE_FILE_INFORMATION information(HANDLE handle, bool directory) {
    BY_HANDLE_FILE_INFORMATION result{};
    if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &result))
        reject(failure::identity, "disk-identity-required", GetLastError());
    if (result.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) reject(failure::reparse, "reparse-point-rejected");
    if (result.dwFileAttributes & FILE_ATTRIBUTE_DEVICE) reject(failure::identity, "device-rejected");
    if (!!(result.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory
        || (!result.nFileIndexHigh && !result.nFileIndexLow)) reject(failure::identity, "plain-object-required");
    if (!directory && result.nNumberOfLinks != 1) reject(failure::identity, "hard-link-rejected");
    return result;
}
[[nodiscard]] std::uint64_t length(const BY_HANDLE_FILE_INFORMATION& info) noexcept {
    return (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
}
[[nodiscard]] bool same_identity(const BY_HANDLE_FILE_INFORMATION& a, const BY_HANDLE_FILE_INFORMATION& b) noexcept {
    return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber && a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow;
}
[[nodiscard]] bool same_body_info(const BY_HANDLE_FILE_INFORMATION& a, const BY_HANDLE_FILE_INFORMATION& b) noexcept {
    return same_identity(a, b) && length(a) == length(b) && a.nNumberOfLinks == b.nNumberOfLinks
        && a.ftLastWriteTime.dwLowDateTime == b.ftLastWriteTime.dwLowDateTime
        && a.ftLastWriteTime.dwHighDateTime == b.ftLastWriteTime.dwHighDateTime;
}
[[nodiscard]] held_handle open_plain(const std::wstring& path, bool directory) {
    const std::wstring extended = L"\\\\?\\" + path;
    const DWORD access = directory ? FILE_READ_ATTRIBUTES : (GENERIC_READ | FILE_READ_ATTRIBUTES);
    HANDLE handle = CreateFileW(extended.c_str(), access, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_FLAG_SEQUENTIAL_SCAN), nullptr);
    if (handle == INVALID_HANDLE_VALUE) reject(failure::open, "approved-object-open-failed", GetLastError());
    return held_handle(handle);
}
struct pinned_directory final {
    held_handle handle;
    BY_HANDLE_FILE_INFORMATION identity{};
    std::wstring final;
};
class directory_pins final {
public:
    void add(const std::wstring& path) {
        if (pins_.find(path) != pins_.end()) return;
        if (pins_.size() >= max_directories) reject(failure::budget, "directory-count-budget");
        auto handle = open_plain(path, true);
        const auto identity = information(handle.get(), true);
        const auto final = final_path(handle.get());
        if (!same_path(final, L"\\\\?\\" + path)) reject(failure::identity, "directory-final-path-mismatch");
        pins_.emplace(path, pinned_directory{ std::move(handle), identity, final });
    }
    void ancestors(const std::wstring& path) {
        add(path.substr(0, 3));
        std::size_t position = 3;
        for (;;) {
            position = path.find(L'\\', position);
            if (position == std::wstring::npos) break;
            add(path.substr(0, position)); ++position;
        }
        add(path);
    }
    [[nodiscard]] const pinned_directory& at(const std::wstring& path) const { return pins_.at(path); }
    void verify() const {
        for (const auto& pair : pins_) {
            const auto after = information(pair.second.handle.get(), true);
            if (!same_identity(after, pair.second.identity) || !same_path(final_path(pair.second.handle.get()), pair.second.final))
                reject(failure::changed, "directory-identity-changed");
        }
    }
private:
    std::map<std::wstring, pinned_directory> pins_;
};
struct pinned_resource final {
    manifest_entry entry;
    held_handle handle;
    BY_HANDLE_FILE_INFORMATION identity{};
    std::wstring final;
};
struct algorithm_closer final { void operator()(void* value) const noexcept { if (value) BCryptCloseAlgorithmProvider(value, 0); } };
struct hash_closer final { void operator()(void* value) const noexcept { if (value) BCryptDestroyHash(value); } };

// Separate from member loading: a kernel image has its own frozen bound and
// is hashed incrementally, without increasing the inspector resource budget.
[[nodiscard]] digest hash_kernel_image(HANDLE file, std::uint64_t extent) {
    BCRYPT_ALG_HANDLE raw_provider{};
    if (BCryptOpenAlgorithmProvider(&raw_provider, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        reject(failure::crypto, "kernel-hash-provider");
    std::unique_ptr<void, algorithm_closer> provider(raw_provider);
    DWORD object_bytes{}, received{};
    if (BCryptGetProperty(provider.get(), BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_bytes),
            sizeof(object_bytes), &received, 0) < 0
        || received != sizeof(object_bytes) || !object_bytes || object_bytes > 65536)
        reject(failure::crypto, "kernel-hash-object-budget");
    std::vector<unsigned char> object(object_bytes);
    BCRYPT_HASH_HANDLE raw_hash{};
    if (BCryptCreateHash(provider.get(), &raw_hash, object.data(), object_bytes, nullptr, 0, 0) < 0)
        reject(failure::crypto, "kernel-hash-create");
    std::unique_ptr<void, hash_closer> hash(raw_hash);
    std::array<unsigned char, 65536> block{};
    std::uint64_t offset{};
    while (offset < extent) {
        const auto amount = static_cast<DWORD>(std::min<std::uint64_t>(block.size(), extent - offset));
        DWORD count{};
        if (!ReadFile(file, block.data(), amount, &count, nullptr) || !count)
            reject(failure::read, "kernel-read-failed", GetLastError());
        if (BCryptHashData(hash.get(), block.data(), count, 0) < 0)
            reject(failure::crypto, "kernel-hash-data");
        offset += count;
    }
    unsigned char extra{};
    DWORD additional{};
    if (!ReadFile(file, &extra, 1, &additional, nullptr) || additional)
        reject(failure::changed, "kernel-extent-changed", GetLastError());
    digest result{};
    if (BCryptFinishHash(hash.get(), result.data(), static_cast<ULONG>(result.size()), 0) < 0)
        reject(failure::crypto, "kernel-hash-finish");
    return result;
}
} // namespace

resource_error::resource_error(failure code, const char* reason, unsigned long native_error)
    : std::runtime_error(reason), code_(code), native_error_(native_error) {}

digest sha256(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() > std::numeric_limits<ULONG>::max()) reject(failure::budget, "hash-extent");
    BCRYPT_ALG_HANDLE raw_provider{};
    if (BCryptOpenAlgorithmProvider(&raw_provider, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) reject(failure::crypto, "hash-provider");
    std::unique_ptr<void, algorithm_closer> provider(raw_provider);
    DWORD object_bytes{}, received{};
    if (BCryptGetProperty(provider.get(), BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_bytes), sizeof(object_bytes), &received, 0) < 0
        || received != sizeof(object_bytes) || !object_bytes || object_bytes > 65536) reject(failure::crypto, "hash-object-budget");
    std::vector<unsigned char> object(object_bytes);
    BCRYPT_HASH_HANDLE raw_hash{};
    if (BCryptCreateHash(provider.get(), &raw_hash, object.data(), object_bytes, nullptr, 0, 0) < 0) reject(failure::crypto, "hash-create");
    std::unique_ptr<void, hash_closer> hash(raw_hash);
    // The historical CNG signature takes mutable PUCHAR even for input. Keep
    // immutable caller storage const and feed a fixed bounded scratch block.
    std::array<unsigned char, 65536> block{};
    std::size_t offset{};
    while (offset < bytes.size()) {
        const auto amount = std::min(block.size(), bytes.size() - offset);
        std::copy_n(bytes.data() + offset, amount, block.data());
        if (BCryptHashData(hash.get(), block.data(), static_cast<ULONG>(amount), 0) < 0) reject(failure::crypto, "hash-data");
        offset += amount;
    }
    digest result{};
    if (BCryptFinishHash(hash.get(), result.data(), static_cast<ULONG>(result.size()), 0) < 0) reject(failure::crypto, "hash-finish");
    return result;
}

digest parse_digest(std::string_view hex) {
    if (hex.size() != 64 || hex.find_first_not_of("0123456789abcdef") != std::string_view::npos)
        reject(failure::digest_mismatch, "digest-format");
    const auto nibble = [](char ch) { return static_cast<std::uint8_t>(ch <= '9' ? ch - '0' : ch - 'a' + 10); };
    digest value{};
    for (std::size_t index = 0; index < value.size(); ++index) value[index] = static_cast<std::uint8_t>((nibble(hex[index * 2]) << 4) | nibble(hex[index * 2 + 1]));
    return value;
}
std::string digest_hex(const digest& value) {
    constexpr char alphabet[] = "0123456789abcdef";
    std::string text(value.size() * 2, '0');
    for (std::size_t index = 0; index < value.size(); ++index) { text[index * 2] = alphabet[value[index] >> 4]; text[index * 2 + 1] = alphabet[value[index] & 15]; }
    return text;
}
const char* failure_name(failure value) noexcept {
    switch (value) {
    case failure::invalid_root: return "invalid-root"; case failure::invalid_path: return "invalid-path";
    case failure::unknown_resource: return "unknown-resource"; case failure::manifest_duplicate: return "manifest-duplicate";
    case failure::budget: return "budget"; case failure::open: return "open"; case failure::reparse: return "reparse";
    case failure::identity: return "identity"; case failure::size: return "size"; case failure::digest_mismatch: return "digest";
    case failure::read: return "read"; case failure::changed: return "changed"; case failure::crypto: return "crypto";
    }
    return "unknown";
}

resource_snapshot::resource_snapshot(resources contents, snapshot_identity identity)
    : contents_(std::move(contents)), identity_(identity) {}

resource_snapshot resource_snapshot::load(const std::filesystem::path& approved_root, const std::vector<manifest_entry>& manifest
) {
    if (manifest.empty() || manifest.size() > max_files) reject(failure::budget, "manifest-count-budget");
    std::set<std::string> names;
    std::uint64_t total{};
    for (const auto& entry : manifest) {
        validate_relative(entry.path);
        if (!names.insert(ascii_lower(entry.path)).second) reject(failure::manifest_duplicate, "manifest-case-alias");
        if (entry.bytes > max_file_bytes || entry.bytes > max_total_bytes - total) reject(failure::budget, "manifest-byte-budget");
        total += entry.bytes;
    }
    const std::wstring root = approved_root_path(approved_root);
    directory_pins directories;
    directories.ancestors(root);
    const auto root_info = directories.at(root).identity;
    std::wstring root_final = directories.at(root).final;
    if (root_final.back() != L'\\') root_final += L'\\';
    std::vector<pinned_resource> opened;
    opened.reserve(manifest.size());
    for (const auto& entry : manifest) {
        const std::wstring relative = native_relative(entry.path);
        const std::wstring full = root + (root.back() == L'\\' ? L"" : L"\\") + relative;
        std::size_t separator = 0;
        while ((separator = relative.find(L'\\', separator)) != std::wstring::npos) {
            directories.add(root + (root.back() == L'\\' ? L"" : L"\\") + relative.substr(0, separator)); ++separator;
        }
        auto handle = open_plain(full, false);
        const auto info = information(handle.get(), false);
        const auto final = final_path(handle.get());
        if (info.dwVolumeSerialNumber != root_info.dwVolumeSerialNumber || !same_path(final, root_final + relative))
            reject(failure::identity, "file-final-root-mismatch");
        if (length(info) != entry.bytes) reject(failure::size, "file-size-mismatch");
        opened.push_back(pinned_resource{ entry, std::move(handle), info, final });
    }
    resources contents;
    for (const auto& file : opened) {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file.entry.bytes));
        std::size_t offset{};
        while (offset < bytes.size()) {
            const auto amount = static_cast<DWORD>(std::min<std::size_t>(65536, bytes.size() - offset));
            DWORD received{};
            if (!ReadFile(file.handle.get(), bytes.data() + offset, amount, &received, nullptr) || !received)
                reject(failure::read, "resource-read-failed", GetLastError());
            offset += received;
        }
        unsigned char extra{}; DWORD additional{};
        if (!ReadFile(file.handle.get(), &extra, 1, &additional, nullptr) || additional)
            reject(failure::changed, "resource-extent-changed", GetLastError());
        if (sha256(bytes) != file.entry.sha256) reject(failure::digest_mismatch, "resource-digest-mismatch");
        const auto after = information(file.handle.get(), false);
        if (!same_body_info(file.identity, after) || !same_path(final_path(file.handle.get()), file.final))
            reject(failure::changed, "resource-identity-changed");
        contents.emplace(file.entry.path, std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes)));
    }
    directories.verify();
    return resource_snapshot(std::move(contents), snapshot_identity{ root_info.dwVolumeSerialNumber,
        (static_cast<std::uint64_t>(root_info.nFileIndexHigh) << 32) | root_info.nFileIndexLow, total, manifest.size() });
}

package_manifest read_package_manifest(const std::filesystem::path& approved_root) {
    const std::wstring root = approved_root_path(approved_root);
    directory_pins directories;
    directories.ancestors(root);
    const auto root_info = directories.at(root).identity;
    std::wstring root_final = directories.at(root).final;
    if (root_final.back() != L'\\') root_final += L'\\';
    const auto full = root + (root.back() == L'\\' ? L"" : L"\\") + L"approved-manifest.json";
    auto handle = open_plain(full, false);
    const auto before = information(handle.get(), false);
    const auto final = final_path(handle.get());
    if (before.dwVolumeSerialNumber != root_info.dwVolumeSerialNumber
        || !same_path(final, root_final + L"approved-manifest.json"))
        reject(failure::identity, "manifest-final-root-mismatch");
    const auto extent = length(before);
    if (!extent || extent > max_manifest_bytes) reject(failure::budget, "manifest-byte-budget");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(extent));
    std::size_t offset{};
    while (offset < bytes.size()) {
        const auto amount = static_cast<DWORD>(std::min<std::size_t>(65536, bytes.size() - offset));
        DWORD received{};
        if (!ReadFile(handle.get(), bytes.data() + offset, amount, &received, nullptr) || !received)
            reject(failure::read, "manifest-read-failed", GetLastError());
        offset += received;
    }
    unsigned char extra{};
    DWORD additional{};
    if (!ReadFile(handle.get(), &extra, 1, &additional, nullptr) || additional)
        reject(failure::changed, "manifest-extent-changed", GetLastError());
    const auto identity = sha256(bytes);
    const auto after = information(handle.get(), false);
    if (!same_body_info(before, after) || !same_path(final_path(handle.get()), final))
        reject(failure::changed, "manifest-identity-changed");
    directories.verify();
    return {std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes)), identity};
}

void verify_kernel_image(const std::filesystem::path& loaded_kernel, std::string_view expected_sha256) {
    const auto expected = parse_digest(expected_sha256);
    const auto leaf = loaded_kernel.filename().native();
    if (leaf.empty() || leaf == L"." || leaf == L".." || leaf.back() == L'.' || leaf.back() == L' ')
        reject(failure::invalid_path, "kernel-leaf-required");
    for (const auto ch : leaf)
        if (ch < 0x20 || ch == L':' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|'
            || ch == L'?' || ch == L'*' || ch == L'\\' || ch == L'/')
            reject(failure::invalid_path, "kernel-leaf-character");
    const auto root = approved_root_path(loaded_kernel.parent_path());
    directory_pins directories;
    directories.ancestors(root);
    const auto root_info = directories.at(root).identity;
    auto root_final = directories.at(root).final;
    if (root_final.back() != L'\\') root_final += L'\\';
    const auto full = root + (root.back() == L'\\' ? L"" : L"\\") + leaf;
    auto handle = open_plain(full, false);
    const auto before = information(handle.get(), false);
    const auto final = final_path(handle.get());
    if (before.dwVolumeSerialNumber != root_info.dwVolumeSerialNumber || !same_path(final, root_final + leaf))
        reject(failure::identity, "kernel-final-root-mismatch");
    const auto extent = length(before);
    if (!extent || extent > max_kernel_image_bytes) reject(failure::budget, "kernel-image-byte-budget");
    if (hash_kernel_image(handle.get(), extent) != expected)
        reject(failure::digest_mismatch, "kernel-image-digest-mismatch");
    const auto after = information(handle.get(), false);
    if (!same_body_info(before, after) || !same_path(final_path(handle.get()), final))
        reject(failure::changed, "kernel-identity-changed");
    directories.verify();
}

immutable_bytes resource_snapshot::read_virtual(std::string_view url) const {
    if (url.size() < virtual_origin.size() || url.substr(0, virtual_origin.size()) != virtual_origin)
        reject(failure::invalid_path, "fixed-origin-required");
    const auto path = url.substr(virtual_origin.size());
    validate_relative(path);
    const auto found = contents_.find(path);
    if (found == contents_.end()) reject(failure::unknown_resource, "manifest-resource-required");
    return found->second;
}

} // namespace soui::sdk::detail::inspector_resources
