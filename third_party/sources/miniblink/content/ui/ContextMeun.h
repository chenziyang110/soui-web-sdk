#ifndef content_browser_ContextMeun_h
#define content_browser_ContextMeun_h

#include "content/browser/WebPage.h"
#include "content/web_impl_win/WebThreadImpl.h"
#include "third_party/WebKit/Source/web/WebViewImpl.h"
#include "third_party/WebKit/Source/platform/Timer.h"
#include "third_party/WebKit/Source/wtf/ThreadingPrimitives.h"
#include "third_party/WebKit/public/web/WebContextMenuData.h"
#include "third_party/WebKit/public/web/WebFrame.h"
#include "wke/wkeGlobalVar.h"
#include "wke/wkeWebView.h"
#include "wke/CursorDescriptorBroker.h"
#include <atomic>
#include <functional>
#include <memory>
#include <utility>
#include <windows.h>

namespace content {

#define kContextMenuClassName L"MbContextMenu"

class ContextMenu : public std::enable_shared_from_this<ContextMenu> {
public:
    static std::shared_ptr<ContextMenu> create(WebPage* webPage)
    {
        std::shared_ptr<ContextMenu> menu(new ContextMenu(webPage));
        if (menu->m_broker) {
            std::weak_ptr<ContextMenu> weakMenu(menu);
            if (!wke::brokerAddMenuActions(menu->m_view, [weakMenu] {
                if (std::shared_ptr<ContextMenu> menu = weakMenu.lock())
                    menu->requestClose();
            }, [weakMenu] {
                if (std::shared_ptr<ContextMenu> menu = weakMenu.lock())
                    menu->cancelIdentity();
            })) {
                menu->m_closing = true;
                wke::brokerRecordFailure(menu->m_view);
                return menu; // No half registration and no UI resource.
            }
        }
        menu->asyncCallUiThread([menu] { menu->initImpl(); });
        return menu;
    }

private:
    explicit ContextMenu(WebPage* webPage)
    {
        m_popMenu = nullptr;
        m_hWnd = nullptr;
        m_webPage = webPage;
        m_view = webPage->wkeWebView();
        m_broker = wke::brokerOwnsView(m_view);

        m_uiCallback = wke::g_wkeUiThreadPostTaskCallback;
        if (!m_uiCallback)
            m_uiCallback = onUiThreadCallback;

    }

public:
    void initImpl()
    {
        if (m_hWnd || m_closing.load())
            return;

        registerClass();
        m_windowKeepAlive = shared_from_this();
        m_hWnd = CreateWindowExW(WS_EX_TOOLWINDOW, kContextMenuClassName, kContextMenuClassName, WS_POPUP, 
            0, 0, 1, 1, HWND_DESKTOP, NULL, nullptr, this);
        if (!m_hWnd) {
            m_windowKeepAlive.reset();
            if (m_broker)
                wke::brokerRecordFailure(m_view);
            return;
        }
        // Retain storage even if a violated factory/authority contract cannot
        // be registered. That fault cannot become a clean drain receipt.
        if (m_broker) {
            m_windowHeld = wke::brokerHoldNativeResource(m_view, wke::BrokerNativeWindow, m_hWnd);
            if (!m_windowHeld) {
                wke::brokerRecordFailure(m_view);
                return;
            }
        }
        // The native window owns its callback target until UI destruction.
        if (!::SetPropW(m_hWnd, kContextMenuClassName, (HANDLE)this)) {
            if (m_broker) wke::brokerRecordFailure(m_view);
            return; // Owned resource ledger survives property admission failure.
        }
        ::SetForegroundWindow(m_hWnd);
        ::ShowWindow(m_hWnd, SW_SHOW);
    }

    ~ContextMenu()
    {
        // requestClose is scheduled before the engine owner releases us.
        // A failed UI teardown remains a failed drain, never an engine delete.
        if (m_broker && (m_hWnd || m_popMenu))
            wke::brokerRecordFailure(m_view);
    }

