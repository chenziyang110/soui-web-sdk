#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define NOMINMAX
#include <windows.h>
#include <iostream>

// This compiled fixture replaces only assignment. Process creation, held
// handles, termination, waiting, exit-code reads and reporting remain the
// production runner's real Windows operations. It proves branch cleanup,
// not any real Win7/PCA/nested-Job policy or platform compatibility.
static BOOL WINAPI test_only_deny_assignment(HANDLE, HANDLE) {
    std::cerr << "TEST_ONLY_ASSIGNMENT_DENIAL: synthetic ERROR_ACCESS_DENIED; "
                 "real suspended-process cleanup\n";
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}

// Headers are loaded before replacement to leave all Win32 declarations
// intact. No injection option or hook is added to the production binary.
#define AssignProcessToJobObject test_only_deny_assignment
#include "../../benchmarks/protocol/owned-process-runner.cpp"
#undef AssignProcessToJobObject
