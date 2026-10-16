#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <wbemidl.h>
#include <shellapi.h>
#include <algorithm>
#include <cstddef>
#include <chrono>
#include <cwchar>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <set>
#include <json/json.hpp>

namespace {
class handle {
public:
    explicit handle(HANDLE value = nullptr) noexcept : value_(value) {}
    ~handle() { reset(); }
    handle(const handle&) = delete;
    handle& operator=(const handle&) = delete;
    HANDLE get() const noexcept { return value_; }
    bool valid() const noexcept { return value_ && value_ != INVALID_HANDLE_VALUE; }
    HANDLE release() noexcept { const auto value = value_; value_ = nullptr; return value; }
    void reset(HANDLE value = nullptr) noexcept { if (valid()) CloseHandle(value_); value_ = value; }
private:
    HANDLE value_;
};
struct api_error : std::runtime_error {
    api_error(const char* operation, unsigned long value) : std::runtime_error(operation), code(value) {}
    unsigned long code;
};
void need(bool value, const char* operation) { if (!value) throw api_error(operation, ERROR_INVALID_DATA); }
void check(bool value, const char* operation) { if (!value) throw api_error(operation, GetLastError()); }
void hcheck(HRESULT value, const char* operation) { if (FAILED(value)) throw api_error(operation, static_cast<unsigned long>(value)); }
std::uint64_t joined(DWORD high, DWORD low) { return (std::uint64_t(high) << 32) | low; }
HANDLE duplicate(HANDLE value) {
    HANDLE result = nullptr;
    check(DuplicateHandle(GetCurrentProcess(), value, GetCurrentProcess(), &result, 0, FALSE, DUPLICATE_SAME_ACCESS) != FALSE, "duplicate-handle");
    return result;
}
std::wstring full(const std::wstring& raw) {
    need(raw.size() > 2 && raw[1] == L':' && (raw[2] == L'\\' || raw[2] == L'/'), "absolute-local-path");
    std::wstring value(32768, L'\0');
    const auto length = GetFullPathNameW(raw.c_str(), static_cast<DWORD>(value.size()), value.data(), nullptr);
    check(length != 0, "full-path"); need(length < value.size(), "path-bound"); value.resize(length); return value;
}
void plain_ancestors(std::wstring value) {
    for (;;) {
        const auto attributes = GetFileAttributesW(value.c_str());
        check(attributes != INVALID_FILE_ATTRIBUTES, "path-attributes");
        need((attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0, "plain-path-only");
        const auto slash = value.find_last_of(L"\\/");
        if (slash == std::wstring::npos || slash <= 2) break;
        value.resize(slash);
    }
}
std::wstring quote(const std::wstring& value) {
    // The existing OwnedProcessRunner rule, including empty argv.
    std::wstring result = L"\""; std::size_t slashes = 0;
    for (const auto character : value) {
        if (character == L'\\') { ++slashes; continue; }
        result.append(slashes * (character == L'\"' ? 2 : 1), L'\\'); slashes = 0;
        if (character == L'\"') result += L'\\';
        result += character;
    }
    result.append(slashes * 2, L'\\'); result += L'\"'; return result;
}
std::string jtext(const std::wstring& value) {
    const auto length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    check(length != 0 || value.empty(), "utf8-size");
    std::string raw(static_cast<std::size_t>(length), '\0');
    check(length == 0 || WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), raw.data(), length, nullptr, nullptr) != 0, "utf8-encode");
    std::string result = "\""; const char* hex = "0123456789abcdef";
    for (const unsigned char ch : raw) {
        if (ch == '\\' || ch == '"') result += '\\';
        if (ch < 32) { result += "\\u00"; result += hex[ch >> 4]; result += hex[ch & 15]; }
        else result += static_cast<char>(ch);
    }
    return result + "\"";
}
class reporter {
public:
    explicit reporter(const std::wstring& path, const std::wstring& nonce,const std::string& inputSha)
        : file_(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)) { check(file_.valid(), "fresh-report"); nonce_=nonce; inputSha_=inputSha; }
    void row(const char* event, const std::string& fields = "") {
        const auto line = "{\"schema\":\"soui.host64-build-debug-addendum/2\",\"sequence\":" + std::to_string(++sequence_) + ",\"nonce\":" + jtext(nonce_) +
            ",\"inputSha256\":\"" + inputSha_ + "\",\"event\":\"" + event + "\"" + fields + "}\n";
        need(sequence_<=100000 && line.size()<=262144 && bytes_+line.size()<=67108864, "report-bound"); bytes_+=line.size();
        DWORD written = 0;
        check(WriteFile(file_.get(), line.data(), static_cast<DWORD>(line.size()), &written, nullptr) != FALSE && written == line.size(), "write-report");
        check(FlushFileBuffers(file_.get()) != FALSE, "flush-report");
    }
