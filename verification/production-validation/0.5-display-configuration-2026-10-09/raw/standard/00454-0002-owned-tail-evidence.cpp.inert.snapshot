#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define NOMINMAX
#include "owned-tail-evidence.h"
#include <bcrypt.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace soui::benchmark {
namespace {
class held_handle final {
public:
    explicit held_handle(HANDLE value = nullptr) noexcept : value_(value) {}
    ~held_handle() { reset(); }
    held_handle(const held_handle&) = delete;
    held_handle& operator=(const held_handle&) = delete;
    held_handle(held_handle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    held_handle& operator=(held_handle&& other) noexcept {
        if (this != &other) { reset(); value_ = std::exchange(other.value_, nullptr); }
        return *this;
    }
    HANDLE get() const noexcept { return value_; }
    bool valid() const noexcept { return value_ && value_ != INVALID_HANDLE_VALUE; }
private:
    void reset() noexcept { if (valid()) CloseHandle(value_); value_ = nullptr; }
    HANDLE value_;
};

[[noreturn]] void api_error(const char* operation) {
    throw std::runtime_error(std::string(operation) + ":" + std::to_string(GetLastError()));
}

class budget final {
public:
    explicit budget(std::uint64_t milliseconds)
        : deadline_(std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds)) {}
    void check() const {
        if (std::chrono::steady_clock::now() >= deadline_) throw std::runtime_error("evidence-budget-expired");
    }
    DWORD remaining_ms() const {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline_) throw std::runtime_error("evidence-budget-expired");
        return static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline_ - now).count());
    }
private:
    std::chrono::steady_clock::time_point deadline_;
};

std::string text_json(const std::string& value) {
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char ch : value) {
        if (ch == '\\' || ch == '"') result += '\\';
        if (ch < 32) {
            result += "\\u00";
            result += hex[ch >> 4]; result += hex[ch & 15];
        } else result += static_cast<char>(ch);
    }
    return result + '"';
}

std::string wide_json(const std::wstring& value) {
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!size && !value.empty()) api_error("encode-path");
    std::string result(static_cast<std::size_t>(size), '\0');
    if (size && !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size, nullptr, nullptr)) api_error("encode-path");
    return text_json(result);
}

