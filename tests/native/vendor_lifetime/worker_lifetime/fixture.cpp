#include "fixture_support.h"
#include "content/web_impl_win/WebThreadImpl.h"
#include "content/web_impl_win/WebTimerBase.h"
#include "fragments/shutdown_fire_entrypoint.inc"
#include <array>

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
constexpr UINT schedulerProbeMessage = WM_APP + 0x31;
std::atomic<bool> controlledSchedulerClock{false}, workerSchedulerProbe{false};
std::atomic<double> schedulerSeconds{100.0};
std::atomic<unsigned> schedulerTaskRuns{0}, nativeProbeRuns{0}, nativeProbeSeen{0};
std::atomic<unsigned> workerFirstYieldRuns{0}, workerMidChainWaits{0};

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
    if (workerSchedulerProbe && handle == wake && ::GetCurrentThreadId() == workerNativeId
        && timeout != INFINITE && schedulerTaskRuns > 0 && schedulerTaskRuns < 96) {
        unsigned expected = 0;
        workerFirstYieldRuns.compare_exchange_strong(expected, schedulerTaskRuns.load());
        ++workerMidChainWaits;
    }
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
LRESULT WINAPI sourceDefWindowProc(HWND window, UINT message, WPARAM first, LPARAM second) {
    if (message == schedulerProbeMessage) {
        nativeProbeRuns = schedulerTaskRuns.load();
        ++nativeProbeSeen;
        return 0;
    }
    return ::DefWindowProcW(window, message, first, second);
}
}

namespace WTF {
// Only the dependency clock is controlled. The real scheduler loop, heap,
// task ownership, native timer window and message dispatch remain production code.
double fixtureSchedulerTime() {
    return fixture::controlledSchedulerClock ? fixture::schedulerSeconds.load() : currentTime();
}
}
using WTF::fixtureSchedulerTime;

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
#undef DefWindowProc
#define DefWindowProc fixture::sourceDefWindowProc
#define currentTime fixtureSchedulerTime
#define monotonicallyIncreasingTime fixtureSchedulerTime
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
#undef DefWindowProc
#undef currentTime
#undef monotonicallyIncreasingTime

blink::Platform* blink::Platform::current() { return fixture::platform; }

