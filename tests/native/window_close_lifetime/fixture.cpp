// Regression SOURCE: production method bytes with deterministic native callouts.
#include "PaintLifetime.h"
#include <cstdio>
#include <cstddef>
#include <map>
#include <memory>
#include <functional>

using HWND = void*;
using HANDLE = void*;
using UINT_PTR = uintptr_t;
using DWORD = unsigned long;
using BOOL = int;
using UINT = unsigned; using WPARAM = uintptr_t; using LPARAM = intptr_t; using LRESULT = intptr_t;
struct RECT { int left, top, right, bottom; };
constexpr UINT WM_SHOWWINDOW = 0x18, WM_WINDOWPOSCHANGED = 0x47, WM_SIZE = 5;
constexpr WPARAM SIZE_MINIMIZED = 1, SIZE_RESTORED = 0;
constexpr int SW_SHOW = 5, SW_HIDE = 0, GA_ROOTOWNER = 3;
constexpr BOOL TRUE = 1, FALSE = 0;
constexpr unsigned WM_NCDESTROY = 0x82;
namespace wke { class CWebView; class CWebWindow; void wkeDestroyWebView(CWebWindow*); void wkeRepaintIfNeeded(CWebWindow*); }
namespace fixture {
enum class result { failure, success, deleted_but_false, identity_lost, another_attached_window };
struct Window { bool alive = true; void* property = nullptr; bool visible = true, iconic = false; HWND root = nullptr; };
struct Page {
    unsigned calls = 0; bool visible = true, accept = true;
    Page* webPageImpl() { return this; }
    Page* layerTreeHost() { return this; }
    bool hostVisible() const { return visible; }
    bool setHostVisible(bool value) { ++calls; if (!accept) return false; visible = value; return true; }
};
DWORD thread_id = 11;
unsigned show_calls = 0, repaints = 0;
std::function<void()> inside_show;
std::map<HWND, Window> windows;
result next_result = result::failure;
unsigned native_calls = 0, destroyed_views = 0, failures = 0;
bool delete_on_nc = true;
void require(bool value) { if (!value) { ++failures; std::fputs("window close lifetime failure\n", stderr); } }
}
BOOL IsWindow(HWND window) { const auto found = fixture::windows.find(window); return found != fixture::windows.end() && found->second.alive; }
DWORD GetCurrentThreadId() { return fixture::thread_id; }
DWORD GetCurrentProcessId() { return 17; }
DWORD GetWindowThreadProcessId(HWND window, DWORD* process) { if (process) *process = 17; return IsWindow(window) ? 11 : 0; }
HANDLE GetPropW(HWND window, const wchar_t*) { return IsWindow(window) ? fixture::windows[window].property : nullptr; }
HANDLE RemovePropW(HWND window, const wchar_t*) { auto& entry = fixture::windows[window]; auto* previous = entry.property; entry.property = nullptr; return previous; }
BOOL KillTimer(HWND, UINT_PTR) { return TRUE; }
BOOL DestroyWindow(HWND window);
BOOL IsWindowVisible(HWND window) { return IsWindow(window) && fixture::windows[window].visible; }
BOOL IsIconic(HWND window) { return IsWindow(window) && fixture::windows[window].iconic; }
HWND GetAncestor(HWND window, unsigned) { const HWND root = fixture::windows[window].root; return root ? root : window; }
BOOL GetClientRect(HWND, RECT* rect) { *rect = {0, 0, 100, 100}; return TRUE; }
LRESULT DefWindowProcW(HWND, UINT, WPARAM, LPARAM) { return 55; }
BOOL ShowWindow(HWND window, int command);

