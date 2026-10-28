#include "local_resource_snapshot.h"

#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <exception>
#include <mutex>
#include <set>
#include <utility>

namespace soui::sdk::detail {
namespace {
// These plain-object, directory-pin and stable-body checks are adapted from
// devtools/offline_snapshot.cpp. Business resources deliberately have their own
// bounds and closed-tree capture; no inspector policy or grant is reused.
constexpr std::size_t max_root_characters = 2048;
constexpr std::size_t max_root_components = 64;

struct process_snapshot_budget final {
    std::mutex mutex;
    std::size_t owners{};
    std::uint64_t bytes{};
};
[[nodiscard]] process_snapshot_budget& process_budget() {
    static process_snapshot_budget value;
    return value;
}

[[noreturn]] void reject(local_resource_failure code, const char* reason, DWORD error = 0) {
    throw local_resource_error(code, reason, error);
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
    void reset() noexcept {
        if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
        value_ = INVALID_HANDLE_VALUE;
    }
    HANDLE value_{INVALID_HANDLE_VALUE};
};

class find_handle final {
public:
    explicit find_handle(HANDLE value) noexcept : value_(value) {}
    ~find_handle() { if (value_ != INVALID_HANDLE_VALUE) FindClose(value_); }
    find_handle(const find_handle&) = delete;
    find_handle& operator=(const find_handle&) = delete;
    [[nodiscard]] HANDLE get() const noexcept { return value_; }
private:
    HANDLE value_{INVALID_HANDLE_VALUE};
};

[[nodiscard]] char lower_ascii(char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
}
[[nodiscard]] bool equal_ascii(std::string_view value, std::string_view expected) noexcept {
    if (value.size() != expected.size()) return false;
    for (std::size_t i = 0; i < value.size(); ++i)
        if (lower_ascii(value[i]) != expected[i]) return false;
    return true;
}
[[nodiscard]] bool device_name(std::string_view part) noexcept {
    const auto name = part.substr(0, part.find('.'));
    return equal_ascii(name, "con") || equal_ascii(name, "prn")
        || equal_ascii(name, "aux") || equal_ascii(name, "nul")
        || (name.size() == 4 && (equal_ascii(name.substr(0, 3), "com")
                || equal_ascii(name.substr(0, 3), "lpt")) && name[3] >= '1' && name[3] <= '9');
}
[[nodiscard]] std::string folded(std::string value) {
    for (auto& ch : value) ch = lower_ascii(ch);
    return value;
}
[[nodiscard]] std::wstring native_key(std::string_view key) {
    std::wstring value(key.begin(), key.end());
    std::replace(value.begin(), value.end(), L'/', L'\\');
    return value;
}
[[nodiscard]] bool same_path(const std::wstring& a, const std::wstring& b) {
    return a.size() == b.size() && CompareStringOrdinal(a.data(), static_cast<int>(a.size()),
        b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}
[[nodiscard]] std::wstring root_path(const std::filesystem::path& input) {
    auto value = input.native();
    if (value.size() < 3 || value.size() > max_root_characters || value[1] != L':'
        || !((value[0] >= L'A' && value[0] <= L'Z') || (value[0] >= L'a' && value[0] <= L'z'))
        || (value[2] != L'/' && value[2] != L'\\'))
        reject(local_resource_failure::invalid_root, "absolute-local-root-required");
    std::replace(value.begin(), value.end(), L'/', L'\\');
    while (value.size() > 3 && value.back() == L'\\') value.pop_back();
    const auto drive = value.substr(0, 3);
    const auto type = GetDriveTypeW(drive.c_str());
    if (type != DRIVE_FIXED && type != DRIVE_RAMDISK)
        reject(local_resource_failure::invalid_root, "local-fixed-volume-required");
    std::size_t begin = 3, components = 1;
    while (begin < value.size()) {
        const auto end = value.find(L'\\', begin);
        const auto part = value.substr(begin, end == std::wstring::npos ? value.size() - begin : end - begin);
        if (part.empty() || part == L"." || part == L".." || part.back() == L'.' || part.back() == L' '
            || ++components > max_root_components)
            reject(local_resource_failure::invalid_root, "root-segment");
        for (const wchar_t ch : part)
            if (ch < 0x20 || ch == L':' || ch == L'"' || ch == L'<' || ch == L'>'
                || ch == L'|' || ch == L'?' || ch == L'*')
                reject(local_resource_failure::invalid_root, "root-character");
        if (end == std::wstring::npos) break;
        begin = end + 1;
    }
    return value;
}
[[nodiscard]] std::wstring final_path(HANDLE handle) {
    std::array<wchar_t, 4096> buffer{};
    const auto count = GetFinalPathNameByHandleW(handle, buffer.data(), static_cast<DWORD>(buffer.size()),
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!count || count >= buffer.size())
        reject(local_resource_failure::identity, "final-path-unavailable", GetLastError());
    std::wstring value(buffer.data(), count);
    while (value.size() > 7 && value.back() == L'\\') value.pop_back();
    return value;
}
[[nodiscard]] BY_HANDLE_FILE_INFORMATION information(HANDLE handle, bool directory) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &info))
        reject(local_resource_failure::identity, "disk-identity-required", GetLastError());
    if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        reject(local_resource_failure::reparse, "reparse-point-rejected");
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_DEVICE)
        || !!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory
        || (!info.nFileIndexHigh && !info.nFileIndexLow))
        reject(local_resource_failure::identity, "plain-object-required");
    if (!directory && info.nNumberOfLinks != 1)
        reject(local_resource_failure::identity, "hard-link-rejected");
    return info;
}
[[nodiscard]] std::uint64_t length(const BY_HANDLE_FILE_INFORMATION& info) noexcept {
    return (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
}
[[nodiscard]] bool same_identity(const BY_HANDLE_FILE_INFORMATION& a, const BY_HANDLE_FILE_INFORMATION& b) noexcept {
    return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber
        && a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow;
}
[[nodiscard]] bool same_body(const BY_HANDLE_FILE_INFORMATION& a, const BY_HANDLE_FILE_INFORMATION& b) noexcept {
    return same_identity(a, b) && length(a) == length(b) && a.nNumberOfLinks == b.nNumberOfLinks
        && a.dwFileAttributes == b.dwFileAttributes
        && a.ftCreationTime.dwLowDateTime == b.ftCreationTime.dwLowDateTime
        && a.ftCreationTime.dwHighDateTime == b.ftCreationTime.dwHighDateTime
        && a.ftLastWriteTime.dwLowDateTime == b.ftLastWriteTime.dwLowDateTime
        && a.ftLastWriteTime.dwHighDateTime == b.ftLastWriteTime.dwHighDateTime;
}
[[nodiscard]] held_handle open_plain(const std::wstring& path, bool directory) {
    const std::wstring extended = L"\\\\?\\" + path;
    const DWORD access = directory ? FILE_READ_ATTRIBUTES : (GENERIC_READ | FILE_READ_ATTRIBUTES);
    const auto handle = CreateFileW(extended.c_str(), access, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_FLAG_SEQUENTIAL_SCAN), nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        reject(local_resource_failure::open, "resource-object-open-failed", GetLastError());
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
        if (pins_.size() >= local_resource_directories)
            reject(local_resource_failure::budget, "directory-count-budget");
        auto handle = open_plain(path, true);
        const auto identity = information(handle.get(), true);
        const auto final = final_path(handle.get());
        if (!same_path(final, L"\\\\?\\" + path))
            reject(local_resource_failure::identity, "directory-final-path-mismatch");
        pins_.emplace(path, pinned_directory{std::move(handle), identity, final});
    }
    void ancestors(const std::wstring& path) {
        add(path.substr(0, 3));
        std::size_t position = 3;
        while ((position = path.find(L'\\', position)) != std::wstring::npos) {
            add(path.substr(0, position));
            ++position;
        }
        add(path);
    }
    [[nodiscard]] const pinned_directory& at(const std::wstring& path) const { return pins_.at(path); }
    void verify() const {
        for (const auto& item : pins_) {
            const auto after = information(item.second.handle.get(), true);
            if (!same_identity(item.second.identity, after)
                || !same_path(item.second.final, final_path(item.second.handle.get())))
                reject(local_resource_failure::changed, "directory-identity-changed");
        }
    }
private:
    std::map<std::wstring, pinned_directory> pins_;
};

