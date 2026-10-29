#include "fixture_support.h"
#include "content/web_impl_win/WebThreadImpl.h"
#include "content/web_impl_win/WebTimerBase.h"

namespace fixture {
constexpr DWORD boundMs = 5000;
[[noreturn]] void terminated();
DWORD engineThreadId = 0;
std::atomic<bool> failScheduler{false};
thread_local bool failNextAllocation = false;
thread_local bool closingRegistry = false;
const char* caseId = nullptr;
const char* waitFault = "wait";
content::BlinkPlatformImpl* platform = nullptr;
std::atomic<void*> schedulerStorage{nullptr}, workerStorage{nullptr};
std::atomic<int> schedulerAllocations{0}, schedulerFrees{0}, workerFrees{0}, allocationFailures{0};
std::atomic<int> eventAttempts{0}, eventAcquired{0}, eventClosed{0};
std::atomic<int> launchAttempts{0}, launched{0}, workerClosed{0};
std::atomic<int> sectionsInitialized{0}, sectionsDeleted{0}, tlsAttempts{0}, tlsFailures{0};
std::atomic<int> observedJoins{0}, exitQueries{0}, injectedWaitFailures{0}, injectedExitFailures{0};
std::atomic<int> debugBreaks{0}, closeInspections{0}, inspectedCount{-1}, inspectedReady{-1};
std::atomic<int> windowAcquired{0}, windowDestroyed{0}, timerAcquired{0}, timerKilled{0}, classUnregistered{0};
std::atomic<bool> failEvent{false}, failLaunch{false}, failTls{false}, failJoin{false};
std::atomic<bool> holdStartup{false}, holdNativeReturn{false};
std::atomic<bool> constructorReturned{false};
std::atomic<unsigned> entryReturnCode{STILL_ACTIVE};
std::atomic<DWORD> lastExitCode{STILL_ACTIVE}, lastJoin{WAIT_FAILED}, workerNativeId{0};
std::atomic<HANDLE> wake{nullptr}, worker{nullptr};
HANDLE startupEntered = nullptr, startupRelease = nullptr, entryReturned = nullptr, nativeReturnRelease = nullptr;
HANDLE contenderAttempted = nullptr, contenderReturned = nullptr, constructorDone = nullptr;
HWND nativeWindow = nullptr;
unsigned (__stdcall* actualEntry)(void*) = nullptr;
void* actualArgument = nullptr;
content::WebThreadImpl* committed = nullptr;

bool isCase(const char* name) { return caseId && std::strcmp(caseId, name) == 0; }
void note(const char* phase, unsigned long long value = 0) {
    std::printf("{\"record\":\"native-observation\",\"case\":\"%s\",\"pid\":%lu,\"tid\":%lu,\"phase\":\"%s\",\"value\":%llu}\n",
        caseId, ::GetCurrentProcessId(), ::GetCurrentThreadId(), phase, value);
    std::fflush(stdout);
}
void assertFailure(const char* expression, int line) {
    std::fprintf(stderr, "fixture source assertion at %d: %s\n", line, expression);
    ::ExitProcess(91);
}
void* allocateScheduler(std::size_t size) {
    if (failScheduler.exchange(false)) { ++allocationFailures; note("scheduler-bad-alloc"); throw std::bad_alloc(); }
    void* result = ::operator new(size);
    schedulerStorage = result;
    ++schedulerAllocations;
    return result;
}
HANDLE WINAPI sourceCreateEvent(LPSECURITY_ATTRIBUTES security, BOOL manual, BOOL initial, LPCWSTR name) {
    ++eventAttempts;
    if (failEvent.exchange(false)) { ::SetLastError(ERROR_NOT_ENOUGH_MEMORY); note("event-null"); return nullptr; }
    HANDLE result = ::CreateEventW(security, manual, initial, name);
    wake = result;
    if (result) ++eventAcquired;
    note("event-return", reinterpret_cast<std::uintptr_t>(result));
    return result;
}
unsigned __stdcall nativeTrampoline(void*) {
    workerNativeId = ::GetCurrentThreadId();
    // MSVC's CRT stores this observer in per-thread data. Observe the actual
    // source's std::terminate on its own worker; do not replace that source call.
    std::set_terminate(terminated);
    if (holdStartup) {
        ::SetEvent(startupEntered);
        if (::WaitForSingleObject(startupRelease, boundMs) != WAIT_OBJECT_0) ::ExitProcess(92);
    }
    // The actual extracted CRT entry calls actual threadEntryPoint/fireOnExit.
    const unsigned result = actualEntry(actualArgument);
    entryReturnCode = result;
    ::SetEvent(entryReturned);
    if (holdNativeReturn && ::WaitForSingleObject(nativeReturnRelease, boundMs) != WAIT_OBJECT_0) ::ExitProcess(93);
    return result;
}
std::uintptr_t __cdecl sourceBeginThread(void* security, unsigned stack,
    unsigned (__stdcall* entry)(void*), void* argument, unsigned flags, unsigned* id) {
    ++launchAttempts;
    workerStorage = argument;
    if (failLaunch.exchange(false)) { note("native-launch-zero"); return 0; }
    actualEntry = entry;
    actualArgument = argument;
    const std::uintptr_t result = ::_beginthreadex(security, stack, nativeTrampoline, nullptr, flags, id);
    worker = reinterpret_cast<HANDLE>(result);
    if (worker) ++launched;
    note("beginthreadex-return", result);
    return result;
}
DWORD WINAPI sourceWait(HANDLE handle, DWORD timeout) {
    if (handle == worker && timeout == INFINITE && failJoin && std::strcmp(waitFault, "wait") == 0) {
        failJoin = false;
        ++injectedWaitFailures;
        ::SetLastError(ERROR_INVALID_HANDLE);
        note("injected-native-wait-failure", WAIT_FAILED);
        return WAIT_FAILED;
    }
    const DWORD result = ::WaitForSingleObject(handle, timeout);
    if (handle == worker && result == WAIT_OBJECT_0) { ++observedJoins; lastJoin = result; note("native-thread-signaled", result); }
    return result;
}
BOOL WINAPI sourceExitCode(HANDLE handle, LPDWORD exitCode) {
    ++exitQueries;
    if (handle == worker && failJoin && std::strcmp(waitFault, "exit-code") == 0) {
        failJoin = false;
        ++injectedExitFailures;
        ::SetLastError(ERROR_INVALID_HANDLE);
        note("injected-exit-code-failure");
        return FALSE;
    }
    const BOOL result = ::GetExitCodeThread(handle, exitCode);
    if (result && handle == worker) { lastExitCode = *exitCode; note("native-exit-code", *exitCode); }
    return result;
}
BOOL WINAPI sourceClose(HANDLE handle) {
    const BOOL result = ::CloseHandle(handle);
    if (result && handle == worker) ++workerClosed;
    if (result && handle == wake) ++eventClosed;
    note(result ? "native-close-success" : "native-close-failed", reinterpret_cast<std::uintptr_t>(handle));
    return result;
}
BOOL WINAPI sourceTlsSet(DWORD key, LPVOID value) {
    ++tlsAttempts;
    if (failTls.exchange(false)) { ++tlsFailures; ::SetLastError(ERROR_NOT_ENOUGH_MEMORY); note("tls-set-failed"); return FALSE; }
    const BOOL result = ::TlsSetValue(key, value);
    note(result ? "tls-set-success" : "tls-set-native-failure", reinterpret_cast<std::uintptr_t>(value));
    return result;
}
void WINAPI sourceInitSection(LPCRITICAL_SECTION section) { ::InitializeCriticalSection(section); ++sectionsInitialized; }
BOOL WINAPI sourceInitSectionSpin(LPCRITICAL_SECTION section, DWORD spin) {
    const BOOL result = ::InitializeCriticalSectionAndSpinCount(section, spin);
    if (result) ++sectionsInitialized;
    return result;
}
void WINAPI sourceDeleteSection(LPCRITICAL_SECTION section) { ::DeleteCriticalSection(section); ++sectionsDeleted; }
void WINAPI sourceEnterSection(LPCRITICAL_SECTION section) {
    const bool registry = platform && section == platform->m_lock;
    if (registry && closingRegistry) ::SetEvent(contenderAttempted);
    ::EnterCriticalSection(section);
    if (registry && closingRegistry) {
        ++closeInspections;
        inspectedCount = platform->m_threadNum;
        inspectedReady = platform->m_threads.size() == 1
            && platform->m_threads[0]->threadId() == workerNativeId ? 1 : 0;
        note("close-registry-acquired", static_cast<unsigned>(platform->m_threadNum));
    }
}
void WINAPI sourceDebugBreak() {
    ++debugBreaks;
    note("source-debug-break", platform ? static_cast<unsigned>(platform->m_threadNum) : 0);
}
HWND WINAPI sourceCreateWindow(DWORD exStyle, LPCWSTR className, LPCWSTR title, DWORD style,
    int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID argument) {
    const HWND result = ::CreateWindowExW(exStyle, className, title, style, x, y, width, height, parent, menu, instance, argument);
    nativeWindow = result;
    if (result) ++windowAcquired;
    note("hidden-window-return", reinterpret_cast<std::uintptr_t>(result));
    return result;
}
UINT_PTR WINAPI sourceSetTimer(HWND window, UINT_PTR id, UINT interval, TIMERPROC callback) {
    const UINT_PTR result = ::SetTimer(window, id, interval, callback);
    if (result) ++timerAcquired;
    return result;
}
BOOL WINAPI sourceKillTimer(HWND window, UINT_PTR id) { const BOOL result = ::KillTimer(window, id); if (result) ++timerKilled; return result; }
BOOL WINAPI sourceDestroyWindow(HWND window) { const BOOL result = ::DestroyWindow(window); if (result) ++windowDestroyed; return result; }
BOOL WINAPI sourceUnregisterClass(LPCWSTR name, HINSTANCE instance) { const BOOL result = ::UnregisterClassW(name, instance); if (result) ++classUnregistered; return result; }
}

