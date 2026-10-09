#ifndef WKE_CURSOR_DESCRIPTOR_BROKER_H
#define WKE_CURSOR_DESCRIPTOR_BROKER_H

#include "wke/wkedefine.h"
#include <functional>
#include <memory>

namespace blink { struct WebCursorInfo; }
namespace wke {
struct CursorBrokerControl;

const wkeCursorBrokerApi* cursorBrokerApi(unsigned int version);
bool brokerActive();
bool brokerMayCreateRawView();
bool brokerMayDestroyRawView(wkeWebView);
bool brokerMaySetHandle(wkeWebView, HWND);
bool brokerMayFinalize();
bool brokerOwnedFactoryPermission();
bool brokerNoteViewCreated();
bool brokerPluginEntry(); // Records pre-realm history, rejects irreversible mode.
bool brokerOwnsView(wkeWebView);
bool brokerClosing(wkeWebView);
bool brokerDrained(wkeWebView);
bool brokerBeginClose(wkeWebView);
void brokerRecordFailure(wkeWebView);
bool brokerPublishCursor(wkeWebView, const blink::WebCursorInfo&);
bool brokerPostUiTask(wkeWebView, unsigned int kind,
    std::function<void(bool cancelled)>&&, bool terminal = false);
// UI-owner-only continuation of an already closing menu cleanup. No new work.
bool brokerPostUiCleanupRetry(wkeWebView, std::function<void(bool cancelled)>&&);
enum BrokerNativeResourceKind { BrokerNativeWindow = 1, BrokerNativeMenu = 2 };
bool brokerHoldNativeResource(wkeWebView, unsigned int kind, void* handle);
// Called only after the exact owned destruction API returned success.
bool brokerReleaseNativeResource(wkeWebView, unsigned int kind, void* handle);
bool brokerPostEngineTask(wkeWebView, std::function<void()>&&);
std::function<bool()> brokerUiCancelPredicate(wkeWebView);
bool brokerUiCancelled(wkeWebView);
void brokerSetOleRegistered(wkeWebView, bool);
void brokerAddComReference(wkeWebView);
void brokerReleaseComReference(wkeWebView);
void brokerAddCloseAction(wkeWebView, std::function<void()>&&);
void brokerAddIdentityAction(wkeWebView, std::function<void()>&&);
bool brokerAddMenuActions(wkeWebView, std::function<void()>&& close,
    std::function<void()>&& cancelIdentity);
// Fixed independent cancellation owners: 0 menu, 1 drag. Coalescing one
// owner's newest epoch must not erase the other owner's cancellation.
bool brokerPostUiCancellation(wkeWebView, unsigned int ownerSlot, std::function<void(bool)>&&);
std::function<bool()> brokerUiClosingPredicate(wkeWebView);
void brokerDocumentCommitted(wkeWebView, bool mainDocument);

class BrokerNativeScope {
public:
    BrokerNativeScope(wkeWebView, unsigned int kind, bool independentInvocation = false);
    ~BrokerNativeScope();
    bool entered() const { return m_entered; }
    BrokerNativeScope(const BrokerNativeScope&) = delete;
    BrokerNativeScope& operator=(const BrokerNativeScope&) = delete;
private:
    std::shared_ptr<CursorBrokerControl> m_control;
    wkeCursorBrokerNativeReceipt m_receipt;
    bool m_entered;
};
} // namespace wke
#endif
