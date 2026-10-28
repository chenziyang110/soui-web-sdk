// Pure dependency contract. No host window, timer, input or browser is created.
// SharedTimerWin and schedulerTasks are compiled from exact production bytes.
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <cstdio>
#include <deque>
#include <functional>
#include <string>
#include <vector>
#include "TaskSchedulingPolicy.h"

namespace fixture {
struct Message { HWND window; UINT id; WPARAM first; LPARAM second; };
std::deque<Message> messages;
std::vector<std::wstring> messageNames;
DWORD queueStatus = 0;
bool failPost = false;
unsigned postAttempts = 0, timerCalls = 0, runs = 0, destroyed = 0;
double now = 100.0, step = 0;
int enters = 0, failures = 0, assertions = 0;
HWND window = reinterpret_cast<HWND>(static_cast<UINT_PTR>(1));
BOOL WINAPI post(HWND w, UINT id, WPARAM a, LPARAM b) {
    ++postAttempts;
    if (failPost) return FALSE;
    messages.push_back({w, id, a, b});
    return TRUE;
}
DWORD WINAPI status(UINT flags) { return queueStatus & (flags | (flags << 16)); }
LRESULT WINAPI defaultProc(HWND, UINT, WPARAM, LPARAM) { return 0; }
UINT_PTR WINAPI setTimer(HWND, UINT_PTR id, UINT, TIMERPROC) { ++timerCalls; return id; }
BOOL WINAPI killTimer(HWND, UINT_PTR) { return TRUE; }
BOOL WINAPI destroyWindow(HWND) { return TRUE; }
BOOL WINAPI unregisterClass(LPCWSTR, HINSTANCE) { return TRUE; }
HMODULE WINAPI module(LPCWSTR) { return nullptr; }
FARPROC WINAPI proc(HMODULE, LPCSTR) { return nullptr; }
ATOM WINAPI registerClass(const WNDCLASSEXW*) { return 1; }
HWND WINAPI createWindow(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int,
    HWND, HMENU, HINSTANCE, LPVOID) { return window; }
UINT WINAPI registerMessage(LPCWSTR name) {
    messageNames.emplace_back(name);
    return WM_APP + 100 + static_cast<UINT>(messageNames.size());
}
MMRESULT WINAPI endPeriod(UINT) { return TIMERR_NOERROR; }
void expect(bool condition, const char* name) {
    ++assertions;
    if (!condition) { ++failures; std::printf("FAIL: %s\n", name); }
}
}
namespace WTF { inline unsigned currentThread() { return 1; } }
inline double currentTime() { return fixture::now; }
#define ASSERT(expression) ((void)0)
namespace content {
#include "budgets.inc"
class WebThreadImpl;
class WebTimerBase {
public:
    WebThreadImpl* owner;
    double m_nextFireTime;
    int m_priority = 200;
    std::function<void()> callback;
    ~WebTimerBase() { ++fixture::destroyed; }
    double repeatInterval() const { return 0; }
    void heapDelete();
    void setNextFireTime(double, void*) {}
    void fired() { ++fixture::runs; fixture::now += fixture::step; if (callback) callback(); delete this; }
};
class WebThreadImpl {
public:
    unsigned m_threadId = 1;
    int m_firingTimers = 0;
    bool m_isMainThread = true, m_willExit = false;
    std::vector<WebTimerBase*> m_timerHeap;
    void schedulerTasks();
    void schedulerTasks(bool);
    void updateSharedTimer();
    void fire() { schedulerTasks(); }
    void deleteUnusedTimers() {}
    void startTriggerTasks() {}
    void startTriggerTasks(unsigned*, double) {}
    bool hasPendingTaskAdmission() { return false; }
    void willProcessTasks() {}
    void didProcessTasks() {}
    void add(double deadline = fixture::now, std::function<void()> callback = {}) {
        auto* timer = new WebTimerBase{this, deadline, 200, std::move(callback)};
        m_timerHeap.push_back(timer);
        std::stable_sort(m_timerHeap.begin(), m_timerHeap.end(), [](const auto* a, const auto* b) {
            return a->m_nextFireTime < b->m_nextFireTime;
        });
    }
    void clear() { for (auto* timer : m_timerHeap) delete timer; m_timerHeap.clear(); }
};
void WebTimerBase::heapDelete() {
    auto& heap = owner->m_timerHeap;
    heap.erase(std::find(heap.begin(), heap.end(), this));
}
class CheckReEnter {
public:
    static int getEnterCount() { return fixture::enters; }
    static void incrementEnterCount() { ++fixture::enters; }
    static void decrementEnterCount() { --fixture::enters; }
};
}
namespace blink {
class Platform {
public:
    content::WebThreadImpl* owner = nullptr;
    static Platform* current() { static Platform platform; return &platform; }
    content::WebThreadImpl* currentThread() { return owner; }
};
}
#undef PostMessage
#undef DefWindowProc
#undef GetModuleHandle
#undef RegisterClassEx
#undef RegisterWindowMessage
#define PostMessage fixture::post
#define GetQueueStatus fixture::status
#define DefWindowProc fixture::defaultProc
#define SetTimer fixture::setTimer
#define KillTimer fixture::killTimer
#define DestroyWindow fixture::destroyWindow
#define UnregisterClassW fixture::unregisterClass
#define GetModuleHandle fixture::module
#define GetModuleHandleW fixture::module
#define GetProcAddress fixture::proc
#define RegisterClassEx fixture::registerClass
#define CreateWindowExW fixture::createWindow
#define RegisterWindowMessage fixture::registerMessage
#define timeEndPeriod fixture::endPeriod
#pragma warning(push)
// Unchanged vendor source uses UINT for SetTimer's return and shadows timer.
#pragma warning(disable: 4244 4459)
#include "SharedTimerWin.h"
namespace content {
#include "scheduler.inc"
}
#pragma warning(pop)