struct directory_member final {
    std::string name;
    bool directory{};
    bool operator==(const directory_member& other) const noexcept {
        return name == other.name && directory == other.directory;
    }
};
using directory_listing = std::vector<directory_member>;
[[nodiscard]] directory_listing list_directory(const std::wstring& path, std::string_view relative) {
    auto pattern = L"\\\\?\\" + path;
    if (pattern.back() != L'\\') pattern += L'\\';
    pattern += L'*';
    WIN32_FIND_DATAW info{};
    const auto raw = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &info, FindExSearchNameMatch, nullptr, 0);
    if (raw == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) return {};
        reject(local_resource_failure::open, "directory-enumeration-failed", error);
    }
    const find_handle handle(raw);
    directory_listing result;
    std::size_t names_bytes{};
    do {
        const std::wstring_view leaf(info.cFileName);
        if (leaf == L"." || leaf == L"..") continue;
        if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            reject(local_resource_failure::reparse, "tree-reparse-point-rejected");
        if ((info.dwFileAttributes & FILE_ATTRIBUTE_DEVICE)
            || leaf.empty() || leaf.size() > local_resource_segment_bytes)
            reject(local_resource_failure::invalid_path, "tree-leaf-rejected");
        std::string name;
        name.reserve(leaf.size());
        for (const auto ch : leaf) {
            if (ch > 0x7f || ch < 0x20)
                reject(local_resource_failure::invalid_path, "tree-noncanonical-character");
            name += static_cast<char>(ch);
        }
        const auto key = relative.empty() ? name : std::string(relative) + '/' + name;
        if (!local_resource_snapshot::valid_key(key))
            reject(local_resource_failure::invalid_path, "tree-key-rejected");
        if (result.size() >= local_resource_files + local_resource_directories
            || key.size() > local_resource_name_bytes - names_bytes)
            reject(local_resource_failure::budget, "directory-entry-budget");
        names_bytes += key.size();
        result.push_back({std::move(name), !!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)});
    } while (FindNextFileW(handle.get(), &info));
    if (GetLastError() != ERROR_NO_MORE_FILES)
        reject(local_resource_failure::read, "directory-enumeration-incomplete", GetLastError());
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    return result;
}
struct pinned_resource final {
    std::string key;
    held_handle handle;
    BY_HANDLE_FILE_INFORMATION identity{};
    std::wstring final;
};
class tree_capture final {
public:
    explicit tree_capture(std::wstring root) : root_(std::move(root)) {
        directories_.ancestors(root_);
        root_identity_ = directories_.at(root_).identity;
        root_final_ = directories_.at(root_).final;
        if (root_final_.back() != L'\\') root_final_ += L'\\';
        collect(root_, "");
        std::sort(files_.begin(), files_.end(), [](const auto& a, const auto& b) { return a.key < b.key; });
    }
    [[nodiscard]] const std::vector<pinned_resource>& files() const noexcept { return files_; }
    [[nodiscard]] std::uint64_t bytes() const noexcept { return bytes_; }
    [[nodiscard]] const BY_HANDLE_FILE_INFORMATION& root_identity() const noexcept { return root_identity_; }
    void verify() const {
        for (const auto& file : files_) {
            if (!same_body(file.identity, information(file.handle.get(), false))
                || !same_path(file.final, final_path(file.handle.get())))
                reject(local_resource_failure::changed, "resource-identity-changed");
        }
        directories_.verify();
        for (const auto& listing : listings_) {
            const auto full = listing.first.empty() ? root_ : root_ + L'\\' + native_key(listing.first);
            if (list_directory(full, listing.first) != listing.second)
                reject(local_resource_failure::changed, "closed-tree-membership-changed");
        }
        directories_.verify();
    }
private:
    void collect(const std::wstring& full, const std::string& relative) {
        auto listing = list_directory(full, relative);
        for (const auto& member : listing) {
            const auto key = relative.empty() ? member.name : relative + '/' + member.name;
            if (key.size() > local_resource_name_bytes - names_bytes_)
                reject(local_resource_failure::budget, "tree-name-byte-budget");
            names_bytes_ += key.size();
            if (!names_.insert(folded(key)).second)
                reject(local_resource_failure::invalid_path, "tree-case-alias");
            const auto child = full + (full.back() == L'\\' ? L"" : L"\\") + native_key(member.name);
            if (member.directory) {
                directories_.add(child);
                collect(child, key);
                continue;
            }
            if (files_.size() >= local_resource_files)
                reject(local_resource_failure::budget, "file-count-budget");
            auto handle = open_plain(child, false);
            const auto before = information(handle.get(), false);
            const auto final = final_path(handle.get());
            if (before.dwVolumeSerialNumber != root_identity_.dwVolumeSerialNumber
                || !same_path(final, root_final_ + native_key(key)))
                reject(local_resource_failure::identity, "resource-final-root-mismatch");
            const auto extent = length(before);
            if (extent > local_resource_file_bytes || extent > local_resource_total_bytes - bytes_)
                reject(local_resource_failure::budget, "resource-byte-budget");
            bytes_ += extent;
            files_.push_back({key, std::move(handle), before, final});
        }
        listings_.emplace(relative, std::move(listing));
    }
    std::wstring root_, root_final_;
    BY_HANDLE_FILE_INFORMATION root_identity_{};
    directory_pins directories_;
    std::vector<pinned_resource> files_;
    std::map<std::string, directory_listing> listings_;
    std::set<std::string> names_;
    std::size_t names_bytes_{};
    std::uint64_t bytes_{};
};