namespace fixture {
struct TaskReceipt {
    std::atomic<int> run{0}, disposed{0};
    std::atomic<DWORD> tid{0};
    std::atomic<bool> tlsMatches{false};
    HANDLE ran = nullptr;
};
class KnownTask final : public blink::WebThread::Task {
public:
    KnownTask(TaskReceipt& receipt, content::WebThreadImpl* owner) : receipt_(receipt), owner_(owner) { }
    ~KnownTask() override { ++receipt_.disposed; }
    void run() override {
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
struct SchedulerReceipt {
    static constexpr unsigned taskCount = 96;
    std::array<std::atomic<unsigned>, taskCount> runs{};
    std::atomic<unsigned> disposed{0};
    double stepSeconds = 0;
    HANDLE completed = nullptr;
    HANDLE retired = nullptr;
};
class SchedulerTask final : public blink::WebThread::Task {
public:
    SchedulerTask(SchedulerReceipt& receipt, content::WebThreadImpl* owner, unsigned index, bool repost)
        : receipt_(receipt), owner_(owner), index_(index), repost_(repost) { }
    ~SchedulerTask() override {
        if (++receipt_.disposed == SchedulerReceipt::taskCount && receipt_.retired) ::SetEvent(receipt_.retired);
    }
    void run() override {
        ++receipt_.runs[index_];
        ++schedulerTaskRuns;
        if (receipt_.stepSeconds)
            schedulerSeconds = schedulerSeconds.load() + receipt_.stepSeconds;
        if (repost_ && index_ == 0 && ::GetCurrentThreadId() == engineThreadId)
            ::PostMessageW(nativeWindow, schedulerProbeMessage, 0, 0);
        if (repost_ && index_ + 1 < SchedulerReceipt::taskCount)
            owner_->postTask(FROM_HERE, new SchedulerTask(receipt_, owner_, index_ + 1, true));
        if (receipt_.completed && schedulerTaskRuns == SchedulerReceipt::taskCount)
            ::SetEvent(receipt_.completed);
    }
private:
    SchedulerReceipt& receipt_;
    content::WebThreadImpl* owner_;
    unsigned index_;
    bool repost_;
};
bool schedulerTasksExactlyOnce(const SchedulerReceipt& receipt) {
    for (const auto& count : receipt.runs)
        if (count != 1) return false;
    return receipt.disposed == SchedulerReceipt::taskCount;
}
HANDLE makeGate() {
    HANDLE result = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!result) ::ExitProcess(94);
    return result;
}
bool waitGate(HANDLE gate) { return ::WaitForSingleObject(gate, boundMs) == WAIT_OBJECT_0; }
unsigned __stdcall registryContender(void*) {
    std::set_terminate(terminated);
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
    const bool registryFence = isCase("registry-live-fence") || isCase("concurrent-registry-close");
    const bool expectedCase = isCase("native-wait-fails") || isCase("worker-self-delete") || registryFence;
    const bool seamObserved = registryFence ? debugBreaks >= 1 && (isCase("registry-live-fence")
        || (closeInspections == 1 && inspectedCount == 1 && inspectedReady == 1))
        : isCase("worker-self-delete") ? ::GetCurrentThreadId() == workerNativeId
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
    // Deadline scheduling leaves an idle MainThread without a periodic tick.
    const bool firstNativeTick = content::armedDeadline == 0;
    TaskReceipt receipt; receipt.ran = makeGate();
    thread->postTask(FROM_HERE, new KnownTask(receipt, thread));
    const ULONGLONG deadline = ::GetTickCount64() + boundMs;
    while (receipt.run == 0 && ::GetTickCount64() < deadline) {
        ::MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { ::TranslateMessage(&message); ::DispatchMessageW(&message); }
    }
    const bool taskRan = receipt.run == 1 && receipt.tid == engineThreadId && receipt.tlsMatches;
    const HWND window = nativeWindow;
    const bool nativeAlive = window && ::IsWindow(window) && windowAcquired == 1 && content::armedDeadline == 0;
    delete thread;
    const bool nativeReleased = windowDestroyed == 1 && !::IsWindow(window) && !content::deadlineWait && !content::deadlineTimer && classUnregistered == 1;
    ::CloseHandle(receipt.ran);
    return mainIdentity && firstNativeTick && taskRan && receipt.disposed == 1 && nativeAlive && nativeReleased
        && launchAttempts == 0 && eventAttempts == 0 && platform->m_threadNum == 0 && platform->m_threads.empty()
        && schedulerFrees == 1 && sectionsInitialized == 2 && sectionsDeleted == 2;
}
content::WebThreadImpl* schedulerMainThread() {
    ::TlsFree(content::sCurrentThreadTlsKey);
    content::sCurrentThreadTlsKey = static_cast<DWORD>(-1);
    platform->m_mainThreadId = static_cast<ThreadIdentifier>(-1);
    controlledSchedulerClock = true;
    return static_cast<content::WebThreadImpl*>(platform->currentThread());
}
bool dispatchSchedulerTurn(const TaskReceipt* requiredReceipt = nullptr) {
    const ULONGLONG deadline = ::GetTickCount64() + boundMs;
    const unsigned runsBefore = schedulerTaskRuns.load();
    while (::GetTickCount64() < deadline) {
        ::MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            const bool schedulerTurn = message.hwnd == nativeWindow
                && ((message.message == WM_TIMER && message.wParam == content::timerID)
                    || message.message == content::continuationMessage || message.message == content::timerFiredMessage);
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
            // A continuation can deliberately yield to pending native work.
            // Observe actual task progress before treating it as a batch.
            if (schedulerTurn && (requiredReceipt ? requiredReceipt->run != 0
                                                   : schedulerTaskRuns.load() > runsBefore)) return true;
        }
    }
    return false;
}
bool mainSchedulerBudgetCase(bool clockBudget) {
    SchedulerReceipt receipt;
    receipt.stepSeconds = clockBudget ? 0.001 : 0;
    auto* thread = schedulerMainThread();
    if (!thread || !nativeWindow) return false;
    TaskReceipt future;
    future.ran = makeGate();
    thread->postDelayedTask(FROM_HERE, new KnownTask(future, thread), 500);
    thread->postTask(FROM_HERE, new SchedulerTask(receipt, thread, 0, true));
    if (!dispatchSchedulerTurn()) return false;
    const unsigned firstSlice = schedulerTaskRuns.load();
    note("first-timer-ready-tasks", firstSlice);
    const bool yielded = firstSlice > 0 && firstSlice < SchedulerReceipt::taskCount
        && (clockBudget ? firstSlice >= 2 && firstSlice <= 3 : firstSlice <= 32);
    const ULONGLONG deadline = ::GetTickCount64() + boundMs;
    while (schedulerTaskRuns < SchedulerReceipt::taskCount && ::GetTickCount64() < deadline)
        if (!dispatchSchedulerTurn()) break;
    const bool nativeTurn = nativeProbeSeen == 1 && nativeProbeRuns > 0
        && nativeProbeRuns < SchedulerReceipt::taskCount;
    note("native-probe-ready-tasks", nativeProbeRuns);
    const bool futureNotEarly = future.run == 0 && schedulerSeconds < 100.5;
    schedulerSeconds = 100.5;
    const bool futureCompleted = dispatchSchedulerTurn(&future) && future.run == 1 && future.tlsMatches;
    delete thread;
    ::CloseHandle(future.ran);
    return yielded && nativeTurn && futureNotEarly && futureCompleted && future.disposed == 1
        && schedulerTasksExactlyOnce(receipt) && windowDestroyed == 1 && timerKilled >= 1
        && platform->m_threadNum == 0 && platform->m_threads.empty();
}
bool mainSchedulerContinuationCase() {
    SchedulerReceipt receipt;
    auto* thread = schedulerMainThread();
    if (!thread || !nativeWindow) return false;
    // Isolate the posted continuation. With the native fallback disabled only
    // in this test, retained ready tasks must still finish through the real
    // window procedure. This is not a hardware-input fairness measurement.
    const bool fallbackStopped = content::armedDeadline == 0; // Idle has no fallback to disable.
    TaskReceipt future;
    future.ran = makeGate();
    thread->postDelayedTask(FROM_HERE, new KnownTask(future, thread), 500);
    thread->postTask(FROM_HERE, new SchedulerTask(receipt, thread, 0, true));
    thread->fire();
    const unsigned firstSlice = schedulerTaskRuns.load();
    unsigned messagesDispatched = 0;
    while (schedulerTaskRuns < SchedulerReceipt::taskCount && messagesDispatched < 128) {
        MSG message{};
        if (!::PeekMessageW(&message, nativeWindow, WM_APP, 0xffff, PM_REMOVE)) break;
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
        ++messagesDispatched;
    }
    const bool continuationCompleted = schedulerTaskRuns == SchedulerReceipt::taskCount;
    const bool futureNotEarly = future.run == 0 && schedulerSeconds == 100.0;
    note("continuation-messages-dispatched", messagesDispatched);
    note("continuation-ready-tasks", schedulerTaskRuns);
    delete thread;
    ::CloseHandle(future.ran);
    return fallbackStopped && firstSlice > 0 && firstSlice <= 32 && continuationCompleted
        && futureNotEarly && future.run == 1 && future.disposed == 1
        && schedulerTasksExactlyOnce(receipt) && windowDestroyed == 1
        && platform->m_threadNum == 0 && platform->m_threads.empty();
}
class DelayedPostAfterLongCallback : public blink::WebThread::Task {
public:
    DelayedPostAfterLongCallback(content::WebThreadImpl* thread, TaskReceipt& receipt)
        : thread_(thread), receipt_(receipt) {}
    void run() override {
        schedulerSeconds.store(schedulerSeconds.load() + 0.010);
        thread_->postDelayedTask(FROM_HERE, new KnownTask(receipt_, thread_), 1);
    }
private:
    content::WebThreadImpl* thread_;
    TaskReceipt& receipt_;
};
bool mainSchedulerFirstWakeCase() {
    auto* thread = schedulerMainThread();
    if (!thread || !nativeWindow) return false;
    const bool fallbackStopped = content::armedDeadline == 0; // Idle has no fallback to disable.
    TaskReceipt receipt;
    receipt.ran = makeGate();
    thread->postTask(FROM_HERE, new KnownTask(receipt, thread));
    MSG message{};
    const bool queued = ::PeekMessageW(&message, nativeWindow, WM_APP, 0xffff, PM_REMOVE) != FALSE;
    if (queued) ::DispatchMessageW(&message);
    const bool ranWithoutTick = receipt.run == 1 && receipt.tlsMatches;
    TaskReceipt delayed; delayed.ran = makeGate();
    thread->postTask(FROM_HERE, new DelayedPostAfterLongCallback(thread, delayed));
    thread->fire();
    thread->fire(); // No clock advance: the retry must still be in the future.
    const bool delayedFromPost = delayed.run == 0;
    schedulerSeconds.store(schedulerSeconds.load() + 0.002);
    thread->fire();
    const bool delayedCompleted = delayed.run == 1 && delayed.tlsMatches;
    delete thread;
    ::CloseHandle(receipt.ran); ::CloseHandle(delayed.ran);
    return fallbackStopped && queued && ranWithoutTick && receipt.disposed == 1
        && delayedFromPost && delayedCompleted && delayed.disposed == 1;
}
struct SchedulerAdmissionProducer {
    content::WebThreadImpl* thread;
    SchedulerReceipt* receipt;
};
unsigned __stdcall postFutureSchedulerBatch(void* value) {
    auto* producer = static_cast<SchedulerAdmissionProducer*>(value);
    for (unsigned i = 0; i < SchedulerReceipt::taskCount; ++i)
        producer->thread->postDelayedTask(FROM_HERE,
            new SchedulerTask(*producer->receipt, producer->thread, i, false), 1000);
    return 0;
}
bool mainSchedulerAdmissionCase(bool closeWithPendingAdmission) {
    SchedulerReceipt receipt;
    auto* thread = schedulerMainThread();
    if (!thread || !nativeWindow) return false;
    const bool fallbackStopped = content::armedDeadline == 0; // Idle has no fallback to disable.
    SchedulerAdmissionProducer producer{thread, &receipt};
    HANDLE native = reinterpret_cast<HANDLE>(::_beginthreadex(nullptr, 0,
        postFutureSchedulerBatch, &producer, 0, nullptr));
    if (!native || ::WaitForSingleObject(native, boundMs) != WAIT_OBJECT_0) ::ExitProcess(98);
    DWORD exitCode = STILL_ACTIVE;
    const bool producerReturned = ::GetExitCodeThread(native, &exitCode) && exitCode == 0;
    const bool producerClosed = ::CloseHandle(native) != FALSE;
    MSG message{};
    const bool queued = ::PeekMessageW(&message, nativeWindow, WM_APP, 0xffff, PM_REMOVE) != FALSE;
    if (queued) ::DispatchMessageW(&message);
    const size_t firstAdmitted = thread->timerHeap().size();
    const bool bounded = firstAdmitted > 0 && firstAdmitted <= 32;
    unsigned dispatched = 0;
    if (!closeWithPendingAdmission) {
        while (thread->timerHeap().size() < SchedulerReceipt::taskCount && dispatched++ < 128) {
            if (!::PeekMessageW(&message, nativeWindow, WM_APP, 0xffff, PM_REMOVE)) break;
            ::DispatchMessageW(&message);
        }
    }
    const bool admissionState = closeWithPendingAdmission
        ? thread->timerHeap().size() < SchedulerReceipt::taskCount
        : thread->timerHeap().size() == SchedulerReceipt::taskCount;
    const bool futureNotRun = schedulerTaskRuns == 0;
    note("first-batch-admitted", firstAdmitted);
    note("admitted-before-shutdown", thread->timerHeap().size());
    delete thread;
    return fallbackStopped && producerReturned && producerClosed && queued && bounded
        && admissionState && futureNotRun && schedulerTasksExactlyOnce(receipt)
        && windowDestroyed == 1 && platform->m_threadNum == 0 && platform->m_threads.empty();
}
bool workerSchedulerBudgetCase(bool clockBudget) {
    SchedulerReceipt receipt;
    receipt.stepSeconds = clockBudget ? 0.001 : 0;
    receipt.completed = makeGate();
    receipt.retired = makeGate();
    controlledSchedulerClock = true;
    workerSchedulerProbe = true;
    auto* thread = static_cast<content::WebThreadImpl*>(platform->createThread("FixtureWorker"));
    thread->postTask(FROM_HERE, new SchedulerTask(receipt, thread, 0, true));
    const bool completed = waitGate(receipt.completed);
    const bool retired_while_idle = completed && waitGate(receipt.retired);
    delete thread;
    ::CloseHandle(receipt.completed);
    ::CloseHandle(receipt.retired);
    note("worker-first-yield-ready-tasks", workerFirstYieldRuns);
    const bool originalBudget = clockBudget
        ? workerFirstYieldRuns > 32 && workerFirstYieldRuns < SchedulerReceipt::taskCount
        : workerMidChainWaits == 0;
    return completed && retired_while_idle && originalBudget && schedulerTasksExactlyOnce(receipt) && cleanWorkerReceipt();
}
bool mainSchedulerShutdownCase() {
    SchedulerReceipt receipt;
    auto* thread = schedulerMainThread();
    for (unsigned index = 0; index < SchedulerReceipt::taskCount; ++index)
        thread->postDelayedTask(FROM_HERE, new SchedulerTask(receipt, thread, index, false), 1000);
    const bool notRunEarly = schedulerTaskRuns == 0;
    delete thread;
    note("shutdown-drained-ready-tasks", schedulerTaskRuns);
    return notRunEarly && schedulerTaskRuns == SchedulerReceipt::taskCount
        && schedulerTasksExactlyOnce(receipt) && windowDestroyed == 1 && timerKilled >= 1
        && platform->m_threadNum == 0 && platform->m_threads.empty();
}
bool mainSchedulerPreShutdownCase() {
    SchedulerReceipt receipt;
    auto* thread = schedulerMainThread();
    for (unsigned index = 0; index < SchedulerReceipt::taskCount; ++index)
        thread->postTask(FROM_HERE, new SchedulerTask(receipt, thread, index, false));
    // The proof gate derives this token from all three actual shutdown-owner
    // callsites, while the complete owner function is copied as exact bytes.
    thread->FIXTURE_PRE_SHUTDOWN_FIRE_ENTRYPOINT();
    const unsigned beforeShutdown = schedulerTaskRuns.load();
    note("pre-shutdown-drained-ready-tasks", beforeShutdown);
    TaskReceipt acceptedAfterDrain;
    acceptedAfterDrain.ran = makeGate();
    thread->postTask(FROM_HERE, new KnownTask(acceptedAfterDrain, thread));
    thread->fire();
    const bool stillAccepting = acceptedAfterDrain.run == 1 && acceptedAfterDrain.tlsMatches;
    delete thread;
    ::CloseHandle(acceptedAfterDrain.ran);
    return beforeShutdown == SchedulerReceipt::taskCount && stillAccepting
        && acceptedAfterDrain.disposed == 1 && schedulerTasksExactlyOnce(receipt)
        && platform->m_threadNum == 0 && platform->m_threads.empty();
}
bool dispatch() {
    if (isCase("scheduler-new-bad-alloc") || isCase("event-null") || isCase("native-launch-zero")
        || isCase("registry-reserve-bad-alloc") || isCase("startup-tls-failure")) return startupFailureCase();
    if (isCase("normal-worker-close")) return normalCase();
    if (isCase("ready-acquire-visibility")) return visibilityCase();
    if (isCase("native-exit-delayed")) return delayedExitCase();
    if (isCase("concurrent-registry-close")) return concurrentRegistryCase();
    if (isCase("registry-live-fence")) {
        (void)platform->createThread("FixtureWorker");
        platform->closeThread();
        return false; // DebugBreak returning must not permit close success.
    }
    if (isCase("main-thread-compatibility")) return mainThreadCase();
    if (isCase("main-scheduler-task-budget")) return mainSchedulerBudgetCase(false);
    if (isCase("main-scheduler-clock-budget")) return mainSchedulerBudgetCase(true);
    if (isCase("main-scheduler-continuation")) return mainSchedulerContinuationCase();
    if (isCase("main-scheduler-first-task-wake")) return mainSchedulerFirstWakeCase();
    if (isCase("main-scheduler-admission-budget")) return mainSchedulerAdmissionCase(false);
    if (isCase("main-scheduler-admission-shutdown")) return mainSchedulerAdmissionCase(true);
    if (isCase("worker-scheduler-no-count-cap")) return workerSchedulerBudgetCase(false);
    if (isCase("worker-scheduler-clock-budget")) return workerSchedulerBudgetCase(true);
    if (isCase("main-scheduler-shutdown-drain")) return mainSchedulerShutdownCase();
    if (isCase("main-scheduler-pre-shutdown-drain")) return mainSchedulerPreShutdownCase();
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