    void requestClose()
    {
        m_closing = true;
        queueCleanup(false);
    }

    void cancelIdentity()
    {
        std::shared_ptr<ContextMenu> self = shared_from_this();
        if (!wke::brokerPostUiCancellation(m_view, 0, [self](bool cancelled) {
            if (!cancelled && self->m_tracking)
                ::EndMenu();
        }) && !wke::brokerUiCancelled(m_view))
            wke::brokerRecordFailure(m_view);
    }

    void queueCleanup(bool fromUi) noexcept
    {
        if (m_cleanupPosted.exchange(true)) return;
        auto attempt = m_cleanupAttempts.load();
        while (attempt < 4 && !m_cleanupAttempts.compare_exchange_weak(attempt, attempt + 1)) { }
        if (attempt >= 4) { // Initial + three retries, saturated rather than wrap.
            m_cleanupPosted = false;
            return;
        }
        try {
            std::shared_ptr<ContextMenu> self = shared_from_this();
            std::function<void(bool)> cleanup = [self](bool cancelled) {
                bool retry = false;
                try {
                    if (cancelled) {
                        wke::brokerRecordFailure(self->m_view);
                    } else if (self->m_tracking) {
                        if (!::EndMenu() && self->m_broker)
                            wke::brokerRecordFailure(self->m_view);
                    } else {
                        retry = !self->destroyNativeResources();
                    }
                } catch (...) {
                    retry = true;
                    try { if (self->m_broker) wke::brokerRecordFailure(self->m_view); } catch (...) { }
                }
                self->m_cleanupPosted = false;
                if (retry) self->queueCleanup(true);
            };
            const bool posted = m_broker
                ? (fromUi ? wke::brokerPostUiCleanupRetry(m_view, std::move(cleanup))
                    : wke::brokerPostUiTask(m_view, WKE_CURSOR_BROKER_NATIVE_MENU, std::move(cleanup), true))
                : asyncCallUiThread([cleanup] { cleanup(false); }, true);
            if (!posted) {
                m_cleanupPosted = false;
                if (m_broker) wke::brokerRecordFailure(m_view);
            }
        } catch (...) {
            m_cleanupPosted = false;
            try { if (m_broker) wke::brokerRecordFailure(m_view); } catch (...) { }
        }
    }

    bool destroyNativeResources()
    {
        if (m_tracking)
            return false;
        if (m_hWnd) {
            const HWND exactWindow = m_hWnd;
            m_destroyingWindow = true;
            m_ncDestroyObserved = false;
            const BOOL destroyed = ::DestroyWindow(exactWindow);
            m_destroyingWindow = false;
            if (!destroyed || !m_ncDestroyObserved) {
                if (m_broker)
                    wke::brokerRecordFailure(m_view);
                return false; // Neither disappearance nor failed flag is proof.
            }
            if (m_windowHeld && !wke::brokerReleaseNativeResource(m_view, wke::BrokerNativeWindow, exactWindow))
                return false;
            m_windowHeld = false;
            m_hWnd = nullptr;
        }
        if (m_popMenu) {
            if (!destroyOwnedMenu()) {
                if (m_broker)
                    wke::brokerRecordFailure(m_view);
                return false;
            }
        }
        m_windowKeepAlive.reset();
        return true;
    }

    bool destroyOwnedMenu()
    {
        const HMENU exactMenu = m_popMenu;
        if (!exactMenu) return true;
        if (!::DestroyMenu(exactMenu)) return false;
        if (m_menuHeld && !wke::brokerReleaseNativeResource(m_view, wke::BrokerNativeMenu, exactMenu))
            return false;
        m_menuHeld = false;
        m_popMenu = nullptr;
        return true;
    }