namespace wke {
DWORD wkeThreadId = 11;
class CWebView {
public:
    virtual ~CWebView() = default;
    fixture::Page page; fixture::Page* m_webPage = &page;
    unsigned resizes = 0;
    void resize(int, int) { ++resizes; }
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
    bool m_hostVisibilityKnown = false, m_hostVisible = true;
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
    bool _syncHostVisibility(HWND, bool forceHidden = false);
    void show(bool);
    LRESULT visibilityMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam = 0)
    {
        switch (message) {
#include "visibility_messages.inc"
#include "resize_visibility.inc"
        }
        return 0;
    }
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
#include "window_visibility.inc"
void wkeRepaintIfNeeded(CWebWindow*) { ++fixture::repaints; }
void wkeDestroyWebView(CWebWindow* view)
{
    // Native callout may delete its target, unless an outer view-close permit
    // already owns deletion. This fake does not reproduce the native method.
    if (fixture::delete_on_nc && !(view->m_paintLifetime->policy.callouts & 1U))
        delete view;
}
}
BOOL ShowWindow(HWND window, int command)
{
    ++fixture::show_calls;
    auto* view = static_cast<wke::CWebWindow*>(fixture::windows[window].property);
    // Deliberately deliver SHOWWINDOW before style mutation; final native
    // WINDOWPOS processing must sample the actual state without a posted task.
    view->visibilityMessage(window, WM_SHOWWINDOW, command != SW_HIDE);
    fixture::windows[window].visible = command != SW_HIDE;
    view->visibilityMessage(window, WM_WINDOWPOSCHANGED, 0);
    if (fixture::inside_show) fixture::inside_show();
    return TRUE;
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
    if (fixture::next_result == fixture::result::another_attached_window) {
        view->m_hWnd = reinterpret_cast<HWND>(uintptr_t(0x105));
        fixture::windows[view->m_hWnd] = {true, view};
    }
    view->nativeDestroyed(window); // Exact production NCDESTROY bytes can delete view here.
    fixture::windows[window].alive = false;
    return fixture::next_result == fixture::result::deleted_but_false ? FALSE : TRUE;
}
static void visibilityCases()
{
    using Kind = content::PaintCloseKind;
    auto& runtime = content::paintLifetimeRuntime();
    const HWND window = reinterpret_cast<HWND>(uintptr_t(0x201));
    const HWND root = reinterpret_cast<HWND>(uintptr_t(0x202));
    auto* view = new wke::CWebWindow(window);
    const auto state = view->m_paintLifetime;
    view->show(false);
    fixture::require(!view->page.visible && view->page.calls == 1);
    view->show(true);
    fixture::require(view->page.visible && view->page.calls == 2);
    view->visibilityMessage(window, WM_WINDOWPOSCHANGED, 0);
    fixture::require(view->page.calls == 2); // Only acknowledged same-state work is coalesced.
    view->page.visible = false; // Another owner API changed actual eligibility.
    fixture::require(view->_syncHostVisibility(window) && view->page.visible && view->page.calls == 3);
    fixture::windows[root] = {true, nullptr}; fixture::windows[root].iconic = true;
    fixture::windows[window].root = root;
    view->visibilityMessage(window, WM_SIZE, SIZE_RESTORED);
    fixture::require(!view->page.visible && view->resizes == 1);
    view->visibilityMessage(window, WM_SIZE, SIZE_MINIMIZED);
    fixture::require(!view->page.visible && view->resizes == 2);
    fixture::windows[root].iconic = false;
    view->visibilityMessage(window, WM_SIZE, SIZE_RESTORED);
    fixture::require(view->page.visible && view->resizes == 3 && fixture::repaints == 3);
    view->page.accept = false;
    fixture::require(!view->_syncHostVisibility(window, true) && view->m_hostVisible);
    const unsigned rejected = view->page.calls;
    view->page.accept = true;
    fixture::require(view->_syncHostVisibility(window, true) && !view->page.visible
        && view->page.calls == rejected + 1);
    fixture::require(view->_syncHostVisibility(window) && view->page.visible);
    const unsigned beforeIdentity = view->page.calls;
    fixture::windows[window].property = reinterpret_cast<void*>(uintptr_t(0x999));
    fixture::require(!view->_syncHostVisibility(window) && view->page.calls == beforeIdentity);
    fixture::windows[window].property = view;
    const unsigned showCalls = fixture::show_calls;
    fixture::thread_id = 12; view->show(false); fixture::thread_id = 11;
    fixture::require(fixture::show_calls == showCalls && state->policy.active == 0);
    unsigned atClose = 0;
    fixture::inside_show = [&] {
        atClose = view->page.calls;
        content::PaintClosePermit pending(state, Kind::view);
        fixture::require(!pending && state->policy.active > 0 && !state->policy.dead);
    };
    view->show(true);
    fixture::inside_show = {};
    fixture::require(state->policy.ready() && !state->policy.active && view->page.calls == atClose);
    fixture::next_result = fixture::result::success;
    {
        content::PaintClosePermit close(state, Kind::view);
        fixture::require(bool(close) && view->prepareNativeWindowClose());
        delete view;
    }
    fixture::require(runtime.drained() && state->policy.dead);
}

int main()
{
    using Kind = content::PaintCloseKind;
    auto& runtime = content::paintLifetimeRuntime();
    visibilityCases();
    const unsigned visibilityDestroyed = fixture::destroyed_views;
    fixture::next_result = fixture::result::failure;
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
    fixture::require(runtime.drained() && fixture::destroyed_views == visibilityDestroyed + 1);

    view = new wke::CWebWindow(reinterpret_cast<HWND>(uintptr_t(0x102)));
    state = view->m_paintLifetime;
    fixture::next_result = fixture::result::deleted_but_false;
    {
        content::PaintClosePermit permit(state, Kind::window);
        fixture::require(view->_destroyWindow()); // Reentrant delete: exact NC receipt, no this access after call.
        fixture::require(state->policy.dead && runtime.nativeFailures == 0);
    }
    fixture::require(runtime.drained() && fixture::destroyed_views == visibilityDestroyed + 2);

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
    fixture::require(runtime.drained() && fixture::destroyed_views == visibilityDestroyed + 3);

    view = new wke::CWebWindow(reinterpret_cast<HWND>(uintptr_t(0x104)));
    state = view->m_paintLifetime;
    fixture::next_result = fixture::result::another_attached_window;
    {
        content::PaintClosePermit permit(state, Kind::view);
        fixture::require(!view->_destroyWindow());
        fixture::require(view->m_hWnd == reinterpret_cast<HWND>(uintptr_t(0x105)));
        fixture::require(!state->policy.dead && state->policy.nativeFailed && !state->policy.nativeRetryable);
        fixture::require(fixture::destroyed_views == visibilityDestroyed + 3 && !runtime.drained());
    }
    // Permanent unknown ownership is an expected negative result. Teardown
    // only the fake world; do not manufacture a native success/drain receipt.
    fixture::require(!runtime.drained() && state->policy.nativeFailed);
    delete view;
    return fixture::failures ? 1 : 0;
}