struct algorithm_closer final {
    void operator()(void* value) const noexcept { if (value) BCryptCloseAlgorithmProvider(value, 0); }
};
struct hash_closer final {
    void operator()(void* value) const noexcept { if (value) BCryptDestroyHash(value); }
};
class snapshot_hash final {
public:
    snapshot_hash() {
        BCRYPT_ALG_HANDLE raw_provider{};
        if (BCryptOpenAlgorithmProvider(&raw_provider, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            reject(local_resource_failure::crypto, "snapshot-hash-provider");
        provider_.reset(raw_provider);
        DWORD object_size{}, received{};
        if (BCryptGetProperty(provider_.get(), BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size),
                sizeof(object_size), &received, 0) < 0
            || received != sizeof(object_size) || !object_size || object_size > 65536)
            reject(local_resource_failure::crypto, "snapshot-hash-object-budget");
        object_.resize(object_size);
        BCRYPT_HASH_HANDLE raw_hash{};
        if (BCryptCreateHash(provider_.get(), &raw_hash, object_.data(), object_size, nullptr, 0, 0) < 0)
            reject(local_resource_failure::crypto, "snapshot-hash-create");
        hash_.reset(raw_hash);
    }
    void append(const void* data, std::size_t size) {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        std::array<std::uint8_t, 65536> block{};
        for (std::size_t offset = 0; offset < size;) {
            const auto amount = std::min(block.size(), size - offset);
            std::copy_n(bytes + offset, amount, block.data());
            if (BCryptHashData(hash_.get(), block.data(), static_cast<ULONG>(amount), 0) < 0)
                reject(local_resource_failure::crypto, "snapshot-hash-data");
            offset += amount;
        }
    }
    void number(std::uint64_t number) {
        std::array<std::uint8_t, 8> bytes{};
        for (std::size_t i = 0; i < bytes.size(); ++i)
            bytes[i] = static_cast<std::uint8_t>(number >> (i * 8));
        append(bytes.data(), bytes.size());
    }
    void text(std::string_view value) { number(value.size()); append(value.data(), value.size()); }
    [[nodiscard]] std::string finish() {
        std::array<std::uint8_t, 32> bytes{};
        if (BCryptFinishHash(hash_.get(), bytes.data(), static_cast<ULONG>(bytes.size()), 0) < 0)
            reject(local_resource_failure::crypto, "snapshot-hash-finish");
        constexpr char hex[] = "0123456789abcdef";
        std::string result(bytes.size() * 2, '0');
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            result[i * 2] = hex[bytes[i] >> 4];
            result[i * 2 + 1] = hex[bytes[i] & 15];
        }
        return result;
    }
private:
    std::unique_ptr<void, algorithm_closer> provider_;
    std::vector<unsigned char> object_;
    std::unique_ptr<void, hash_closer> hash_;
};

