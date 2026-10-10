#include <atomic>
#include <cstdio>
#include <cstring>
#include <windows.h>
#include "net/ActivatingObjCheck.h"
#include "allocation_fault.h"

struct Contender {
    net::ActivatingObjCheck* registry;
    HANDLE start;
    HANDLE entered;
    HANDLE done;
    std::atomic<bool> succeeded;
};

DWORD WINAPI contend(void* opaque)
{
    Contender* contender = static_cast<Contender*>(opaque);
    if (::WaitForSingleObject(contender->start, INFINITE) != WAIT_OBJECT_0) return 3;
    ::SetEvent(contender->entered);
    try {
        const bool originalPresent = contender->registry->isActivating(101);
        const bool failedAbsent = !contender->registry->isActivating(202);
        contender->registry->add(303);
        contender->succeeded = originalPresent && failedAbsent && contender->registry->isActivating(303);
    } catch (...) {
        contender->succeeded = false;
    }
    ::SetEvent(contender->done);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    const bool snapshot = std::strcmp(argv[1], "snapshot-failure-lock-release") == 0;
    const bool insert = std::strcmp(argv[1], "insert-failure-lock-release") == 0;
    if (!snapshot && !insert) return 2;
    net::ActivatingObjCheck* registry = net::ActivatingObjCheck::inst();
    registry->add(101);
    if (snapshot) for (int i = 0; i != 12; ++i) registry->add(400 + i);
    Contender contender{registry, ::CreateEventW(nullptr, TRUE, FALSE, nullptr),
        ::CreateEventW(nullptr, TRUE, FALSE, nullptr), ::CreateEventW(nullptr, TRUE, FALSE, nullptr), false};
    if (!contender.start || !contender.entered || !contender.done) return 3;
    HANDLE thread = ::CreateThread(nullptr, 0, contend, &contender, 0, nullptr);
    if (!thread) return 3;
    allocation_fault::arm(0);
    bool threw = false;
    try {
        if (snapshot) registry->doGarbageCollected(true);
        else registry->add(202);
    } catch (const std::bad_alloc&) { threw = true; }
    allocation_fault::disarm();
    const long attempts = allocation_fault::attempts.load();
    const long failures = allocation_fault::failures.load();
    if (!threw || attempts != 1 || failures != 1) {
        std::fprintf(stderr, "FAIL exact allocation boundary not reached threw=%d attempts=%ld failures=%ld\n", threw ? 1 : 0, attempts, failures);
        std::fflush(stderr);
        std::_Exit(3);
    }
    ::SetEvent(contender.start);
    if (::WaitForSingleObject(contender.entered, 2000) != WAIT_OBJECT_0) {
        std::fprintf(stderr, "FAIL contender did not start\n");
        std::fflush(stderr);
        std::_Exit(3);
    }
    if (::WaitForSingleObject(contender.done, 2000) != WAIT_OBJECT_0) {
        std::fprintf(stderr, "OBSERVED allocationException=bad_alloc attempts=1 failures=1 contenderStarted=1 contenderCompleted=0\nFAIL registry critical section retained after unwind\n");
        std::fflush(stderr);
        // Do not destroy a registry/critical section beneath a blocked native
        // contender. This failing, Root-owned diagnostic process exits as a whole.
        std::_Exit(1);
    }
    if (::WaitForSingleObject(thread, 2000) != WAIT_OBJECT_0 || !contender.succeeded.load()) return 1;
    ::CloseHandle(thread);
    ::CloseHandle(contender.start);
    ::CloseHandle(contender.entered);
    ::CloseHandle(contender.done);
    registry->remove(101);
    registry->remove(303);
    if (snapshot) for (int i = 0; i != 12; ++i) registry->remove(400 + i);
    registry->destroy(); // Legacy singleton lifetime is unchanged; no later access.
    std::fprintf(stdout, "PASS %s allocationException=bad_alloc contenderCompleted=1 registryReusable=1\n", argv[1]);
    return 0;
}