namespace fixture {
void resetCase(content::WebThreadImpl& thread) {
    thread.clear();
    // Consume queued requests through the actual window procedure before resetting counters.
    queueStatus = QS_MOUSEMOVE << 16;
    while (!messages.empty()) {
        auto message = messages.front(); messages.pop_front();
        content::TimerWindowWndProc(message.window, message.id, message.first, message.second);
    }
    queueStatus = 0; enters = 0; now = 100.0; step = 0;
    postAttempts = 0; runs = 0; destroyed = 0; failPost = false;
    thread.m_isMainThread = true; thread.m_willExit = false;
}
bool dispatchOne() {
    if (messages.empty()) return false;
    const auto message = messages.front(); messages.pop_front();
    content::TimerWindowWndProc(message.window, message.id, message.first, message.second);
    return true;
}
void addTasks(content::WebThreadImpl& thread, unsigned count) {
    for (unsigned i = 0; i < count; ++i) thread.add();
}
void drainMessages() {
    for (unsigned i = 0; i < 1024 && !messages.empty(); ++i) dispatchOne();
    expect(messages.empty(), "continuation chain is finite");
}
int run() {
    content::WebThreadImpl thread;
    blink::Platform::current()->owner = &thread;
    content::setSharedTimerFireInterval(0.016);
    expect(timerCalls == 1, "periodic fallback remains armed");

    resetCase(thread);
    addTasks(thread, 256);
    thread.schedulerTasks();
    expect(runs == 32, "first batch retains 32-task budget");
    expect(messages.size() == 1, "ready backlog posts one continuation without waiting for timer");
    thread.schedulerTasks();
    expect(messages.size() == 1, "repeated scheduling coalesces pending continuation");
    drainMessages();
    expect(runs == 256 && destroyed == 256, "all ready tasks complete and dispose exactly once before another tick");
    expect(now == 100.0, "no simulated timer time was advanced to drain ready chain");

    resetCase(thread);
    std::function<void()> replenish;
    replenish = [&] { if (runs < 128) thread.add(now, replenish); };
    thread.add(now, replenish);
    thread.schedulerTasks();
    expect(runs == 32 && messages.size() == 1, "self-replenishing chain yields with one continuation");
    queueStatus = QS_KEY << 16;
    dispatchOne();
    expect(runs == 32 && messages.empty(), "input arriving after post interrupts continuous ready chain");
    queueStatus = 0;
    content::TimerWindowWndProc(window, WM_TIMER, 1000, 0);
    drainMessages();
    expect(runs == 128 && destroyed == 128, "self-replenishing tasks complete exactly once");

    resetCase(thread);
    step = 0.001;
    addTasks(thread, 16);
    thread.schedulerTasks();
    expect(runs >= 2 && runs <= 3, "time budget is preserved");
    expect(messages.size() == 1, "time-budget exit also continues ready work");
    drainMessages();
    expect(runs == 16 && destroyed == 16, "time-limited chain completes once");

    const UINT nativeFlags[] = { QS_MOUSEMOVE, QS_MOUSEBUTTON, QS_KEY, QS_RAWINPUT, QS_PAINT, QS_TIMER, QS_HOTKEY };
    for (UINT flag : nativeFlags) {
        resetCase(thread);
        addTasks(thread, 70);
        thread.schedulerTasks();
        queueStatus = flag << 16; // Currently pending, even when LOWORD is zero.
        expect(dispatchOne(), "pending-native case has continuation to consume");
        expect(runs == 32 && messages.empty(), "pending native work gets the turn without immediate repost");
        const unsigned attemptsBeforePressure = postAttempts;
        for (unsigned i = 0; i < 64; ++i) content::requestSharedTimerContinuation();
        expect(postAttempts == attemptsBeforePressure && messages.empty(),
            "producers cannot refill posted wakes while native work is pending");
        queueStatus = 0;
        content::TimerWindowWndProc(window, WM_TIMER, 1000, 0);
        drainMessages();
        expect(runs == 70 && destroyed == 70, "fallback resumes work after native pressure clears");
    }

    resetCase(thread);
    addTasks(thread, 64);
    thread.schedulerTasks();
    queueStatus = QS_MOUSEMOVE; // Historical change only, no currently pending input.
    dispatchOne();
    expect(runs == 64, "LOWORD changes do not create a false pending-input stall");

    resetCase(thread);
    addTasks(thread, 70);
    thread.schedulerTasks();
    enters = 1;
    dispatchOne();
    expect(runs == 32 && messages.empty(), "nested delivery does not enter scheduler or immediately repost");
    enters = 0;
    content::TimerWindowWndProc(window, WM_TIMER, 1000, 0);
    expect(messages.size() == 1, "owner timer reopens wake gate after nested delivery");
    drainMessages();
    expect(runs == 70, "reentry recovery completes remaining work");

    resetCase(thread);
    addTasks(thread, 70);
    thread.schedulerTasks();
    thread.m_firingTimers = 1;
    dispatchOne();
    expect(runs == 32 && messages.empty(), "scheduler's own reentry guard also prevents nested execution");
    thread.m_firingTimers = 0;
    thread.schedulerTasks();
    drainMessages();
    expect(runs == 70, "scheduler reentry does not lose the next continuation");

    resetCase(thread);
    addTasks(thread, 64);
    thread.schedulerTasks();
    const Message stale = messages.front();
    dispatchOne();
    addTasks(thread, 64);
    thread.schedulerTasks();
    content::TimerWindowWndProc(stale.window, stale.id, stale.first, stale.second);
    expect(runs == 96 && messages.size() == 1, "stale ticket cannot consume a newer pending wake");
    const Message current = messages.front();
    content::TimerWindowWndProc(reinterpret_cast<HWND>(static_cast<UINT_PTR>(2)),
        current.id, current.first, current.second);
    expect(runs == 96 && messages.size() == 1, "wrong window cannot consume the current wake");
    dispatchOne();
    expect(runs == 128 && destroyed == 128, "current ticket remains usable after stale deliveries");

    resetCase(thread);
    failPost = true;
    addTasks(thread, 70);
    thread.schedulerTasks();
    expect(postAttempts == 1 && messages.empty(), "failed post leaves no queued request");
    failPost = false;
    thread.schedulerTasks();
    expect(messages.size() == 1, "failed post does not latch pending forever");
    drainMessages();

    resetCase(thread);
    addTasks(thread, 32);
    thread.add(101.0);
    thread.schedulerTasks();
    expect(runs == 32 && messages.empty(), "future-only remainder never requests immediate continuation");
    now = 101.0;
    thread.schedulerTasks();
    expect(runs == 33 && messages.empty(), "future task runs at deadline and empty heap does not spin");

    resetCase(thread);
    thread.m_isMainThread = false;
    addTasks(thread, 96);
    thread.schedulerTasks();
    expect(runs == 96 && messages.empty(), "worker keeps original throughput policy");
    resetCase(thread);
    addTasks(thread, 96);
    thread.schedulerTasks(false);
    expect(runs == 96 && messages.empty(), "pre-shutdown drain has no normal continuation");
    resetCase(thread);
    thread.m_willExit = true;
    addTasks(thread, 96);
    thread.schedulerTasks();
    expect(runs == 96 && messages.empty(), "exit drain keeps original policy");

    resetCase(thread);
    addTasks(thread, 70);
    thread.schedulerTasks();
    content::stopSharedTimer();
    dispatchOne();
    expect(runs == 32 && messages.empty(), "queued continuation after stop does not execute tasks");
    const unsigned attemptsBeforeStoppedBatch = postAttempts;
    thread.schedulerTasks();
    expect(postAttempts == attemptsBeforeStoppedBatch && messages.empty(), "stop prevents new continuation posts");
    thread.clear();
    std::printf("assertions=%d failures=%d mode=deterministic-production-source host-ui=false\n", assertions, failures);
    return failures ? 1 : 0;
}
}
int main() { return fixture::run(); }