[[nodiscard]] std::uint32_t mime_type(std::string_view key) noexcept {
    const auto dot = key.rfind('.');
    if (dot == std::string_view::npos) return 0;
    const auto ext = key.substr(dot + 1);
    if (equal_ascii(ext, "html") || equal_ascii(ext, "htm")) return 1;
    if (equal_ascii(ext, "js") || equal_ascii(ext, "mjs") || equal_ascii(ext, "cjs")) return 2;
    if (equal_ascii(ext, "css")) return 3;
    if (equal_ascii(ext, "json") || equal_ascii(ext, "map")) return 4;
    if (equal_ascii(ext, "png")) return 5;
    if (equal_ascii(ext, "svg")) return 6;
    if (equal_ascii(ext, "gif")) return 7;
    if (equal_ascii(ext, "jpg") || equal_ascii(ext, "jpeg")) return 8;
    if (equal_ascii(ext, "woff")) return 9;
    if (equal_ascii(ext, "woff2")) return 10;
    if (equal_ascii(ext, "txt") || equal_ascii(ext, "md")) return 11;
    if (equal_ascii(ext, "wasm")) return 12;
    if (equal_ascii(ext, "ico")) return 13;
    if (equal_ascii(ext, "webp")) return 14;
    if (equal_ascii(ext, "ttf")) return 15;
    if (equal_ascii(ext, "otf")) return 16;
    if (equal_ascii(ext, "xml")) return 17;
    if (equal_ascii(ext, "pdf")) return 18;
    if (equal_ascii(ext, "mp4")) return 19;
    if (equal_ascii(ext, "mp3")) return 20;
    return 0;
}
[[nodiscard]] std::vector<std::uint8_t> read_body(const pinned_resource& file) {
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length(file.identity)));
    std::size_t offset{};
    while (offset < bytes.size()) {
        const auto amount = static_cast<DWORD>(std::min<std::size_t>(65536, bytes.size() - offset));
        DWORD received{};
        if (!ReadFile(file.handle.get(), bytes.data() + offset, amount, &received, nullptr) || !received)
            reject(local_resource_failure::read, "resource-read-failed", GetLastError());
        offset += received;
    }
    unsigned char extra{};
    DWORD additional{};
    if (!ReadFile(file.handle.get(), &extra, 1, &additional, nullptr) || additional)
        reject(local_resource_failure::changed, "resource-extent-changed", GetLastError());
    return bytes;
}
} // namespace

