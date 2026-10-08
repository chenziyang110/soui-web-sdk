#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <string>

// TEST ONLY: inject a real newly-created suspended process into the already
// snapshotted held Job immediately before real TerminateJobObject. This proves
// fixed-total rejection, not a naturally observed compiler race or Win7 policy.
static BOOL WINAPI test_only_add_member_then_terminate(HANDLE job, UINT code) {
    wchar_t image[32768]{};
    const auto size = GetEnvironmentVariableW(L"SOUI_TEST_TAIL_MUTATION_EXE", image, 32768);
    if (!size || size >= 32768) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    auto command = std::wstring(L"\"") + image + L"\" brief";
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION info{};
    if (!CreateProcessW(image, command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &info)) return FALSE;
    struct handles { HANDLE process, thread; ~handles() { CloseHandle(thread); CloseHandle(process); } } held{info.hProcess, info.hThread};
    if (!AssignProcessToJobObject(job, info.hProcess)) {
        const auto error = GetLastError();
        TerminateProcess(info.hProcess, 125); WaitForSingleObject(info.hProcess, 5000);
        SetLastError(error); return FALSE;
    }
    std::cerr << "TEST_ONLY_FIXED_TOTAL_MUTATION: real newly-created Job member before cleanup\n";
    return TerminateJobObject(job, code);
}
#define TerminateJobObject test_only_add_member_then_terminate
#include "../../benchmarks/protocol/owned-process-runner.cpp"
#undef TerminateJobObject