private:
    handle file_; unsigned sequence_ = 0; std::uint64_t bytes_=0; std::wstring nonce_;std::string inputSha_;
};
struct file_identity {
    DWORD volume = 0; std::uint64_t index = 0, bytes = 0; FILETIME write{};
    std::wstring path; std::string sha; WORD machine = 0;
    std::string fields() const {
        return "\"volumeSerial\":\"" + std::to_string(volume) + "\",\"fileIndex\":\"" + std::to_string(index) +
            "\",\"bytes\":\"" + std::to_string(bytes) + "\",\"sha256\":\"" + sha + "\",\"finalPath\":" + jtext(path) + ",\"machine\":" + std::to_string(machine);
    }
};
struct bcrypt_algorithm_owner {
    BCRYPT_ALG_HANDLE value = nullptr;
    bcrypt_algorithm_owner() = default;
    ~bcrypt_algorithm_owner() noexcept { if (value) BCryptCloseAlgorithmProvider(value, 0); }
    bcrypt_algorithm_owner(const bcrypt_algorithm_owner&) = delete;
    bcrypt_algorithm_owner& operator=(const bcrypt_algorithm_owner&) = delete;
};
struct bcrypt_hash_owner {
    BCRYPT_HASH_HANDLE value = nullptr;
    bcrypt_hash_owner() = default;
    ~bcrypt_hash_owner() noexcept { if (value) BCryptDestroyHash(value); }
    bcrypt_hash_owner(const bcrypt_hash_owner&) = delete;
    bcrypt_hash_owner& operator=(const bcrypt_hash_owner&) = delete;
};
std::string sha256(HANDLE file) {
    bcrypt_algorithm_owner algorithm;
    need(BCryptOpenAlgorithmProvider(&algorithm.value, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "sha-provider");
    DWORD objectBytes = 0, returned = 0;
    need(BCryptGetProperty(algorithm.value, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes), &returned, 0) >= 0 && returned == sizeof(objectBytes), "sha-object-length");
    // BCryptCreateHash requires backing to outlive its hash handle. Reverse
    // destruction releases hash, then object, then provider on every exit.
    std::vector<unsigned char> object(objectBytes);
    bcrypt_hash_owner hash;
    need(BCryptCreateHash(algorithm.value, &hash.value, object.data(), objectBytes, nullptr, 0, 0) >= 0, "sha-create");
    LARGE_INTEGER zero{}; check(SetFilePointerEx(file, zero, nullptr, FILE_BEGIN) != FALSE, "sha-seek");
    std::vector<unsigned char> block(65536); std::uint64_t total = 0;
    for (;;) {
        DWORD got = 0; check(ReadFile(file, block.data(), static_cast<DWORD>(block.size()), &got, nullptr) != FALSE, "sha-read");
        if (!got) break;
        total += got; need(total <= 134217728, "experiment-file-bound"); need(BCryptHashData(hash.value, block.data(), got, 0) >= 0, "sha-data");
    }
    unsigned char bytes[32]{}; need(BCryptFinishHash(hash.value, bytes, sizeof(bytes), 0) >= 0, "sha-finish");
    const char* hex = "0123456789abcdef"; std::string result;
    for (const auto ch : bytes) { result += hex[ch >> 4]; result += hex[ch & 15]; }
    return result;
}
file_identity identity(HANDLE file) {
    BY_HANDLE_FILE_INFORMATION before{}, after{};
    check(GetFileInformationByHandle(file, &before) != FALSE, "file-info");
    need((before.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0, "plain-image-file");
    file_identity result; result.volume = before.dwVolumeSerialNumber; result.index = joined(before.nFileIndexHigh, before.nFileIndexLow);
    result.bytes = joined(before.nFileSizeHigh, before.nFileSizeLow); result.write = before.ftLastWriteTime;
    need(result.index != 0 && result.bytes >= 96 && result.bytes <= 134217728, "experiment-image-size-id");
    result.path.resize(32768);
    const auto length = GetFinalPathNameByHandleW(file, result.path.data(), static_cast<DWORD>(result.path.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    check(length != 0, "image-final-path"); need(length < result.path.size(), "image-final-path-bound"); result.path.resize(length);
    LARGE_INTEGER zero{}; check(SetFilePointerEx(file, zero, nullptr, FILE_BEGIN) != FALSE, "pe-seek");
    unsigned char dos[64]{}; DWORD got = 0; check(ReadFile(file, dos, sizeof(dos), &got, nullptr) != FALSE && got == sizeof(dos), "pe-dos");
    need(dos[0] == 'M' && dos[1] == 'Z', "pe-mz"); DWORD offset = 0; CopyMemory(&offset, dos + 60, sizeof(offset));
    need(offset >= 64 && offset <= 1048576 && std::uint64_t(offset) + 24 <= result.bytes, "pe-offset");
    LARGE_INTEGER at{}; at.QuadPart = offset; check(SetFilePointerEx(file, at, nullptr, FILE_BEGIN) != FALSE, "pe-header-seek");
    unsigned char pe[24]{}; check(ReadFile(file, pe, sizeof(pe), &got, nullptr) != FALSE && got == sizeof(pe), "pe-header");
    need(pe[0] == 'P' && pe[1] == 'E' && pe[2] == 0 && pe[3] == 0, "pe-signature"); CopyMemory(&result.machine, pe + 4, sizeof(result.machine));
    need(result.machine == IMAGE_FILE_MACHINE_I386 || result.machine == IMAGE_FILE_MACHINE_AMD64, "pe-known-machine");
    result.sha = sha256(file); check(GetFileInformationByHandle(file, &after) != FALSE, "file-info-after");
    need(before.dwVolumeSerialNumber == after.dwVolumeSerialNumber && before.nFileIndexHigh == after.nFileIndexHigh &&
        before.nFileIndexLow == after.nFileIndexLow && before.nFileSizeHigh == after.nFileSizeHigh && before.nFileSizeLow == after.nFileSizeLow &&
        CompareFileTime(&before.ftLastWriteTime, &after.ftLastWriteTime) == 0, "image-changed"); return result;
}
bool physical_matches(const file_identity& a, const file_identity& b) {
    return a.volume == b.volume && a.index == b.index && a.bytes == b.bytes && a.sha == b.sha && a.machine == b.machine &&
        CompareFileTime(&a.write, &b.write) == 0 && _wcsicmp(a.path.c_str(), b.path.c_str()) == 0;
}
void same(const file_identity& a, const file_identity& b) {
    need(physical_matches(a, b), "different-physical-image");
}
std::uint64_t creation(HANDLE process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    check(GetProcessTimes(process, &created, &exited, &kernel, &user) != FALSE, "process-times");
    const auto value = joined(created.dwHighDateTime, created.dwLowDateTime); need(value != 0, "positive-process-filetime"); return value;
}
void member(HANDLE process, HANDLE job, DWORD pid, std::uint64_t ft, bool alive) {
    need(GetProcessId(process) == pid && creation(process) == ft, "same-held-process"); BOOL owned = FALSE;
    check(IsProcessInJob(process, job, &owned) != FALSE, "query-child-job"); need(owned != FALSE, "child-in-own-job");
    const auto waited = WaitForSingleObject(process, 0); check(waited != WAIT_FAILED, "wait-held-process");
    if (alive) need(waited == WAIT_TIMEOUT, "held-child-alive");
}
JOBOBJECT_BASIC_ACCOUNTING_INFORMATION account(HANDLE job) {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION value{};
    check(QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &value, sizeof(value), nullptr) != FALSE, "job-accounting"); return value;
}

// Bounded PE file parsing, never target memory or private loader structures.
void file_at(HANDLE file, std::uint64_t at, void* buffer, DWORD size, std::uint64_t bytes) {
    need(at <= bytes && size <= bytes - at, "pe-read-bound");
    LARGE_INTEGER offset{}; offset.QuadPart = static_cast<LONGLONG>(at);
    check(SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) != FALSE, "pe-layout-seek");
    DWORD got = 0;
    check(ReadFile(file, buffer, size, &got, nullptr) != FALSE && got == size, "pe-layout-read");
}
struct code_range { DWORD rva = 0, bytes = 0; };
struct pe_code_layout {
    DWORD imageBytes = 0; std::vector<code_range> executable;
    bool contains(std::uint64_t base, std::uint64_t address) const {
        if (address < base || address - base >= imageBytes) return false;
        const auto rva = address - base;
        for (const auto& range : executable)
            if (rva >= range.rva && rva - range.rva < range.bytes) return true;
        return false;
    }
    std::string fields() const {
        std::string text = "\"sizeOfImage\":" + std::to_string(imageBytes) + ",\"executableRanges\":[";
        for (std::size_t i = 0; i < executable.size(); ++i) {
            if (i) text += ",";
            text += "{\"rva\":" + std::to_string(executable[i].rva) +
                ",\"bytes\":" + std::to_string(executable[i].bytes) + "}";
        }
        return text + "]";
    }
};
pe_code_layout code_layout(HANDLE file, const file_identity& physical) {
    IMAGE_DOS_HEADER dos{}; file_at(file, 0, &dos, sizeof(dos), physical.bytes);
    need(dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew >= 64 && dos.e_lfanew <= 1048576, "layout-dos");
    const auto nt = static_cast<std::uint64_t>(dos.e_lfanew);
    DWORD signature = 0; file_at(file, nt, &signature, sizeof(signature), physical.bytes);
    need(signature == IMAGE_NT_SIGNATURE, "layout-signature");
    IMAGE_FILE_HEADER header{}; file_at(file, nt + sizeof(signature), &header, sizeof(header), physical.bytes);
    need(header.Machine == physical.machine && header.NumberOfSections > 0 && header.NumberOfSections <= 96 &&
        header.SizeOfOptionalHeader <= 4096, "layout-header");
    const auto optionalAt = nt + sizeof(signature) + sizeof(header);
    WORD magic = 0; file_at(file, optionalAt, &magic, sizeof(magic), physical.bytes);
    pe_code_layout result;
    if (physical.machine == IMAGE_FILE_MACHINE_I386) {
        need(magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC && header.SizeOfOptionalHeader >= sizeof(IMAGE_OPTIONAL_HEADER32), "layout-pe32");
        IMAGE_OPTIONAL_HEADER32 optional{}; file_at(file, optionalAt, &optional, sizeof(optional), physical.bytes);
        result.imageBytes = optional.SizeOfImage;
    } else {
        need(magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC && header.SizeOfOptionalHeader >= sizeof(IMAGE_OPTIONAL_HEADER64), "layout-pe64");
        IMAGE_OPTIONAL_HEADER64 optional{}; file_at(file, optionalAt, &optional, sizeof(optional), physical.bytes);
        result.imageBytes = optional.SizeOfImage;
    }
    need(result.imageBytes != 0 && result.imageBytes <= 1073741824, "layout-image-bound");
    const auto sectionsAt = optionalAt + header.SizeOfOptionalHeader;
    for (WORD i = 0; i < header.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section{};
        file_at(file, sectionsAt + std::uint64_t(i) * sizeof(section), &section, sizeof(section), physical.bytes);
        if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        const auto size = section.Misc.VirtualSize;
        need(size != 0 && section.VirtualAddress < result.imageBytes && size <= result.imageBytes - section.VirtualAddress,
            "layout-executable-range");
        need(section.PointerToRawData <= physical.bytes && section.SizeOfRawData <= physical.bytes - section.PointerToRawData,
            "layout-executable-file-range");
        result.executable.push_back({section.VirtualAddress, size});
    }
    need(!result.executable.empty(), "layout-executable-required"); return result;
}
std::wstring file_path(HANDLE file) {
    std::wstring value(32768, L'\0');
    const auto length = GetFinalPathNameByHandleW(file, value.data(), static_cast<DWORD>(value.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    check(length != 0, "module-file-path"); need(length < value.size(), "module-file-path-bound"); value.resize(length); return value;
}
struct ntdll_pin {
    handle file; file_identity physical;
    explicit ntdll_pin(const std::wstring& path, WORD machine)
        : file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) {
        check(file.valid(), "system-ntdll-open"); physical = identity(file.get());
        need(physical.machine == machine, "system-ntdll-machine"); plain_ancestors(path);
    }
};
template<class T> class com_ptr {
public:
    ~com_ptr() { if (value_) value_->Release(); }
    T* get() const noexcept { return value_; }
    T** put() noexcept { return &value_; }
    com_ptr(const com_ptr&) = delete; com_ptr& operator=(const com_ptr&) = delete;
    com_ptr() = default;
private:
    T* value_ = nullptr;
};
class bstring {
public:
    explicit bstring(const wchar_t* text) : value_(SysAllocString(text)) { need(value_ != nullptr, "bstr-allocation"); }
    ~bstring() { SysFreeString(value_); }
    operator BSTR() const noexcept { return value_; }
private:
    BSTR value_;
};
class variant_value {
public:
    variant_value() noexcept { VariantInit(&value_); }
    ~variant_value() { VariantClear(&value_); }
    variant_value(const variant_value&) = delete;
    variant_value& operator=(const variant_value&) = delete;
    VARIANT* put() noexcept { return &value_; }
    const VARIANT& get() const noexcept { return value_; }
private:
    VARIANT value_{};
};
struct wmi_state {
    handle ready{CreateEventW(nullptr, TRUE, FALSE, nullptr)}, request{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    handle cancel{CreateEventW(nullptr, TRUE, FALSE, nullptr)}, done{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::mutex mutex;
    DWORD pid = 0, queryMs = 0;
    bool connected = false, succeeded = false, cancelled = false;
    std::wstring raw;
    std::string failure;
    unsigned long code = 0;
};
void wmi_worker(const std::shared_ptr<wmi_state>& state) {
    bool initialized = false;
    try {
        hcheck(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "wmi-com-init"); initialized = true;
        {
            com_ptr<IWbemLocator> locator; com_ptr<IWbemServices> services;
            hcheck(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                reinterpret_cast<void**>(locator.put())), "wmi-locator");
            bstring space(L"ROOT\\CIMV2");
            hcheck(locator.get()->ConnectServer(space, nullptr, nullptr, nullptr, WBEM_FLAG_CONNECT_USE_MAX_WAIT,
                nullptr, nullptr, services.put()), "wmi-connect");
            hcheck(CoSetProxyBlanket(services.get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE), "wmi-proxy");
            { std::lock_guard<std::mutex> lock(state->mutex); state->connected = true; }
            check(SetEvent(state->ready.get()) != FALSE, "wmi-ready-event");
            HANDLE waitFor[] = {state->cancel.get(), state->request.get()};
            const auto selected = WaitForMultipleObjects(2, waitFor, FALSE, INFINITE);
            check(selected != WAIT_FAILED, "wmi-request-wait");
            if (selected == WAIT_OBJECT_0) {
                std::lock_guard<std::mutex> lock(state->mutex); state->cancelled = true;
            } else {
                DWORD pid = 0, queryMs = 0;
                { std::lock_guard<std::mutex> lock(state->mutex); pid = state->pid; queryMs = state->queryMs; }
                need(pid != 0 && queryMs != 0, "wmi-request-identity");
                const auto text = L"SELECT ProcessId,CommandLine FROM Win32_Process WHERE ProcessId=" + std::to_wstring(pid);
                bstring language(L"WQL"), query(text.c_str()); com_ptr<IEnumWbemClassObject> rows;
                const auto began = std::chrono::steady_clock::now();
                hcheck(services.get()->ExecQuery(language, query, WBEM_FLAG_RETURN_IMMEDIATELY | WBEM_FLAG_FORWARD_ONLY,
                    nullptr, rows.put()), "wmi-query");
                unsigned count = 0; bool ended = false; std::wstring raw;
                while (!ended) {
                    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - began).count();
                    need(elapsed < queryMs, "wmi-query-deadline");
                    need(WaitForSingleObject(state->cancel.get(), 0) == WAIT_TIMEOUT, "wmi-query-cancelled");
                    com_ptr<IWbemClassObject> row; ULONG returned = 0;
                    const auto status = rows.get()->Next(20, 1, row.put(), &returned); hcheck(status, "wmi-next");
                    need(returned <= 1, "wmi-row-count");
                    if (returned) {
                        need(++count == 1, "wmi-duplicate-row");
                        variant_value pidValue, commandValue;
                        const auto pstatus = row.get()->Get(L"ProcessId", 0, pidValue.put(), nullptr, nullptr);
                        const auto cstatus = row.get()->Get(L"CommandLine", 0, commandValue.put(), nullptr, nullptr);
                        const auto& actualPid = pidValue.get(); const auto& command = commandValue.get();
                        const bool pidMatches = SUCCEEDED(pstatus) &&
                            ((actualPid.vt == VT_I4 && static_cast<DWORD>(actualPid.lVal) == pid) ||
                             (actualPid.vt == VT_UI4 && actualPid.ulVal == pid));
                        const bool commandTyped = SUCCEEDED(cstatus) && command.vt == VT_BSTR && command.bstrVal &&
                            SysStringLen(command.bstrVal) > 0 && SysStringLen(command.bstrVal) <= 32766;
                        if (commandTyped) raw.assign(command.bstrVal, SysStringLen(command.bstrVal));
                        need(pidMatches && commandTyped, "wmi-target-commandline-type");
                        need(raw.find(L'\0') == std::wstring::npos, "wmi-commandline-nul");
                    }
                    ended = status == WBEM_S_FALSE;
                    need(status == WBEM_S_FALSE || status == WBEM_S_NO_ERROR || status == WBEM_S_TIMEDOUT, "wmi-next-status");
                }
                need(count == 1, "wmi-one-target-row");
                { std::lock_guard<std::mutex> lock(state->mutex); state->raw = raw; state->succeeded = true; }
            }
        } // Release every WMI interface before the worker announces completion.
    } catch (const api_error& error) {
        std::lock_guard<std::mutex> lock(state->mutex); state->failure = error.what(); state->code = error.code;
    } catch (...) {
        std::lock_guard<std::mutex> lock(state->mutex); state->failure = "wmi-unknown-error"; state->code = ERROR_GEN_FAILURE;
    }
    if (initialized) CoUninitialize();
    SetEvent(state->done.get());
}
class attribute_list_owner {
public:
    attribute_list_owner() = default;
    ~attribute_list_owner() { if (value_) DeleteProcThreadAttributeList(value_); }
    attribute_list_owner(const attribute_list_owner&) = delete;
    attribute_list_owner& operator=(const attribute_list_owner&) = delete;
    void adopt(LPPROC_THREAD_ATTRIBUTE_LIST value) noexcept { value_ = value; }
    LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept { return value_; }
private:
    LPPROC_THREAD_ATTRIBUTE_LIST value_ = nullptr;
};
class inherited_handles {
public:
    explicit inherited_handles(const std::vector<HANDLE>& handles) {
        SIZE_T count = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &count);
        need(count != 0, "handle-list-size"); bytes_.resize(count);
        const auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(bytes_.data());
        check(InitializeProcThreadAttributeList(list, 1, 0, &count) != FALSE, "handle-list-init"); list_.adopt(list);
        check(UpdateProcThreadAttribute(list_.get(), 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            const_cast<HANDLE*>(handles.data()), handles.size() * sizeof(HANDLE), nullptr, nullptr) != FALSE, "handle-list-set");
    }
    LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept { return list_.get(); }
private:
    // The owner is destroyed before the backing bytes, including constructor failure.
    std::vector<unsigned char> bytes_; attribute_list_owner list_;
};
HANDLE child_standard(DWORD role) {
    HANDLE value = nullptr; const auto source = GetStdHandle(role);
    check(source && source != INVALID_HANDLE_VALUE && DuplicateHandle(GetCurrentProcess(), source,
        GetCurrentProcess(), &value, 0, TRUE, DUPLICATE_SAME_ACCESS) != FALSE, "child-standard-handle"); return value;
}

// Inputs are frozen once, before any child or WMI worker is started.
struct frozen_spec { std::wstring role,path; std::uint64_t bytes; std::string sha; };
std::vector<frozen_spec> images,controls;
frozen_spec ntdllSpec;
std::wstring buildCwd;
bool consumerBuild=false;
std::vector<std::wstring> buildArgs;
std::wstring wide(const std::string& raw) {
    need(raw.size()<=32766 && raw.find('\0')==std::string::npos,"input-string-bound");
    const int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,raw.data(),static_cast<int>(raw.size()),nullptr,0);
    check(size>0 || raw.empty(),"input-utf8");std::wstring out(static_cast<std::size_t>(size),L'\0');
    check(size==0 || MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,raw.data(),static_cast<int>(raw.size()),out.data(),size)!=0,"input-utf8");return out;
}
std::string hash_text(const nlohmann::json& value) {
    need(value.is_string(),"input-sha-type");const auto raw=value.get<std::string>();need(raw.size()==64,"input-sha-size");
    for(const char c:raw)need((c>='0'&&c<='9')||(c>='a'&&c<='f'),"input-sha-format");return raw;
}
void exact_keys(const nlohmann::json& value,std::initializer_list<const char*> keys) {
    need(value.is_object()&&value.size()==keys.size(),"input-exact-keys");for(const auto key:keys)need(value.contains(key),"input-key");
}
frozen_spec input_pin(const nlohmann::json& value,bool image) {
    if(image)exact_keys(value,{"role","path","bytes","sha256","machine"});else exact_keys(value,{"path","bytes","sha256"});
    need(value["path"].is_string()&&value["bytes"].is_number_unsigned(),"input-pin-type");
    const auto bytes=value["bytes"].get<std::uint64_t>();need(bytes>0&&bytes<=134217728,"input-pin-bound");
    if(image)need(value["role"].is_string()&&value["machine"]==34404,"input-amd64-pin");
    return {image?wide(value["role"].get<std::string>()):L"SOURCE",full(wide(value["path"].get<std::string>())),bytes,hash_text(value["sha256"])};
}
void load_input(const std::wstring& file,const std::string& expected,const std::wstring& nonce,const std::wstring& report) {
    plain_ancestors(file);handle held(CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    check(held.valid(),"input-open");BY_HANDLE_FILE_INFORMATION before{},after{};check(GetFileInformationByHandle(held.get(),&before)!=FALSE,"input-info");
    const auto size=joined(before.nFileSizeHigh,before.nFileSizeLow);need(size>0&&size<=1048576&&(before.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))==0,"input-bound");
    need(sha256(held.get())==expected,"input-sha");LARGE_INTEGER zero{};check(SetFilePointerEx(held.get(),zero,nullptr,FILE_BEGIN)!=FALSE,"input-rewind");
    std::string raw(static_cast<std::size_t>(size),'\0');DWORD read=0;check(ReadFile(held.get(),raw.data(),static_cast<DWORD>(size),&read,nullptr)!=FALSE&&read==size,"input-read");
    // Duplicate keys, depth and aggregate parse events are rejected before use.
    std::vector<std::set<std::string>> keys;std::size_t events=0;
    auto callback=[&](int depth,nlohmann::json::parse_event_t event,nlohmann::json& parsed){
        need(depth>=0&&depth<=16&&++events<=20000,"input-json-bound");
        if(event==nlohmann::json::parse_event_t::object_start)keys.emplace_back();
        else if(event==nlohmann::json::parse_event_t::key){need(!keys.empty()&&keys.back().insert(parsed.get<std::string>()).second,"input-duplicate-key");}
        else if(event==nlohmann::json::parse_event_t::object_end){need(!keys.empty(),"input-object-stack");keys.pop_back();}
        return true;
    };
    const auto input=nlohmann::json::parse(raw,callback);
    consumerBuild=input.value("schema",std::string{})=="soui.host64-build-debug-inputs/3";
    if(consumerBuild)exact_keys(input,{"schema","nonce","root","rootArguments","images","modules","controlPins","report","profile","sdkArchitecture","configuration","sourceSetSha256","producer","outerRunner","budgets","consumerKind","buildDirectory"});
    else exact_keys(input,{"schema","nonce","root","rootArguments","images","modules","controlPins","report","profile","sdkArchitecture","configuration","sourceSetSha256","producer","outerRunner","budgets"});
    need((consumerBuild||input["schema"]=="soui.host64-build-debug-inputs/2")&&input["profile"]=="LLVM22MSVC142"&&wide(input["nonce"].get<std::string>())==nonce&&full(wide(input["report"].get<std::string>()))==report,"input-protocol");
    need((input["sdkArchitecture"]=="x64"||input["sdkArchitecture"]=="x86")&&(input["configuration"]=="Release"||input["configuration"]=="Debug"),"input-target");
    need(input["budgets"]==nlohmann::json({{"outerMs",900000},{"innerMs",880000},{"drainMs",2000},{"queryMs",10000},{"closeMs",5000},{"members",2048},{"queries",2048},{"events",100000},{"reportBytes",67108864}}),"input-fixed-budgets");
    (void)hash_text(input["sourceSetSha256"]);buildCwd=full(wide(input["root"].get<std::string>()));plain_ancestors(buildCwd);
    need(input["images"].is_array()&&input["images"].size()==12&&input["modules"].is_array()&&input["modules"].size()==1&&input["controlPins"].is_array()&&input["controlPins"].size()>0&&input["controlPins"].size()<=128,"input-members");
    const std::vector<std::wstring> roles={L"POWERSHELL",L"CMAKE",L"NODE",L"CMD",L"NINJA",L"CLANG",L"LINKER",L"ARCHIVER",L"RC",L"MT",L"CMCLDEPS",L"CONHOST"};
    for(std::size_t i=0;i<roles.size();++i){auto spec=input_pin(input["images"][i],true);need(spec.role==roles[i],"input-image-role");images.push_back(std::move(spec));}
    auto module=input["modules"][0];exact_keys(module,{"role","path","bytes","sha256","machine","codeLayout"});module.erase("codeLayout");ntdllSpec=input_pin(module,true);need(ntdllSpec.role==L"NTDLL","input-ntdll-role");
    std::set<std::wstring> names;for(const auto& pin:input["controlPins"]){auto spec=input_pin(pin,false);need(names.insert(spec.path).second,"input-control-duplicate");controls.push_back(std::move(spec));}
    controls.push_back({L"INPUT",file,size,expected});
    // Producer and original runner remain real pinned artifacts, never guessed PE labels.
    controls.push_back(input_pin(input["producer"],false));controls.push_back(input_pin(input["outerRunner"],false));
    need(input["rootArguments"].is_array()&&(consumerBuild?input["rootArguments"].size()==7:(input["rootArguments"].size()>=24&&input["rootArguments"].size()<=40)),"input-root-vector-bound");
    for(const auto& arg:input["rootArguments"])need(arg.is_string(),"input-argument-type");
    for(const auto& arg:input["rootArguments"])buildArgs.push_back(wide(arg.get<std::string>()));
    if(consumerBuild){
        const auto kind=input["consumerKind"].get<std::string>();need(kind=="acceptance"||kind=="release","input-consumer-kind");
        auto spelling=wide(input["buildDirectory"].get<std::string>());for(auto& ch:spelling)if(ch==L'/')ch=L'\\';
        const auto directory=full(spelling);need(_wcsicmp(directory.c_str(),spelling.c_str())==0,"input-consumer-canonical-directory");plain_ancestors(directory);
        const auto end=directory.find_last_of(L"\\/");const auto suite=directory.substr(0,end);const auto split=suite.find_last_of(L"\\/");
        need(end!=std::wstring::npos&&split!=std::wstring::npos&&directory.substr(end+1)==L"consumer","input-consumer-directory");
        const auto name=suite.substr(split+1),parent=full(buildCwd+(kind=="acceptance"?L"/out/acceptance":L"/out/release-validation/"+wide(input["sdkArchitecture"].get<std::string>())));
        need(_wcsicmp(suite.substr(0,split).c_str(),parent.c_str())==0&&name.size()==(kind=="acceptance"?36:32),"input-consumer-owner");
        for(std::size_t i=0;i<name.size();++i){const bool hyphen=kind=="acceptance"&&(i==8||i==13||i==18||i==23);need(hyphen?name[i]==L'-':(name[i]>=L'0'&&name[i]<=L'9')||(name[i]>=L'a'&&name[i]<=L'f'),"input-consumer-id");}
        need(buildArgs==std::vector<std::wstring>{L"--build",wide(input["buildDirectory"].get<std::string>()),L"--config",wide(input["configuration"].get<std::string>()),L"--parallel",L"2",L"--verbose"},"input-fixed-consumer-entry");
    }else{
    need(buildArgs[0]==L"-NoProfile"&&buildArgs[1]==L"-NonInteractive"&&buildArgs[2]==L"-ExecutionPolicy"&&buildArgs[3]==L"Bypass"&&buildArgs[4]==L"-File"&&_wcsicmp(full(buildArgs[5]).c_str(),full(buildCwd+L"/tools/Build.ps1").c_str())==0&&buildArgs[6]==L"-Architecture"&&buildArgs[7]==wide(input["sdkArchitecture"].get<std::string>())&&buildArgs[8]==L"-Configuration"&&buildArgs[9]==wide(input["configuration"].get<std::string>()),"input-fixed-build-entry");
    std::size_t at=10;std::set<std::wstring> flags;const std::set<std::wstring> allowed={L"-SdkOnly",L"-SkipWeb",L"-WithWebView2",L"-WithGuiBindings",L"-WithVendorTests",L"-Install",L"-FreshConfigure"};
    while(at<buildArgs.size()&&allowed.count(buildArgs[at])){need(flags.insert(buildArgs[at]).second,"input-duplicate-switch");++at;}
    need(at+14==buildArgs.size()&&buildArgs[at++]==L"-NativeToolchain"&&buildArgs[at++]==L"LLVM22MSVC142","input-explicit-profile");
    for(const auto flag:{L"-LlvmRoot",L"-MsvcRoot",L"-WindowsSdkRoot",L"-NinjaExe",L"-WindowsCmdExe",L"-PowerShellExe"}){need(buildArgs[at++]==flag,"input-profile-vector");(void)full(buildArgs[at++]);}
    }
    check(GetFileInformationByHandle(held.get(),&after)!=FALSE,"input-after");need(before.dwVolumeSerialNumber==after.dwVolumeSerialNumber&&before.nFileIndexHigh==after.nFileIndexHigh&&before.nFileIndexLow==after.nFileIndexLow&&CompareFileTime(&before.ftLastWriteTime,&after.ftLastWriteTime)==0&&sha256(held.get())==expected,"input-stable");
}

