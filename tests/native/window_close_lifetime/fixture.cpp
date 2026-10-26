// Regression SOURCE: production method bytes with deterministic native callouts.
#include "PaintLifetime.h"
#include <cstdio>
#include <cstddef>
#include <map>
#include <memory>

using HWND = void*;
using HANDLE = void*;
using UINT_PTR = uintptr_t;
using DWORD = unsigned long;
using BOOL = int;
constexpr BOOL TRUE = 1, FALSE = 0;
constexpr unsigned WM_NCDESTROY = 0x82;
namespace wke { class CWebView; class CWebWindow; void wkeDestroyWebView(CWebWindow*); }
namespace fixture {
enum class result { failure, success, deleted_but_false, identity_lost };
struct Window { bool alive = true; void* property = nullptr; };
std::map<HWND, Window> windows;
result next_result = result::failure;
unsigned native_calls = 0, destroyed_views = 0, failures = 0;
bool delete_on_nc = true;
void require(bool value) { if (!value) { ++failures; std::fputs("window close lifetime failure\n", stderr); } }
}
BOOL IsWindow(HWND window) { const auto found = fixture::windows.find(window); return found != fixture::windows.end() && found->second.alive; }
DWORD GetCurrentThreadId() { return 11; }
DWORD GetCurrentProcessId() { return 17; }
DWORD GetWindowThreadProcessId(HWND window, DWORD* process) { if (process) *process = 17; return IsWindow(window) ? 11 : 0; }
HANDLE GetPropW(HWND window, const wchar_t*) { return IsWindow(window) ? fixture::windows[window].property : nullptr; }
HANDLE RemovePropW(HWND window, const wchar_t*) { auto& entry = fixture::windows[window]; auto* previous = entry.property; entry.property = nullptr; return previous; }
BOOL KillTimer(HWND, UINT_PTR) { return TRUE; }
BOOL DestroyWindow(HWND window);

namespace wke {
class CWebView {
public:
    virtual ~CWebView() = default;
    bool takeWindowDestroyCallback();
    virtual bool prepareNativeWindowClose();
    std::shared_ptr<content::PaintLifetimeState> m_paintLifetime;
};
#include "window_helpers.inc"
class CWebWindow final : public CWebView {
public:
    enum State { kWkeWebWindowUninit, kWkeWebWindowInit, kWkeWebWindowDestroing, kWkeWebWindowDestroyed };
    HWND m_hWnd = nullptr;
    State m_state = kWkeWebWindowInit;
    explicit CWebWindow(HWND window)
    {
        m_hWnd = window;
        m_paintLifetime = std::make_shared<content::PaintLifetimeState>();
        m_paintLifetime->target = static_cast<CWebView*>(this);
        fixture::windows[window] = {true, this};
    }
    ~CWebWindow() override
    {
        ++fixture::destroyed_views;
        m_paintLifetime->policy.invalidate();
        m_paintLifetime->target = nullptr;
    }
    bool _destroyWindow();
    bool prepareNativeWindowClose() override { return _destroyWindow(); }
    int nativeDestroyed(HWND hwnd)
    {
        switch (WM_NCDESTROY) {
#include "native_receipt.inc"
        }
        return 0;
    }
};
#include "native_close.inc"
#include "callback_once.inc"
void wkeDestroyWebView(CWebWindow* view)
{
    // Native callout may delete its target, unless an outer view-close permit
    // already owns deletion. This fake does not reproduce the native method.
    if (fixture::delete_on_nc && !(view->m_paintLifetime->policy.callouts & 1U))
        delete view;
}
}
BOOL DestroyWindow(HWND window)
{
    ++fixture::native_calls;
    if (fixture::next_result == fixture::result::failure)
        return FALSE;
    if (fixture::next_result == fixture::result::identity_lost) {
        fixture::windows[window].property = reinterpret_cast<void*>(uintptr_t(0x999));
        return FALSE;
    }
    auto* view = static_cast<wke::CWebWindow*>(fixture::windows[window].property);
    view->nativeDestroyed(window); // Exact production NCDESTROY bytes can delete view here.
    fixture::windows[window].alive = false;
    return fixture::next_result == fixture::result::deleted_but_false ? FALSE : TRUE;
}
int main()
{
    using Kind = content::PaintCloseKind;
    auto& runtime = content::paintLifetimeRuntime();
    auto* view = new wke::CWebWindow(reinterpret_cast<HWND>(uintptr_t(0x101)));
    auto state = view->m_paintLifetime;
    {
        content::PaintClosePermit permit(state, Kind::view);
        fixture::require(bool(permit) && view->takeWindowDestroyCallback());
        fixture::require(!view->prepareNativeWindowClose());
        fixture::require(view->m_state == wke::CWebWindow::kWkeWebWindowInit);
        fixture::require(state->policy.nativeFailed && runtime.nativeFailures == 1 && runtime.pending == 1);
        fixture::require(!state->policy.reserveTask() && !runtime.drained());
    }
    fixture::next_result = fixture::result::success;
    {
        content::PaintClosePermit permit(state, Kind::view);
        fixture::require(bool(permit) && !view->takeWindowDestroyCallback());
        fixture::require(view->prepareNativeWindowClose()); // NC duplicate cannot delete under outer permit.
        fixture::require(!state->policy.dead && runtime.nativeFailures == 0);
        delete view;
        fixture::require(!runtime.drained()); // Outer close callout survives deletion.
    }
    fixture::require(runtime.drained() && fixture::destroyed_views == 1);

    view = new wke::CWebWindow(reinterpret_cast<HWND>(uintptr_t(0x102)));
    state = view->m_paintLifetime;
    fixture::next_result = fixture::result::deleted_but_false;
    {
        content::PaintClosePermit permit(state, Kind::window);
        fixture::require(view->_destroyWindow()); // Reentrant delete: exact NC receipt, no this access after call.
        fixture::require(state->policy.dead && runtime.nativeFailures == 0);
    }
    fixture::require(runtime.drained() && fixture::destroyed_views == 2);

    view = new wke::CWebWindow(reinterpret_cast<HWND>(uintptr_t(0x103)));
    state = view->m_paintLifetime;
    fixture::next_result = fixture::result::identity_lost;
    {
        content::PaintClosePermit permit(state, Kind::window);
        fixture::require(!view->_destroyWindow());
        fixture::require(state->policy.nativeFailed && !state->policy.nativeRetryable);
    }
    const auto calls = fixture::native_calls;
    {
        content::PaintClosePermit repeated(state, Kind::view);
        fixture::require(!repeated && fixture::native_calls == calls && !runtime.drained());
    }
    fixture::require(!state->policy.noteNativeDestroyed(0x104)); // Reused/other HWND cannot clear fault.
    fixture::windows[view->m_hWnd].property = view; // External owned-window recovery, not guessed by the close code.
    const auto window = view->m_hWnd;
    view->nativeDestroyed(window);
    fixture::windows[window].alive = false;
    fixture::require(runtime.drained() && fixture::destroyed_views == 3);
    return fixture::failures ? 1 : 0;
}
