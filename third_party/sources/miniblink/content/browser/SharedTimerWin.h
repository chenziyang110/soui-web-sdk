#ifndef content_browser_SharedTimerWin_h
#define content_browser_SharedTimerWin_h

#include "content/web_impl_win/WebThreadImpl.h"
#include "content/browser/CheckReEnter.h"
#include <mmsystem.h>
#include <limits>
#include <exception>
#include <stdexcept>
#include <math.h>

namespace content {

const LPCWSTR kTimerWindowClassName = L"MiniBlinkTimerWindowClass";
static HWND timerWindowHandle = nullptr;
static UINT timerFiredMessage;
static PTP_TIMER deadlineTimer;
static PTP_TIMER retryTimer;
static double armedDeadline;
static bool highResTimerActive;
static SRWLOCK timerPublicationLock = SRWLOCK_INIT;
static UINT_PTR wakeSerial;
static UINT_PTR pendingWake;
static UINT_PTR fallbackTimerID;
static bool wakeClosed = true;
static bool wakeDeferred;

class SharedTimerPublicationLock {
public:
    SharedTimerPublicationLock() { ::AcquireSRWLockExclusive(&timerPublicationLock); }
    ~SharedTimerPublicationLock() { ::ReleaseSRWLockExclusive(&timerPublicationLock); }
private:
    SharedTimerPublicationLock(const SharedTimerPublicationLock&) = delete;
    SharedTimerPublicationLock& operator=(const SharedTimerPublicationLock&) = delete;
};

static UINT_PTR nextWakeSerial()
{
    if (wakeSerial == (std::numeric_limits<UINT_PTR>::max)())
        std::terminate();
    return ++wakeSerial;
}

// Publication lock held. Native fallback IDs are also generation tickets: an
// already queued WM_TIMER cannot alias a later arm or a recreated window.
static void cancelFallbackTimer()
{
    if (fallbackTimerID)
        ::KillTimer(timerWindowHandle, fallbackTimerID);
    fallbackTimerID = 0;
    wakeDeferred = false;
}

static void armFallbackTimer(UINT due, bool deferProducers)
{
    cancelFallbackTimer();
    fallbackTimerID = nextWakeSerial();
    if (!::SetTimer(timerWindowHandle, fallbackTimerID, due ? due : 1, nullptr))
        std::terminate();
    wakeDeferred = deferProducers;
}

static FILETIME relativeTimerDue(DWORD milliseconds)
{
    ULARGE_INTEGER due;
    due.QuadPart = static_cast<ULONGLONG>(-static_cast<LONGLONG>(milliseconds ? milliseconds : 1) * 10000);
    FILETIME result = { due.LowPart, due.HighPart };
    return result;
}

// Cross-thread safe. No task, COM call or window callback executes under this lock.
static void requestSharedTimerContinuation()
{
    SharedTimerPublicationLock lock;
    if (wakeClosed || wakeDeferred || pendingWake)
        return;
    pendingWake = nextWakeSerial();
    if (!::PostMessageW(timerWindowHandle, timerFiredMessage, pendingWake, 0)) {
        pendingWake = 0;
        // SetTimer(HWND) belongs to the window owner, whereas this entry may run
        // on a producer or timer-queue thread. Retry publication from one held
        // timer object only on failure; no idle/normal periodic polling.
        FILETIME due = relativeTimerDue(8);
        ::SetThreadpoolTimer(retryTimer, &due, 0, 0);
    }
}

static void CALLBACK sharedTimerProc(PTP_CALLBACK_INSTANCE, PVOID, PTP_TIMER)
{
    requestSharedTimerContinuation(); // Timer thread only publishes; it never enters Blink.
}

static void setTimerPrecision(bool highResolution)
{
    if (highResolution == highResTimerActive)
        return;
    if (highResolution)
        highResTimerActive = ::timeBeginPeriod(1) == TIMERR_NOERROR;
    else {
        ::timeEndPeriod(1);
        highResTimerActive = false;
    }
}

static void sharedTimerFiredFunction()
{
    armedDeadline = 0;
    {
        SharedTimerPublicationLock lock;
        if (wakeClosed)
            return;
        cancelFallbackTimer();
    }
    content::WebThreadImpl* threadImpl = static_cast<content::WebThreadImpl*>(blink::Platform::current()->currentThread());
    threadImpl->fire();
}

static LRESULT CALLBACK TimerWindowWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_POWERBROADCAST && (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)) {
        requestSharedTimerContinuation(); // Re-evaluate monotonic deadlines after sleep.
        return TRUE;
    }
    if (timerFiredMessage && message == timerFiredMessage) {
        {
            SharedTimerPublicationLock lock;
            if (wakeClosed || window != timerWindowHandle || !pendingWake || wParam != pendingWake)
                return 0;
            pendingWake = 0;
            // Producers cannot continually refill posted messages ahead of native input.
            wakeDeferred = true;
        }
        const UINT nativeWork = QS_INPUT | QS_PAINT | QS_TIMER | QS_HOTKEY;
        MSG quitMessage;
        if (::PeekMessageW(&quitMessage, nullptr, WM_QUIT, WM_QUIT, PM_NOREMOVE))
            return 0; // Let the host consume quit; shutdown drains accepted work.
        if (CheckReEnter::getEnterCount() || (HIWORD(::GetQueueStatus(nativeWork)) & nativeWork)) {
            SharedTimerPublicationLock lock;
            armFallbackTimer(1, true);
            return 0;
        }
    } else if (message == WM_TIMER) {
        {
            SharedTimerPublicationLock lock;
            if (wakeClosed || window != timerWindowHandle || !fallbackTimerID || wParam != fallbackTimerID)
                return 0;
            cancelFallbackTimer(); // Every fallback is one-shot, never an idle tick.
        }
        if (CheckReEnter::getEnterCount()) {
            SharedTimerPublicationLock lock;
            armFallbackTimer(1, true);
            return 0;
        }
    } else
        return ::DefWindowProcW(window, message, wParam, lParam);

    CheckReEnter::incrementEnterCount();
    sharedTimerFiredFunction();
    CheckReEnter::decrementEnterCount();
    return 0;
}