std::uint64_t joined(DWORD high, DWORD low) { return (std::uint64_t(high) << 32) | low; }
std::string decimal_json(std::uint64_t value) { return text_json(std::to_string(value)); }
std::string ascii_hex(const std::wstring& value) {
    std::string result; result.reserve(value.size());
    for (const auto ch : value) {
        if (!((ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f'))) throw std::runtime_error("non-ascii-hex");
        result += static_cast<char>(ch);
    }
    return result;
}

bool equal_path(const std::wstring& left, const std::wstring& right) {
    return CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()), right.c_str(),
                                static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

std::wstring final_path(HANDLE file, DWORD flags, const budget& time) {
    time.check();
    std::wstring result(32768, L'\0');
    const auto length = GetFinalPathNameByHandleW(file, result.data(), static_cast<DWORD>(result.size()), flags);
    if (!length || length >= result.size()) api_error("read-final-path");
    result.resize(length); time.check(); return result;
}

std::wstring canonical_input(std::wstring path) {
    std::replace(path.begin(), path.end(), L'/', L'\\');
    if (path.size() < 4 || path.size() >= 32767 || path[1] != L':' || path[2] != L'\\' ||
        !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')))
        throw std::runtime_error("pin-needs-local-drive-absolute-path");
    if (path.find(L':', 2) != std::wstring::npos) throw std::runtime_error("pin-alternate-stream-rejected");
    std::size_t start = 3;
    while (start < path.size()) {
        const auto end = path.find(L'\\', start);
        const auto component = path.substr(start, end == std::wstring::npos ? end : end - start);
        if (component.empty() || component == L"." || component == L".." ||
            component.back() == L'.' || component.back() == L' ')
            throw std::runtime_error("pin-noncanonical-component");
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    if (path.back() == L'\\') throw std::runtime_error("pin-directory-rejected");
    return path;
}

BY_HANDLE_FILE_INFORMATION information(HANDLE file, const budget& time) {
    time.check(); BY_HANDLE_FILE_INFORMATION result{};
    if (!GetFileInformationByHandle(file, &result)) api_error("read-file-object");
    time.check(); return result;
}

void crypto_ok(NTSTATUS status, const char* operation) {
    if (status < 0) throw std::runtime_error(std::string(operation) + ":" + std::to_string(status));
}

class cng_library final {
public:
    decltype(&BCryptOpenAlgorithmProvider) open = nullptr;
    decltype(&BCryptCloseAlgorithmProvider) close = nullptr;
    decltype(&BCryptGetProperty) property = nullptr;
    decltype(&BCryptCreateHash) create = nullptr;
    decltype(&BCryptDestroyHash) destroy = nullptr;
    decltype(&BCryptHashData) update = nullptr;
    decltype(&BCryptFinishHash) finish = nullptr;
    explicit cng_library(const budget& time) {
        time.check();
        wchar_t directory[32768]{};
        const auto length = GetSystemDirectoryW(directory, 32768);
        if (!length || length >= 32768) api_error("read-cng-system-directory");
        const auto path = std::wstring(directory, length) + L"\\bcrypt.dll";
        module_ = LoadLibraryW(path.c_str());
        if (!module_) api_error("load-system-cng");
        try {
            load(open, "BCryptOpenAlgorithmProvider"); load(close, "BCryptCloseAlgorithmProvider");
            load(property, "BCryptGetProperty"); load(create, "BCryptCreateHash");
            load(destroy, "BCryptDestroyHash"); load(update, "BCryptHashData"); load(finish, "BCryptFinishHash");
            time.check();
        } catch (...) { FreeLibrary(module_); module_ = nullptr; throw; }
    }
    ~cng_library() { if (module_) FreeLibrary(module_); }
    cng_library(const cng_library&) = delete;
    cng_library& operator=(const cng_library&) = delete;
private:
    template<class Function> void load(Function& target, const char* name) {
        const auto address = GetProcAddress(module_, name);
        if (!address) api_error(name);
        static_assert(sizeof(target) == sizeof(address), "Win32 function pointer representation");
        // Preserve the declared Win32 calling convention without a warning-
        // prone cast between unrelated C++ function pointer signatures.
        std::memcpy(&target, &address, sizeof(target));
    }
    HMODULE module_ = nullptr;
};

std::string hash_file(HANDLE file, std::uint64_t size, const budget& time) {
    constexpr std::uint64_t maximum = 64ULL * 1024 * 1024;
    if (!size || size > maximum) throw std::runtime_error("pin-file-size-out-of-range");
    time.check();
    const cng_library api(time); // The no-pin generic path never loads CNG.
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    crypto_ok(api.open(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0), "open-sha256");
    struct algorithm_owner { BCRYPT_ALG_HANDLE value; const cng_library& api; ~algorithm_owner() { api.close(value, 0); } } owner{algorithm, api};
    DWORD objectSize = 0, returned = 0;
    crypto_ok(api.property(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize),
        sizeof(objectSize), &returned, 0), "size-sha256-object");
    if (!objectSize || objectSize > 65536 || returned != sizeof(objectSize)) throw std::runtime_error("invalid-sha256-object-size");
    std::vector<unsigned char> object(objectSize);
    BCRYPT_HASH_HANDLE hash = nullptr;
    crypto_ok(api.create(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0), "create-sha256");
    struct hash_owner { BCRYPT_HASH_HANDLE value; const cng_library& api; ~hash_owner() { api.destroy(value); } } hashOwner{hash, api};
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN)) api_error("rewind-pin");
    std::vector<unsigned char> bytes(65536);
    std::uint64_t total = 0;
    while (total < size) {
        time.check(); DWORD read = 0;
        const auto wanted = static_cast<DWORD>(std::min<std::uint64_t>(bytes.size(), size - total));
        if (!ReadFile(file, bytes.data(), wanted, &read, nullptr)) api_error("read-pin-bytes");
        time.check();
        if (read != wanted) throw std::runtime_error("pin-file-length-changed");
        crypto_ok(api.update(hash, bytes.data(), read, 0), "hash-pin-bytes");
        time.check(); total += read;
    }
    unsigned char digest[32]{};
    crypto_ok(api.finish(hash, digest, sizeof(digest), 0), "finish-pin-hash");
    time.check();
    constexpr char hex[] = "0123456789abcdef";
    std::string result; result.reserve(64);
    for (const auto byte : digest) { result += hex[byte >> 4]; result += hex[byte & 15]; }
    return result;
}

struct disk_identity {
    std::wstring canonical;
    std::wstring volumePath;
    DWORD volumeSerial = 0;
    std::uint64_t fileId = 0;
    std::uint64_t size = 0;
    std::string sha256;
    std::string fields() const {
        return "\"canonicalPath\":" + wide_json(canonical) + ",\"volumePath\":" + wide_json(volumePath) +
            ",\"volumeSerialNumber\":" + decimal_json(volumeSerial) + ",\"fileId\":" + decimal_json(fileId) +
            ",\"size\":" + decimal_json(size) + ",\"sha256\":" + text_json(sha256);
    }
    bool equals(const disk_identity& other) const {
        return equal_path(canonical, other.canonical) && equal_path(volumePath, other.volumePath) &&
            volumeSerial == other.volumeSerial && fileId == other.fileId && size == other.size && sha256 == other.sha256;
    }
};

class pinned_file final {
public:
    pinned_file(const std::wstring& raw, const budget& time) {
        const auto path = canonical_input(raw);
        time.check();
        const std::wstring drive = path.substr(0, 3);
        if (GetDriveTypeW(drive.c_str()) != DRIVE_FIXED) throw std::runtime_error("pin-needs-local-fixed-volume");
        // Hold each directory without delete sharing: rename/reparse replacement
        // cannot change our traversed namespace while the final file stays pinned.
        for (std::size_t end = 2; end != std::wstring::npos; end = path.find(L'\\', end + 1)) {
            const auto directory = end == 2 ? drive : path.substr(0, end);
            time.check();
            held_handle held(CreateFileW(directory.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            if (!held.valid()) api_error("open-pin-directory");
            const auto info = information(held.get(), time);
            if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                throw std::runtime_error("pin-reparse-directory-rejected");
            auto canonical = final_path(held.get(), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS, time);
            auto expected = L"\\\\?\\" + directory;
            if (canonical.size() > 7 && canonical.back() == L'\\') canonical.pop_back();
            if (expected.size() > 7 && expected.back() == L'\\') expected.pop_back();
            if (!equal_path(canonical, expected)) throw std::runtime_error("pin-directory-path-differs");
            directories_.push_back(std::move(held));
        }
        file_ = held_handle(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        if (!file_.valid()) api_error("open-pin-file");
        const auto info = information(file_.get(), time);
        if (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))
            throw std::runtime_error("pin-reparse-or-directory-file-rejected");
        identity_.canonical = final_path(file_.get(), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS, time);
        if (!equal_path(identity_.canonical, L"\\\\?\\" + path)) throw std::runtime_error("pin-file-path-differs");
        identity_.volumePath = final_path(file_.get(), FILE_NAME_NORMALIZED | VOLUME_NAME_GUID, time);
        const auto volumeEnd = identity_.volumePath.find(L"}\\");
        if (identity_.volumePath.rfind(L"\\\\?\\Volume{", 0) != 0 || volumeEnd == std::wstring::npos)
            throw std::runtime_error("pin-volume-guid-unavailable");
        const auto volumeRoot = identity_.volumePath.substr(0, volumeEnd + 2);
        wchar_t filesystem[32]{};
        DWORD serial = 0;
        if (!GetVolumeInformationW(volumeRoot.c_str(), nullptr, 0, &serial, nullptr, nullptr, filesystem, 32))
            api_error("read-pin-filesystem");
        time.check();
        if (!equal_path(filesystem, L"NTFS") || serial != info.dwVolumeSerialNumber)
            throw std::runtime_error("pin-needs-consistent-ntfs-volume");
        identity_.volumeSerial = info.dwVolumeSerialNumber;
        identity_.fileId = joined(info.nFileIndexHigh, info.nFileIndexLow);
        identity_.size = joined(info.nFileSizeHigh, info.nFileSizeLow);
        if (!identity_.fileId) throw std::runtime_error("pin-file-id-unavailable");
        identity_.sha256 = hash_file(file_.get(), identity_.size, time);
        const auto after = information(file_.get(), time);
        if (after.dwVolumeSerialNumber != identity_.volumeSerial || joined(after.nFileIndexHigh, after.nFileIndexLow) != identity_.fileId ||
            joined(after.nFileSizeHigh, after.nFileSizeLow) != identity_.size)
            throw std::runtime_error("pin-file-object-changed");
    }
    const disk_identity& identity() const noexcept { return identity_; }
private:
    std::vector<held_handle> directories_;
    held_handle file_;
    disk_identity identity_;
};

JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting(HANDLE job, const budget& time) {
    time.check(); JOBOBJECT_BASIC_ACCOUNTING_INFORMATION result{};
    if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &result, sizeof(result), nullptr)) api_error("read-tail-accounting");
    time.check(); return result;
}

std::string account_json(const JOBOBJECT_BASIC_ACCOUNTING_INFORMATION& state) {
    return "{\"activeProcesses\":" + std::to_string(state.ActiveProcesses) +
        ",\"totalProcesses\":" + std::to_string(state.TotalProcesses) +
        ",\"limitTerminatedProcesses\":" + std::to_string(state.TotalTerminatedProcesses) + "}";
}

struct pid_list {
    DWORD assigned = 0;
    DWORD listed = 0;
    std::vector<DWORD> pids;
};
std::string pids_json(const std::vector<DWORD>& pids) {
    std::string result = "[";
    for (const auto pid : pids) { if (result.size() != 1) result += ','; result += std::to_string(pid); }
    return result + ']';
}

struct held_member {
    DWORD pid = 0;
    std::uint64_t creation = 0;
    held_handle process;
};

std::uint64_t process_creation(HANDLE process, const budget& time) {
    time.check(); FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) api_error("read-tail-creation");
    time.check();
    const auto value = joined(created.dwHighDateTime, created.dwLowDateTime);
    if (!value) throw std::runtime_error("tail-zero-creation-identity");
    return value;
}
bool owned_alive(HANDLE process, HANDLE job, const budget& time) {
    time.check(); BOOL inJob = FALSE;
    if (!IsProcessInJob(process, job, &inJob)) api_error("read-tail-membership");
    if (!inJob) throw std::runtime_error("tail-not-in-held-job");
    const auto waited = WaitForSingleObject(process, 0);
    if (waited == WAIT_FAILED) api_error("wait-tail-member");
    time.check(); return waited == WAIT_TIMEOUT;
}
std::wstring process_image(HANDLE process, const budget& time) {
    time.check(); std::wstring result(32768, L'\0'); DWORD size = static_cast<DWORD>(result.size());
    if (!QueryFullProcessImageNameW(process, 0, result.data(), &size)) api_error("read-tail-image");
    result.resize(size); time.check(); return result;
}
} // namespace