    bool registerClass()
    {
        WNDCLASSEX wcex;

        wcex.cbSize = sizeof(WNDCLASSEX);
        wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
        wcex.lpfnWndProc = wndProc;
        wcex.cbClsExtra = 0;
        wcex.cbWndExtra = 0;
        wcex.hInstance = nullptr;
        wcex.hIcon = nullptr;
        wcex.hCursor = LoadCursor(0, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcex.lpszMenuName = 0;
        wcex.lpszClassName = kContextMenuClassName;
        wcex.hIconSm = nullptr;
        return !!RegisterClassEx(&wcex);
    }

    enum MenuId {
        kSelectedAllId = 1 << 1,
        kSelectedTextId = 1 << 2,
        kUndoId = 1 << 3,
        kCopyImageId = 1 << 4,
        kInspectElementAtId = 1 << 5,
        kCutId = 1 << 6,
        kPasteId = 1 << 7,
        kPrintId = 1 << 8,
        kGoForwardId = 1 << 9,
        kGoBackId = 1 << 10,
        kReloadId = 1 << 11,
    };

    static int WKE_CALL_TYPE onUiThreadCallback(HWND hWnd, wkeUiThreadRunCallback callback, void* param)
    {
        callback(hWnd, param);
        return 0;
    }

    bool asyncCallUiThread(std::function<void()>&& func, bool terminal = false) try
    {
        if (m_broker) {
            const bool posted = wke::brokerPostUiTask(m_view, WKE_CURSOR_BROKER_NATIVE_MENU,
                [func = std::move(func)](bool cancelled) {
                    if (!cancelled)
                        func();
                }, terminal);
            if (!posted && (terminal || !wke::brokerClosing(m_view)))
                wke::brokerRecordFailure(m_view);
            return posted;
        }
        HWND hWnd = m_webPage->getHWND();
        m_uiCallback(hWnd, UiTaskCall::asyncFunc, new UiTaskCall(std::move(func)));
        return true;
    }
    catch (...) {
        try { if (m_broker) wke::brokerRecordFailure(m_view); } catch (...) { }
        return false;
    }

    class UiTaskCall {
    public:
        explicit UiTaskCall(std::function<void()>&& func)
            : m_func(std::move(func)) {}

        ~UiTaskCall() {}
        
    private:
        std::function<void()> m_func;

    public:
        static void WKE_CALL_TYPE asyncFunc(HWND hWnd, void* param)
        {
            std::unique_ptr<UiTaskCall> self(static_cast<UiTaskCall*>(param));
            (self->m_func)();
        }
    };

    struct CommandSnapshot {
        int64_t frameId;
        blink::IntPoint imagePos;
        blink::IntPoint mousePosition;
        std::function<bool()> cancelled;
    };

    class LegacyCommandTask : public blink::WebThread::Task {
    public:
        LegacyCommandTask(std::shared_ptr<ContextMenu> owner, UINT itemID, const CommandSnapshot& snapshot)
            : m_owner(std::move(owner)), m_itemID(itemID), m_snapshot(snapshot) {}
        void run() override { m_owner->onCommand(m_itemID, m_snapshot); }
    private:
        std::shared_ptr<ContextMenu> m_owner;
        UINT m_itemID;
        CommandSnapshot m_snapshot;
    };

    class TrackingGuard {
    public:
        explicit TrackingGuard(ContextMenu* owner) : m_owner(owner) { owner->m_tracking = true; }
        ~TrackingGuard()
        {
            m_owner->m_tracking = false;
            if (m_owner->m_closing.load() && !m_owner->destroyNativeResources())
                m_owner->queueCleanup(true);
        }
    private:
        ContextMenu* m_owner;
    };

    void show(const blink::WebContextMenuData& data, int64_t frameId)
    {
        if (m_closing.load())
            return;
        std::shared_ptr<ContextMenu> self = shared_from_this();
        const CommandSnapshot snapshot = { frameId,
            blink::IntPoint(data.mousePosition), blink::IntPoint(data.mousePosition),
            m_broker ? wke::brokerUiCancelPredicate(m_view) : std::function<bool()>() };

        UINT actionFlags = 0;
        if ((!data.selectedText.isNull() && !data.selectedText.isEmpty()))
            actionFlags |= kSelectedTextId;

        if (data.hasImageContents) {
            actionFlags |= kCopyImageId;
        }

        if (m_webPage->isDevtoolsConneted())
            actionFlags |= kInspectElementAtId;

        if (data.isEditable) {
            actionFlags |= kCutId;
            actionFlags |= kPasteId;
            actionFlags |= kSelectedAllId;
            actionFlags |= kUndoId;
        }

        if (m_webPage->canGoForward())
            actionFlags |= kGoForwardId;
        if (m_webPage->canGoBack())
            actionFlags |= kGoBackId;
        actionFlags |= kReloadId;

        bool needCreatePrintItem = false;
        wkeOnContextMenuItemClickCallback clickCallback = m_webPage->wkeHandler().contextMenuItemClickCallback;
        void* callbackParam = m_webPage->wkeHandler().contextMenuItemClickCallbackParam;
        if (clickCallback)
            needCreatePrintItem = clickCallback(m_webPage->wkeWebView(), callbackParam, kWkeContextMenuItemClickTypePrint, kWkeContextMenuItemClickStepShow, (wkeWebFrameHandle)frameId, nullptr);
        if (needCreatePrintItem)
            actionFlags |= kPrintId;
        actionFlags &= wke::g_contextMenuItemMask;

        asyncCallUiThread([self, actionFlags, snapshot] {
            if (self->m_closing.load())
                return;
            self->initImpl();
            self->showImpl(actionFlags, snapshot);
        });
    }

    static bool canShowItem(UINT actionFlags, MenuId id)
    {
        if (actionFlags & id)
            return true;
        return false;
    }

    void showImpl(UINT actionFlags, const CommandSnapshot& snapshot)
    {
        if (!m_hWnd || m_tracking || m_closing.load()
            || (snapshot.cancelled && snapshot.cancelled()))
            return;
        m_commandSnapshot = snapshot;
        POINT screenPt = { 0 };
        ::GetCursorPos(&screenPt);

        POINT clientPt = screenPt;
        ::ScreenToClient(m_hWnd, &clientPt);

        if (m_popMenu) {
            if (!destroyOwnedMenu()) {
                if (m_broker)
                    wke::brokerRecordFailure(m_view);
                return;
            }
        }
        m_popMenu = ::CreatePopupMenu();
        if (!m_popMenu) {
            if (m_broker)
                wke::brokerRecordFailure(m_view);
            return;
        }
        if (m_broker) {
            m_menuHeld = wke::brokerHoldNativeResource(m_view, wke::BrokerNativeMenu, m_popMenu);
            if (!m_menuHeld) {
                wke::brokerRecordFailure(m_view);
                return;
            }
        }
        
        //m_data = blink::WebContextMenuData();
        
        if (canShowItem(actionFlags, kSelectedTextId))
            ::AppendMenu(m_popMenu, MF_STRING, kSelectedTextId, L"复制");

        if (canShowItem(actionFlags, kCopyImageId))
            ::AppendMenu(m_popMenu, MF_STRING, kCopyImageId, L"复制图片");

        if (canShowItem(actionFlags, kInspectElementAtId))
            ::AppendMenu(m_popMenu, MF_STRING, kInspectElementAtId, L"检查");

        if (canShowItem(actionFlags, kCutId))
            ::AppendMenu(m_popMenu, MF_STRING, kCutId, L"剪切");

        if (canShowItem(actionFlags, kPasteId))
            ::AppendMenu(m_popMenu, MF_STRING, kPasteId, L"粘贴");

        if (canShowItem(actionFlags, kSelectedAllId))
            ::AppendMenu(m_popMenu, MF_STRING, kSelectedAllId, L"全选");

        if (canShowItem(actionFlags, kUndoId))
            ::AppendMenu(m_popMenu, MF_STRING, kUndoId, L"撤销");

        if (canShowItem(actionFlags, kGoForwardId))
            ::AppendMenu(m_popMenu, MF_STRING, kGoForwardId, L"前进");

        if (canShowItem(actionFlags, kGoBackId))
            ::AppendMenu(m_popMenu, MF_STRING, kGoBackId, L"后退");

        if (canShowItem(actionFlags, kReloadId))
            ::AppendMenu(m_popMenu, MF_STRING, kReloadId, L"刷新");

        if (canShowItem(actionFlags, kPrintId))
            ::AppendMenu(m_popMenu, MF_STRING, kPrintId, L"打印");
        
        if (0 == ::GetMenuItemCount(m_popMenu)) {
            if (!destroyOwnedMenu() && m_broker)
                wke::brokerRecordFailure(m_view);
            return;
        }

        //m_data = data;

        //::ShowWindow(m_hWnd, SW_SHOWMINNOACTIVE);
        //::SetForegroundWindow(m_hWnd);
        ::SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOREPOSITION | SWP_NOACTIVATE);

        UINT flags = TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_VERPOSANIMATION | TPM_HORIZONTAL | TPM_LEFTALIGN | TPM_HORPOSANIMATION;
        if (m_broker) {
            wke::BrokerNativeScope scope(m_view, WKE_CURSOR_BROKER_NATIVE_MENU);
            TrackingGuard tracking(this);
            if (scope.entered())
                ::TrackPopupMenuEx(m_popMenu, flags, clientPt.x, clientPt.y, m_hWnd, 0);
            else
                wke::brokerRecordFailure(m_view);
        } else {
            TrackingGuard tracking(this);
            ::TrackPopupMenuEx(m_popMenu, flags, clientPt.x, clientPt.y, m_hWnd, 0);
        }
    }