static void initializeOffScreenTimerWindow()
{
    if (timerWindowHandle)
        return;
    timerFiredMessage = ::RegisterWindowMessageW(L"com.weolar.Miniblink.DeadlineWake");
    if (!timerFiredMessage)
        throw std::runtime_error("MiniBlink wake message registration failed");
    WNDCLASSEXW windowClass = { 0 };
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = TimerWindowWndProc;
    windowClass.hInstance = ::GetModuleHandleW(nullptr);
    windowClass.lpszClassName = kTimerWindowClassName;
    const ATOM registered = ::RegisterClassExW(&windowClass);
    if (!registered && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        throw std::runtime_error("MiniBlink timer window class registration failed");
    HWND window = ::CreateWindowExW(0, kTimerWindowClassName, L"OffScreenTimer", WS_POPUP,
        1, 1, 1, 1, nullptr, nullptr, windowClass.hInstance, nullptr);
    if (!window) {
        if (registered)
            ::UnregisterClassW(kTimerWindowClassName, windowClass.hInstance);
        throw std::runtime_error("MiniBlink timer window creation failed");
    }
    PTP_TIMER retry = ::CreateThreadpoolTimer(sharedTimerProc, nullptr, nullptr);
    if (!retry) {
        ::DestroyWindow(window);
        if (registered)
            ::UnregisterClassW(kTimerWindowClassName, windowClass.hInstance);
        throw std::runtime_error("MiniBlink wake retry timer creation failed");
    }
    SharedTimerPublicationLock lock;
    retryTimer = retry;
    timerWindowHandle = window;
    pendingWake = 0;
    wakeDeferred = false;
    wakeClosed = false;
}

// Owner-thread only. Zero means no future work. Immediate work uses the same
// coalesced publication path as cross-thread admission. All deadlines are monotonic.
static void setSharedTimerDeadline(double deadline)
{
    initializeOffScreenTimerWindow();
    if (deadline > 0 && deadline <= WTF::monotonicallyIncreasingTime()) {
        setTimerPrecision(false);
        requestSharedTimerContinuation();
        return;
    }
    if (deadline <= 0) {
        armedDeadline = 0;
        {
            SharedTimerPublicationLock lock;
            cancelFallbackTimer();
        }
        if (deadlineTimer)
            ::SetThreadpoolTimer(deadlineTimer, nullptr, 0, 0);
        setTimerPrecision(false);
        return;
    }
    if (deadline == armedDeadline)
        return;
    armedDeadline = deadline;
    {
        SharedTimerPublicationLock lock;
        cancelFallbackTimer();
    }
    const double remainingMs = (deadline - WTF::monotonicallyIncreasingTime()) * 1000.0;
    if (remainingMs <= 0) {
        setTimerPrecision(false);
        requestSharedTimerContinuation();
        return;
    }
    const DWORD due = remainingMs >= static_cast<double>(INFINITE - 1)
        ? INFINITE - 1 : static_cast<DWORD>(ceil(remainingMs));
    setTimerPrecision(due < 16);
    if (!deadlineTimer)
        deadlineTimer = ::CreateThreadpoolTimer(sharedTimerProc, nullptr, nullptr);
    if (deadlineTimer) {
        FILETIME dueTime = relativeTimerDue(due);
        ::SetThreadpoolTimer(deadlineTimer, &dueTime, 0, 0);
    } else {
        SharedTimerPublicationLock lock;
        armFallbackTimer(due, false);
    }
}

void stopSharedTimer()
{
    HWND retiredWindow;
    {
        SharedTimerPublicationLock lock;
        wakeClosed = true;
        wakeDeferred = false;
        pendingWake = 0;
        retiredWindow = timerWindowHandle;
        cancelFallbackTimer();
        timerWindowHandle = nullptr;
    }
    // Callback takes no owner resources and only briefly publishes under the lock
    // above. Drain outside that lock before destroying/reusing the native window.
    if (deadlineTimer) {
        ::SetThreadpoolTimer(deadlineTimer, nullptr, 0, 0);
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
    armedDeadline = 0;
    setTimerPrecision(false);
    if (retiredWindow) {
        ::DestroyWindow(retiredWindow);
        ::UnregisterClassW(kTimerWindowClassName, ::GetModuleHandleW(nullptr));
    }
}

} // namespace content

#endif // content_browser_SharedTimerWin_h