struct owned_tail_evidence::implementation {
    writer write;
    std::string nonce;
    std::uint64_t sequence = 0;
    std::unique_ptr<pinned_file> pin;
    std::vector<held_member> held;
    std::vector<DWORD> fixedPids;
    DWORD fixedTotal = 0;
    std::uint64_t decisive = 0;
    bool confirmed = false;
    bool complete = false;
    std::string failure = "not-applicable";
    std::uint64_t event(const char* name, const char* phase, unsigned attempt, const std::string& fields) {
        ++sequence;
        write("{\"schema\":\"soui.owned-tail-snapshot/v1\",\"event\":" + text_json(name) +
            ",\"nonce\":" + text_json(nonce) + ",\"sequence\":" + std::to_string(sequence) +
            ",\"phase\":" + text_json(phase) + ",\"attempt\":" + std::to_string(attempt) +
            (fields.empty() ? "" : "," + fields) + "}");
        return sequence;
    }
    pid_list list(HANDLE job, const budget& time, unsigned attempt, const char* phase, const char* pass) {
        std::vector<unsigned char> bytes(offsetof(JOBOBJECT_BASIC_PROCESS_ID_LIST, ProcessIdList) + 4 * sizeof(ULONG_PTR));
        for (unsigned growth = 0; growth <= 4; ++growth) {
            time.check();
            const bool ok = QueryInformationJobObject(job, JobObjectBasicProcessIdList, bytes.data(), static_cast<DWORD>(bytes.size()), nullptr) != FALSE;
            const auto error = ok ? ERROR_SUCCESS : GetLastError();
            time.check();
            const auto* raw = reinterpret_cast<const JOBOBJECT_BASIC_PROCESS_ID_LIST*>(bytes.data());
            const auto capacity = (bytes.size() - offsetof(JOBOBJECT_BASIC_PROCESS_ID_LIST, ProcessIdList)) / sizeof(ULONG_PTR);
            event("snapshot.list", phase, attempt, "\"pass\":" + text_json(pass) + ",\"growth\":" + std::to_string(growth) +
                ",\"capacity\":" + std::to_string(capacity) + ",\"assigned\":" + std::to_string(raw->NumberOfAssignedProcesses) +
                ",\"listed\":" + std::to_string(raw->NumberOfProcessIdsInList) + ",\"win32Error\":" + std::to_string(error));
            if (!ok && error != ERROR_MORE_DATA) { SetLastError(error); api_error("list-tail-job"); }
            if (ok && raw->NumberOfProcessIdsInList > capacity) throw std::runtime_error("tail-list-invalid-capacity");
            if (!ok || raw->NumberOfProcessIdsInList < raw->NumberOfAssignedProcesses) {
                if (growth == 4 || bytes.size() == 65536) throw std::runtime_error("tail-list-growth-budget");
                const auto requested = offsetof(JOBOBJECT_BASIC_PROCESS_ID_LIST, ProcessIdList) +
                    std::uint64_t(raw->NumberOfAssignedProcesses) * sizeof(ULONG_PTR);
                if (requested > 65536) throw std::runtime_error("tail-list-size-budget");
                bytes.assign(static_cast<std::size_t>(std::min<std::uint64_t>(65536, std::max<std::uint64_t>(bytes.size() * 2, requested))), 0);
                continue;
            }
            if (raw->NumberOfAssignedProcesses != raw->NumberOfProcessIdsInList) throw std::runtime_error("tail-list-assigned-differs");
            pid_list result{raw->NumberOfAssignedProcesses, raw->NumberOfProcessIdsInList, {}};
            std::set<DWORD> unique;
            for (DWORD index = 0; index < raw->NumberOfProcessIdsInList; ++index) {
                const auto pid = raw->ProcessIdList[index];
                if (!pid || pid > std::numeric_limits<DWORD>::max() || !unique.insert(static_cast<DWORD>(pid)).second)
                    throw std::runtime_error("tail-list-duplicate-or-invalid-pid");
                result.pids.push_back(static_cast<DWORD>(pid));
            }
            std::sort(result.pids.begin(), result.pids.end()); return result;
        }
        throw std::runtime_error("tail-list-unreachable");
    }
};

