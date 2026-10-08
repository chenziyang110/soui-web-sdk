#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define NOMINMAX
#include <windows.h>
#include <string>

// TEST ONLY diagnostic API responses. Real processes, the held Job, disk pin,
// termination and waits remain real. These cases prove fail-closed branches;
// they do not claim the synthetic response was observed from this OS.
static std::wstring test_mode() {
    wchar_t text[64]{};
    const auto count = GetEnvironmentVariableW(L"SOUI_TEST_TAIL_API_MODE", text, 64);
    return count && count < 64 ? std::wstring(text, count) : std::wstring{};
}
static BOOL WINAPI test_only_query(HANDLE job, JOBOBJECTINFOCLASS kind, LPVOID buffer, DWORD size, LPDWORD returned) {
    static unsigned accountCalls = 0, listCalls = 0;
    const auto ok = QueryInformationJobObject(job, kind, buffer, size, returned);
    if (!ok) return ok;
    const auto mode = test_mode();
    if (kind == JobObjectBasicAccountingInformation) {
        ++accountCalls;
        if (mode == L"retry-count" && accountCalls == 1)
            ++static_cast<JOBOBJECT_BASIC_ACCOUNTING_INFORMATION*>(buffer)->ActiveProcesses;
    }
    if (kind == JobObjectBasicProcessIdList) {
        ++listCalls;
        auto* list = static_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(buffer);
        if (mode == L"success-truncated" && listCalls == 1) {
            list->NumberOfAssignedProcesses = list->NumberOfProcessIdsInList + 5;
        } else if (mode == L"duplicate-pid" && list->NumberOfProcessIdsInList == 1) {
            list->NumberOfAssignedProcesses = 2; list->NumberOfProcessIdsInList = 2;
            list->ProcessIdList[1] = list->ProcessIdList[0];
        } else if (mode == L"pid-hole" && list->NumberOfProcessIdsInList)
            list->ProcessIdList[0] = 0;
    }
    return ok;
}
static BOOL WINAPI test_only_image(HANDLE process, DWORD flags, LPWSTR path, PDWORD size) {
    static unsigned calls = 0; ++calls;
    if (test_mode() == L"image-permission" && calls == 1) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return QueryFullProcessImageNameW(process, flags, path, size);
}
#define QueryInformationJobObject test_only_query
#define QueryFullProcessImageNameW test_only_image
#include "../../benchmarks/protocol/owned-tail-evidence.cpp"
#undef QueryFullProcessImageNameW
#undef QueryInformationJobObject
// The generic supervision path is deliberately outside the replacements.
#include "../../benchmarks/protocol/owned-process-runner.cpp"