struct pinned_image {
    const frozen_spec* spec; handle file; file_identity physical;
    explicit pinned_image(const frozen_spec& value) : spec(&value),
        file(CreateFileW(value.path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) {
        plain_ancestors(value.path); check(file.valid(), "frozen-image-open"); physical=identity(file.get());
        need(physical.machine==IMAGE_FILE_MACHINE_AMD64 && physical.bytes==value.bytes && physical.sha==value.sha,"frozen-image-pin");
    }
};
void source_pins() {
    for(const auto& spec:controls) {
        plain_ancestors(spec.path); handle file(CreateFileW(spec.path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
        check(file.valid(),"source-pin-open"); BY_HANDLE_FILE_INFORMATION before{},after{};
        check(GetFileInformationByHandle(file.get(),&before)!=FALSE,"source-pin-info");
        need((before.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))==0 && joined(before.nFileSizeHigh,before.nFileSizeLow)==spec.bytes,"source-pin-size");
        need(sha256(file.get())==spec.sha,"source-pin-hash"); check(GetFileInformationByHandle(file.get(),&after)!=FALSE,"source-pin-after");
        need(before.nFileIndexHigh==after.nFileIndexHigh && before.nFileIndexLow==after.nFileIndexLow && CompareFileTime(&before.ftLastWriteTime,&after.ftLastWriteTime)==0,"source-pin-stable");
    }
}
struct debug_member {
    handle held,imageFile; DWORD pid=0,mainTid=0,exitCode=STILL_ACTIVE; std::uint64_t ft=0;
    std::wstring role; bool root=false,exitSeen=false,initial=false,queried=false;
    file_identity image; handle moduleFile; file_identity module; pe_code_layout layout; std::uint64_t moduleBase=0;
};
debug_member* active_member(const std::vector<std::unique_ptr<debug_member>>& values,DWORD pid) {
    for(auto i=values.rbegin();i!=values.rend();++i)if((*i)->pid==pid && !(*i)->exitSeen)return i->get(); return nullptr;
}
bool closed_members(const std::vector<std::unique_ptr<debug_member>>& values) {
    for(const auto& p:values){if(!p->exitSeen || WaitForSingleObject(p->held.get(),0)!=WAIT_OBJECT_0)return false;
        need(GetProcessId(p->held.get())==p->pid && creation(p->held.get())==p->ft,"history-held-identity"); DWORD code=STILL_ACTIVE;
        check(GetExitCodeProcess(p->held.get(),&code)!=FALSE,"history-exit-code"); need(code==p->exitCode,"history-exit-code-match");}
    return true;
}
std::wstring query_paused(debug_member& subject,HANDLE job,reporter& report,const std::chrono::steady_clock::time_point& began,bool& forced) {
    auto state=std::make_shared<wmi_state>();
    need(state->ready.valid()&&state->request.valid()&&state->cancel.valid()&&state->done.valid(),"wmi-events");
    state->pid=subject.pid; state->queryMs=10000; std::thread worker(wmi_worker,state);
    struct thread_guard { std::thread& worker; HANDLE cancel,job; bool& forced;
        ~thread_guard() { if(worker.joinable()) { SetEvent(cancel); if(WaitForSingleObject(worker.native_handle(),5000)!=WAIT_OBJECT_0) { forced=true; TerminateJobObject(job,124); } worker.join(); } }
    } guard{worker,state->cancel.get(),job,forced};
    const auto queryBegan=std::chrono::steady_clock::now(); bool queryFailed=false; unsigned long code=0;
    const auto deadline=[&]{need(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-queryBegan).count()<10000 &&
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-began).count()<880000,"paused-query-deadline");};
    try {
        while(WaitForSingleObject(state->ready.get(),0)!=WAIT_OBJECT_0){deadline();need(WaitForSingleObject(state->done.get(),0)!=WAIT_OBJECT_0,"wmi-connect-failed");Sleep(10);}
        check(SetEvent(state->request.get())!=FALSE,"wmi-request");
        while(WaitForSingleObject(state->done.get(),0)!=WAIT_OBJECT_0){deadline();Sleep(10);}
    }catch(const api_error& e){queryFailed=true;code=e.code;SetEvent(state->cancel.get());}
    if(WaitForSingleObject(worker.native_handle(),5000)!=WAIT_OBJECT_0){
        report.row("wmi-closeout-pending",",\"pid\":"+std::to_string(subject.pid)+",\"passed\":false,\"wmiThreadExited\":false,\"providerCancellationProved\":false");
        // No detach/TerminateThread or promise that an external provider was cancelled.
        // This is an owned failure. The unchanged outer Owned deadline ends a stuck observer.
        forced=true; TerminateJobObject(job,124); worker.join(); throw api_error("wmi-worker-closure-unconfirmed",ERROR_TIMEOUT);
    }
    worker.join(); if(queryFailed)throw api_error("paused-query-failed",code);
    std::lock_guard<std::mutex> lock(state->mutex);
    if(!state->succeeded)throw api_error(state->failure.empty()?"wmi-query-failed":state->failure.c_str(),state->code);
    member(subject.held.get(),job,subject.pid,subject.ft,true); return state->raw;
}
std::string argv_fields(const std::wstring& raw,const pinned_image& pin,bool root,const std::wstring& command) {
    int count=0; auto* argv=CommandLineToArgvW(raw.c_str(),&count); check(argv!=nullptr,"whole-argv-parse");
    struct argv_owner { wchar_t** value; ~argv_owner() { LocalFree(value); } } owner{argv};
        need(count>=1 && count<=4096,"whole-argv-count");
        // CMake may spell an unchanged tool with an 8.3 path. Prove the opened
        // physical file, never convert a spelling into an identity assertion.
        const auto spelling=full(argv[0]); plain_ancestors(spelling);
        handle argvFile(CreateFileW(spelling.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
        check(argvFile.valid(),"whole-argv-image-open"); const auto argvImage=identity(argvFile.get()); same(pin.physical,argvImage);
        if(root){need(raw==command && count==static_cast<int>(buildArgs.size()+1),"root-whole-argv");
            for(std::size_t i=0;i<buildArgs.size();++i)need(buildArgs[i]==argv[i+1],"root-whole-argv-token");}
        std::string result=",\"raw\":"+jtext(raw)+",\"wholeArgv\":[";
        for(int i=0;i<count;++i){if(i)result+=",";result+=jtext(argv[i]);}
        same(argvImage,identity(argvFile.get()));
        result+="],\"argv0File\":{"+argvImage.fields()+"},\"argv0HeldBeforeAndAfter\":true"; return result;
}
struct options {std::wstring report,nonce,input;std::string inputSha;};
options parse(int count,wchar_t** args) {
    need(count==9&&std::wcscmp(args[1],L"--input")==0&&std::wcscmp(args[3],L"--input-sha256")==0&&std::wcscmp(args[5],L"--report")==0&&std::wcscmp(args[7],L"--nonce")==0,"fixed-observer-argv");
    options result{full(args[6]),args[8],full(args[2]),{}};
    for(const wchar_t ch:std::wstring(args[4])){need((ch>=L'0'&&ch<=L'9')||(ch>=L'a'&&ch<=L'f'),"input-sha-argv");result.inputSha+=static_cast<char>(ch);}need(result.inputSha.size()==64,"input-sha-size");
    need(result.nonce.size()==36,"nonce-size");for(std::size_t i=0;i<36;++i)need((i==8||i==13||i==18||i==23)?result.nonce[i]==L'-':(result.nonce[i]>=L'0'&&result.nonce[i]<=L'9')||(result.nonce[i]>=L'a'&&result.nonce[i]<=L'f'),"nonce-format");
    const auto folder=result.report.substr(0,result.report.find_last_of(L"\\/"));plain_ancestors(folder);
    need(result.report.substr(result.report.find_last_of(L"\\/")+1)==L"host64-debug.ndjson"&&_wcsicmp(folder.c_str(),result.input.substr(0,result.input.find_last_of(L"\\/")).c_str())==0,"owned-report-boundary");
    load_input(result.input,result.inputSha,result.nonce,result.report);return result;
}
int run(const options& cfg) {
    const auto began=std::chrono::steady_clock::now();const auto budget=[&]{need(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-began).count()<880000,"inner-deadline");};
    SYSTEM_INFO native{};GetNativeSystemInfo(&native);need(sizeof(void*)==8 && native.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_AMD64,"host-amd64-only");
    BOOL inOuter=FALSE;check(IsProcessInJob(GetCurrentProcess(),nullptr,&inOuter)!=FALSE,"observer-outer-job");need(inOuter!=FALSE,"outer-owned-required");
    reporter report(cfg.report,cfg.nonce,cfg.inputSha); report.row("observer-start",",\"observerPid\":"+std::to_string(GetCurrentProcessId())+
        ",\"observerCreationFileTime\":\""+std::to_string(creation(GetCurrentProcess()))+"\",\"observerBits\":64,\"outerJobPresent\":true,\"sdkProof\":false");
    handle job(CreateJobObjectW(nullptr,nullptr));check(job.valid(),"inner-job-create");JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;check(SetInformationJobObject(job.get(),JobObjectExtendedLimitInformation,&limits,sizeof(limits))!=FALSE,"inner-job-limits");
    std::vector<std::unique_ptr<pinned_image>> frozen;for(const auto& image:images)frozen.push_back(std::make_unique<pinned_image>(image));
    pinned_image ntdll(ntdllSpec);const auto layout=code_layout(ntdll.file.get(),ntdll.physical);source_pins();budget();
    auto entry=frozen[consumerBuild?1:0].get();std::wstring command=quote(entry->spec->path);for(const auto& arg:buildArgs)command+=L" "+quote(arg);
    handle out(child_standard(STD_OUTPUT_HANDLE)),err(child_standard(STD_ERROR_HANDLE));SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    handle input(CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));check(input.valid(),"child-null-input");
    inherited_handles inheritance({input.get(),out.get(),err.get()});STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);
    startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;startup.StartupInfo.hStdInput=input.get();startup.StartupInfo.hStdOutput=out.get();startup.StartupInfo.hStdError=err.get();startup.lpAttributeList=inheritance.get();
    PROCESS_INFORMATION pi{};std::vector<std::unique_ptr<debug_member>> history;handle root,initialProcess,initialThread;DWORD rootPid=0,rootTid=0,rootExit=STILL_ACTIVE;std::uint64_t rootFt=0;
    unsigned queries=0,events=0;bool assigned=false,rootExitSeen=false,forced=false,protocol=true;std::string failure;unsigned long failureCode=0;
    try {
        auto mutableCommand=command;check(CreateProcessW(entry->spec->path.c_str(),mutableCommand.data(),nullptr,nullptr,TRUE,
            DEBUG_PROCESS|CREATE_SUSPENDED|CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,buildCwd.c_str(),&startup.StartupInfo,&pi)!=FALSE,"debug-build-create");
        initialProcess.reset(pi.hProcess);initialThread.reset(pi.hThread);root.reset(duplicate(pi.hProcess));rootPid=pi.dwProcessId;rootTid=pi.dwThreadId;rootFt=creation(root.get());
        check(AssignProcessToJobObject(job.get(),root.get())!=FALSE,"inner-job-assignment");assigned=true;member(root.get(),job.get(),rootPid,rootFt,true);
        report.row("build-created",",\"pid\":"+std::to_string(rootPid)+",\"creationFileTime\":\""+std::to_string(rootFt)+"\",\"mainTid\":"+std::to_string(rootTid)+",\"assignedBeforeResume\":true,\"breakawayAllowed\":false");
        check(ResumeThread(initialThread.get())!=static_cast<DWORD>(-1),"debug-build-resume");
        while(!rootExitSeen || !closed_members(history) || account(job.get()).ActiveProcesses!=0) {
            budget();DEBUG_EVENT event{};if(!WaitForDebugEvent(&event,20)){need(GetLastError()==ERROR_SEM_TIMEOUT,"debug-event-wait");continue;}
            need(++events<=100000,"debug-event-count");handle eventFile(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT?event.u.CreateProcessInfo.hFile:event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT?event.u.LoadDll.hFile:nullptr);
            DWORD disposition=DBG_CONTINUE;bool continued=false;debug_member* p=nullptr;
            try {
                p=active_member(history,event.dwProcessId);
                if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT) {
                    need(p==nullptr && history.size()<2048,"unique-live-create-and-bound");auto value=std::make_unique<debug_member>();value->held.reset(duplicate(event.u.CreateProcessInfo.hProcess));
                    value->pid=event.dwProcessId;value->ft=creation(value->held.get());value->mainTid=event.dwThreadId;member(value->held.get(),job.get(),value->pid,value->ft,true);
                    need(GetThreadId(event.u.CreateProcessInfo.hThread)==value->mainTid && GetProcessIdOfThread(event.u.CreateProcessInfo.hThread)==value->pid,"main-thread-identity");
                    check(eventFile.valid(),"create-hfile-required");value->image=identity(eventFile.get());BOOL wow=FALSE;check(IsWow64Process(value->held.get(),&wow)!=FALSE,"member-wow64");need(!wow && value->image.machine==IMAGE_FILE_MACHINE_AMD64,"all-host-images-amd64");
                    const pinned_image* selected=nullptr;for(const auto& image:frozen)if(physical_matches(image->physical,value->image)){need(selected==nullptr,"ambiguous-image-role");selected=image.get();}
                    need(selected!=nullptr,"unknown-host-image");value->role=selected->spec->role;value->root=value->pid==rootPid;
                    if(value->root){need(value->ft==rootFt && value->mainTid==rootTid && value->role==entry->spec->role,"actual-build-root");
                        if(initialProcess.get()==event.u.CreateProcessInfo.hProcess)initialProcess.release();if(initialThread.get()==event.u.CreateProcessInfo.hThread)initialThread.release();}
                    value->imageFile.reset(duplicate(eventFile.get()));report.row("create-paused",",\"pid\":"+std::to_string(value->pid)+",\"creationFileTime\":\""+std::to_string(value->ft)+
                        "\",\"mainTid\":"+std::to_string(value->mainTid)+",\"role\":"+jtext(value->role)+",\"buildRoot\":"+(value->root?"true":"false")+",\"inInnerJob\":true,\"alive\":true,\"wow64\":false,\"imageFile\":{"+value->image.fields()+"}");
                    history.push_back(std::move(value));p=history.back().get();
                    if(p->root || p->role==L"CLANG" || p->role==L"RC" || p->role==L"MT") {
                        need(++queries<=2048,"query-count");const auto raw=query_paused(*p,job.get(),report,began,forced);const auto fields=argv_fields(raw,*selected,p->root,command);
                        member(p->held.get(),job.get(),p->pid,p->ft,true);report.row("commandline-paused",",\"pid\":"+std::to_string(p->pid)+",\"creationFileTime\":\""+std::to_string(p->ft)+
                            "\",\"role\":"+jtext(p->role)+",\"buildRoot\":"+(p->root?"true":"false")+fields+",\"provider\":\"Win32_Process.CommandLine\",\"heldBeforeAndAfter\":true,\"wmiThreadExited\":true,\"providerCancellationProved\":false");p->queried=true;
                    }
                }else{
                    need(p!=nullptr,"unregistered-debug-member");member(p->held.get(),job.get(),p->pid,p->ft,event.dwDebugEventCode!=EXIT_PROCESS_DEBUG_EVENT);
                    if(event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT) {
                        if(eventFile.valid()){const auto path=file_path(eventFile.get());if(_wcsicmp(path.c_str(),ntdll.physical.path.c_str())==0){
                            need(!p->moduleFile.valid(),"duplicate-native-ntdll");p->moduleFile.reset(duplicate(eventFile.get()));p->module=identity(eventFile.get());same(ntdll.physical,p->module);p->layout=layout;
                            p->moduleBase=static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(event.u.LoadDll.lpBaseOfDll));need(p->moduleBase!=0,"ntdll-base");
                            report.row("ntdll-load",",\"pid\":"+std::to_string(p->pid)+",\"creationFileTime\":\""+std::to_string(p->ft)+"\",\"moduleBase\":\""+std::to_string(p->moduleBase)+"\",\"moduleFile\":{"+p->module.fields()+"},\"peLayout\":{"+p->layout.fields()+"}");}}
                    }else if(event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT) {
                        const auto& x=event.u.Exception;const auto code=x.ExceptionRecord.ExceptionCode;const auto address=static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(x.ExceptionRecord.ExceptionAddress));
                        const bool initial=x.dwFirstChance!=0 && event.dwThreadId==p->mainTid && !p->initial && code==EXCEPTION_BREAKPOINT && p->moduleFile.valid() && p->layout.contains(p->moduleBase,address);
                        if(initial)p->initial=true;else disposition=DBG_EXCEPTION_NOT_HANDLED;
                        // First chance belongs to the original runtime/SEH handler. CLR caught exceptions are not sticky failures.
                        if(!initial && !x.dwFirstChance){failure="last-chance-exception";failureCode=code;}
                        report.row("exception-disposition",",\"pid\":"+std::to_string(p->pid)+",\"creationFileTime\":\""+std::to_string(p->ft)+"\",\"tid\":"+std::to_string(event.dwThreadId)+
                            ",\"code\":"+std::to_string(code)+",\"address\":\""+std::to_string(address)+"\",\"firstChance\":"+(x.dwFirstChance?"true":"false")+",\"initialLoader\":"+(initial?"true":"false")+",\"continueStatus\":"+std::to_string(disposition));
                    }else if(event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT) {
                        p->exitSeen=true;p->exitCode=event.u.ExitProcess.dwExitCode;
                        report.row("member-exit",",\"pid\":"+std::to_string(p->pid)+",\"creationFileTime\":\""+std::to_string(p->ft)+"\",\"exitCode\":"+std::to_string(p->exitCode));
                        if(p->root){rootExitSeen=true;rootExit=p->exitCode;}
                    }else if(event.dwDebugEventCode==RIP_EVENT){failure="rip-event";failureCode=event.u.RipInfo.dwError;report.row("rip",",\"pid\":"+std::to_string(p->pid)+",\"error\":"+std::to_string(failureCode));}
                    else if(event.dwDebugEventCode==UNLOAD_DLL_DEBUG_EVENT)need(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(event.u.UnloadDll.lpBaseOfDll))!=p->moduleBase,"native-ntdll-unloaded");
                    else need(event.dwDebugEventCode==CREATE_THREAD_DEBUG_EVENT || event.dwDebugEventCode==EXIT_THREAD_DEBUG_EVENT || event.dwDebugEventCode==OUTPUT_DEBUG_STRING_EVENT,"known-debug-event");
                }
                check(ContinueDebugEvent(event.dwProcessId,event.dwThreadId,disposition)!=FALSE,"debug-continue");continued=true;
                if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT)report.row("create-continued",",\"pid\":"+std::to_string(p->pid)+",\"creationFileTime\":\""+std::to_string(p->ft)+"\",\"sameHeldProcess\":true");
            }catch(...){protocol=false;forced=true;TerminateJobObject(job.get(),124);if(!continued)ContinueDebugEvent(event.dwProcessId,event.dwThreadId,event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT?DBG_EXCEPTION_NOT_HANDLED:DBG_CONTINUE);throw;}
        }
        need(rootExitSeen && rootExit==0 && failure.empty(),"build-natural-root-zero");need(closed_members(history),"all-held-historical-members-signaled");
        for(const auto& p:history){need(p->initial,"one-initial-loader-per-member");same(p->image,identity(p->imageFile.get()));}
        for(const auto& image:frozen)same(image->physical,identity(image->file.get()));same(ntdll.physical,identity(ntdll.file.get()));source_pins();budget();
    }catch(const api_error& e){if(failure.empty()){failure=e.what();failureCode=e.code;}}catch(...){failure="unknown-observer-error";failureCode=ERROR_GEN_FAILURE;}
    if(root.valid() && (!failure.empty()) && (!assigned || account(job.get()).ActiveProcesses!=0)) {
        forced=true;if(assigned)TerminateJobObject(job.get(),124);else TerminateProcess(root.get(),125);
        const auto cleanup=std::chrono::steady_clock::now();while(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-cleanup).count()<5000 && (!closed_members(history)||account(job.get()).ActiveProcesses!=0)){
            DEBUG_EVENT e{};if(WaitForDebugEvent(&e,20)){handle f(e.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT?e.u.CreateProcessInfo.hFile:e.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT?e.u.LoadDll.hFile:nullptr);
                if(e.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT){auto* p=active_member(history,e.dwProcessId);if(p){p->exitSeen=true;p->exitCode=e.u.ExitProcess.dwExitCode;}else protocol=false;}
                if(e.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT){protocol=false;if(initialProcess.get()==e.u.CreateProcessInfo.hProcess)initialProcess.release();if(initialThread.get()==e.u.CreateProcessInfo.hThread)initialThread.release();}
                if(!ContinueDebugEvent(e.dwProcessId,e.dwThreadId,e.dwDebugEventCode==EXCEPTION_DEBUG_EVENT?DBG_EXCEPTION_NOT_HANDLED:DBG_CONTINUE))break;
            }
        }
    }
    const auto final=account(job.get());const bool allClosed=closed_members(history);const bool historyComplete=history.size()==final.TotalProcesses;bool allQueries=true,allLoader=true;
    for(const auto& p:history){if(p->root||p->role==L"CLANG"||p->role==L"RC"||p->role==L"MT")allQueries=allQueries&&p->queried;allLoader=allLoader&&p->initial;}
    const bool passed=failure.empty() && protocol && !forced && rootExitSeen && rootExit==0 && allClosed && historyComplete && allQueries && allLoader && final.ActiveProcesses==0 && final.TotalTerminatedProcesses==0;
    report.row("terminal",",\"passed\":"+std::string(passed?"true":"false")+",\"failure\":\""+failure+"\",\"failureCode\":"+std::to_string(failureCode)+
        ",\"rootPid\":"+std::to_string(rootPid)+",\"rootCreationFileTime\":\""+std::to_string(rootFt)+"\",\"rootExitCode\":"+std::to_string(rootExit)+",\"historicalMembers\":"+std::to_string(history.size())+
        ",\"totalProcesses\":"+std::to_string(final.TotalProcesses)+",\"activeProcesses\":"+std::to_string(final.ActiveProcesses)+",\"limitTerminatedProcesses\":"+std::to_string(final.TotalTerminatedProcesses)+
        ",\"allMembersSignaled\":"+(allClosed?"true":"false")+",\"allPausedQueriesClosed\":"+(allQueries?"true":"false")+",\"allLoaderSequences\":"+(allLoader?"true":"false")+",\"debugProtocolValid\":"+(protocol?"true":"false")+
        ",\"forced\":"+(forced?"true":"false")+",\"queryCount\":"+std::to_string(queries)+",\"eventCount\":"+std::to_string(events)+",\"sdkProof\":false,\"normalSdkIntegrated\":false,\"win7Proof\":false");return passed?0:124;
}
} // namespace
int wmain(int count,wchar_t** args){try{return run(parse(count,args));}catch(const api_error& e){const auto s=std::string("host64-source-error|")+e.what()+"|"+std::to_string(e.code)+"\n";DWORD n=0;WriteFile(GetStdHandle(STD_ERROR_HANDLE),s.data(),static_cast<DWORD>(s.size()),&n,nullptr);return 125;}catch(...){return 125;}}