owned_tail_evidence::owned_tail_evidence(const std::wstring& path, const std::wstring& expected,
                                       const std::wstring& nonce, writer write)
    : implementation_(std::make_unique<implementation>()) {
    auto& state = *implementation_; state.write = std::move(write);
    state.nonce = ascii_hex(nonce);
    try {
        FILETIME observed{}; GetSystemTimeAsFileTime(&observed);
        const auto observedTick = GetTickCount64();
        const budget time(2000);
        state.pin = std::make_unique<pinned_file>(path, time);
        const auto expectedText = ascii_hex(expected);
        if (state.pin->identity().sha256 != expectedText) throw std::runtime_error("pin-expected-hash-differs");
        state.event("pin", "prelaunch", 0, state.pin->identity().fields() +
            ",\"observedBeforeLaunchUtcFileTime\":" + decimal_json(joined(observed.dwHighDateTime, observed.dwLowDateTime)) +
            ",\"observedBeforeLaunchTickMs\":" + decimal_json(observedTick) +
            ",\"pinBudgetMs\":2000,\"snapshotBudgetMs\":1000,\"maximumFileBytes\":\"67108864\",\"diskBytesOnly\":true");
    } catch (const std::exception& error) {
        state.event("pin.error", "prelaunch", 0, "\"error\":" + text_json(error.what()));
        throw;
    }
}
owned_tail_evidence::~owned_tail_evidence() = default;