    void onCommand(UINT itemID, const CommandSnapshot& snapshot)
    {
        if (m_closing.load() || (snapshot.cancelled && snapshot.cancelled()))
            return;

        content::WebThreadImpl* threadImpl = (content::WebThreadImpl*)(blink::Platform::current()->currentThread());
        if (!m_broker)
            threadImpl->fire();
        if (m_closing.load())
            return;

        if (kSelectedTextId == itemID) {
            m_webPage->webViewImpl()->focusedFrame()->executeCommand("Copy");
        } else if (kSelectedAllId == itemID) {
            m_webPage->webViewImpl()->focusedFrame()->executeCommand("SelectAll");
        } else if (kUndoId == itemID) {
            m_webPage->webViewImpl()->focusedFrame()->executeCommand("Undo");
        } else if (kCopyImageId == itemID) {
            m_webPage->webViewImpl()->copyImageAt(snapshot.imagePos);
        } else if (kInspectElementAtId == itemID) {
            m_webPage->inspectElementAt(snapshot.mousePosition.x(), snapshot.mousePosition.y());
        } else if (kCutId == itemID) {
            m_webPage->webViewImpl()->focusedFrame()->executeCommand("Cut");
        } else if (kPasteId == itemID) {
            m_webPage->webViewImpl()->focusedFrame()->executeCommand("Paste");
        } else if (kGoForwardId == itemID) {
            m_webPage->goForward();
        } else if (kGoBackId == itemID) {
            m_webPage->goBack();
        } else if (kReloadId == itemID) {
            m_webPage->mainFrame()->reload();
        } else if (kPrintId == itemID) {
            wkeOnContextMenuItemClickCallback clickCallback = m_webPage->wkeHandler().contextMenuItemClickCallback;
            void* callbackParam = m_webPage->wkeHandler().contextMenuItemClickCallbackParam;
            if (clickCallback)
                clickCallback(m_webPage->wkeWebView(), callbackParam, kWkeContextMenuItemClickTypePrint, kWkeContextMenuItemClickStepClick, (wkeWebFrameHandle)snapshot.frameId, nullptr);
        }
    }

