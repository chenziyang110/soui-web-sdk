#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define NOMINMAX
#include <windows.h>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
class handle final {
public:
    explicit handle(HANDLE value = nullptr) noexcept : value_(value) {}
    ~handle() { if (valid()) CloseHandle(value_); }
    handle(const handle&) = delete;
    handle& operator=(const handle&) = delete;
    HANDLE get() const noexcept { return value_; }
    bool valid() const noexcept { return value_ && value_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE value_;
};

[[noreturn]] void fail(const char* operation) {
    throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(GetLastError()));
}

HANDLE child_standard_handle(DWORD standard, DWORD nullAccess) {
    const auto original = GetStdHandle(standard);
    HANDLE copied = nullptr;
    if (original && original != INVALID_HANDLE_VALUE) {
        if (!DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(), &copied,
                             0, TRUE, DUPLICATE_SAME_ACCESS)) fail("Duplicate child output handle");
        return copied;
    }
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    copied = CreateFileW(L"NUL", nullAccess, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (copied == INVALID_HANDLE_VALUE) fail("Create child null output");
    return copied;
}

class inherited_handles final {
public:
    explicit inherited_handles(std::vector<HANDLE>& handles) {
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        if (!bytes) fail("Size child handle list");
        buffer_.resize(bytes);
        list_ = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer_.data());
        if (!InitializeProcThreadAttributeList(list_, 1, 0, &bytes)) fail("Create child handle list");
        initialized_ = true;
        if (!UpdateProcThreadAttribute(list_, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles.data(),
                                       handles.size() * sizeof(HANDLE), nullptr, nullptr)) {
            const auto error = GetLastError();
            DeleteProcThreadAttributeList(list_);
            initialized_ = false;
            SetLastError(error);
            fail("Set child handle list");
        }
    }
    ~inherited_handles() { if (initialized_) DeleteProcThreadAttributeList(list_); }
    inherited_handles(const inherited_handles&) = delete;
    inherited_handles& operator=(const inherited_handles&) = delete;
    LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept { return list_; }
private:
    std::vector<unsigned char> buffer_;
    LPPROC_THREAD_ATTRIBUTE_LIST list_ = nullptr;
    bool initialized_ = false;
};

std::uint64_t number(const wchar_t* raw, std::uint64_t maximum) {
    const std::wstring text(raw);
    if (text.empty() || text.size() > 10 || text.find_first_not_of(L"0123456789") != std::wstring::npos)
        throw std::runtime_error("Invalid time limit");
    const auto value = std::stoull(text);
    if (value > maximum) throw std::runtime_error("Time limit exceeds maximum");
    return value;
}

std::wstring quote(const std::wstring& value) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const auto character : value) {
        if (character == L'\\') { ++slashes; continue; }
        result.append(slashes * (character == L'\"' ? 2 : 1), L'\\');
        slashes = 0;
        if (character == L'\"') result += L'\\';
        result += character;
    }
    result.append(slashes * 2, L'\\');
    result += L'\"';
    return result;
}

bool absolute(const std::wstring& value) {
    return (value.size() > 2 && value[1] == L':' && (value[2] == L'\\' || value[2] == L'/')) ||
        (value.size() > 2 && value[0] == L'\\' && value[1] == L'\\');
}

class reporter final {
public:
    explicit reporter(const std::wstring& path)
        : file_(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL, nullptr)) {
        if (!file_.valid()) fail("Create report");
    }
    void write(const std::string& row) {
        const std::string bytes = row + "\n";
        DWORD written = 0;
        if (!WriteFile(file_.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
            written != bytes.size()) fail("Write report");
        if (!FlushFileBuffers(file_.get())) fail("Flush report");
    }
private:
    handle file_;
};

struct options {
    std::uint64_t timeout = 0;
    std::uint64_t drain = 2000;
    std::wstring report;
    std::wstring cwd;
    std::wstring executable;
    std::wstring command;
    bool stopOnStdin = false;
};

options parse(int count, wchar_t** args) {
    options config;
    int index = 1;
    while (index < count && std::wstring(args[index]) != L"--") {
        const std::wstring flag(args[index++]);
        if (flag == L"--stop-on-stdin") { config.stopOnStdin = true; continue; }
        if (index == count) throw std::runtime_error("Missing option value");
        const auto* value = args[index++];
        if (flag == L"--timeout-ms") config.timeout = number(value, 172800000);
        else if (flag == L"--drain-ms") config.drain = number(value, 60000);
        else if (flag == L"--report") config.report = value;
        else if (flag == L"--cwd") config.cwd = value;
        else throw std::runtime_error("Unknown option");
    }
    if (!config.timeout || !absolute(config.report) || !absolute(config.cwd) || ++index >= count)
        throw std::runtime_error("Usage: --timeout-ms N --drain-ms N --report ABS --cwd ABS [--stop-on-stdin] -- ABS_EXE [args]");
    config.executable = args[index];
    if (!absolute(config.executable)) throw std::runtime_error("Executable must be absolute");
    for (; index < count; ++index) {
        if (!config.command.empty()) config.command += L' ';
        config.command += quote(args[index]);
    }
    if (config.command.size() >= 32767) throw std::runtime_error("Command line too long");
    return config;
}

JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting(HANDLE job) {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION result{};
    if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &result, sizeof(result), nullptr))
        fail("Query owned Job");
    return result;
}

std::string counts(const JOBOBJECT_BASIC_ACCOUNTING_INFORMATION& state) {
    return "\"activeProcesses\":" + std::to_string(state.ActiveProcesses) +
        ",\"totalProcesses\":" + std::to_string(state.TotalProcesses) +
        ",\"limitTerminatedProcesses\":" + std::to_string(state.TotalTerminatedProcesses);
}

std::string json(const std::wstring& value) {
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!length && !value.empty()) fail("Encode member image");
    std::string text(static_cast<std::size_t>(length), '\0');
    if (length && !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), text.data(), length, nullptr, nullptr)) fail("Encode member image");
    std::string escaped = "\"";
    for (const unsigned char character : text) {
        if (character == '\\' || character == '\"') escaped += '\\';
        if (character < 32) throw std::runtime_error("Control character in member image");
        escaped += static_cast<char>(character);
    }
    return escaped + "\"";
}

void members(HANDLE job, reporter& report) {
    std::vector<unsigned char> buffer(sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + 64 * sizeof(ULONG_PTR));
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (!QueryInformationJobObject(job, JobObjectBasicProcessIdList, buffer.data(),
            static_cast<DWORD>(buffer.size()), nullptr)) {
            const auto error = GetLastError();
            if (error == ERROR_MORE_DATA && buffer.size() < 65536) { buffer.resize(buffer.size() * 2); continue; }
            SetLastError(error);
            fail("Enumerate held Job");
        }
        const auto* list = reinterpret_cast<const JOBOBJECT_BASIC_PROCESS_ID_LIST*>(buffer.data());
        for (DWORD index = 0; index < list->NumberOfProcessIdsInList; ++index) {
            const auto pid = static_cast<DWORD>(list->ProcessIdList[index]);
            handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
            if (!process.valid()) {
                report.write("{\"event\":\"member-unavailable\",\"pid\":" + std::to_string(pid) +
                    ",\"win32Error\":" + std::to_string(GetLastError()) + "}");
                continue;
            }
            BOOL inJob = FALSE;
            if (!IsProcessInJob(process.get(), job, &inJob)) {
                report.write("{\"event\":\"member-unavailable\",\"pid\":" + std::to_string(pid) +
                    ",\"operation\":\"verify-membership\",\"win32Error\":" + std::to_string(GetLastError()) + "}");
                continue;
            }
            if (!inJob) {
                report.write("{\"event\":\"member-no-longer-owned\",\"pid\":" + std::to_string(pid) + "}");
                continue;
            }
            FILETIME created{}, exited{}, kernel{}, user{};
            if (!GetProcessTimes(process.get(), &created, &exited, &kernel, &user)) {
                report.write("{\"event\":\"member-unavailable\",\"pid\":" + std::to_string(pid) +
                    ",\"operation\":\"read-identity\",\"win32Error\":" + std::to_string(GetLastError()) + "}");
                continue;
            }
            const auto creation = (std::uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
            std::wstring image(32768, L'\0');
            DWORD length = static_cast<DWORD>(image.size());
            if (!QueryFullProcessImageNameW(process.get(), 0, image.data(), &length)) {
                // Member snapshots race with natural exit. Cleanup ownership is
                // established by the held Job, never by this optional image path.
                report.write("{\"event\":\"owned-member\",\"pid\":" + std::to_string(pid) +
                    ",\"creationFileTime\":\"" + std::to_string(creation) + "\",\"image\":null,"
                    "\"imageReadError\":" + std::to_string(GetLastError()) + "}");
                continue;
            }
            image.resize(length);
            report.write("{\"event\":\"owned-member\",\"pid\":" + std::to_string(pid) +
                ",\"creationFileTime\":\"" + std::to_string(creation) + "\",\"image\":" + json(image) + "}");
        }
        return;
    }
    throw std::runtime_error("Held Job member list remained unstable");
}