local_resource_error::local_resource_error(local_resource_failure code, const char* reason,
    unsigned long native_error) : std::runtime_error(reason), code_(code), native_error_(native_error) {}

bool local_resource_snapshot::valid_key(std::string_view key) noexcept {
    if (key.empty() || key.size() > local_resource_path_bytes) return false;
    std::size_t begin{}, depth{};
    while (begin <= key.size()) {
        const auto end = key.find('/', begin);
        const auto part = key.substr(begin, end == std::string_view::npos ? key.size() - begin : end - begin);
        if (++depth > local_resource_path_depth || part.empty() || part.size() > local_resource_segment_bytes
            || part == "." || part == ".." || part.back() == '.' || device_name(part)) return false;
        for (const unsigned char ch : part)
            if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
                || ch == '_' || ch == '-' || ch == '.')) return false;
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return true;
}

local_resource_snapshot::budget_lease::budget_lease() {
    auto& budget = process_budget();
    std::lock_guard<std::mutex> lock(budget.mutex);
    if (budget.owners >= local_resource_process_snapshots)
        reject(local_resource_failure::budget, "process-snapshot-count-budget");
    ++budget.owners;
}
local_resource_snapshot::budget_lease::~budget_lease() {
    auto& budget = process_budget();
    std::lock_guard<std::mutex> lock(budget.mutex);
    if (!budget.owners || bytes > budget.bytes) std::terminate();
    budget.bytes -= bytes;
    --budget.owners;
}
void local_resource_snapshot::budget_lease::reserve(std::uint64_t amount) {
    auto& budget = process_budget();
    std::lock_guard<std::mutex> lock(budget.mutex);
    if (bytes_reserved || amount > local_resource_total_bytes
        || amount > local_resource_process_bytes - budget.bytes)
        reject(local_resource_failure::budget, "process-snapshot-byte-budget");
    budget.bytes += amount;
    bytes = amount;
    bytes_reserved = true;
}