void owned_tail_evidence::before_cleanup(HANDLE job, bool eligible_reason) {
    auto& state = *implementation_;
    if (!eligible_reason) { state.failure = "reason-ineligible"; return; }
    const budget time(1000);
    for (unsigned attempt = 1; attempt <= 3; ++attempt) {
        bool begun = false;
        bool ended = false;
        try {
            const auto before = accounting(job, time);
            state.event("snapshot.begin", "pre-cleanup", attempt, "\"accountingBefore\":" + account_json(before));
            begun = true;
            const auto first = state.list(job, time, attempt, "pre-cleanup", "first");
            std::vector<held_member> candidate;
            bool alive = true, imagesMatch = true;
            for (std::size_t index = 0; index < first.pids.size(); ++index) {
                const auto pid = first.pids[index]; time.check();
                held_member member{pid, 0, held_handle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid))};
                if (!member.process.valid()) api_error("open-tail-member");
                member.creation = process_creation(member.process.get(), time);
                const bool living = owned_alive(member.process.get(), job, time);
                alive = alive && living;
                const auto image = process_image(member.process.get(), time);
                pinned_file opened(image, time);
                const bool matches = opened.identity().equals(state.pin->identity());
                imagesMatch = imagesMatch && matches;
                state.event("snapshot.member", "pre-cleanup", attempt, "\"index\":" + std::to_string(index) +
                    ",\"pid\":" + std::to_string(pid) + ",\"creationFileTime\":" + decimal_json(member.creation) +
                    ",\"inHeldJob\":true,\"alive\":" + (living ? "true" : "false") + ",\"pinMatched\":" + (matches ? "true" : "false") +
                    ",\"image\":" + wide_json(image) + ",\"diskObject\":{" + opened.identity().fields() + "}");
                candidate.push_back(std::move(member));
            }
            const auto second = state.list(job, time, attempt, "pre-cleanup", "second");
            const auto after = accounting(job, time);
            const bool countsMatch = before.ActiveProcesses == first.assigned && first.assigned == first.listed &&
                first.listed == candidate.size() && candidate.size() == after.ActiveProcesses &&
                first.pids == second.pids && second.assigned == after.ActiveProcesses &&
                before.TotalProcesses == after.TotalProcesses && before.TotalTerminatedProcesses == 0 && after.TotalTerminatedProcesses == 0;
            const bool stable = countsMatch && alive;
            const bool eligible = stable && imagesMatch && candidate.size() == 1;
            const bool sticky = !imagesMatch || before.TotalTerminatedProcesses != 0 || after.TotalTerminatedProcesses != 0;
            const auto end = state.event("snapshot.end", "pre-cleanup", attempt, "\"accountingAfter\":" + account_json(after) +
                ",\"assigned\":" + std::to_string(first.assigned) + ",\"listed\":" + std::to_string(first.listed) +
                ",\"uniquePids\":" + std::to_string(first.pids.size()) + ",\"members\":" + std::to_string(candidate.size()) +
                ",\"pids\":" + pids_json(first.pids) + ",\"secondPids\":" + pids_json(second.pids) +
                ",\"complete\":" + (stable ? "true" : "false") + ",\"eligible\":" + (eligible ? "true" : "false") +
                ",\"stickyFailure\":" + (sticky ? "true" : "false"));
            ended = true;
            if (!eligible) {
                state.failure = sticky ? "image-or-limit-mismatch" : stable ? "tail-count-not-one" : "membership-changed";
                if (sticky || stable) return;
                continue;
            }
            state.held = std::move(candidate); state.fixedPids = first.pids; state.fixedTotal = before.TotalProcesses;
            const auto lastList = state.list(job, time, attempt, "pre-cleanup-confirm", "final");
            const auto last = accounting(job, time);
            bool identities = true;
            for (const auto& member : state.held) {
                identities = identities && process_creation(member.process.get(), time) == member.creation;
                identities = owned_alive(member.process.get(), job, time) && identities;
            }
            const bool confirmed = identities && lastList.pids == state.fixedPids && lastList.assigned == state.held.size() &&
                last.ActiveProcesses == state.held.size() && last.TotalProcesses == state.fixedTotal && last.TotalTerminatedProcesses == 0;
            state.event("pre-cleanup-confirm", "pre-cleanup-confirm", attempt, "\"snapshotSequence\":" + std::to_string(end) +
                ",\"accounting\":" + account_json(last) + ",\"pids\":" + pids_json(lastList.pids) +
                ",\"identitiesMatch\":" + (identities ? "true" : "false") + ",\"confirmed\":" + (confirmed ? "true" : "false"));
            time.check();
            if (!confirmed) { state.failure = "final-membership-changed"; state.held.clear(); continue; }
            state.decisive = end; state.confirmed = true; state.failure.clear(); return;
        } catch (const std::exception& error) {
            if (!begun) state.event("snapshot.begin", "pre-cleanup", attempt, "\"accountingBefore\":null");
            state.event(ended ? "pre-cleanup-error" : "snapshot.end", ended ? "pre-cleanup-confirm" : "pre-cleanup", attempt,
                "\"complete\":false,\"eligible\":false,\"stickyFailure\":true,\"error\":" + text_json(error.what()));
            state.failure = error.what(); state.held.clear(); return;
        }
    }
}

