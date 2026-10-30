#ifndef content_browser_SharedTimerWin_h
#define content_browser_SharedTimerWin_h
#include "content/web_impl_win/WebThreadImpl.h"
#include "content/browser/CheckReEnter.h"
#include <mmsystem.h>
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
static bool continuationClosed = true, continuationDeferred = false, deadlineRetry = false, readyRetry = false;
static PTP_TIMER deadlineTimer = nullptr, retryTimer = nullptr;
static double armedDeadline = 0;
static bool sharedHighResolution = false;
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
static void sharedTimerFiredFunction(unsigned long long deadlineQpc = 0, unsigned long long qpcFrequency = 0);
static void setSharedTimerPrecision(bool required)
{
    if (required == sharedHighResolution) return;
    if (required) sharedHighResolution = ::timeBeginPeriod(1) == TIMERR_NOERROR;
    else { ::timeEndPeriod(1); sharedHighResolution = false; }
}

// Static DLL state plus a ticket: no task/owner pointer crosses this boundary.
static FILETIME sharedTimerDue(double seconds)
{
    ULARGE_INTEGER due;
    const double ticks = seconds > 0 ? ceil(seconds * 10000000.0) : 1.0;
    due.QuadPart = static_cast<ULONGLONG>(-static_cast<LONGLONG>(ticks < 9.0e18 ? ticks : 9.0e18));
    FILETIME result = { due.LowPart, due.HighPart };
    return result;
}
// Publication lock held. This timer owns only a retry, never Blink execution.
static void retrySharedTimerPublication()
{
    if (!retryTimer || continuationClosed) return;
    FILETIME due = sharedTimerDue(0.008);
    ::SetThreadpoolTimer(retryTimer, &due, 0, 0);
}
static VOID CALLBACK sharedDeadlineSignaled(PTP_CALLBACK_INSTANCE, PVOID, PTP_TIMER)
{
    UINT_PTR ticket = 0;
    HWND window = nullptr;
    UINT message = 0;
    {
        SharedTimerContinuationLock lock;
        if (continuationClosed || !deadlineTicket) return;
        ticket = deadlineTicket;
        window = continuationWindow; message = timerFiredMessage;
        if (::PostMessage(window, message, ticket, 0)) return;
        // Native SetTimer can also have failed. Retain explicit owner work
        // rather than claiming that an unverified fallback owns the deadline.
        continuationDeferred = true;
        deadlineRetry = true;
        retrySharedTimerPublication();
    }
    // No publication lock across a native call that may enter the owner. This
    // callback never drains or closes a timer. A callback queued before rearm
    // may publish the new ticket; the owner rechecks its monotonic deadline.
    if (::GetWindowThreadProcessId(window, nullptr) != ::GetCurrentThreadId())
        ::SendNotifyMessageW(window, message, ticket, 0);
}
static VOID CALLBACK sharedPublicationRetry(PTP_CALLBACK_INSTANCE, PVOID, PTP_TIMER)
{
    HWND window = nullptr;
    UINT message = 0;
    UINT_PTR ticket = 0;
    {
        SharedTimerContinuationLock lock;
        if (continuationClosed) return;
        if (readyRetry && !continuationDeferred) {
            if (pendingContinuation) return;
            if (continuationSerial == (std::numeric_limits<UINT_PTR>::max)()) return;
            ticket = pendingContinuation = ++continuationSerial;
            message = continuationMessage;
        } else if (deadlineTicket) {
            ticket = deadlineTicket; message = timerFiredMessage;
        } else return;
        window = continuationWindow;
        if (::PostMessageW(window, message, ticket, 0)) {
            if (message == continuationMessage) readyRetry = false;
            return;
        }
        if (message == continuationMessage) pendingContinuation = 0;
        retrySharedTimerPublication();
    }
}
static void cancelSharedTimerDeadline()
{
    {
        SharedTimerContinuationLock lock;
        deadlineTicket = 0; armedDeadline = 0;
        deadlineRetry = false;
        if (deadlineTimer) ::SetThreadpoolTimer(deadlineTimer, nullptr, 0, 0);
    }
    // Static publication state survives rearm; no per-deadline callback drain.
    setSharedTimerPrecision(false);
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
        if (::PostMessage(window, continuationMessage, ticket, 0)) { readyRetry = false; return true; }
    }
    // Cross-thread SendNotifyMessage returns without waiting for the owner and
    // does not depend on a free posted-message slot. Same-thread fallback stays
    // deferred, preserving postTask's asynchronous execution contract.
    if (::GetWindowThreadProcessId(window, nullptr) != ::GetCurrentThreadId()
        && ::SendNotifyMessageW(window, continuationMessage, ticket, 0)) return true;
    SharedTimerContinuationLock lock;
    if (pendingContinuation == ticket) {
        pendingContinuation = 0;
        readyRetry = true;
        retrySharedTimerPublication();
    }
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
        MSG quitMessage;
        if (::PeekMessageW(&quitMessage, nullptr, WM_QUIT, WM_QUIT, PM_NOREMOVE)) return 0;
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
static void sharedTimerFiredFunction(unsigned long long deadlineQpc, unsigned long long qpcFrequency)
{
    {
        SharedTimerContinuationLock lock;
        if (continuationClosed) return;
        continuationDeferred = false;
        readyRetry = false;
    }
    auto* thread = static_cast<WebThreadImpl*>(blink::Platform::current()->currentThread());
    if (thread) thread->fire(deadlineQpc, qpcFrequency);
}
static void serviceSharedTimerOwnerOpportunity(unsigned long long deadlineQpc = 0, unsigned long long qpcFrequency = 0)
{
    bool deferred = false;
    {
        SharedTimerContinuationLock lock;
        deferred = !continuationClosed && (continuationDeferred || deadlineRetry || readyRetry);
    }
    if (deferred && !CheckReEnter::getEnterCount()) {
        cancelSharedTimerDeadline();
        CheckReEnter::incrementEnterCount();
        sharedTimerFiredFunction(deadlineQpc, qpcFrequency);
        CheckReEnter::decrementEnterCount();
    }
}
static void createSharedDeadlineTimer()
{
    if (!deadlineTimer) deadlineTimer = ::CreateThreadpoolTimer(sharedDeadlineSignaled, nullptr, nullptr);
    if (!retryTimer) retryTimer = ::CreateThreadpoolTimer(sharedPublicationRetry, nullptr, nullptr);
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
    // Include30/60Hz frame deadlines; a16ms threshold would miss both and
    // regress the former high-resolution waitable-timer path on modern Windows.
    setSharedTimerPrecision(remaining > 0 && remaining <= 0.050);
    {
        SharedTimerContinuationLock lock;
        deadlineTicket = ticket; armedDeadline = deadline;
        // Immediate signal is serialized until the timer's deadline is retained.
        if (deadlineTimer) {
            FILETIME due = sharedTimerDue(remaining);
            ::SetThreadpoolTimer(deadlineTimer, &due, 0, 0);
        }
    }
    // Native fallback is needed only when the reusable timer cannot be created.
    // Deadline callback posting failure owns a separate reusable retry timer.
    if (!deadlineTimer && ::SetTimer(timerWindowHandle, ticket, fallback, nullptr)) timerID = ticket;
    else if (!deadlineTimer) {
        SharedTimerContinuationLock lock;
        armedDeadline = 0; deadlineTicket = 0;
        deadlineRetry = true; // Owner opportunity must retry future-only work too.
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
        pendingContinuation = 0; continuationDeferred = false; readyRetry = false;
    }
    cancelSharedTimerDeadline();
    // Closed publication state prevents callbacks from scheduling more work.
    // Draining takes no publication lock and happens before window destruction.
    if (deadlineTimer) {
        ::WaitForThreadpoolTimerCallbacks(deadlineTimer, TRUE);
        ::CloseThreadpoolTimer(deadlineTimer);
        deadlineTimer = nullptr;
    }
    if (retryTimer) {
        ::SetThreadpoolTimer(retryTimer, nullptr, 0, 0);
        ::WaitForThreadpoolTimerCallbacks(retryTimer, TRUE);
        ::CloseThreadpoolTimer(retryTimer);
        retryTimer = nullptr;
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
    createSharedDeadlineTimer();
    SharedTimerContinuationLock lock;
    continuationWindow = timerWindowHandle;
    continuationClosed = !continuationWindow || !continuationMessage || !timerFiredMessage;
    continuationDeferred = false;
}
} // namespace content
#endif // content_browser_SharedTimerWin_h