local_resource_snapshot::local_resource_snapshot(std::unique_ptr<budget_lease> budget,
    resources contents, std::string entry,
    std::uint64_t total, local_resource_identity identity)
    : budget_(std::move(budget)), contents_(std::move(contents)), entry_key_(std::move(entry)),
      total_bytes_(total), identity_(std::move(identity)) {}
local_resource_snapshot::~local_resource_snapshot() = default;

std::shared_ptr<const local_resource_snapshot> local_resource_snapshot::prepare(
    const std::filesystem::path& root, std::string entry_key) {
    // Acquire preparation capacity before walking or reading the tree. Native
    // bind has a separate budget and cannot account for this earlier SDK owner.
    auto budget = std::make_unique<budget_lease>();
    if (!entry_key.empty() && entry_key != "index.html" && entry_key != "react.html")
        reject(local_resource_failure::invalid_path, "entry-key-rejected");
    const tree_capture captured(root_path(root));
    // Metadata capture checked every file and total extent. Reserve the whole
    // immutable body before allocating even the first file response vector.
    budget->reserve(captured.bytes());
    if (entry_key.empty()) {
        const auto& files = captured.files();
        entry_key = std::any_of(files.begin(), files.end(), [](const auto& file) {
            return file.key == "react.html";
        }) ? "react.html" : "index.html";
    }
    resources contents;
    snapshot_hash hash;
    hash.text("soui.local-resource-snapshot/v1");
    hash.text(entry_key);
    hash.number(captured.files().size());
    for (const auto& file : captured.files()) {
        auto body = read_body(file);
        const auto mime = mime_type(file.key);
        hash.text(file.key);
        hash.number(mime);
        hash.number(body.size());
        hash.append(body.data(), body.size());
        contents.emplace(file.key, local_resource{std::move(body), mime});
    }
    if (contents.find(entry_key) == contents.end())
        reject(local_resource_failure::missing_entry, "entry-resource-missing");
    captured.verify();
    const auto& root_info = captured.root_identity();
    local_resource_identity identity{root_info.dwVolumeSerialNumber,
        (static_cast<std::uint64_t>(root_info.nFileIndexHigh) << 32) | root_info.nFileIndexLow, hash.finish()};
    return std::shared_ptr<const local_resource_snapshot>(new local_resource_snapshot(
        std::move(budget), std::move(contents), std::move(entry_key), captured.bytes(), std::move(identity)));
}

const local_resource* local_resource_snapshot::lookup(std::string_view key) const noexcept {
    if (!valid_key(key)) return nullptr;
    const auto found = contents_.find(key);
    return found == contents_.end() ? nullptr : &found->second;
}

const char* local_resource_failure_name(local_resource_failure code) noexcept {
    switch (code) {
    case local_resource_failure::invalid_root: return "invalid-root";
    case local_resource_failure::invalid_path: return "invalid-path";
    case local_resource_failure::budget: return "budget";
    case local_resource_failure::open: return "open";
    case local_resource_failure::reparse: return "reparse";
    case local_resource_failure::identity: return "identity";
    case local_resource_failure::read: return "read";
    case local_resource_failure::changed: return "changed";
    case local_resource_failure::missing_entry: return "missing-entry";
    case local_resource_failure::crypto: return "crypto";
    }
    return "unknown";
}

} // namespace soui::sdk::detail
