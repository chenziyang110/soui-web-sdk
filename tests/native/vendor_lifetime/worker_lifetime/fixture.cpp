#include "fixture_support.h"
#include "content/common/InputFrameTrace.cpp"
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
thread_local int allocationsUntilFailure = -1;
thread_local unsigned sourceLockDepth = 0;
thread_local bool closingRegistry = false;
const char* caseId = nullptr;
const char* contractFailureReason = "actual-source-contract-failed";
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
std::atomic<int> poolTimerAcquired{0}, poolTimerArmed{0}, poolTimerCancelled{0}, poolTimerDrained{0}, poolTimerClosed{0};
std::atomic<int> preciseTimerArmed{0}, preciseWaitAcquired{0}, preciseWaitDrained{0}, preciseWaitClosed{0};
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
std::atomic<bool> controlledProviderClock{false}, failProviderClock{false};
std::atomic<unsigned long long> providerQpc{1000}, providerClockReads{0};
BOOL WINAPI sourceQueryPerformanceCounter(LARGE_INTEGER* value) {
    if (!controlledProviderClock) return ::QueryPerformanceCounter(value);
    ++providerClockReads;
    if (failProviderClock) return FALSE;
    value->QuadPart = static_cast<LONGLONG>(providerQpc.load());
    return TRUE;
}
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
    ++sourceLockDepth;
    if (registry && closingRegistry) {
        ++closeInspections;
        inspectedCount = platform->m_threadNum;
        inspectedReady = platform->m_threads.size() == 1
            && platform->m_threads[0]->threadId() == workerNativeId ? 1 : 0;
        note("close-registry-acquired", static_cast<unsigned>(platform->m_threadNum));
    }
}
void WINAPI sourceLeaveSection(LPCRITICAL_SECTION section) {
    if (sourceLockDepth) --sourceLockDepth;
    ::LeaveCriticalSection(section);
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
PTP_TIMER WINAPI sourceCreatePoolTimer(PTP_TIMER_CALLBACK callback, PVOID context, PTP_CALLBACK_ENVIRON environment) {
    const PTP_TIMER result = ::CreateThreadpoolTimer(callback, context, environment);
    if (result) ++poolTimerAcquired;
    return result;
}
VOID WINAPI sourceSetPoolTimer(PTP_TIMER timer, PFILETIME due, DWORD period, DWORD window) {
    ::SetThreadpoolTimer(timer, due, period, window);
    if (due) ++poolTimerArmed; else ++poolTimerCancelled;
}
VOID WINAPI sourceDrainPoolTimer(PTP_TIMER timer, BOOL cancelPending) {
    ::WaitForThreadpoolTimerCallbacks(timer, cancelPending);
    ++poolTimerDrained;
}
VOID WINAPI sourceClosePoolTimer(PTP_TIMER timer) {
    ::CloseThreadpoolTimer(timer);
    ++poolTimerClosed;
}
BOOL WINAPI sourceSetWaitableTimer(HANDLE timer, const LARGE_INTEGER* due, LONG period,
    PTIMERAPCROUTINE callback, LPVOID argument, BOOL resume) {
    const BOOL result = ::SetWaitableTimer(timer, due, period, callback, argument, resume);
    if (result) ++preciseTimerArmed;
    return result;
}
PTP_WAIT WINAPI sourceCreatePoolWait(PTP_WAIT_CALLBACK callback, PVOID context, PTP_CALLBACK_ENVIRON environment) {
    const PTP_WAIT result = ::CreateThreadpoolWait(callback, context, environment);
    if (result) ++preciseWaitAcquired;
    return result;
}
VOID WINAPI sourceDrainPoolWait(PTP_WAIT wait, BOOL cancelPending) {
    ::WaitForThreadpoolWaitCallbacks(wait, cancelPending);
    ++preciseWaitDrained;
}
VOID WINAPI sourceClosePoolWait(PTP_WAIT wait) {
    ::CloseThreadpoolWait(wait);
    ++preciseWaitClosed;
}
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
    if (fixture::failNextAllocation || fixture::allocationsUntilFailure == 0) {
        fixture::failNextAllocation = false; fixture::allocationsUntilFailure = -1;
        ++fixture::allocationFailures; fixture::note("stl-bad-alloc"); throw std::bad_alloc();
    }
    if (fixture::allocationsUntilFailure > 0) --fixture::allocationsUntilFailure;
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
#define LeaveCriticalSection fixture::sourceLeaveSection
#define DebugBreak fixture::sourceDebugBreak
#define CreateWindowExW fixture::sourceCreateWindow
#define SetTimer fixture::sourceSetTimer
#define KillTimer fixture::sourceKillTimer
#define CreateThreadpoolTimer fixture::sourceCreatePoolTimer
#define SetThreadpoolTimer fixture::sourceSetPoolTimer
#define WaitForThreadpoolTimerCallbacks fixture::sourceDrainPoolTimer
#define CloseThreadpoolTimer fixture::sourceClosePoolTimer
#define SetWaitableTimer fixture::sourceSetWaitableTimer
#define CreateThreadpoolWait fixture::sourceCreatePoolWait
#define WaitForThreadpoolWaitCallbacks fixture::sourceDrainPoolWait
#define CloseThreadpoolWait fixture::sourceClosePoolWait
#define DestroyWindow fixture::sourceDestroyWindow
#define UnregisterClassW fixture::sourceUnregisterClass
#undef DefWindowProc
#define DefWindowProc fixture::sourceDefWindowProc
#define QueryPerformanceCounter fixture::sourceQueryPerformanceCounter
#define currentTime fixtureSchedulerTime
#define monotonicallyIncreasingTime fixtureSchedulerTime
#include "content/web_impl_win/WebThreadImpl.cpp"
#include "content/web_impl_win/WebTimerBase.cpp"
namespace content {
#include "fragments/registry_lock.inc"
#include "fragments/tls.inc"
#include "fragments/registry_and_tls_methods.inc"
#include "fragments/scheduler_ctor_dtor.inc"
#include "fragments/scheduler_post_timer_at.inc"
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
#undef LeaveCriticalSection
#undef DebugBreak
#undef CreateWindowExW
#undef SetTimer
#undef KillTimer
#undef CreateThreadpoolTimer
#undef SetThreadpoolTimer
#undef WaitForThreadpoolTimerCallbacks
#undef CloseThreadpoolTimer
#undef SetWaitableTimer
#undef CreateThreadpoolWait
#undef WaitForThreadpoolWaitCallbacks
#undef CloseThreadpoolWait
#undef DestroyWindow
#undef UnregisterClassW
#undef DefWindowProc
#undef QueryPerformanceCounter
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
    const bool nativeReleased = windowDestroyed == 1 && !::IsWindow(window) && !content::deadlineTimer && !content::retryTimer && classUnregistered == 1;
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
        && schedulerTasksExactlyOnce(receipt) && windowDestroyed == 1 && !content::deadlineTimer && !content::retryTimer
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
    const bool cooperativeBudget = clockBudget
        ? workerFirstYieldRuns >= 2 && workerFirstYieldRuns <= 3
        : workerFirstYieldRuns > 0 && workerFirstYieldRuns <= 32;
    return completed && retired_while_idle && cooperativeBudget && workerMidChainWaits > 0
        && schedulerTasksExactlyOnce(receipt) && cleanWorkerReceipt();
}
bool mainSchedulerShutdownCase() {
    SchedulerReceipt receipt;
    auto* thread = schedulerMainThread();
    for (unsigned index = 0; index < SchedulerReceipt::taskCount; ++index)
        thread->postDelayedTask(FROM_HERE, new SchedulerTask(receipt, thread, index, false), 1000);
    const bool notRunEarly = schedulerTaskRuns == 0;
    delete thread;
    note("shutdown-drained-ready-tasks", schedulerTaskRuns);
    note("shutdown-pool-timers-acquired", poolTimerAcquired);
    note("shutdown-pool-timers-armed", poolTimerArmed);
    note("shutdown-pool-timers-cancelled", poolTimerCancelled);
    note("shutdown-pool-timers-drained", poolTimerDrained);
    note("shutdown-pool-timers-closed", poolTimerClosed);
    const bool nativeTimersClosed = poolTimerAcquired == 2 && poolTimerArmed + preciseTimerArmed >= 1
        && poolTimerCancelled >= 2 && poolTimerDrained == poolTimerAcquired
        && poolTimerClosed == poolTimerAcquired && timerKilled == timerAcquired
        && preciseWaitDrained == preciseWaitAcquired && preciseWaitClosed == preciseWaitAcquired;
    const bool publicationRetired = content::continuationClosed && !content::continuationWindow
        && !content::timerWindowHandle && !content::deadlineTimer && !content::retryTimer
        && !content::preciseDeadlineTimer && !content::preciseDeadlineWait
        && !content::deadlineTicket && !content::pendingContinuation && !content::timerID
        && content::armedDeadline == 0 && !content::sharedHighResolution;
    return notRunEarly && schedulerTaskRuns == SchedulerReceipt::taskCount
        && schedulerTasksExactlyOnce(receipt) && windowDestroyed == 1 && classUnregistered == 1
        && nativeTimersClosed && publicationRetired
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
struct AbsoluteDeadlineReceipt {
    static constexpr unsigned taskCount = 2;
    static constexpr double deadline = 100.00025;
    std::array<std::atomic<unsigned>, taskCount> runs{};
    std::array<unsigned, taskCount> order{};
    std::atomic<unsigned> totalRuns{0}, disposed{0}, earlyRuns{0};
    std::atomic<bool> posting{false}, inlineRun{false}, ownerMatches{true};
};
class AbsoluteDeadlineTask final : public blink::WebThread::Task {
public:
    AbsoluteDeadlineTask(AbsoluteDeadlineReceipt& receipt, content::WebThreadImpl* owner, unsigned index)
        : receipt_(receipt), owner_(owner), index_(index) { }
    ~AbsoluteDeadlineTask() override { ++receipt_.disposed; }
    void run() override {
        if (receipt_.posting) receipt_.inlineRun = true;
        if (schedulerSeconds < AbsoluteDeadlineReceipt::deadline) ++receipt_.earlyRuns;
        if (!owner_->isCurrentThread() || platform->currentThread() != owner_) receipt_.ownerMatches = false;
        const unsigned position = receipt_.totalRuns++;
        if (position < AbsoluteDeadlineReceipt::taskCount) receipt_.order[position] = index_;
        ++receipt_.runs[index_];
    }
private:
    AbsoluteDeadlineReceipt& receipt_;
    content::WebThreadImpl* owner_;
    unsigned index_;
};
struct AbsoluteDeadlineProducer {
    content::WebThreadImpl* thread;
    AbsoluteDeadlineReceipt* receipt;
};
unsigned __stdcall postAbsoluteDeadlineBatch(void* value) {
    auto* producer = static_cast<AbsoluteDeadlineProducer*>(value);
    auto* scheduler = static_cast<content::WebSchedulerImpl*>(producer->thread->scheduler());
    for (unsigned index = 0; index < AbsoluteDeadlineReceipt::taskCount; ++index) {
        // Equal D, but a different producer now for each call. Exercise the
        // projected production adapter, never reproduce its delay arithmetic.
        schedulerSeconds = 100.0 + static_cast<double>(index) * 0.0001;
        producer->receipt->posting = true;
        scheduler->postTimerTaskAt(FROM_HERE,
            new AbsoluteDeadlineTask(*producer->receipt, producer->thread, index), AbsoluteDeadlineReceipt::deadline);
        producer->receipt->posting = false;
    }
    return 0;
}
bool schedulerAbsoluteDeadlineCase(bool crossThread, bool lateAdmission) {
    // Receipt outlives the actual owner even on RED: shutdown is allowed to
    // run future tasks, whose captures must stay alive until delete completes.
    AbsoluteDeadlineReceipt receipt;
    auto* thread = schedulerMainThread();
    if (!thread || !nativeWindow) return false;
    AbsoluteDeadlineProducer producer{thread, &receipt};
    bool producerClosed = true;
    if (crossThread) {
        HANDLE native = reinterpret_cast<HANDLE>(::_beginthreadex(nullptr, 0,
            postAbsoluteDeadlineBatch, &producer, 0, nullptr));
        if (!native || ::WaitForSingleObject(native, boundMs) != WAIT_OBJECT_0) ::ExitProcess(98);
        DWORD exitCode = STILL_ACTIVE;
        const bool returned = ::GetExitCodeThread(native, &exitCode) && exitCode == 0;
        producerClosed = ::CloseHandle(native) != FALSE && returned;
    } else {
        producerClosed = postAbsoluteDeadlineBatch(&producer) == 0;
    }
    const bool asynchronous = receipt.totalRuns == 0 && receipt.disposed == 0 && !receipt.inlineRun;
    const bool queuedOnProducer = !crossThread || thread->timerHeap().empty();
    schedulerSeconds = lateAdmission ? 100.0005 : 100.0002;
    thread->startTriggerTasks(); // Actual pending-index admission, without executing tasks.
    const double ownerNow = schedulerSeconds.load();
    const double expectedRemaining = std::max(0.0, AbsoluteDeadlineReceipt::deadline - ownerNow);
    const bool bothAdmitted = thread->timerHeap().size() == AbsoluteDeadlineReceipt::taskCount;
    bool retainedDeadline = bothAdmitted;
    for (auto* timer : thread->timerHeap()) {
        const double remaining = timer->nextFireInterval();
        std::printf("{\"record\":\"absolute-deadline-target\",\"case\":\"%s\",\"expectedD\":%.17g,\"ownerNow\":%.17g,\"expectedRemaining\":%.17g,\"observedRemaining\":%.17g}\n",
            caseId, AbsoluteDeadlineReceipt::deadline, ownerNow, expectedRemaining, remaining);
        retainedDeadline = remaining == expectedRemaining && retainedDeadline;
    }
    if (!retainedDeadline) {
        contractFailureReason = "absolute-deadline-retained-D-assertion-failed";
        std::fprintf(stderr, "retained-D assertion failed: actual scheduler adapter rebuilt fractional absolute deadline %.17g\n",
            AbsoluteDeadlineReceipt::deadline);
    }
    bool noEarlyRun = receipt.totalRuns == 0;
    if (!lateAdmission) {
        schedulerSeconds = AbsoluteDeadlineReceipt::deadline - 0.000001;
        thread->fire();
        noEarlyRun = receipt.totalRuns == 0 && noEarlyRun;
        schedulerSeconds = AbsoluteDeadlineReceipt::deadline;
    }
    thread->fire();
    const bool ranWhenDue = receipt.totalRuns == AbsoluteDeadlineReceipt::taskCount;
    delete thread; // Also cleanly drains baseline's rebuilt future targets on RED.
    bool exactlyOnce = receipt.disposed == AbsoluteDeadlineReceipt::taskCount;
    for (unsigned index = 0; index < AbsoluteDeadlineReceipt::taskCount; ++index)
        exactlyOnce = receipt.runs[index] == 1 && receipt.order[index] == index && exactlyOnce;
    const bool cleanOwner = windowDestroyed == 1 && schedulerFrees == 1 && sectionsDeleted == 2
        && !content::deadlineTimer && !content::retryTimer
        && platform->m_threadNum == 0 && platform->m_threads.empty();
    std::printf("{\"record\":\"absolute-deadline-receipt\",\"case\":\"%s\",\"retainedD\":%s,\"asynchronous\":%s,\"noEarlyRun\":%s,\"ranWhenDue\":%s,\"exactlyOnce\":%s,\"cleanOwner\":%s}\n",
        caseId, retainedDeadline ? "true" : "false", asynchronous ? "true" : "false",
        noEarlyRun ? "true" : "false", ranWhenDue ? "true" : "false", exactlyOnce ? "true" : "false", cleanOwner ? "true" : "false");
    return producerClosed && asynchronous && queuedOnProducer && retainedDeadline && noEarlyRun && ranWhenDue
        && receipt.earlyRuns == 0 && !receipt.inlineRun && receipt.ownerMatches && exactlyOnce && cleanOwner;
}
struct AbsoluteValueReceipt {
    std::atomic<unsigned> runs{0}, disposed{0};
    std::atomic<DWORD> runTid{0};
    std::atomic<double> runTime{0};
    std::atomic<bool> posting{false}, inlineRun{false}, underLock{false}, ownerMatches{true}, postThrew{false};
};
class AbsoluteValueTask final : public blink::WebThread::Task {
public:
    AbsoluteValueTask(AbsoluteValueReceipt& receipt, content::WebThreadImpl* owner)
        : receipt_(receipt), owner_(owner) { }
    ~AbsoluteValueTask() override {
        if (sourceLockDepth) receipt_.underLock = true;
        ++receipt_.disposed;
    }
    void run() override {
        if (receipt_.posting) receipt_.inlineRun = true;
        if (!owner_->isCurrentThread() || platform->currentThread() != owner_) receipt_.ownerMatches = false;
        receipt_.runTid = ::GetCurrentThreadId();
        receipt_.runTime = schedulerSeconds.load();
        ++receipt_.runs;
    }
private:
    AbsoluteValueReceipt& receipt_;
    content::WebThreadImpl* owner_;
};
struct AbsoluteValuePost {
    content::WebThreadImpl* thread;
    AbsoluteValueReceipt* receipt;
    double deadline;
    bool adapter;
    int failAfter = -1;
};
struct AbsoluteValueBatch { AbsoluteValuePost* posts; unsigned count; };
unsigned __stdcall postAbsoluteValues(void* value) {
    auto* batch = static_cast<AbsoluteValueBatch*>(value);
    for (unsigned index = 0; index < batch->count; ++index) {
        auto& post = batch->posts[index];
        auto* task = new AbsoluteValueTask(*post.receipt, post.thread);
        post.receipt->posting = true;
        allocationsUntilFailure = post.failAfter; // Allocate the transferred task before arming OOM.
        try {
            if (post.adapter)
                static_cast<content::WebSchedulerImpl*>(post.thread->scheduler())->postTimerTaskAt(FROM_HERE, task, post.deadline);
            else
                post.thread->postTaskAtWithPriorityCrossThread(FROM_HERE, task, post.deadline, content::WebThreadImpl::kDefaultPriority);
        } catch (...) { post.receipt->postThrew = true; }
        allocationsUntilFailure = -1;
        post.receipt->posting = false;
    }
    return 0;
}
bool postAbsoluteValuesOnNativeThread(AbsoluteValuePost* posts, unsigned count) {
    AbsoluteValueBatch batch{posts, count};
    HANDLE native = reinterpret_cast<HANDLE>(::_beginthreadex(nullptr, 0, postAbsoluteValues, &batch, 0, nullptr));
    if (!native || ::WaitForSingleObject(native, boundMs) != WAIT_OBJECT_0) ::ExitProcess(98);
    DWORD exitCode = STILL_ACTIVE;
    const bool returned = ::GetExitCodeThread(native, &exitCode) && exitCode == 0;
    return ::CloseHandle(native) != FALSE && returned;
}
bool absoluteValueReleased(const AbsoluteValueReceipt& receipt, unsigned expectedRuns) {
    return receipt.runs == expectedRuns && receipt.disposed == 1
        && !receipt.inlineRun && !receipt.underLock && !receipt.postThrew && receipt.ownerMatches;
}
bool absoluteMainOwnerReleased() {
    return windowDestroyed == 1 && schedulerFrees == 1 && sectionsDeleted == 2
        && !content::deadlineTimer && !content::retryTimer
        && platform->m_threadNum == 0 && platform->m_threads.empty();
}
bool schedulerAbsoluteInputsCase(bool crossThread) {
    const std::array<double, 7> values{{99.5, 100.0, 0.0, -1.0,
        -(std::numeric_limits<double>::infinity)(), (std::numeric_limits<double>::quiet_NaN)(),
        (std::numeric_limits<double>::infinity)()}};
    constexpr unsigned postCount = 14; // Each input through adapter and direct internal entry.
    std::array<AbsoluteValueReceipt, postCount> receipts{};
    std::array<AbsoluteValuePost, postCount> posts{};
    auto* thread = schedulerMainThread();
    for (unsigned index = 0; index < postCount; ++index)
        posts[index] = {thread, &receipts[index], values[index / 2], (index % 2) == 0};
    bool posted = true;
    if (crossThread) posted = postAbsoluteValuesOnNativeThread(posts.data(), postCount);
    else { AbsoluteValueBatch batch{posts.data(), postCount}; posted = postAbsoluteValues(&batch) == 0; }
    bool asynchronous = !crossThread || thread->timerHeap().empty();
    for (unsigned index = 0; index < postCount; ++index)
        asynchronous = receipts[index].runs == 0
            && receipts[index].disposed == (index >= postCount - 2 ? 1u : 0u) && asynchronous;
    const bool pendingReady = thread->hasReadyTaskAtOrAbove(content::WebThreadImpl::kDefaultPriority);
    thread->startTriggerTasks();
    bool activeDueTimers = thread->timerHeap().size() == postCount - 2;
    for (auto* timer : thread->timerHeap())
        activeDueTimers = timer->isActive() && timer->nextFireInterval() == 0 && activeDueTimers;
    for (const auto& receipt : receipts) asynchronous = receipt.runs == 0 && asynchronous;
    thread->fire();
    bool ranOnService = true;
    for (unsigned index = 0; index < postCount - 2; ++index)
        ranOnService = receipts[index].runs == 1 && receipts[index].runTid == engineThreadId
            && receipts[index].runTime == 100.0 && ranOnService;
    delete thread;
    bool exactlyOnce = true;
    for (unsigned index = 0; index < postCount; ++index)
        exactlyOnce = absoluteValueReleased(receipts[index], index >= postCount - 2 ? 0 : 1) && exactlyOnce;
    note("absolute-input-owned-payloads", postCount);
    return posted && asynchronous && pendingReady && activeDueTimers && ranOnService && exactlyOnce && absoluteMainOwnerReleased();
}
bool schedulerAbsoluteHugeCancelCase() {
    std::array<AbsoluteValueReceipt, 2> receipts{};
    auto* thread = schedulerMainThread();
    const double huge = (std::numeric_limits<double>::max)() / 2;
    std::array<AbsoluteValuePost, 2> posts{{{thread, &receipts[0], huge, true}, {thread, &receipts[1], huge, false}}};
    AbsoluteValueBatch ownerPost{posts.data(), 1};
    const bool ownerPosted = postAbsoluteValues(&ownerPost) == 0;
    const bool nativePosted = postAbsoluteValuesOnNativeThread(&posts[1], 1);
    const bool asynchronous = receipts[0].runs == 0 && receipts[1].runs == 0;
    thread->startTriggerTasks();
    const bool backendArmed = (poolTimerArmed > 0 && content::deadlineTimer)
        || (preciseTimerArmed > 0 && content::preciseDeadlineTimer && content::preciseDeadlineWait);
    note("absolute-huge-pool-timer-arms", poolTimerArmed);
    note("absolute-huge-precise-timer-arms", preciseTimerArmed);
    note("absolute-huge-retained-timer-count", thread->timerHeap().size());
    note("absolute-huge-deadline-retained", content::armedDeadline == huge);
    bool retained = thread->timerHeap().size() == 2 && content::armedDeadline == huge && backendArmed;
    for (auto* timer : thread->timerHeap())
        retained = timer->nextFireInterval() == huge - schedulerSeconds.load() && retained;
    thread->fire();
    const bool notEarly = receipts[0].runs == 0 && receipts[1].runs == 0;
    // stop() removes from the actual index/heap and queues ordinary owner retirement.
    while (!thread->timerHeap().empty()) thread->timerHeap().front()->stop();
    thread->retireCompletedTasksForOwner();
    const bool cancelled = absoluteValueReleased(receipts[0], 0) && absoluteValueReleased(receipts[1], 0);
    note("absolute-huge-not-early", notEarly);
    note("absolute-huge-owned-payloads-cancelled", cancelled);
    delete thread;
    return ownerPosted && nativePosted && asynchronous && retained && notEarly && cancelled
        && absoluteValueReleased(receipts[0], 0) && absoluteValueReleased(receipts[1], 0) && absoluteMainOwnerReleased();
}
bool schedulerAbsoluteOwnerOomCase() {
    std::array<AbsoluteValueReceipt, 3> receipts{};
    auto* thread = schedulerMainThread();
    const int before = allocationFailures.load();
    bool consumed = true;
    for (unsigned index = 0; index < receipts.size(); ++index) {
        if (index == 1) thread->timerHeap().reserve(16); // Isolate timer allocation from reserve failure.
        AbsoluteValuePost post{thread, &receipts[index], 100.5, (index % 2) == 0, index == 2 ? 1 : 0};
        AbsoluteValueBatch batch{&post, 1};
        postAbsoluteValues(&batch); // Reserve, then timer allocation, then timer-index allocation.
        consumed = absoluteValueReleased(receipts[index], 0) && thread->timerHeap().empty() && consumed;
    }
    delete thread;
    return consumed && allocationFailures == before + 3 && absoluteMainOwnerReleased();
}
bool schedulerAbsoluteProducerOomCase() {
    std::array<AbsoluteValueReceipt, 5> receipts{};
    auto* thread = schedulerMainThread();
    const int before = allocationFailures.load();
    bool consumed = true;
    // TaskPair, priority node, node vector, then pending vector. All four failed
    // transfers must release the task after leaving the real producer lock.
    for (unsigned index = 0; index < 4; ++index) {
        AbsoluteValuePost post{thread, &receipts[index], 100.5, (index % 2) == 0, static_cast<int>(index)};
        const bool posted = postAbsoluteValuesOnNativeThread(&post, 1);
        consumed = posted && absoluteValueReleased(receipts[index], 0) && thread->timerHeap().empty() && consumed;
    }
    AbsoluteValuePost recovery{thread, &receipts[4], 99.5, true};
    const bool recoveryPosted = postAbsoluteValuesOnNativeThread(&recovery, 1);
    const bool recoveryAsync = receipts[4].runs == 0 && receipts[4].disposed == 0;
    thread->fire();
    const bool recovered = receipts[4].runs == 1;
    delete thread;
    return consumed && recoveryPosted && recoveryAsync && recovered && absoluteValueReleased(receipts[4], 1)
        && allocationFailures == before + 4 && absoluteMainOwnerReleased();
}
bool schedulerAbsoluteAdmissionOomCase() {
    std::array<AbsoluteValueReceipt, 2> receipts{};
    auto* thread = schedulerMainThread();
    const int before = allocationFailures.load();
    AbsoluteValuePost post{thread, &receipts[0], 100.5, true};
    const bool posted = postAbsoluteValuesOnNativeThread(&post, 1);
    const bool heldByPending = receipts[0].runs == 0 && receipts[0].disposed == 0 && thread->timerHeap().empty();
    failNextAllocation = true;
    thread->startTriggerTasks(); // Real TaskPair transfers ownership before owner reserve fails.
    failNextAllocation = false;
    const bool consumed = absoluteValueReleased(receipts[0], 0) && thread->timerHeap().empty();
    AbsoluteValuePost recovery{thread, &receipts[1], 99.5, false};
    AbsoluteValueBatch batch{&recovery, 1};
    postAbsoluteValues(&batch);
    const bool recoveryAsync = receipts[1].runs == 0;
    thread->fire();
    const bool recovered = receipts[1].runs == 1;
    delete thread;
    return posted && heldByPending && consumed && recoveryAsync && recovered && absoluteValueReleased(receipts[1], 1)
        && allocationFailures == before + 1 && absoluteMainOwnerReleased();
}
bool schedulerAbsoluteExitCase(bool futureDrain) {
    std::array<AbsoluteValueReceipt, 2> receipts{};
    controlledSchedulerClock = true;
    auto* thread = static_cast<content::WebThreadImpl*>(platform->createThread("FixtureWorker"));
    const DWORD ownerTid = workerNativeId.load();
    if (!futureDrain) thread->shutdown();
    std::array<AbsoluteValuePost, 2> posts{{{thread, &receipts[0], 100.50025, true}, {thread, &receipts[1], 100.50025, false}}};
    const bool posted = postAbsoluteValuesOnNativeThread(posts.data(), static_cast<unsigned>(posts.size()));
    const bool beforeShutdown = receipts[0].runs == 0 && receipts[1].runs == 0
        && receipts[0].disposed == (futureDrain ? 0u : 1u) && receipts[1].disposed == (futureDrain ? 0u : 1u);
    thread->shutdown();
    const bool joined = thread->threadClosed();
    bool ranOnWorker = true;
    if (futureDrain) for (const auto& receipt : receipts)
        ranOnWorker = receipt.runs == 1 && receipt.runTid == ownerTid && receipt.runTime == 100.0 && ranOnWorker;
    delete thread;
    return posted && beforeShutdown && joined && ranOnWorker
        && absoluteValueReleased(receipts[0], futureDrain ? 1 : 0)
        && absoluteValueReleased(receipts[1], futureDrain ? 1 : 0) && cleanWorkerReceipt();
}
struct ProviderReceipt {
    std::atomic<unsigned> runs{0}, disposed{0};
    std::atomic<DWORD> runTid{0};
};
class ProviderTask final : public blink::WebThread::Task {
public:
    ProviderTask(ProviderReceipt& receipt, unsigned qpcCost = 0) : receipt_(receipt), qpcCost_(qpcCost) {}
    ~ProviderTask() override { ++receipt_.disposed; }
    void run() override { ++receipt_.runs; receipt_.runTid = ::GetCurrentThreadId(); providerQpc += qpcCost_; }
private:
    ProviderReceipt& receipt_;
    unsigned qpcCost_;
};
bool providerReceiptComplete(const ProviderReceipt& receipt) {
    return receipt.runs == 1 && receipt.disposed == 1 && receipt.runTid == engineThreadId;
}
bool providerReadyCase(bool legacy, bool crossCutoff, bool qpcFailure, bool recovery) {
    std::array<ProviderReceipt, 4> receipts{};
    auto* thread = schedulerMainThread();
    if (!thread || !nativeWindow) return false;
    controlledProviderClock = true;
    failProviderClock = qpcFailure;
    for (unsigned i = 0; i < 3; ++i) thread->postTask(FROM_HERE, new ProviderTask(receipts[i], crossCutoff ? 2 : 0));
    thread->postDelayedTask(FROM_HERE, new ProviderTask(receipts[3]), 1000);
    const auto before = providerClockReads.load();
    bool more = false;
    if (recovery) {
        content::continuationDeferred = true;
        content::readyRetry = true;
        content::serviceSharedTimerOwnerOpportunity(0, 1000);
        more = true; // This entry has no result; observe remaining work below.
    } else more = thread->serviceOwnerOpportunity(crossCutoff ? 1001 : 0, legacy ? 0 : 1000);
    const bool first = receipts[0].runs == 1 && receipts[1].runs == (legacy ? 1u : 0u)
        && receipts[2].runs == (legacy ? 1u : 0u) && receipts[3].runs == 0
        && more == !legacy && (!recovery || (!content::continuationDeferred && !content::readyRetry));
    bool drained = true;
    if (!legacy) {
        drained = thread->serviceOwnerOpportunity(0, 1000) && receipts[1].runs == 1 && receipts[2].runs == 0;
        // The last ready callback leaves its completed capture for the next
        // bounded retirement opportunity, which must still report work.
        drained = thread->serviceOwnerOpportunity(0, 1000) && receipts[2].runs == 1 && drained;
    }
    const bool noFutureWake = !thread->serviceOwnerOpportunity(0, 1000) && receipts[3].runs == 0;
    const bool bridgeClockUsed = legacy ? providerClockReads == before : providerClockReads > before;
    const bool exactlyReady = providerReceiptComplete(receipts[0]) && providerReceiptComplete(receipts[1])
        && providerReceiptComplete(receipts[2]);
    failProviderClock = false;
    delete thread; // The unchanged explicit drain owns the future task.
    return first && drained && noFutureWake && bridgeClockUsed && exactlyReady
        && providerReceiptComplete(receipts[3]) && absoluteMainOwnerReleased();
}
struct ProviderBatch {
    content::WebThreadImpl* thread;
    ProviderReceipt* receipts;
    unsigned count;
    long long delay;
};
unsigned __stdcall postProviderBatch(void* value) {
    auto& batch = *static_cast<ProviderBatch*>(value);
    for (unsigned i = 0; i < batch.count; ++i)
        batch.thread->postDelayedTask(FROM_HERE, new ProviderTask(batch.receipts[i]), batch.delay);
    return 0;
}
bool providerAdmissionCase(bool readyFirst, bool futureOnly) {
    std::array<ProviderReceipt, 6> receipts{};
    auto* thread = schedulerMainThread();
    if (!thread || !nativeWindow) return false;
    controlledProviderClock = true;
    if (readyFirst) for (unsigned i = 0; i < 2; ++i) thread->postTask(FROM_HERE, new ProviderTask(receipts[i]));
    ProviderBatch batch{thread, receipts.data() + 2, 4, futureOnly ? 1000 : 0};
    HANDLE producer = reinterpret_cast<HANDLE>(::_beginthreadex(nullptr, 0, postProviderBatch, &batch, 0, nullptr));
    if (!producer || ::WaitForSingleObject(producer, boundMs) != WAIT_OBJECT_0) ::ExitProcess(98);
    DWORD code = STILL_ACTIVE;
    bool passed = ::GetExitCodeThread(producer, &code) && code == 0 && ::CloseHandle(producer);
    if (readyFirst) {
        passed = thread->serviceOwnerOpportunity(0, 1000) && receipts[0].runs == 1 && receipts[1].runs == 0
            && thread->timerHeap().size() == 1 && passed;
        passed = thread->serviceOwnerOpportunity(0, 1000) && receipts[1].runs == 1
            && thread->timerHeap().empty() && passed;
    }
    for (unsigned i = 0; i < 4; ++i) {
        const bool more = thread->serviceOwnerOpportunity(0, 1000);
        passed = more == (!futureOnly || i < 3) && receipts[i + 2].runs == (futureOnly ? 0u : 1u) && passed;
        if (futureOnly) passed = thread->timerHeap().size() == i + 1 && passed;
        else for (unsigned j = i + 1; j < 4; ++j) passed = receipts[j + 2].runs == 0 && passed;
    }
    passed = !thread->serviceOwnerOpportunity(0, 1000) && passed;
    delete thread;
    for (unsigned i = readyFirst ? 0 : 2; i < receipts.size(); ++i)
        passed = providerReceiptComplete(receipts[i]) && passed;
    return passed && absoluteMainOwnerReleased();
}
bool providerShutdownCase() {
    std::array<ProviderReceipt, 96> receipts{};
    auto* thread = schedulerMainThread();
    if (!thread || !nativeWindow) return false;
    controlledProviderClock = true;
    for (auto& receipt : receipts) thread->postTask(FROM_HERE, new ProviderTask(receipt));
    bool passed = thread->serviceOwnerOpportunity(0, 1000) && receipts[0].runs == 1 && receipts[1].runs == 0;
    const auto reads = providerClockReads.load();
    thread->fireForShutdown();
    passed = providerClockReads == reads && passed;
    unsigned completed = 0;
    for (const auto& receipt : receipts) {
        completed += receipt.runs.load();
        passed = receipt.runs == 1 && receipt.runTid == engineThreadId && passed;
    }
    note("provider-shutdown-callbacks-drained", completed);
    delete thread;
    for (const auto& receipt : receipts) passed = providerReceiptComplete(receipt) && passed;
    return passed && absoluteMainOwnerReleased();
}
struct ProviderRetirementBatch {
    static constexpr unsigned count = 128;
    std::array<ProviderReceipt, count> receipts{};
    content::WebThreadImpl* owner = nullptr;
    unsigned disposed = 0;
    unsigned qpcCost = 0;
    bool reenter = false;
    bool valid = true;
};
class ProviderRetirementTask final : public blink::WebThread::Task {
public:
    ProviderRetirementTask(ProviderRetirementBatch& batch, unsigned index) : batch_(batch), index_(index) {}
    void run() override {
        ++batch_.receipts[index_].runs;
        batch_.receipts[index_].runTid = ::GetCurrentThreadId();
    }
    ~ProviderRetirementTask() override {
        auto& receipt = batch_.receipts[index_];
        ++receipt.disposed;
        ++batch_.disposed;
        batch_.valid = receipt.runs == 1 && receipt.disposed == 1
            && receipt.runTid == engineThreadId && !sourceLockDepth && batch_.valid;
        providerQpc += batch_.qpcCost;
        if (batch_.reenter) {
            const unsigned before = batch_.disposed;
            const bool nested = batch_.owner->serviceOwnerOpportunity(0, 1000);
            batch_.owner->retireCompletedTasksForOwner();
            batch_.valid = !nested && batch_.disposed == before && batch_.valid;
        }
    }
private:
    ProviderRetirementBatch& batch_;
    unsigned index_;
};
bool prepareCompletedRetirementBatch(content::WebThreadImpl* thread, ProviderRetirementBatch& batch) {
    batch.owner = thread;
    for (unsigned i = 0; i < batch.count; ++i)
        thread->postTask(FROM_HERE, new ProviderRetirementTask(batch, i));
    // Real callbacks finish through the existing pre-shutdown fire entry. Its
    // unchanged final 32-item retirement leaves 96 completed captures retained.
    thread->fireForShutdown();
    bool completed = batch.disposed == 32 && thread->timerHeap().empty() && batch.valid;
    for (const auto& receipt : batch.receipts)
        completed = receipt.runs == 1 && receipt.runTid == engineThreadId && completed;
    return completed;
}
bool providerRetirementBatchComplete(const ProviderRetirementBatch& batch) {
    bool complete = batch.disposed == batch.count && batch.valid;
    for (const auto& receipt : batch.receipts) complete = providerReceiptComplete(receipt) && complete;
    return complete;
}
bool drainRetirementThroughProvider(content::WebThreadImpl* thread, ProviderRetirementBatch& batch,
    const ProviderReceipt* ready = nullptr) {
    const unsigned total = batch.count + (ready ? 1u : 0u);
    for (unsigned attempt = 0; attempt < total; ++attempt) {
        const unsigned before = batch.disposed + (ready ? ready->disposed.load() : 0u);
        if (before == total) break;
        const bool more = thread->serviceOwnerOpportunity(0, 1000);
        const unsigned after = batch.disposed + (ready ? ready->disposed.load() : 0u);
        if (after != before + 1 || more != (after < total)) return false;
    }
    // Consume the actual outstanding continuation once after its work was
    // drained through provider turns. It must neither repeat work nor rearm.
    const UINT_PTR ticket = content::pendingContinuation;
    MSG message{};
    unsigned continuations = 0;
    bool matchingTicket = ticket != 0;
    while (::PeekMessageW(&message, nativeWindow, content::continuationMessage, content::continuationMessage, PM_REMOVE)) {
        matchingTicket = message.wParam == ticket && matchingTicket;
        ::DispatchMessageW(&message);
        ++continuations;
    }
    note("provider-retirement-continuations-consumed", continuations);
    return providerRetirementBatchComplete(batch) && (!ready || providerReceiptComplete(*ready))
        && matchingTicket && continuations == 1 && !content::pendingContinuation
        && !thread->serviceOwnerOpportunity(0, 1000);
}
bool providerRetirementDeadlineCase() {
    ProviderRetirementBatch expired, future, legacy;
    ProviderReceipt ready;
    auto* thread = schedulerMainThread();
    if (!thread || !nativeWindow) return false;
    controlledProviderClock = true;
    bool passed = prepareCompletedRetirementBatch(thread, expired);
    expired.qpcCost = 2;
    expired.reenter = true;
    thread->postTask(FROM_HERE, new ProviderTask(ready));
    const unsigned beforeExpired = expired.disposed;
    const bool expiredMore = thread->serviceOwnerOpportunity(0, 1000);
    const unsigned expiredRetired = expired.disposed - beforeExpired;
    note("provider-retirement-expired-count", expiredRetired);
    if (expiredRetired != 1) {
        contractFailureReason = "provider-retirement-expired-count-assertion-failed";
        delete thread;
        return false;
    }
    passed = expiredMore && ready.runs == 1 && ready.disposed == 0 && expired.valid && passed;
    passed = drainRetirementThroughProvider(thread, expired, &ready) && passed;
    passed = prepareCompletedRetirementBatch(thread, future) && passed;
    future.qpcCost = 2;
    future.reenter = true;
    providerQpc = 1000;
    const unsigned beforeFuture = future.disposed;
    const bool futureMore = thread->serviceOwnerOpportunity(1005, 1000);
    const unsigned futureRetired = future.disposed - beforeFuture;
    note("provider-retirement-future-count", futureRetired);
    passed = futureMore && futureRetired == 3 && providerQpc == 1006 && future.valid && passed;
    passed = drainRetirementThroughProvider(thread, future) && passed;
    passed = prepareCompletedRetirementBatch(thread, legacy) && passed;
    const auto beforeLegacyReads = providerClockReads.load();
    const unsigned beforeLegacy = legacy.disposed;
    thread->retireCompletedTasksForOwner();
    note("provider-retirement-legacy-count", legacy.disposed - beforeLegacy);
    passed = legacy.disposed == beforeLegacy + 32 && providerClockReads == beforeLegacyReads && passed;
    delete thread; // Real shutdown drains every remaining completed capture.
    return passed && providerRetirementBatchComplete(expired) && providerRetirementBatchComplete(future)
        && providerRetirementBatchComplete(legacy) && providerClockReads == beforeLegacyReads
        && providerReceiptComplete(ready) && absoluteMainOwnerReleased();
}
bool dispatch() {
    if (isCase("provider-retirement-deadline")) return providerRetirementDeadlineCase();
    if (isCase("provider-deadline-expired-ready")) return providerReadyCase(false, false, false, false);
    if (isCase("provider-deadline-callback-crosses")) return providerReadyCase(false, true, false, false);
    if (isCase("provider-deadline-clock-failure")) return providerReadyCase(false, false, true, false);
    if (isCase("provider-deadline-shared-recovery")) return providerReadyCase(false, false, false, true);
    if (isCase("provider-deadline-legacy-no-qpc")) return providerReadyCase(true, false, false, false);
    if (isCase("provider-deadline-pending-minimum")) return providerAdmissionCase(false, false);
    if (isCase("provider-deadline-ready-before-admission")) return providerAdmissionCase(true, false);
    if (isCase("provider-deadline-future-no-spin")) return providerAdmissionCase(false, true);
    if (isCase("provider-deadline-shutdown-drain")) return providerShutdownCase();
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
    if (isCase("worker-scheduler-task-budget")) return workerSchedulerBudgetCase(false);
    if (isCase("worker-scheduler-clock-budget")) return workerSchedulerBudgetCase(true);
    if (isCase("main-scheduler-shutdown-drain")) return mainSchedulerShutdownCase();
    if (isCase("main-scheduler-pre-shutdown-drain")) return mainSchedulerPreShutdownCase();
    if (isCase("scheduler-absolute-same-thread")) return schedulerAbsoluteDeadlineCase(false, false);
    if (isCase("scheduler-absolute-cross-thread")) return schedulerAbsoluteDeadlineCase(true, false);
    if (isCase("scheduler-absolute-late-admission")) return schedulerAbsoluteDeadlineCase(true, true);
    if (isCase("scheduler-absolute-inputs-same-thread")) return schedulerAbsoluteInputsCase(false);
    if (isCase("scheduler-absolute-inputs-cross-thread")) return schedulerAbsoluteInputsCase(true);
    if (isCase("scheduler-absolute-huge-cancel")) return schedulerAbsoluteHugeCancelCase();
    if (isCase("scheduler-absolute-oom-owner")) return schedulerAbsoluteOwnerOomCase();
    if (isCase("scheduler-absolute-oom-producer")) return schedulerAbsoluteProducerOomCase();
    if (isCase("scheduler-absolute-oom-admission")) return schedulerAbsoluteAdmissionOomCase();
    if (isCase("scheduler-absolute-exit-rejection")) return schedulerAbsoluteExitCase(false);
    if (isCase("scheduler-absolute-future-shutdown")) return schedulerAbsoluteExitCase(true);
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
    namespace trace = content::input_frame_trace;
    if (!trace::process_recorder().configure(trace::current_identity(0x20261026, 1), trace::mode::scalar)
        || !trace::register_current_thread()) return 95;
    struct TraceDetach { ~TraceDetach() { trace::detach_current_thread(); } } traceDetach;
    fixture::caseId = argv[1];
    if (argc == 3) {
        if (std::strcmp(argv[2], "wait") && std::strcmp(argv[2], "exit-code")) return 97;
        fixture::waitFault = argv[2];
    }
    fixture::Context context;
    const bool passed = fixture::dispatch();
    fixture::summary(passed, passed ? "actual-source-contract-satisfied" : fixture::contractFailureReason);
    // Failed contracts can retain a blocked contender, even with an empty registry.
    // Never destroy its critical section/platform storage or certify cleanup.
    if (!passed) ::ExitProcess(3);
    return passed ? 0 : 3;
}