void* operator new(std::size_t size) {
    if (fixture::failNextAllocation) { fixture::failNextAllocation = false; ++fixture::allocationFailures; fixture::note("stl-bad-alloc"); throw std::bad_alloc(); }
    void* result = std::malloc(size ? size : 1);
    if (!result) throw std::bad_alloc();
    return result;
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept {
    if (pointer && pointer == fixture::schedulerStorage.load()) ++fixture::schedulerFrees;
    if (pointer && pointer == fixture::workerStorage.load()) ++fixture::workerFrees;
    std::free(pointer);
}
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { ::operator delete(pointer); }

// Macros redirect only Win32 acquisition/observation seams in the unmodified
// actual source bytes. Harness controls and observations call native APIs directly.
#undef CreateEvent
#define CreateEvent fixture::sourceCreateEvent
#define CreateEventW fixture::sourceCreateEvent
#define _beginthreadex fixture::sourceBeginThread
#define WaitForSingleObject fixture::sourceWait
#define GetExitCodeThread fixture::sourceExitCode
#define CloseHandle fixture::sourceClose
#define TlsSetValue fixture::sourceTlsSet
#define InitializeCriticalSection fixture::sourceInitSection
#define InitializeCriticalSectionAndSpinCount fixture::sourceInitSectionSpin
#define DeleteCriticalSection fixture::sourceDeleteSection
#define EnterCriticalSection fixture::sourceEnterSection
#define DebugBreak fixture::sourceDebugBreak
#define CreateWindowExW fixture::sourceCreateWindow
#define SetTimer fixture::sourceSetTimer
#define KillTimer fixture::sourceKillTimer
#define DestroyWindow fixture::sourceDestroyWindow
#define UnregisterClassW fixture::sourceUnregisterClass
#include "content/web_impl_win/WebThreadImpl.cpp"
#include "content/web_impl_win/WebTimerBase.cpp"
namespace content {
#include "fragments/registry_lock.inc"
#include "fragments/tls.inc"
#include "fragments/registry_and_tls_methods.inc"
#include "fragments/scheduler_ctor_dtor.inc"
int CheckReEnter::s_kEnterCount = 0;
#include "fragments/reentry_static_methods.inc"
}
#undef CreateEvent
#undef CreateEventW
#undef _beginthreadex
#undef WaitForSingleObject
#undef GetExitCodeThread
#undef CloseHandle
#undef TlsSetValue
#undef InitializeCriticalSection
#undef InitializeCriticalSectionAndSpinCount
#undef DeleteCriticalSection
#undef EnterCriticalSection
#undef DebugBreak
#undef CreateWindowExW
#undef SetTimer
#undef KillTimer
#undef DestroyWindow
#undef UnregisterClassW

blink::Platform* blink::Platform::current() { return fixture::platform; }

namespace fixture {
struct TaskReceipt {
    std::atomic<int> run{0}, disposed{0};
    std::atomic<DWORD> tid{0};
    std::atomic<bool> tlsMatches{false};
    std::atomic<double> executedAt{0};
    HANDLE ran = nullptr;
};
class KnownTask final : public blink::WebThread::Task {
public:
    KnownTask(TaskReceipt& receipt, content::WebThreadImpl* owner) : receipt_(receipt), owner_(owner) { }
    ~KnownTask() override { ++receipt_.disposed; }
    void run() override {
        receipt_.executedAt = WTF::monotonicallyIncreasingTime();
        ++receipt_.run;
        receipt_.tid = ::GetCurrentThreadId();
        receipt_.tlsMatches = platform->currentThread() == owner_;
        ::SetEvent(receipt_.ran);
    }
private:
    TaskReceipt& receipt_;
    content::WebThreadImpl* owner_;
};
class SelfDeleteTask final : public blink::WebThread::Task {
public:
    explicit SelfDeleteTask(content::WebThreadImpl* worker) : worker_(worker) { }
    void run() override { note("self-delete-entered"); delete worker_; note("self-delete-returned"); }
private:
    content::WebThreadImpl* worker_;
};
HANDLE makeGate() {
    HANDLE result = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!result) ::ExitProcess(94);
    return result;
}
bool waitGate(HANDLE gate) { return ::WaitForSingleObject(gate, boundMs) == WAIT_OBJECT_0; }
unsigned __stdcall registryContender(void*) {
    closingRegistry = true;
    platform->closeThread();
    closingRegistry = false;
    ::SetEvent(contenderReturned);
    return 0;
}
bool contenderCompletes() {
    unsigned id = 0;
    HANDLE thread = reinterpret_cast<HANDLE>(::_beginthreadex(nullptr, 0, registryContender, nullptr, 0, &id));
    if (!thread) return false;
    const bool result = waitGate(contenderReturned) && ::WaitForSingleObject(thread, boundMs) == WAIT_OBJECT_0;
    DWORD exitCode = STILL_ACTIVE;
    const bool clean = result && ::GetExitCodeThread(thread, &exitCode) && exitCode == 0;
    ::CloseHandle(thread);
    return clean;
}
unsigned __stdcall createDriver(void*) {
    committed = static_cast<content::WebThreadImpl*>(platform->createThread("FixtureWorker"));
    constructorReturned = true;
    ::SetEvent(constructorDone);
    return 0;
}
unsigned __stdcall shutdownDriver(void* argument) { static_cast<content::WebThreadImpl*>(argument)->shutdown(); return 0; }
void summary(bool passed, const char* reason, bool expectedFailFast = false) {
    std::printf("{\"schema\":\"worker-lifetime-source-run/1\",\"variant\":\"%s\",\"case\":\"%s\",\"submode\":\"%s\",\"pid\":%lu,\"contractPassed\":%s,\"expectedFailFast\":%s,\"reason\":\"%s\",\"registryCount\":%d,\"registrySize\":%zu,\"eventAttempts\":%d,\"eventAcquired\":%d,\"eventClosed\":%d,\"launchAttempts\":%d,\"launched\":%d,\"workerClosed\":%d,\"workerFrees\":%d,\"schedulerAllocations\":%d,\"schedulerFrees\":%d,\"allocationFailures\":%d,\"sectionsInitialized\":%d,\"sectionsDeleted\":%d,\"tlsFailures\":%d,\"observedJoins\":%d,\"exitQueries\":%d,\"lastJoin\":%lu,\"lastExitCode\":%lu,\"entryReturnCode\":%u,\"workerTid\":%lu,\"waitFailureInjected\":%d,\"exitFailureInjected\":%d,\"closeInspections\":%d,\"inspectedCount\":%d,\"inspectedReady\":%d,\"debugBreaks\":%d,\"windowAcquired\":%d,\"windowDestroyed\":%d,\"timerAcquired\":%d,\"timerKilled\":%d,\"classUnregistered\":%d,\"qualificationPassed\":false,\"constructorOomClosed\":false}\n",
        "after", caseId, waitFault, ::GetCurrentProcessId(), passed ? "true" : "false", expectedFailFast ? "true" : "false", reason,
        platform->m_threadNum, platform->m_threads.size(), eventAttempts.load(), eventAcquired.load(), eventClosed.load(), launchAttempts.load(), launched.load(),
        workerClosed.load(), workerFrees.load(), schedulerAllocations.load(), schedulerFrees.load(), allocationFailures.load(), sectionsInitialized.load(), sectionsDeleted.load(),
        tlsFailures.load(), observedJoins.load(), exitQueries.load(), lastJoin.load(), lastExitCode.load(), entryReturnCode.load(), workerNativeId.load(),
        injectedWaitFailures.load(), injectedExitFailures.load(), closeInspections.load(), inspectedCount.load(), inspectedReady.load(), debugBreaks.load(),
        windowAcquired.load(), windowDestroyed.load(), timerAcquired.load(), timerKilled.load(), classUnregistered.load());
    std::fflush(stdout);
}
[[noreturn]] void terminated() {
    const bool expectedCase = isCase("native-wait-fails") || isCase("worker-self-delete");
    const bool seamObserved = isCase("worker-self-delete") ? ::GetCurrentThreadId() == workerNativeId
        : injectedWaitFailures + injectedExitFailures == 1;
    const bool retained = platform && platform->m_threadNum == 1 && platform->m_threads.size() == 1
        && workerFrees == 0 && schedulerFrees == 0 && workerClosed == 0 && eventClosed == 0;
    const bool valid = expectedCase && seamObserved && retained;
    summary(valid, "actual-source-terminate-before-release", valid);
    ::ExitProcess(valid ? 86 : 87);
}
struct Context {
    CRITICAL_SECTION registry{};
    content::BlinkPlatformImpl actual;
    Context() {
        engineThreadId = ::GetCurrentThreadId();
        ::InitializeCriticalSection(&registry);
        actual.m_lock = &registry;
        actual.m_mainThreadId = engineThreadId;
        platform = &actual;
        content::sCurrentThreadTlsKey = ::TlsAlloc();
        if (content::sCurrentThreadTlsKey == TLS_OUT_OF_INDEXES) ::ExitProcess(95);
        startupEntered = makeGate(); startupRelease = makeGate(); entryReturned = makeGate(); nativeReturnRelease = makeGate();
        contenderAttempted = makeGate(); contenderReturned = makeGate(); constructorDone = makeGate();
        std::set_terminate(terminated);
    }
    ~Context() {
        // Never erase a registry or reset a counter to manufacture drain.
        if (actual.m_threadNum || !actual.m_threads.empty()) return;
        ::TlsFree(content::sCurrentThreadTlsKey);
        for (HANDLE gate : {startupEntered, startupRelease, entryReturned, nativeReturnRelease, contenderAttempted, contenderReturned, constructorDone}) ::CloseHandle(gate);
        ::DeleteCriticalSection(&registry);
    }
};
bool cleanWorkerReceipt(int count = 1) {
    return platform->m_threadNum == 0 && platform->m_threads.empty()
        && eventAcquired == count && eventClosed == count && launched == count && workerClosed == count
        && workerFrees == count && schedulerAllocations == count && schedulerFrees == count
        && sectionsInitialized == 2 * count && sectionsDeleted == 2 * count
        && observedJoins >= count && exitQueries >= count && lastExitCode == 0;
}
bool startupFailureCase() {
    if (isCase("scheduler-new-bad-alloc")) failScheduler = true;
    if (isCase("event-null")) failEvent = true;
    if (isCase("native-launch-zero")) failLaunch = true;
    if (isCase("startup-tls-failure")) failTls = true;
    // Actual first registry reserve is the next global allocation.
    if (isCase("registry-reserve-bad-alloc")) failNextAllocation = true;
    bool threw = false;
    content::WebThreadImpl* escaped = nullptr;
    try { escaped = static_cast<content::WebThreadImpl*>(platform->createThread("FixtureWorker")); }
    catch (const std::bad_alloc&) { threw = true; }
    catch (const std::runtime_error&) { threw = true; }
    failNextAllocation = false;
    const int escapedCount = platform->m_threadNum;
    note("factory-threw", threw ? 1 : 0);
    if (escaped) { note("unexpected-ready-worker-returned"); delete escaped; }
    const bool lockReleased = contenderCompletes();
    bool expectedResources = false;
    if (isCase("scheduler-new-bad-alloc"))
        expectedResources = allocationFailures == 1 && eventAttempts == 0 && launchAttempts == 0 && schedulerAllocations == 0;
    if (isCase("registry-reserve-bad-alloc"))
        expectedResources = allocationFailures == 1 && eventAttempts == 0 && launchAttempts == 0 && schedulerAllocations == 0;
    if (isCase("event-null"))
        expectedResources = eventAttempts == 1 && eventAcquired == 0 && launchAttempts == 0 && schedulerAllocations == 1
            && schedulerFrees == 1 && sectionsInitialized == 2 && sectionsDeleted == 2;
    if (isCase("native-launch-zero"))
        expectedResources = eventAcquired == 1 && eventClosed == 1 && launchAttempts == 1 && launched == 0
            && schedulerFrees == 1 && sectionsInitialized == 2 && sectionsDeleted == 2;
    if (isCase("startup-tls-failure"))
        expectedResources = tlsFailures == 1 && entryReturnCode == 1 && lastJoin == WAIT_OBJECT_0 && lastExitCode == 1
            && eventAcquired == 1 && eventClosed == 1 && launched == 1 && workerClosed == 1 && schedulerFrees == 1 && sectionsDeleted == 2;
    return threw && escapedCount == 0 && platform->m_threadNum == 0 && platform->m_threads.empty() && lockReleased && expectedResources;
}
bool normalCase() {
    auto* thread = static_cast<content::WebThreadImpl*>(platform->createThread("FixtureWorker"));
    constructorReturned = true;
    const bool registered = platform->m_threadNum == 1 && platform->m_threads.size() == 1 && platform->m_threads[0] == thread;
    TaskReceipt receipt; receipt.ran = makeGate();
    thread->postTask(FROM_HERE, new KnownTask(receipt, thread));
    const bool ran = waitGate(receipt.ran);
    thread->shutdown();
    const bool closed = thread->threadClosed();
    TaskReceipt rejected; rejected.ran = makeGate();
    thread->postTask(FROM_HERE, new KnownTask(rejected, thread));
    const bool disposedWithoutRun = rejected.run == 0 && rejected.disposed == 1
        && ::WaitForSingleObject(rejected.ran, 0) == WAIT_TIMEOUT;
    delete thread;
    ::CloseHandle(receipt.ran);
    ::CloseHandle(rejected.ran);
    std::printf("{\"record\":\"task-receipt\",\"outcome\":\"run-then-dispose\",\"run\":%d,\"disposed\":%d,\"taskTid\":%lu,\"tlsMatches\":%s}\n",
        receipt.run.load(), receipt.disposed.load(), receipt.tid.load(), receipt.tlsMatches ? "true" : "false");
    std::printf("{\"record\":\"task-receipt\",\"outcome\":\"cancel-before-run\",\"run\":%d,\"disposed\":%d}\n",
        rejected.run.load(), rejected.disposed.load());
    return registered && ran && closed && disposedWithoutRun && receipt.run == 1 && receipt.disposed == 1
        && receipt.tlsMatches && receipt.tid == workerNativeId && cleanWorkerReceipt();
}
bool visibilityCase() {
    constexpr int iterations = 32;
    bool valid = true;
    for (int i = 0; i != iterations; ++i) {
        auto* thread = static_cast<content::WebThreadImpl*>(platform->createThread("FixtureWorker"));
        valid = valid && thread->threadId() == workerNativeId && thread->threadId() != engineThreadId
            && platform->m_threadNum == 1 && platform->m_threads[0] == thread;
        TaskReceipt receipt; receipt.ran = makeGate();
        thread->postTask(FROM_HERE, new KnownTask(receipt, thread));
        valid = waitGate(receipt.ran) && valid;
        delete thread;
        valid = valid && receipt.run == 1 && receipt.disposed == 1 && receipt.tlsMatches && receipt.tid == workerNativeId;
        ::CloseHandle(receipt.ran);
        ::ResetEvent(entryReturned);
    }
    // Dynamic handoffs alone do not prove data-race freedom for every schedule.
    std::printf("{\"record\":\"ready-handoff\",\"iterations\":%d,\"dynamicVisibility\":%s,\"dataRaceFreedomQualified\":false}\n", iterations, valid ? "true" : "false");
    return valid && cleanWorkerReceipt(iterations);
}
bool delayedExitCase() {
    holdNativeReturn = true;
    auto* thread = static_cast<content::WebThreadImpl*>(platform->createThread("FixtureWorker"));
    unsigned id = 0;
    HANDLE shutdownCaller = reinterpret_cast<HANDLE>(::_beginthreadex(nullptr, 0, shutdownDriver, thread, 0, &id));
    if (!shutdownCaller || !waitGate(entryReturned)) return false;
    const bool nativeUnsignaled = ::WaitForSingleObject(worker, 0) == WAIT_TIMEOUT;
    // shutdown() is executing on another thread; no destructor has begun. The
    // actual object remains alive throughout this existing-public-method read.
    const bool closurePremature = thread->threadClosed();
    const bool storageRetained = workerFrees == 0 && schedulerFrees == 0 && workerClosed == 0 && eventClosed == 0;
    const bool registrationRetained = platform->m_threadNum == 1 && platform->m_threads.size() == 1 && platform->m_threads[0] == thread;
    std::printf("{\"record\":\"delayed-native-exit\",\"nativeUnsignaled\":%s,\"threadClosed\":%s,\"storageRetained\":%s,\"registrationRetained\":%s}\n",
        nativeUnsignaled ? "true" : "false", closurePremature ? "true" : "false", storageRetained ? "true" : "false", registrationRetained ? "true" : "false");
    ::SetEvent(nativeReturnRelease);
    const bool joined = ::WaitForSingleObject(shutdownCaller, boundMs) == WAIT_OBJECT_0;
    DWORD exitCode = STILL_ACTIVE;
    const bool clean = joined && ::GetExitCodeThread(shutdownCaller, &exitCode) && exitCode == 0;
    ::CloseHandle(shutdownCaller);
    if (!clean) return false; // Leave the actual live owner intact on a failed driver join.
    const bool closedAfterJoin = thread->threadClosed();
    delete thread; // Destruction and actual resource/registry release start only here.
    return nativeUnsignaled && !closurePremature && storageRetained && registrationRetained
        && clean && closedAfterJoin && cleanWorkerReceipt();
}
bool concurrentRegistryCase() {
    holdStartup = true;
    unsigned id = 0;
    HANDLE creator = reinterpret_cast<HANDLE>(::_beginthreadex(nullptr, 0, createDriver, nullptr, 0, &id));
    if (!creator || !waitGate(startupEntered)) return false;
    HANDLE contender = reinterpret_cast<HANDLE>(::_beginthreadex(nullptr, 0, registryContender, nullptr, 0, &id));
    if (!contender || !waitGate(contenderAttempted)) return false;
    const BOOL registryAvailable = ::TryEnterCriticalSection(platform->m_lock);
    if (registryAvailable) ::LeaveCriticalSection(platform->m_lock);
    const bool earlyInspection = registryAvailable
        || ::WaitForSingleObject(contenderReturned, 0) == WAIT_OBJECT_0 || closeInspections != 0;
    // Under actual startup exclusion, creator owns the registry lock here.
    const bool emptyBeforeReady = platform->m_threadNum == 0 && platform->m_threads.empty();
    note("startup-close-attempted", earlyInspection ? 1 : 0);
    ::SetEvent(startupRelease);
    const bool created = waitGate(constructorDone) && ::WaitForSingleObject(creator, boundMs) == WAIT_OBJECT_0;
    const bool inspected = waitGate(contenderReturned) && ::WaitForSingleObject(contender, boundMs) == WAIT_OBJECT_0;
    if (!created || !inspected) return false; // Do not read/delete an owner still used by a driver.
    const bool readyCommit = committed && committed->threadId() == workerNativeId && platform->m_threadNum == 1
        && platform->m_threads.size() == 1 && platform->m_threads[0] == committed;
    const bool realInspection = closeInspections == 1 && inspectedCount == 1 && inspectedReady == 1;
    // DebugBreak is observed, not suppressed as a successful empty-registry close.
    const bool liveDiagnostic = debugBreaks >= 1;
    if (committed) delete committed;
    ::CloseHandle(creator); ::CloseHandle(contender);
    return !earlyInspection && emptyBeforeReady && created && inspected && readyCommit && realInspection && liveDiagnostic && cleanWorkerReceipt();
}
bool mainThreadCase() {
    // Input setup selects the actual lazy MainThread path; no pre-existing main worker.
    ::TlsFree(content::sCurrentThreadTlsKey);
    content::sCurrentThreadTlsKey = static_cast<DWORD>(-1);
    platform->m_mainThreadId = static_cast<ThreadIdentifier>(-1);
    auto* thread = static_cast<content::WebThreadImpl*>(platform->currentThread());
    const bool mainIdentity = thread && thread->isCurrentThread() && thread->threadId() == engineThreadId
        && platform->currentThread() == thread && platform->m_threadNum == 1;
    // Idle owns a wake window but no periodic native timer.
    const bool idleWithoutTick = timerAcquired == 0;
    TaskReceipt receipt; receipt.ran = makeGate();
    thread->postTask(FROM_HERE, new KnownTask(receipt, thread));
    const ULONGLONG deadline = ::GetTickCount64() + boundMs;
    while (receipt.run == 0 && ::GetTickCount64() < deadline) {
        ::MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { ::TranslateMessage(&message); ::DispatchMessageW(&message); }
    }
    const bool taskRan = receipt.run == 1 && receipt.tid == engineThreadId && receipt.tlsMatches;
    TaskReceipt delayed; delayed.ran = makeGate();
    const double admittedAt = WTF::monotonicallyIncreasingTime();
    thread->postDelayedTask(FROM_HERE, new KnownTask(delayed, thread), 25);
    const ULONGLONG delayedLimit = ::GetTickCount64() + boundMs;
    while (!delayed.run && ::GetTickCount64() < delayedLimit) {
        ::MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { ::TranslateMessage(&message); ::DispatchMessageW(&message); }
    }
    const bool futureRan = delayed.run == 1 && delayed.tid == engineThreadId && delayed.tlsMatches
        && delayed.executedAt.load() - admittedAt >= .025;
    bool deferredRecovery = true;
    for (bool rearmFuture : {false, true}) {
        TaskReceipt deferred, resumed;
        deferred.ran = makeGate(); resumed.ran = makeGate();
        content::CheckReEnter::incrementEnterCount();
        thread->postTask(FROM_HERE, new KnownTask(deferred, thread));
        MSG message{};
        while (::PeekMessageW(&message, nativeWindow, content::timerFiredMessage,
            content::timerFiredMessage, PM_REMOVE))
            ::DispatchMessageW(&message); // Real guard must defer the accepted task.
        const UINT_PTR retiredTicket = content::fallbackTimerID;
        content::CheckReEnter::decrementEnterCount();
        content::setSharedTimerDeadline(rearmFuture ? WTF::monotonicallyIncreasingTime() + .05 : 0);
        // KillTimer leaves already posted WM_TIMER messages behind. A retired
        // fallback ticket must not consume a new deadline or run deferred work.
        ::PostMessageW(nativeWindow, WM_TIMER, retiredTicket, 0);
        while (::PeekMessageW(&message, nativeWindow, WM_TIMER, WM_TIMER, PM_REMOVE))
            ::DispatchMessageW(&message);
        deferredRecovery = deferredRecovery && retiredTicket != 0 && deferred.run == 0;
        thread->postTask(FROM_HERE, new KnownTask(resumed, thread));
        const ULONGLONG recoveryLimit = ::GetTickCount64() + boundMs;
        while ((!deferred.run || !resumed.run) && ::GetTickCount64() < recoveryLimit) {
            ::MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { ::TranslateMessage(&message); ::DispatchMessageW(&message); }
        }
        thread->fire(); // Retire actual self-deleting timers before local receipts die.
        deferredRecovery = deferredRecovery && deferred.run == 1 && resumed.run == 1
            && deferred.disposed == 1 && resumed.disposed == 1;
        ::CloseHandle(deferred.ran); ::CloseHandle(resumed.ran);
    }
    const HWND window = nativeWindow;
    const bool nativeAlive = window && ::IsWindow(window) && windowAcquired == 1;
    TaskReceipt quitPending; quitPending.ran = makeGate();
    thread->postTask(FROM_HERE, new KnownTask(quitPending, thread));
    ::PostQuitMessage(0);
    MSG quitMessage{};
    while (::PeekMessageW(&quitMessage, nativeWindow, content::timerFiredMessage,
        content::timerFiredMessage, PM_REMOVE))
        ::DispatchMessageW(&quitMessage);
    const bool quitUnstarved = quitPending.run == 0
        && ::PeekMessageW(&quitMessage, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
    delete thread;
    const bool nativeReleased = windowDestroyed == 1 && !::IsWindow(window) && classUnregistered == 1;
    ::CloseHandle(receipt.ran);
    ::CloseHandle(delayed.ran);
    ::CloseHandle(quitPending.ran);
    return mainIdentity && idleWithoutTick && taskRan && futureRan && deferredRecovery && quitUnstarved
        && quitPending.run == 1 && quitPending.disposed == 1 && delayed.disposed == 1 && receipt.disposed == 1 && nativeAlive && nativeReleased
        && launchAttempts == 0 && eventAttempts == 0 && platform->m_threadNum == 0 && platform->m_threads.empty()
        && schedulerFrees == 1 && sectionsInitialized == 2 && sectionsDeleted == 2;
}
bool dispatch() {
    if (isCase("scheduler-new-bad-alloc") || isCase("event-null") || isCase("native-launch-zero")
        || isCase("registry-reserve-bad-alloc") || isCase("startup-tls-failure")) return startupFailureCase();
    if (isCase("normal-worker-close")) return normalCase();
    if (isCase("ready-acquire-visibility")) return visibilityCase();
    if (isCase("native-exit-delayed")) return delayedExitCase();
    if (isCase("concurrent-registry-close")) return concurrentRegistryCase();
    if (isCase("main-thread-compatibility")) return mainThreadCase();
    if (isCase("native-wait-fails")) {
        auto* thread = static_cast<content::WebThreadImpl*>(platform->createThread("FixtureWorker"));
        failJoin = true;
        delete thread;
        return false; // Actual source must terminate before closure/release on failure.
    }
    if (isCase("worker-self-delete")) {
        auto* thread = static_cast<content::WebThreadImpl*>(platform->createThread("FixtureWorker"));
        thread->postTask(FROM_HERE, new SelfDeleteTask(thread));
        waitGate(entryReturned);
        return false; // The supervisor owns the enclosing bound if the worker never exits.
    }
    return false;
}
}
int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) return 96;
    fixture::caseId = argv[1];
    if (argc == 3) {
        if (std::strcmp(argv[2], "wait") && std::strcmp(argv[2], "exit-code")) return 97;
        fixture::waitFault = argv[2];
    }
    fixture::Context context;
    const bool passed = fixture::dispatch();
    fixture::summary(passed, passed ? "actual-source-contract-satisfied" : "actual-source-contract-failed");
    // Failed contracts can retain a blocked contender, even with an empty registry.
    // Never destroy its critical section/platform storage or certify cleanup.
    if (!passed) ::ExitProcess(3);
    return passed ? 0 : 3;
}