void owned_tail_evidence::after_cleanup(HANDLE job) {
    auto& state = *implementation_;
    if (state.confirmed) {
        try {
            const budget time(1000);
            bool closed = true;
            // Job accounting may become empty before process objects signal.
            // Wait on the held objects under one shared budget, then take both
            // empty accounting observations without changing the fixed total.
            for (std::size_t index = 0; index < state.held.size(); ++index) {
                const auto& member = state.held[index];
                const auto waited = WaitForSingleObject(member.process.get(), time.remaining_ms());
                if (waited == WAIT_FAILED) api_error("wait-tail-closure");
                const auto creation = process_creation(member.process.get(), time);
                const bool matching = waited == WAIT_OBJECT_0 && creation == member.creation;
                state.event("cleanup.member", "post-cleanup", 0, "\"index\":" + std::to_string(index) +
                    ",\"pid\":" + std::to_string(member.pid) + ",\"creationFileTime\":" + decimal_json(creation) +
                    ",\"waitResult\":" + std::to_string(waited) + ",\"identityMatches\":" + (creation == member.creation ? "true" : "false") +
                    ",\"signaled\":" + (waited == WAIT_OBJECT_0 ? "true" : "false"));
                time.check(); closed = closed && matching;
            }
            for (unsigned attempt = 1; attempt <= 2; ++attempt) {
                const auto current = accounting(job, time);
                bool signaled = true;
                for (const auto& member : state.held) {
                    const auto waited = WaitForSingleObject(member.process.get(), 0);
                    if (waited == WAIT_FAILED) api_error("confirm-tail-closed");
                    signaled = signaled && waited == WAIT_OBJECT_0;
                }
                const bool matching = current.ActiveProcesses == 0 && current.TotalProcesses == state.fixedTotal &&
                    current.TotalTerminatedProcesses == 0 && signaled;
                state.event("cleanup.confirm", "post-cleanup", attempt, "\"snapshotSequence\":" + std::to_string(state.decisive) +
                    ",\"accounting\":" + account_json(current) + ",\"fixedTotalProcesses\":" + std::to_string(state.fixedTotal) +
                    ",\"allHeldSignaled\":" + (signaled ? "true" : "false") + ",\"confirmed\":" + (matching ? "true" : "false"));
                time.check();
                closed = closed && matching;
            }
            state.complete = closed;
            if (!closed) state.failure = "post-cleanup-accounting-or-identity-changed";
        } catch (const std::exception& error) {
            state.failure = error.what(); state.complete = false;
            state.event("cleanup.error", "post-cleanup", 0, "\"error\":" + text_json(error.what()));
        }
    }
    state.event("tail-evidence", "post-cleanup", 0, "\"preCleanupSnapshotSequence\":" +
        (state.decisive ? std::to_string(state.decisive) : "null") + ",\"tailEvidenceComplete\":" +
        (state.complete ? "true" : "false") + ",\"failure\":" + (state.failure.empty() ? "null" : text_json(state.failure)) +
        ",\"diskBytesOnly\":true");
}

std::string owned_tail_evidence::terminal_fields() const {
    const auto& state = *implementation_;
    return ",\"preCleanupSnapshotSequence\":" + (state.decisive ? std::to_string(state.decisive) : "null") +
        ",\"tailEvidenceComplete\":" + (state.complete ? "true" : "false");
}
} // namespace soui::benchmark
