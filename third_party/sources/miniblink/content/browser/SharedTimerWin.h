#ifndef content_browser_SharedTimerWin_h
#define content_browser_SharedTimerWin_h
#include "content/web_impl_win/WebThreadImpl.h"
#include "content/browser/CheckReEnter.h"
#include <limits>
#include <exception>
#include <math.h>
namespace content {
const LPCWSTR kTimerWindowClassName = L"MiniBlinkTimerWindowClass";
static HWND timerWindowHandle = nullptr, continuationWindow = nullptr;
static UINT continuationMessage = 0, timerFiredMessage = 0;
static SRWLOCK continuationLock = SRWLOCK_INIT;
static UINT_PTR continuationSerial = 0, pendingContinuation = 0;
static UINT_PTR nativeTimerSerial = 1001, timerID = 0, deadlineTicket = 0;
static bool continuationClosed = true, continuationDeferred = false;
static HANDLE deadlineTimer = nullptr, deadlineWait = nullptr;
static double armedDeadline = 0;
class SharedTimerContinuationLock {
public:
    SharedTimerContinuationLock() { ::AcquireSRWLockExclusive(&continuationLock); }
    ~SharedTimerContinuationLock() { ::ReleaseSRWLockExclusive(&continuationLock); }
private:
    SharedTimerContinuationLock(const SharedTimerContinuationLock&) = delete;
    SharedTimerContinuationLock& operator=(const SharedTimerContinuationLock&) = delete;
};
static void initializeOffScreenTimerWindow();
static void setSharedTimerFireDeadline(double);
static void sharedTimerFiredFunction();

// Static DLL state plus a ticket: no task/owner pointer crosses this boundary.
static VOID CALLBACK sharedDeadlineSignaled(PVOID parameter, BOOLEAN)
{
    const UINT_PTR ticket = reinterpret_cast<UINT_PTR>(parameter);
    HWND window = nullptr;
    UINT message = 0;
    {
        SharedTimerContinuationLock lock;
        if (continuationClosed || !ticket || ticket != deadlineTicket) return;
        window = continuationWindow; message = timerFiredMessage;
        if (::PostMessage(window, message, ticket, 0)) return;
        // Native SetTimer can also have failed. Retain explicit owner work
        // rather than claiming that an unverified fallback owns the deadline.
        continuationDeferred = true;
    }
    // No publication lock across a native call that may enter the owner. This
    // callback never unregisters its own wait or closes/rearms its timer.
    if (::GetWindowThreadProcessId(window, nullptr) != ::GetCurrentThreadId())
        ::SendNotifyMessageW(window, message, ticket, 0);
}
static void cancelSharedTimerDeadline()
{
    HANDLE wait = nullptr;
    {
        SharedTimerContinuationLock lock;
        deadlineTicket = 0; armedDeadline = 0;
        wait = deadlineWait; deadlineWait = nullptr;
    }
    // Callback takes publication lock. Wait outside it before HANDLE reuse.
    if (wait && !::UnregisterWaitEx(wait, INVALID_HANDLE_VALUE)) std::terminate();
    if (deadlineTimer) ::CancelWaitableTimer(deadlineTimer);
    if (timerID) { ::KillTimer(timerWindowHandle, timerID); timerID = 0; }
}
static bool requestSharedTimerContinuation()
{
    HWND window = nullptr;
    UINT_PTR ticket = 0;
    {
        SharedTimerContinuationLock lock;
        if (continuationClosed || continuationDeferred || !continuationWindow) return false;
        if (pendingContinuation) return true;
        if (continuationSerial == (std::numeric_limits<UINT_PTR>::max)()) return false;
        ticket = pendingContinuation = ++continuationSerial;
        window = continuationWindow;
        if (::PostMessage(window, continuationMessage, ticket, 0)) return true;
    }
    // Cross-thread SendNotifyMessage returns without waiting for the owner and
    // does not depend on a free posted-message slot. Same-thread fallback stays
    // deferred, preserving postTask's asynchronous execution contract.
    if (::GetWindowThreadProcessId(window, nullptr) != ::GetCurrentThreadId()
        && ::SendNotifyMessageW(window, continuationMessage, ticket, 0)) return true;
    SharedTimerContinuationLock lock;
    if (pendingContinuation == ticket) pendingContinuation = 0;
    return false;
}
static void armSharedTimerRecovery()
{
    // Only unresolved work owns this low-priority compatibility opportunity.
    setSharedTimerFireDeadline(monotonicallyIncreasingTime() + 0.010);
}
static LRESULT CALLBACK TimerWindowWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (continuationMessage && message == continuationMessage) {
        {
            SharedTimerContinuationLock lock;
            if (continuationClosed || hWnd != continuationWindow || !pendingContinuation || wParam != pendingContinuation) return 0;
            pendingContinuation = 0; continuationDeferred = true;
        }
        const UINT nativeWork = QS_INPUT | QS_PAINT | QS_TIMER | QS_HOTKEY;
        if (CheckReEnter::getEnterCount() || (HIWORD(::GetQueueStatus(nativeWork)) & nativeWork)) {
            armSharedTimerRecovery(); return 0;
        }
    } else if (message == timerFiredMessage || message == WM_TIMER) {
        {
            SharedTimerContinuationLock lock;
            if (continuationClosed || hWnd != continuationWindow || !deadlineTicket || wParam != deadlineTicket) return 0;
        }
        if (CheckReEnter::getEnterCount()) { armSharedTimerRecovery(); return 0; }
        if (armedDeadline > monotonicallyIncreasingTime()) {
            const double deadline = armedDeadline;
            cancelSharedTimerDeadline(); setSharedTimerFireDeadline(deadline); return 0;
        }
    } else return DefWindowProc(hWnd, message, wParam, lParam);
    cancelSharedTimerDeadline();
    CheckReEnter::incrementEnterCount();
    sharedTimerFiredFunction();
    CheckReEnter::decrementEnterCount();
    return 0;
}
static void sharedTimerFiredFunction()
{
    {
        SharedTimerContinuationLock lock;
        if (continuationClosed) return;
        continuationDeferred = false;
    }
    auto* thread = static_cast<WebThreadImpl*>(blink::Platform::current()->currentThread());
    if (thread) thread->fire();
}
static void serviceSharedTimerOwnerOpportunity()
{
    bool deferred = false;
    {
        SharedTimerContinuationLock lock;
        deferred = !continuationClosed && continuationDeferred;
    }
    if (deferred && !CheckReEnter::getEnterCount()) {
        cancelSharedTimerDeadline();
        CheckReEnter::incrementEnterCount();
        sharedTimerFiredFunction();
        CheckReEnter::decrementEnterCount();
    }
}
static void createSharedDeadlineTimer()
{
    if (deadlineTimer) return;
    typedef HANDLE (WINAPI* CreateTimerEx)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
    const auto createTimerEx = reinterpret_cast<CreateTimerEx>(::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "CreateWaitableTimerExW"));
    // Unsupported flag on Win7 fails, then uses its compatible waitable timer.
    if (createTimerEx) deadlineTimer = createTimerEx(nullptr, nullptr, 0x00000002, TIMER_ALL_ACCESS);
    if (!deadlineTimer) deadlineTimer = ::CreateWaitableTimerW(nullptr, FALSE, nullptr);
}
static void setSharedTimerFireDeadline(double deadline)
{
    initializeOffScreenTimerWindow();
    if (continuationClosed || !timerWindowHandle) return;
    if (deadline > 0 && deadline == armedDeadline && deadlineTicket) return;
    cancelSharedTimerDeadline();
    if (!(deadline > 0) || nativeTimerSerial == (std::numeric_limits<UINT_PTR>::max)()) return;
    const UINT_PTR ticket = ++nativeTimerSerial;
    createSharedDeadlineTimer();
    const double remaining = deadline - monotonicallyIncreasingTime();
    const double milliseconds = remaining > 0 ? ceil(remaining * 1000.0) : 1.0;
    const UINT fallback = milliseconds >= 0x7fffffff ? 0x7fffffff : static_cast<UINT>(milliseconds);
    {
        SharedTimerContinuationLock lock;
        deadlineTicket = ticket; armedDeadline = deadline;
        // Immediate signal is serialized until the registered wait is retained.
        if (deadlineTimer) {
            LARGE_INTEGER due;
            const double ticks = remaining > 0 ? ceil(remaining * 10000000.0) : 1.0;
            due.QuadPart = -static_cast<LONGLONG>(ticks < 9.0e18 ? ticks : 9.0e18);
            if (::SetWaitableTimer(deadlineTimer, &due, 0, nullptr, nullptr, FALSE))
                ::RegisterWaitForSingleObject(&deadlineWait, deadlineTimer, sharedDeadlineSignaled,
                    reinterpret_cast<PVOID>(ticket), INFINITE, WT_EXECUTEONLYONCE);
        }
    }
    // Ticketed native timer covers wait creation/registration/post failure.
    if (::SetTimer(timerWindowHandle, ticket, fallback, nullptr)) timerID = ticket;
    else if (!deadlineWait) {
        SharedTimerContinuationLock lock;
        armedDeadline = 0; deadlineTicket = 0;
    }
}
void setSharedTimerFireInterval(double interval)
{
    setSharedTimerFireDeadline(interval >= 0 ? monotonicallyIncreasingTime() + interval : 0);
}
void stopSharedTimer()
{
    {
        SharedTimerContinuationLock lock;
        continuationClosed = true; continuationWindow = nullptr;
        pendingContinuation = 0; continuationDeferred = false;
    }
    cancelSharedTimerDeadline();
    if (deadlineTimer) {
        if (!::CloseHandle(deadlineTimer)) std::terminate();
        deadlineTimer = nullptr; // Failed close retains the only reference.
    }
    const HWND closed = timerWindowHandle; timerWindowHandle = nullptr;
    if (closed) ::DestroyWindow(closed);
    ::UnregisterClassW(kTimerWindowClassName, nullptr);
}
static void initializeOffScreenTimerWindow()
{
    if (timerWindowHandle) return;
    WNDCLASSEX windowClass = { 0 };
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = TimerWindowWndProc;
    windowClass.hInstance = ::GetModuleHandleW(nullptr);
    windowClass.lpszClassName = kTimerWindowClassName;
    ::RegisterClassExW(&windowClass);
    timerWindowHandle = ::CreateWindowExW(0, kTimerWindowClassName, L"OffScreenTimer", WS_POPUP,
        1, 1, 1, 1, nullptr, nullptr, windowClass.hInstance, nullptr);
    timerFiredMessage = ::RegisterWindowMessageW(L"com.weolar.Miniblink.DeadlineReady.v1");
    continuationMessage = ::RegisterWindowMessageW(L"com.weolar.Miniblink.ReadyTaskContinuation");
    SharedTimerContinuationLock lock;
    continuationWindow = timerWindowHandle;
    continuationClosed = !continuationWindow || !continuationMessage || !timerFiredMessage;
    continuationDeferred = false;
}
} // namespace content
#endif // content_browser_SharedTimerWin_h