int run(const options& config) {
    const auto control = config.stopOnStdin ? GetStdHandle(STD_INPUT_HANDLE) : nullptr;
    if (config.stopOnStdin && (!control || control == INVALID_HANDLE_VALUE || GetFileType(control) != FILE_TYPE_PIPE))
        throw std::runtime_error("Stop control requires the supervisor's dedicated stdin pipe");
    std::string controlBytes;
    const auto controlReason = [&]() -> std::string {
        if (!config.stopOnStdin) return {};
        DWORD available = 0;
        if (!PeekNamedPipe(control, nullptr, 0, nullptr, &available, nullptr))
            return GetLastError() == ERROR_BROKEN_PIPE ? "supervisor-disconnected" : "stop-control-failed";
        if (!available) return {};
        char buffer[6]{};
        DWORD read = 0;
        if (available > sizeof(buffer) || !ReadFile(control, buffer, available, &read, nullptr))
            return "stop-control-failed";
        controlBytes.append(buffer, read);
        if (controlBytes == "STOP\n") return "supervisor-stop";
        if (controlBytes.size() > 5 || std::string("STOP\n").compare(0, controlBytes.size(), controlBytes) != 0)
            return "stop-control-failed";
        return {};
    };
    reporter report(config.report);
    handle job(CreateJobObjectW(nullptr, nullptr));
    if (!job.valid()) fail("Create owned Job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        fail("Set owned Job limits");
    // Preserve child output through a selective handle list. The Job, report
    // and supervisor's stdin control pipe are never inherited by the subject.
    handle childOut(child_standard_handle(STD_OUTPUT_HANDLE, GENERIC_WRITE));
    handle childErr(child_standard_handle(STD_ERROR_HANDLE, GENERIC_WRITE));
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    handle childIn(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!childIn.valid()) fail("Create child null input");
    std::vector<HANDLE> standardHandles{childIn.get(), childOut.get(), childErr.get()};
    inherited_handles attributes(standardHandles);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = childIn.get();
    startup.StartupInfo.hStdOutput = childOut.get();
    startup.StartupInfo.hStdError = childErr.get();
    startup.lpAttributeList = attributes.get();
    PROCESS_INFORMATION info{};
    auto command = config.command;
    if (!CreateProcessW(config.executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                        nullptr, config.cwd.c_str(), &startup.StartupInfo, &info))
        fail("Create suspended child");
    handle process(info.hProcess);
    handle thread(info.hThread);
    if (!AssignProcessToJobObject(job.get(), process.get())) {
        const auto error = GetLastError();
        // Only the held, newly-created suspended process is stopped when assignment fails.
        FILETIME created{}, exited{}, kernel{}, user{};
        const bool identityRead = GetProcessTimes(process.get(), &created, &exited, &kernel, &user) != FALSE;
        const auto identityError = identityRead ? ERROR_SUCCESS : GetLastError();
        const auto creation = (std::uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        const bool terminateSucceeded = TerminateProcess(process.get(), 125) != FALSE;
        const auto terminateError = terminateSucceeded ? ERROR_SUCCESS : GetLastError();
        const auto waited = WaitForSingleObject(process.get(), 5000);
        const auto waitError = waited == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
        DWORD exitCode = STILL_ACTIVE;
        const bool exitRead = waited == WAIT_OBJECT_0 && GetExitCodeProcess(process.get(), &exitCode);
        const auto exitError = waited == WAIT_OBJECT_0 && !exitRead ? GetLastError() : ERROR_SUCCESS;
        report.write("{\"schema\":\"soui.owned-process-run/v1\",\"event\":\"terminal\","
            "\"reason\":\"assignment-failed\",\"pid\":" + std::to_string(info.dwProcessId) +
            ",\"creationFileTime\":" + (identityRead ? "\"" + std::to_string(creation) + "\"" : "null") +
            ",\"identityError\":" + std::to_string(identityError) +
            ",\"assignmentError\":" + std::to_string(error) +
            ",\"terminateSucceeded\":" + (terminateSucceeded ? "true" : "false") +
            ",\"terminateError\":" + std::to_string(terminateError) +
            ",\"waitResult\":" + std::to_string(waited) + ",\"waitError\":" + std::to_string(waitError) +
            ",\"rootExitCode\":" + (exitRead ? std::to_string(exitCode) : "null") +
            ",\"exitReadError\":" + std::to_string(exitError) +
            ",\"closureConfirmed\":" + (exitRead ? "true" : "false") +
            ",\"jobAssignedBeforeResume\":false,\"passed\":false}");
        return 125;
    }
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process.get(), &created, &exited, &kernel, &user)) fail("Read child identity");
    const auto creation = (std::uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    report.write("{\"schema\":\"soui.owned-process-run/v1\",\"event\":\"start\",\"pid\":" +
        std::to_string(info.dwProcessId) + ",\"creationFileTime\":\"" + std::to_string(creation) +
        "\",\"timeoutMs\":" + std::to_string(config.timeout) + ",\"drainMs\":" +
        std::to_string(config.drain) + ",\"jobAssignedBeforeResume\":true,\"breakawayAllowed\":false}");
    const auto started = std::chrono::steady_clock::now();
    const auto elapsed = [&] {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count());
    };
    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) fail("Resume owned child");
    bool rootExited = false;
    std::uint64_t rootExitedAt = 0;
    DWORD rootCode = STILL_ACTIVE;
    std::string reason;
    auto state = accounting(job.get());
    members(job.get(), report);
    auto lastTotal = state.TotalProcesses;
    auto lastActive = state.ActiveProcesses;
    while (true) {
        const auto waited = WaitForSingleObject(process.get(), 20);
        if (waited == WAIT_FAILED) fail("Wait owned child");
        if (!rootExited && waited == WAIT_OBJECT_0) {
            if (!GetExitCodeProcess(process.get(), &rootCode)) fail("Read child exit");
            rootExited = true;
            rootExitedAt = elapsed();
            report.write("{\"event\":\"root-exit\",\"elapsedMs\":" + std::to_string(rootExitedAt) +
                ",\"exitCode\":" + std::to_string(rootCode) + "}");
        }
        state = accounting(job.get());
        if (state.TotalProcesses != lastTotal || state.ActiveProcesses != lastActive) {
            report.write("{\"event\":\"job-members\",\"elapsedMs\":" + std::to_string(elapsed()) +
                "," + counts(state) + "}");
            lastTotal = state.TotalProcesses;
            lastActive = state.ActiveProcesses;
            members(job.get(), report);
        }
        if (elapsed() >= config.timeout) { reason = "timeout"; break; }
        const auto requested = controlReason();
        if (!requested.empty()) { reason = requested; break; }
        if (rootExited && state.ActiveProcesses == 0) {
            reason = rootCode == 0 ? "completed" : "root-failed";
            break;
        }
        if (rootExited && (rootCode != 0 || elapsed() - rootExitedAt >= config.drain)) {
            reason = rootCode == 0 ? "owned-descendants-remain" : "root-failed";
            break;
        }
        if (rootExited) Sleep(20); // The root handle remains signaled during bounded descendant drain.
    }
    const auto activeBeforeCleanup = state.ActiveProcesses;
    bool terminated = false;
    if (activeBeforeCleanup != 0) {
        if (!TerminateJobObject(job.get(), 124)) fail("Terminate owned Job");
        terminated = true;
        const auto cleanupAt = elapsed();
        do {
            state = accounting(job.get());
            if (state.ActiveProcesses == 0) break;
            Sleep(20);
        } while (elapsed() - cleanupAt < 5000);
    }
    if (!rootExited && WaitForSingleObject(process.get(), 5000) == WAIT_OBJECT_0) {
        if (!GetExitCodeProcess(process.get(), &rootCode)) fail("Read terminated child exit");
        rootExited = true;
    }
    const auto finalState = accounting(job.get());
    const bool closureConfirmed = state.ActiveProcesses == 0 && finalState.ActiveProcesses == 0 &&
        state.TotalProcesses == finalState.TotalProcesses;
    const bool passed = reason == "completed" && rootExited && rootCode == 0 && closureConfirmed;
    report.write("{\"event\":\"terminal\",\"reason\":\"" + reason + "\",\"elapsedMs\":" +
        std::to_string(elapsed()) + ",\"rootExited\":" + (rootExited ? "true" : "false") +
        ",\"rootExitCode\":" + (rootExited ? std::to_string(rootCode) : "null") +
        ",\"activeBeforeCleanup\":" + std::to_string(activeBeforeCleanup) +
        ",\"terminatedOwnedJob\":" + (terminated ? "true" : "false") +
        ",\"closureConfirmed\":" + (closureConfirmed ? "true" : "false") +
        "," + counts(finalState) + ",\"passed\":" + (passed ? "true" : "false") + "}");
    if (!closureConfirmed) return 125;
    if (passed) return 0;
    if (reason == "timeout" || reason == "owned-descendants-remain" || reason == "supervisor-stop" ||
        reason == "supervisor-disconnected") return 124;
    return rootCode != 0 ? static_cast<int>(rootCode) : 125;
}
} // namespace

int wmain(int count, wchar_t** args) {
    try { return run(parse(count, args)); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 125; }
}
