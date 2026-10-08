#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct handle {
    HANDLE value = nullptr;
    ~handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    handle(const handle&) = delete;
    handle& operator=(const handle&) = delete;
    explicit handle(HANDLE raw = nullptr) : value(raw) {}
};
std::wstring quote(const std::wstring& value) {
    std::wstring result = L"\""; std::size_t slashes = 0;
    for (const auto ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        result.append(slashes * (ch == L'"' ? 2 : 1), L'\\'); slashes = 0;
        if (ch == L'"') result += L'\\'; result += ch;
    }
    result.append(slashes * 2, L'\\'); return result + L'"';
}
void write_new(const std::wstring& path, const std::string& text) {
    handle file(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.value == INVALID_HANDLE_VALUE) throw std::runtime_error("fixture-create-file");
    DWORD written = 0;
    if (!WriteFile(file.value, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) || written != text.size() ||
        !FlushFileBuffers(file.value)) throw std::runtime_error("fixture-write-file");
}
void spawn_ready(const std::wstring& image, const std::wstring& directory, unsigned index) {
    const auto eventName = L"Local\\SOUI-owned-tail-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
        std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(index);
    handle ready(CreateEventW(nullptr, TRUE, FALSE, eventName.c_str()));
    if (!ready.value || GetLastError() == ERROR_ALREADY_EXISTS) throw std::runtime_error("fixture-ready-event");
    auto command = quote(image) + L" idle " + quote(directory) + L" " + quote(eventName);
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION info{};
    if (!CreateProcessW(image.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        directory.c_str(), &startup, &info)) throw std::runtime_error("fixture-spawn");
    handle process(info.hProcess), thread(info.hThread);
    if (WaitForSingleObject(ready.value, 5000) != WAIT_OBJECT_0 || WaitForSingleObject(process.value, 0) != WAIT_TIMEOUT)
        throw std::runtime_error("fixture-child-not-ready");
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process.value, &created, &exited, &kernel, &user)) throw std::runtime_error("fixture-child-identity");
    const auto creation = (std::uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    write_new(directory + L"\\child-" + std::to_wstring(index) + L".json",
        "{\"pid\":" + std::to_string(info.dwProcessId) + ",\"creationFileTime\":\"" + std::to_string(creation) + "\"}\n");
}
int run(int count, wchar_t** args) {
    if (count < 2) throw std::runtime_error("fixture-mode");
    const std::wstring mode(args[1]);
    if (mode == L"brief") return 0;
    if (count < 3) throw std::runtime_error("fixture-directory");
    const std::wstring directory(args[2]);
    if (mode == L"idle") {
        if (count != 4) throw std::runtime_error("fixture-ready-name");
        handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, args[3]));
        if (!ready.value || !SetEvent(ready.value)) throw std::runtime_error("fixture-signal-ready");
        // Only an owned Job may stop this process. The bounded fallback cannot
        // itself yield successful tail evidence in a test that runs too slowly.
        Sleep(30000); return 3;
    }
    if (mode == L"external") {
        write_new(directory + L"\\external-ready", std::to_string(GetCurrentProcessId()) + "\n");
        const auto began = GetTickCount64();
        while (GetTickCount64() - began < 20000) {
            if (GetFileAttributesW((directory + L"\\external-finish").c_str()) != INVALID_FILE_ATTRIBUTES) return 0;
            Sleep(10);
        }
        return 3;
    }
    if (count != 4) throw std::runtime_error("fixture-image");
    const auto children = mode == L"single" || mode == L"unknown" ? 1U : mode == L"multi" ? 2U : mode == L"many" ? 9U : 0U;
    if (!children) throw std::runtime_error("fixture-unknown-mode");
    for (unsigned index = 0; index < children; ++index) spawn_ready(args[3], directory, index);
    return 0;
}
} // namespace

// WINDOWS subsystem deliberately avoids a console host; no window is created.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int count = 0; auto** args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) return 125;
    struct local_owner { wchar_t** value; ~local_owner() { LocalFree(value); } } owner{args};
    try { return run(count, args); } catch (...) { return 125; }
}