    static LRESULT CALLBACK wndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) try
    {
//         String output = String::format("ContextMenu.wndProc: %d\n", uMsg);
//         OutputDebugStringA(output.utf8().data());

        PAINTSTRUCT ps;
        HDC hdc = nullptr;
        ContextMenu* self = reinterpret_cast<ContextMenu*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (uMsg == WM_NCCREATE) {
            self = static_cast<ContextMenu*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            if (!self) return FALSE;
            ::SetLastError(0);
            if (!::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self))
                && ::GetLastError() != 0) return FALSE;
        }
        if (!self)
            return ::DefWindowProc(hWnd, uMsg, wParam, lParam);

        switch (uMsg) {
        case WM_TIMER:
            //self->hide();
            break;

        case WM_PAINT:
            hdc = ::BeginPaint(hWnd, &ps);
            self->onPaint(hWnd, hdc);
            ::EndPaint(hWnd, &ps);
            break;
        case WM_NCDESTROY:
            // Keep the target alive throughout this callback. Only an actual
            // owner DestroyWindow call plus this witness retires its ledger.
            self->m_ncDestroyObserved = self->m_destroyingWindow;
            ::RemovePropW(hWnd, kContextMenuClassName);
            ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
            if (!self->m_destroyingWindow && self->m_broker)
                wke::brokerRecordFailure(self->m_view);
            break;
        case WM_COMMAND: try {
            UINT itemID = LOWORD(wParam);
            const CommandSnapshot snapshot = self->m_commandSnapshot;
            if (snapshot.cancelled && snapshot.cancelled())
                break;
            std::shared_ptr<ContextMenu> owner = self->shared_from_this();
            if (self->m_broker) {
                wke::brokerPostEngineTask(self->m_view, [owner, itemID, snapshot] {
                    if (!snapshot.cancelled || !snapshot.cancelled())
                        owner->onCommand(itemID, snapshot);
                });
            } else if (WTF::isMainThread()) {
                self->onCommand(itemID, snapshot);
            } else {
                blink::Platform::current()->mainThread()->postTask(FROM_HERE,
                    new LegacyCommandTask(owner, itemID, snapshot));
            }
        }
        catch (...) {
            try { if (self->m_broker) wke::brokerRecordFailure(self->m_view); } catch (...) { }
        }
            break;
        case WM_CANCELMODE:
            if (self->m_tracking)
                ::EndMenu();
            break;
        case WM_CLOSE:
            break;
        case WM_EXITMENULOOP:
            //::ShowWindow(self->m_hWnd, SW_HIDE);
            break;
        }

        return ::DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
    catch (...) {
        ContextMenu* self = reinterpret_cast<ContextMenu*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        try { if (self && self->m_broker) wke::brokerRecordFailure(self->m_view); } catch (...) { }
        return ::DefWindowProc(hWnd, uMsg, wParam, lParam);
    }

    void onPaint(HWND hWnd, HDC hdc)
    {

    }

    HWND m_hWnd;
    HMENU m_popMenu;
    WebPage* m_webPage;

    int m_lastX;
    int m_lastY;

    wkeUiThreadPostTaskCallback m_uiCallback;
    wkeWebView m_view;
    bool m_broker;
    std::atomic<bool> m_closing { false };
    std::atomic<bool> m_cleanupPosted { false };
    std::atomic<unsigned int> m_cleanupAttempts { 0 };
    bool m_windowHeld = false; // UI owner, exact factory output.
    bool m_menuHeld = false;
    bool m_destroyingWindow = false;
    bool m_ncDestroyObserved = false;
    bool m_tracking { false }; // UI owner only.
    CommandSnapshot m_commandSnapshot;
    std::shared_ptr<ContextMenu> m_windowKeepAlive;
};

}

#endif // content_browser_ContextMeun_h
