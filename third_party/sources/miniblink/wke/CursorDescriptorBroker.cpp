#include "wke/CursorDescriptorBroker.h"
#include "wke/wkeWebView.h"
#include "wke/wkeGlobalVar.h"
#include "content/browser/WebPage.h"
#include "content/browser/WebPageImpl.h"
#include "content/browser/InspectorKeyboardSnapshot.h"
#include "content/devtools/DevToolsAgent.h"
#include "content/devtools/DevToolsClient.h"
#include "content/web_impl_win/LocalResourceState.h"
#include "third_party/WebKit/Source/core/loader/InspectorResourcePolicy.h"
#include "third_party/WebKit/Source/core/frame/Settings.h"
#include "third_party/WebKit/Source/web/WebViewImpl.h"
#include "content/web_impl_win/BlinkPlatformImpl.h"
#include "content/web_impl_win/WebThreadImpl.h"
#include "third_party/WebKit/public/platform/WebCursorInfo.h"
#include "gen/blink/platform/RuntimeEnabledFeatures.h"
#include "base/rand_util.h"
#include "skia/ext/platform_canvas.h"
#include "third_party/skia/include/core/SkColorPriv.h"
#include "third_party/WebKit/public/platform/Platform.h"
#include "cc/base/BdColor.h"
#include <cstring>
#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <mutex>
#include <limits>
#include <functional>
#include <memory>
#include <vector>

namespace blink {
// Private command defined only in InspectorResourceSnapshot.cpp; no WKE export.
bool finishAndRetireInspectorResourceOwner(const std::shared_ptr<InspectorResourceState>&,
    bool& finished);
}

namespace wke {
extern DWORD wkeThreadId;
using BrokerActionOwner = std::shared_ptr<const std::function<void()>>;
struct CursorBrokerControl {
    wkeCursorBrokerEndpoint token = {};
    wkeCursorBrokerIdentity identity = {};
    wkeCursorBrokerHooks hooks = {};
    HWND window = nullptr;
    DWORD ownerThread = 0;
    wkeWebView view = nullptr;
    std::atomic<bool> closing{false};
    std::atomic<bool> failed{false};
    std::atomic<bool> destroying{false};
    std::atomic<bool> oleRegistered{false};
    std::atomic<unsigned int> queued{0};
    std::atomic<unsigned int> running{0};
    std::atomic<unsigned int> scopes{0};
    std::atomic<unsigned int> engineCallbacks{0};
    std::atomic<unsigned int> devToolsBorrows{0}; // Private stage-0 session pin.
    std::shared_ptr<content::LocalResourceState> localResources;
    bool localResourceBinding = false; // Engine-only callout/reentrancy fence.
    std::shared_ptr<blink::InspectorResourceState> inspectorResources;
    unsigned int inspectorDocuments = 0; // Engine-only; a frontend cannot be promoted after use.
    std::atomic<unsigned int> comReferences{0};
    std::atomic<unsigned int> nativeResources{0};
    std::atomic<bool> nativeResourceAdmissionFault{false};
    struct NativeResource { unsigned int kind = 0; void* handle = nullptr; };
    std::array<NativeResource, 2> nativeResourceOwners; // UI menu HWND + HMENU.
    std::atomic<unsigned __int64> taskSequence{0};
    std::atomic<unsigned __int64> scopeSequence{0};
    std::atomic<unsigned __int64> epoch{1};
    unsigned __int64 publishSequence = 0; // Engine only.
    unsigned __int64 closeSequence = 0; // Engine only.
    std::vector<BrokerActionOwner> closeActions; // Engine only.
    std::vector<BrokerActionOwner> identityActions; // Engine only; Drag + Menu.
    bool documentPending = false; // Engine only.
    unsigned __int64 pendingDocumentGeneration = 0;
};
namespace {
void stage0DocumentCommitted(wkeWebView, bool);
struct UiTask {
    std::shared_ptr<CursorBrokerControl> control;
    wkeCursorBrokerNativeReceipt receipt = {};
    std::function<void(bool)> body;
    std::array<std::function<void(bool)>, 2> cancellationBodies;
    bool terminal = false;
    bool cancellation = false;
    unsigned __int64 epoch = 0;
};
std::mutex s_mutex;
std::vector<std::shared_ptr<CursorBrokerControl>> s_endpoints;
std::deque<std::unique_ptr<UiTask>> s_uiTasks;
// Mandatory close continuations have their own fixed bound (menu, drag
// storage, OLE revoke). Queue pressure cannot prevent a window from closing.
// They are not admitted as ordinary native work until its owner drains it.
std::deque<std::unique_ptr<UiTask>> s_closeContinuations;
std::deque<std::unique_ptr<UiTask>> s_cancelContinuations;
const unsigned int kCloseContinuationsPerEndpoint = 3;
const unsigned int kIdentityActionsPerEndpoint = 2;
wkeCursorBrokerRealm s_realm = {};
std::atomic<bool> s_mode{false};
bool s_realmEnded = false;
unsigned __int64 s_viewHistory = 0;
unsigned __int64 s_pluginHistory = 0;
unsigned int s_maxEndpoints = 0;
unsigned int s_maxTasks = 0;
bool s_ownedCreate = false; // Engine-only scoped permission.
wkeWebView s_ownedDestroy = nullptr; // One consumed permit for the exact view.
const UINT_PTR kBrokerTaskTimer = 0x15324547;
thread_local const wkeCursorBrokerNativeReceipt* s_uiTaskLineage = nullptr;
class UiTaskLineage {
public:
    explicit UiTaskLineage(const wkeCursorBrokerNativeReceipt* receipt)
        : m_previous(s_uiTaskLineage) { s_uiTaskLineage = receipt; }
    ~UiTaskLineage() { s_uiTaskLineage = m_previous; }
private:
    const wkeCursorBrokerNativeReceipt* m_previous;
};
unsigned __int64 nextSequence(std::atomic<unsigned __int64>& counter)
{
    auto value = counter.load();
    while (value != (std::numeric_limits<unsigned __int64>::max)()) {
        if (counter.compare_exchange_weak(value, value + 1)) return value + 1;
    }
    return 0; // Overflow is rejected; zero never masquerades as an invocation.
}

// Keep the actual view inside the endpoint's counted drain throughout callout
// and disposal of the stable snapshots. Registration may re-enter and grow a
// vector, but cannot move or destroy the callable currently being invoked.
class ActionCalloutLease {
public:
    explicit ActionCalloutLease(std::shared_ptr<CursorBrokerControl> control)
        : m_control(std::move(control)), m_entered(false)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        unsigned int total = 0;
        for (const auto& endpoint : s_endpoints) {
            const auto count = endpoint->engineCallbacks.load();
            if (count > s_maxTasks || total > s_maxTasks - count) {
                m_control->failed = true;
                return;
            }
            total += count;
        }
        if (total >= s_maxTasks) { m_control->failed = true; return; }
        m_control->engineCallbacks.fetch_add(1);
        m_entered = true;
    }
    ~ActionCalloutLease()
    {
        if (m_entered) m_control->engineCallbacks.fetch_sub(1);
    }
    bool entered() const { return m_entered; }
private:
    ActionCalloutLease(const ActionCalloutLease&) = delete;
    ActionCalloutLease& operator=(const ActionCalloutLease&) = delete;
    std::shared_ptr<CursorBrokerControl> m_control;
    bool m_entered;
};

template<size_t Capacity>
bool snapshotActions(const std::shared_ptr<CursorBrokerControl>& control,
    const std::vector<BrokerActionOwner>& source,
    std::array<BrokerActionOwner, Capacity>& snapshot)
{
    if (source.size() > Capacity) { control->failed = true; return false; }
    for (size_t i = 0; i < source.size(); ++i) {
        if (!source[i] || !*source[i]) { control->failed = true; return false; }
        snapshot[i] = source[i]; // shared ownership only; no functor copy.
    }
    return true;
}

template<size_t Capacity>
void invokeActions(const std::shared_ptr<CursorBrokerControl>& control,
    const std::array<BrokerActionOwner, Capacity>& snapshot)
{
    for (const auto& action : snapshot) {
        if (action) {
            try { (*action)(); } catch (...) { control->failed = true; }
        }
    }
}

bool equalToken(const wkeCursorBrokerToken& a, const wkeCursorBrokerToken& b)
{
    return a.low == b.low && a.high == b.high && (a.low || a.high);
}
bool ownerEngine()
{
    return wkeIsInit && GetCurrentThreadId() == wkeThreadId;
}
bool validIdentity(const wkeCursorBrokerIdentity& identity)
{
    return identity.structSize == sizeof(identity) && !identity.reserved
        && identity.viewGeneration && identity.documentGeneration
        && identity.geometryGeneration;
}
bool sameIdentity(const wkeCursorBrokerIdentity& a, const wkeCursorBrokerIdentity& b)
{
    return a.viewGeneration == b.viewGeneration
        && a.documentGeneration == b.documentGeneration
        && a.geometryGeneration == b.geometryGeneration;
}
wkeCursorBrokerToken newToken()
{
    wkeCursorBrokerToken token;
    do { base::RandBytes(&token, sizeof(token)); } while (!(token.low || token.high));
    return token;
}
std::shared_ptr<CursorBrokerControl> findToken(wkeCursorBrokerEndpoint token)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    for (const auto& control : s_endpoints)
        if (equalToken(control->token, token))
            return control;
    return nullptr;
}
std::shared_ptr<CursorBrokerControl> findView(wkeWebView view)
{
    if (!view)
        return nullptr;
    std::lock_guard<std::mutex> lock(s_mutex);
    for (const auto& control : s_endpoints)
        if (control->view == view)
            return control;
    return nullptr;
}
bool actualOwner(const CursorBrokerControl& control)
{
    DWORD pid = 0;
    const DWORD tid = GetWindowThreadProcessId(control.window, &pid);
    return tid == control.ownerThread && tid == GetCurrentThreadId()
        && pid == GetCurrentProcessId() && IsWindow(control.window);
}
bool drained(const CursorBrokerControl& control)
{
    return !control.queued && !control.running
        && (!control.localResources || control.localResources->retired())
        && (!control.inspectorResources || (!control.inspectorResources->active()
            && !control.inspectorResources->queued() && !control.inspectorResources->dispatcherAlive()))
        && !control.scopes && !control.engineCallbacks && !control.devToolsBorrows && !control.oleRegistered
        && !control.comReferences && !control.nativeResources
        && !control.nativeResourceAdmissionFault;
}
void receipt(const CursorBrokerControl& control, wkeCursorBrokerDrainReceipt* out)
{
    out->closing = control.closing ? 1 : 0;
    out->endpoint = control.token;
    out->queuedUiTasks = control.queued;
    out->runningUiTasks = control.running;
    out->nativeScopes = control.scopes;
    out->engineCallbacks = control.engineCallbacks.load() + control.devToolsBorrows.load()
        + (control.inspectorResources ? control.inspectorResources->active() + control.inspectorResources->queued() : 0);
    if (control.localResources) {
        SouiLocalResourceDrain local = {}; local.structSize = sizeof(local);
        control.localResources->snapshot(local);
        out->engineCallbacks += local.activeOperations + local.callbackBorrows + local.queuedOperations;
    }
    out->oleRegistered = control.oleRegistered ? 1 : 0;
    out->drained = control.closing && drained(control) ? 1 : 0;
    out->closeSequence = control.closeSequence;
}
void WINAPI timerProc(HWND window, UINT, UINT_PTR, DWORD)
{
    KillTimer(window, kBrokerTaskTimer);
    // Fixed storage: allocation failure cannot escape a Win32 TIMERPROC or
    // partially move payloads before their counter transition.
    std::array<std::unique_ptr<UiTask>, WKE_CURSOR_BROKER_MAX_NATIVE_TASKS
        + (kCloseContinuationsPerEndpoint + 1) * WKE_CURSOR_BROKER_MAX_ENDPOINTS> batch;
    size_t batchSize = 0;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto take = [&batch, &batchSize, window](std::deque<std::unique_ptr<UiTask>>& pending) {
            for (auto it = pending.begin(); it != pending.end();) {
                if ((*it)->control->window != window) { ++it; continue; }
                (*it)->control->running.fetch_add(1);
                (*it)->control->queued.fetch_sub(1);
                batch[batchSize++] = std::move(*it);
                it = pending.erase(it);
            }
        };
        take(s_uiTasks);
        take(s_closeContinuations);
        take(s_cancelContinuations);
    }
    // No registry lock is held across an SDK callback or a native modal loop.
    for (size_t index = 0; index < batchSize; ++index) {
        auto& task = batch[index];
        const auto control = task->control;
        UiTaskLineage lineage(&task->receipt); // Nested owner timers restore it.
        bool cancelled;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            cancelled = !actualOwner(*control) || (control->failed && !task->terminal)
                || (!task->terminal && (!sameIdentity(control->identity, task->receipt.identity)
                    || task->epoch != control->epoch))
                || (control->closing && !task->terminal);
        }
        if (task->cancellation) {
            for (auto& body : task->cancellationBodies) {
                try { if (body) body(cancelled); }
                catch (...) { control->failed = true; }
                body = nullptr;
            }
        } else {
            try { task->body(cancelled); }
            catch (...) { control->failed = true; }
        }
        task->body = nullptr; // Once disposal before completion receipt.
        if (actualOwner(*control)) {
            try {
                control->hooks.onNativeComplete(control->hooks.userData,
                    &task->receipt, cancelled ? 1U : 0U);
            } catch (...) { control->failed = true; }
        } else { control->failed = true; }
        task.reset();
        control->running.fetch_sub(1); // Last access after callback/payload drain.
    }
}
unsigned int WKE_CALL_TYPE beginRealm(const wkeCursorBrokerRealmConfig* config, wkeCursorBrokerRealmReceipt* out)
{
    if (!ownerEngine()) return WKE_CURSOR_BROKER_WRONG_THREAD;
    if (!config || !out || config->structSize != sizeof(*config)
        || out->structSize != sizeof(*out) || config->reserved
        || !config->maxEndpoints || config->maxEndpoints > WKE_CURSOR_BROKER_MAX_ENDPOINTS
        || !config->maxNativeTasks || config->maxNativeTasks > WKE_CURSOR_BROKER_MAX_NATIVE_TASKS)
        return WKE_CURSOR_BROKER_INVALID;
    if (s_mode || s_realmEnded) return WKE_CURSOR_BROKER_UNAVAILABLE;
    std::lock_guard<std::mutex> admission(s_mutex);
    if (s_viewHistory || s_pluginHistory || !g_liveWebViews.empty()
        || g_wkePluginFindcallback) return WKE_CURSOR_BROKER_HISTORY;
    // Monotonic process mode. It is never reset by endRealm or a raw setter.
    s_realm = newToken();
    blink::RuntimeEnabledFeatures::setNpapiPluginsEnabled(false);
    s_maxEndpoints = config->maxEndpoints;
    s_maxTasks = config->maxNativeTasks;
    s_mode = true;
    out->capabilities = WKE_CURSOR_BROKER_CAP_NPAPI_UNAVAILABLE | WKE_CURSOR_BROKER_CAP_DESCRIPTOR_ONLY;
    out->realm = s_realm;
    out->realmGeneration = 1;
    return WKE_CURSOR_BROKER_OK;
}
enum class EndpointPresentation { NativeWindow, CallbackOnly };

unsigned int createEndpointImpl(wkeCursorBrokerRealm realm, const wkeCursorBrokerEndpointConfig* config,
    wkeCursorBrokerEndpointReceipt* out, EndpointPresentation presentation,
    std::shared_ptr<CursorBrokerControl>* retainedControl)
{
    if (!ownerEngine()) return WKE_CURSOR_BROKER_WRONG_THREAD;
    if (!equalToken(realm, s_realm) || s_realmEnded) return WKE_CURSOR_BROKER_STALE;
    if (!config || !out || config->structSize != sizeof(*config) || out->structSize != sizeof(*out)
        || config->reserved || !validIdentity(config->identity)
        || config->hooks.structSize != sizeof(config->hooks) || config->hooks.reserved
        || !config->hooks.onDescriptor || !config->hooks.onNativeEnter
        || !config->hooks.onNativeLeave || !config->hooks.onNativeComplete)
        return WKE_CURSOR_BROKER_INVALID;
    // No owner token is promised until registration succeeds. In particular,
    // allocation failure before handoff cannot leave caller garbage as a token.
    out->ownerThreadId = 0;
    out->endpoint = {};
    out->webView = nullptr;
    out->identity = {};
    DWORD pid = 0;
    const DWORD tid = GetWindowThreadProcessId(config->ownerWindow, &pid);
    if (!tid || pid != GetCurrentProcessId() || !IsWindow(config->ownerWindow))
        return WKE_CURSOR_BROKER_INVALID;
    auto control = std::make_shared<CursorBrokerControl>();
    control->token = newToken();
    control->identity = config->identity;
    control->hooks = config->hooks;
    control->window = config->ownerWindow;
    control->ownerThread = tid;
    // Private factory cleanup obtains its strong owner before registration or
    // handoff can expose a token. This assignment does not allocate or lock.
    if (retainedControl) *retainedControl = control;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_endpoints.size() >= s_maxEndpoints) return WKE_CURSOR_BROKER_LIMIT;
        for (const auto& existing : s_endpoints)
            if (existing->window == control->window) return WKE_CURSOR_BROKER_INVALID;
        s_endpoints.push_back(control);
    }
    // Handoff precedes any possible UI callback. A failure after construction
    // still returns this owned token and must keep hook storage until drain.
    out->ownerThreadId = tid;
    out->endpoint = control->token;
    out->webView = nullptr;
    out->identity = control->identity;
    // Register before setHandle can schedule OLE tasks. No document exists yet.
    try {
        s_ownedCreate = true;
        control->view = CWebView::createForCursorBroker(cc::s_kBgColor);
        s_ownedCreate = false;
        if (!control->view) throw std::bad_alloc();
        // Business endpoints use the engine's HWND presentation. Inspector
        // endpoints retain their callback-owned pixel buffer and WM_PAINT.
        control->view->webPage()->setNeedAutoDrawToHwnd(presentation == EndpointPresentation::NativeWindow);
        control->view->setHandle(config->ownerWindow);
    } catch (...) {
        s_ownedCreate = false;
        if (!control->view) {
            // No handle binding or broker callback has been admitted. Remove
            // the provisional registration; the caller retains no endpoint.
            std::lock_guard<std::mutex> lock(s_mutex);
            s_endpoints.erase(std::remove(s_endpoints.begin(), s_endpoints.end(), control), s_endpoints.end());
            out->endpoint = {};
            return WKE_CURSOR_BROKER_FAILED;
        }
        control->failed = true;
        out->webView = control->view;
        return WKE_CURSOR_BROKER_FAILED;
    }
    out->ownerThreadId = tid;
    out->endpoint = control->token;
    out->webView = control->view;
    out->identity = control->identity;
    return control->failed ? WKE_CURSOR_BROKER_FAILED : WKE_CURSOR_BROKER_OK;
}
unsigned int WKE_CALL_TYPE createEndpoint(wkeCursorBrokerRealm realm, const wkeCursorBrokerEndpointConfig* config,
    wkeCursorBrokerEndpointReceipt* out)
{
    return createEndpointImpl(realm, config, out, EndpointPresentation::NativeWindow, nullptr);
}
unsigned int WKE_CALL_TYPE updateIdentity(wkeCursorBrokerEndpoint endpoint, const wkeCursorBrokerIdentity* identity)
{
    if (!ownerEngine()) return WKE_CURSOR_BROKER_WRONG_THREAD;
    auto control = findToken(endpoint);
    if (!control) return WKE_CURSOR_BROKER_STALE;
    if (!identity || !validIdentity(*identity)) return WKE_CURSOR_BROKER_INVALID;
    if (control->closing) return WKE_CURSOR_BROKER_CLOSING;
    if (control->failed) return WKE_CURSOR_BROKER_FAILED;
    const auto requested = *identity;
    const auto view = control->view;
    ActionCalloutLease lease(control);
    if (!lease.entered()) return WKE_CURSOR_BROKER_FAILED;
    // Declared after lease: owners are released while drain is still pinned.
    std::array<BrokerActionOwner, kIdentityActionsPerEndpoint> actions = {};
    if (!snapshotActions(control, control->identityActions, actions)) return WKE_CURSOR_BROKER_FAILED;
    bool changed;
    unsigned __int64 epoch;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (requested.viewGeneration != control->identity.viewGeneration
            || requested.documentGeneration < control->identity.documentGeneration
            || requested.geometryGeneration < control->identity.geometryGeneration)
            return WKE_CURSOR_BROKER_STALE;
        if (control->documentPending
            && requested.documentGeneration <= control->pendingDocumentGeneration)
            return WKE_CURSOR_BROKER_STALE;
        changed = !sameIdentity(control->identity, requested);
        epoch = changed ? nextSequence(control->epoch) : control->epoch.load();
        if (!epoch) { control->failed = true; return WKE_CURSOR_BROKER_FAILED; }
        control->identity = requested;
        control->documentPending = false;
    }
    if (changed) invokeActions(control, actions);
    const auto currentStatus = [&] {
        if (control->failed) return WKE_CURSOR_BROKER_FAILED;
        if (control->closing || control->destroying) return WKE_CURSOR_BROKER_CLOSING;
        if (!view || control->view != view || findToken(endpoint).get() != control.get()
            || findView(view).get() != control.get() || control->epoch.load() != epoch
            || !sameIdentity(control->identity, requested)) return WKE_CURSOR_BROKER_STALE;
        return WKE_CURSOR_BROKER_OK;
    };
    const auto status = currentStatus();
    if (status != WKE_CURSOR_BROKER_OK) return status;
    brokerPublishCursor(view, view->webPage()->webPageImpl()->m_cursor);
    return currentStatus(); // The descriptor hook is also a callout boundary.
}
unsigned int WKE_CALL_TYPE requestClose(wkeCursorBrokerEndpoint endpoint)
{
    if (!ownerEngine()) return WKE_CURSOR_BROKER_WRONG_THREAD;
    auto control = findToken(endpoint);
    if (!control) return WKE_CURSOR_BROKER_STALE;
    if (control->destroying) return WKE_CURSOR_BROKER_CLOSING;
    const bool alreadyClosing = control->closing.exchange(true);
    // Publish the original first-close sequence before the new external release
    // boundary, including when its counted admission has to return BUSY.
    if (!alreadyClosing) {
        if (control->publishSequence == (std::numeric_limits<unsigned __int64>::max)())
            control->failed = true;
        else
            control->closeSequence = ++control->publishSequence;
    }
    if (control->localResources) {
        control->localResources->close();
        ActionCalloutLease resourceLease(control);
        if (!resourceLease.entered()) return WKE_CURSOR_BROKER_BUSY;
        control->localResources->finishClose();
        if (control->localResources->failed()) control->failed = true;
    }
    if (control->inspectorResources) {
        control->inspectorResources->close();
        ActionCalloutLease resourceLease(control);
        if (!resourceLease.entered()) return WKE_CURSOR_BROKER_BUSY;
        bool finished = false;
        if (!blink::finishAndRetireInspectorResourceOwner(control->inspectorResources, finished))
            return WKE_CURSOR_BROKER_BUSY;
    }
    if (alreadyClosing) {
        // Observe queue transitions under the same admission lock. Completed
        // drain and an active callout never admit another cleanup invocation.
        // Hidden native/COM ledgers still permit a genuine empty-queue retry.
        std::lock_guard<std::mutex> admission(s_mutex);
        if (control->queued || control->running || control->engineCallbacks
            || drained(*control))
            return control->failed ? WKE_CURSOR_BROKER_FAILED : WKE_CURSOR_BROKER_OK;
    }
    ActionCalloutLease lease(control);
    if (!lease.entered()) return WKE_CURSOR_BROKER_FAILED;
    std::array<BrokerActionOwner, kCloseContinuationsPerEndpoint> actions = {};
    if (!snapshotActions(control, control->closeActions, actions)) return WKE_CURSOR_BROKER_FAILED;
    // Fixed owned actions remain until destruction. An admission allocation
    // failure can be retried without losing the only cleanup entry point.
    invokeActions(control, actions);
    return control->failed ? WKE_CURSOR_BROKER_FAILED : WKE_CURSOR_BROKER_OK;
}
unsigned int WKE_CALL_TYPE inspectDrain(wkeCursorBrokerEndpoint endpoint, wkeCursorBrokerDrainReceipt* out)
{
    if (!ownerEngine()) return WKE_CURSOR_BROKER_WRONG_THREAD;
    auto control = findToken(endpoint);
    if (!control) return WKE_CURSOR_BROKER_STALE;
    if (!out || out->structSize != sizeof(*out)) return WKE_CURSOR_BROKER_INVALID;
    bool finished = true;
    if (control->closing && control->inspectorResources) {
        ActionCalloutLease resourceLease(control);
        if (!resourceLease.entered()) return WKE_CURSOR_BROKER_BUSY;
        finished = false;
        if (!blink::finishAndRetireInspectorResourceOwner(control->inspectorResources, finished))
            return WKE_CURSOR_BROKER_BUSY;
    }
    if (control->closing && control->localResources) {
        ActionCalloutLease resourceLease(control);
        if (!resourceLease.entered()) return WKE_CURSOR_BROKER_BUSY;
        if (!control->localResources->finishClose()) finished = false;
        if (control->localResources->failed()) control->failed = true;
    }
    // No drained receipt may stand in for a callback owner that has not retired.
    if (!finished) return WKE_CURSOR_BROKER_BUSY;
    // The callout count must be gone before publishing this receipt. Recursive
    // destroy during release instead sees the outer live count and stays BUSY.
    receipt(*control, out);
    return control->failed ? WKE_CURSOR_BROKER_FAILED : WKE_CURSOR_BROKER_OK;
}
unsigned int WKE_CALL_TYPE destroyEndpoint(wkeCursorBrokerEndpoint endpoint, wkeCursorBrokerDrainReceipt* out)
{
    const unsigned int status = inspectDrain(endpoint, out);
    if (status != WKE_CURSOR_BROKER_OK && status != WKE_CURSOR_BROKER_FAILED)
        return status;
    auto control = findToken(endpoint);
    if (control->destroying) return WKE_CURSOR_BROKER_BUSY;
    if (!control->closing || !out->drained) return WKE_CURSOR_BROKER_BUSY;
    control->destroying = true; // Reentrant callbacks cannot admit new cleanup.
    s_ownedDestroy = control->view;
    try { wkeDestroyWebView(control->view); }
    catch (...) { s_ownedDestroy = nullptr; control->destroying = false; control->failed = true; return WKE_CURSOR_BROKER_FAILED; }
    s_ownedDestroy = nullptr;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        control->view = nullptr;
        s_endpoints.erase(std::remove(s_endpoints.begin(), s_endpoints.end(), control), s_endpoints.end());
    }
    return WKE_CURSOR_BROKER_OK;
}
unsigned int WKE_CALL_TYPE endRealm(wkeCursorBrokerRealm realm)
{
    if (!ownerEngine()) return WKE_CURSOR_BROKER_WRONG_THREAD;
    if (!equalToken(realm, s_realm) || s_realmEnded) return WKE_CURSOR_BROKER_STALE;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_endpoints.empty() || !s_uiTasks.empty() || !s_closeContinuations.empty()
        || !s_cancelContinuations.empty())
        return WKE_CURSOR_BROKER_BUSY;
    s_realmEnded = true;
    return WKE_CURSOR_BROKER_OK;
}
class EngineTask : public blink::WebThread::Task {
public:
    EngineTask(std::shared_ptr<CursorBrokerControl> control, std::function<void()>&& body)
        : m_control(control), m_body(std::move(body))
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        m_identity = control->identity;
        m_epoch = control->epoch;
    }
    ~EngineTask() override
    {
        m_body = nullptr;
        if (!m_ran && !m_control->closing && m_epoch == m_control->epoch)
            m_control->failed = true;
        m_control->engineCallbacks.fetch_sub(1);
    }
    void run() override
    {
        m_ran = true;
        bool current;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            current = !m_control->closing && !m_control->failed
                && sameIdentity(m_identity, m_control->identity)
                && m_epoch == m_control->epoch;
        }
        if (current) {
            try { m_body(); } catch (...) { m_control->failed = true; }
        }
    }
private:
    std::shared_ptr<CursorBrokerControl> m_control;
    wkeCursorBrokerIdentity m_identity;
    unsigned __int64 m_epoch;
    bool m_ran = false;
    std::function<void()> m_body;
};

// C ABI boundaries contain allocation failures and client exceptions. No C++
// exception is allowed to cross a table entry into a dynamic consumer.
template<class F> unsigned int boundary(F&& body)
{
    try { return body(); } catch (...) { return WKE_CURSOR_BROKER_FAILED; }
}
unsigned int WKE_CALL_TYPE safeBeginRealm(const wkeCursorBrokerRealmConfig* config, wkeCursorBrokerRealmReceipt* out)
{ return boundary([&] { return beginRealm(config, out); }); }
unsigned int WKE_CALL_TYPE safeCreateEndpoint(wkeCursorBrokerRealm realm, const wkeCursorBrokerEndpointConfig* config, wkeCursorBrokerEndpointReceipt* out)
{ return boundary([&] { return createEndpoint(realm, config, out); }); }
unsigned int WKE_CALL_TYPE safeUpdateIdentity(wkeCursorBrokerEndpoint endpoint, const wkeCursorBrokerIdentity* identity)
{ return boundary([&] { return updateIdentity(endpoint, identity); }); }
unsigned int WKE_CALL_TYPE safeRequestClose(wkeCursorBrokerEndpoint endpoint)
{ return boundary([&] { return requestClose(endpoint); }); }
unsigned int WKE_CALL_TYPE safeInspectDrain(wkeCursorBrokerEndpoint endpoint, wkeCursorBrokerDrainReceipt* out)
{ return boundary([&] { return inspectDrain(endpoint, out); }); }
unsigned int WKE_CALL_TYPE safeDestroyEndpoint(wkeCursorBrokerEndpoint endpoint, wkeCursorBrokerDrainReceipt* out)
{ return boundary([&] { return destroyEndpoint(endpoint, out); }); }
unsigned int WKE_CALL_TYPE safeEndRealm(wkeCursorBrokerRealm realm)
{ return boundary([&] { return endRealm(realm); }); }
} // namespace

namespace {
uint64_t localResourceSequence = 0; // Engine owner only; never reused.
wkeCursorBrokerEndpoint localEndpoint(SouiLocalResourceEndpoint token) {
    wkeCursorBrokerEndpoint result = {}; result.low = token.low; result.high = token.high; return result;
}
unsigned int SOUI_LOCAL_RESOURCE_CALL bindLocalResources(SouiLocalResourceEndpoint token,
    uint64_t viewGeneration, uint64_t brokerDocumentGeneration,
    const SouiLocalResourceConfig* config, SouiLocalResourceReceipt* output) {
    if (!ownerEngine()) return SouiLocalWrongThread;
    if (!config || !output || output->structSize != sizeof(*output)) return SouiLocalInvalid;
    *output = {}; output->structSize = sizeof(*output); output->abiVersion = SOUI_LOCAL_RESOURCE_ABI;
    auto control = findToken(localEndpoint(token));
    if (!control || !control->view || control->identity.viewGeneration != viewGeneration
        || control->identity.documentGeneration != brokerDocumentGeneration) return SouiLocalStale;
    if (control->closing || control->destroying || control->failed || control->inspectorResources) return SouiLocalFailed;
    if (control->localResourceBinding || (control->localResources && !control->localResources->retired())) return SouiLocalPending;
    if (control->localResources && control->localResources->failed()) return SouiLocalFailed;
    if (localResourceSequence == (std::numeric_limits<uint64_t>::max)()) return SouiLocalLimit;
    ActionCalloutLease resourceLease(control);
    if (!resourceLease.entered()) return SouiLocalPending;
    control->localResourceBinding = true;
    struct Binding { CursorBrokerControl& control;
        ~Binding() { control.localResourceBinding = false; } } binding{*control};
    try {
        unsigned char random[16]; base::RandBytes(random, sizeof(random));
        const char* hex = "0123456789abcdef"; char nonce[33] = {};
        for (unsigned int i = 0; i < sizeof(random); ++i) { nonce[2 * i] = hex[random[i] >> 4]; nonce[2 * i + 1] = hex[random[i] & 15]; }
        unsigned int creationStatus = SouiLocalFailed;
        auto resources = content::LocalResourceState::create(*config, nonce, viewGeneration,
            brokerDocumentGeneration, ++localResourceSequence, &creationStatus);
        if (!resources) return creationStatus;
        // Publish the physical owner before any later failure or callout can
        // lose it. The returned nonzero receipt always names this exact state.
        control->localResources = resources;
        resources->receipt(*output);
        auto* view = control->view ? control->view->webPage()->webViewImpl() : nullptr;
        auto* page = view ? view->page() : nullptr;
        if (control->closing || control->destroying || control->failed || !page || !resources->install(page)) {
            control->failed = true; resources->fault(); resources->finishClose(); return SouiLocalFailed;
        }
        return SouiLocalOk;
    } catch (...) { control->failed = true; return SouiLocalFailed; }
}
unsigned int localResourceDrain(SouiLocalResourceEndpoint token, uint64_t documentReceipt,
    SouiLocalResourceDrain* output, bool close) {
    if (!ownerEngine()) return SouiLocalWrongThread;
    if (!output || output->structSize != sizeof(*output) || !documentReceipt) return SouiLocalInvalid;
    auto control = findToken(localEndpoint(token));
    if (!control || !control->localResources || control->localResources->id() != documentReceipt) return SouiLocalStale;
    auto resources = control->localResources;
    ActionCalloutLease resourceLease(control);
    if (!resourceLease.entered()) { resources->snapshot(*output); return SouiLocalPending; }
    if (close) resources->close();
    if (resources->closed()) resources->finishClose();
    resources->snapshot(*output);
    if (resources->failed()) { control->failed = true; return SouiLocalFailed; }
    return resources->retired() ? SouiLocalOk : SouiLocalPending;
}
unsigned int SOUI_LOCAL_RESOURCE_CALL closeLocalResources(SouiLocalResourceEndpoint token,
    uint64_t documentReceipt, SouiLocalResourceDrain* output) {
    try { return localResourceDrain(token, documentReceipt, output, true); } catch (...) { return SouiLocalFailed; }
}
unsigned int SOUI_LOCAL_RESOURCE_CALL queryLocalResources(SouiLocalResourceEndpoint token,
    uint64_t documentReceipt, SouiLocalResourceDrain* output) {
    try { return localResourceDrain(token, documentReceipt, output, false); } catch (...) { return SouiLocalFailed; }
}
unsigned int SOUI_LOCAL_RESOURCE_CALL safeBindLocalResources(SouiLocalResourceEndpoint token,
    uint64_t viewGeneration, uint64_t brokerDocumentGeneration,
    const SouiLocalResourceConfig* config, SouiLocalResourceReceipt* output) {
    try { return bindLocalResources(token, viewGeneration, brokerDocumentGeneration, config, output); }
    catch (...) { return SouiLocalFailed; }
}
}
const SouiLocalResourceApi* localResourceApi(unsigned int version) {
    static const SouiLocalResourceApi api = { sizeof(api), SOUI_LOCAL_RESOURCE_ABI, 7,
        safeBindLocalResources, closeLocalResources, queryLocalResources };
    return version == SOUI_LOCAL_RESOURCE_ABI ? &api : nullptr;
}

const wkeCursorBrokerApi* cursorBrokerApi(unsigned int version)
{
    static const wkeCursorBrokerApi api = { sizeof(api), WKE_CURSOR_BROKER_ABI_VERSION,
        WKE_CURSOR_BROKER_CAP_NPAPI_UNAVAILABLE | WKE_CURSOR_BROKER_CAP_DESCRIPTOR_ONLY, 0,
        safeBeginRealm, safeCreateEndpoint, safeUpdateIdentity, safeRequestClose,
        safeInspectDrain, safeDestroyEndpoint, safeEndRealm };
    return version == WKE_CURSOR_BROKER_ABI_VERSION ? &api : nullptr;
}
bool brokerActive() { return s_mode; }
bool brokerMayCreateRawView() { return !s_mode; }
bool brokerMayFinalize()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return !s_mode || (s_realmEnded && s_endpoints.empty() && s_uiTasks.empty()
        && s_closeContinuations.empty() && s_cancelContinuations.empty());
}
bool brokerMayDestroyRawView(wkeWebView view)
{
    if (!s_mode) return true;
    if (!view || s_ownedDestroy != view) return false;
    s_ownedDestroy = nullptr; // A destroy callback cannot reenter this permit.
    return brokerOwnsView(view) && brokerDrained(view);
}
bool brokerMaySetHandle(wkeWebView view, HWND window)
{
    if (!s_mode) return true;
    auto control = findView(view);
    return control && !control->closing && window == control->window;
}
bool brokerOwnedFactoryPermission() { return s_mode && s_ownedCreate && ownerEngine(); }

bool controlledDevToolsEngineThread() { return ownerEngine(); }
bool brokerNoteViewCreated()
{
    if (s_mode && !brokerOwnedFactoryPermission()) return false;
    s_ownedCreate = false; // Consume before construction can reenter any callback.
    ++s_viewHistory;
    return true;
}
bool brokerPluginEntry()
{
    // Plugin discovery/load can originate outside the engine queue. Serialize
    // its irreversible history admission with beginRealm's mode transition.
    std::lock_guard<std::mutex> admission(s_mutex);
    if (s_mode) return false;
    ++s_pluginHistory;
    return true;
}
bool brokerOwnsView(wkeWebView view) { return !!findView(view); }
bool brokerClosing(wkeWebView view) { auto c = findView(view); return c && c->closing; }
bool brokerDrained(wkeWebView view) { auto c = findView(view); return !c || (c->closing && drained(*c)); }
bool brokerBeginClose(wkeWebView view) { auto c = findView(view); return !c || requestClose(c->token) == WKE_CURSOR_BROKER_OK; }
void brokerRecordFailure(wkeWebView view) { auto c = findView(view); if (c) c->failed = true; }
bool brokerPublishCursor(wkeWebView view, const blink::WebCursorInfo& cursor) try
{
    auto control = findView(view);
    if (!control) return false; // Caller continues its legacy route.
    if (!ownerEngine() || control->closing || control->failed
        || (control->documentPending && cursor.type != blink::WebCursorInfo::TypeNone)) return true;
    wkeCursorBrokerDescriptor descriptor = {};
    descriptor.structSize = sizeof(descriptor);
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        descriptor.identity = control->identity;
    }
    descriptor.publicationSequence = ++control->publishSequence;
    descriptor.cursorType = static_cast<unsigned int>(cursor.type);
    descriptor.kind = cursor.type == blink::WebCursorInfo::TypeNone
        ? WKE_CURSOR_BROKER_NONE : WKE_CURSOR_BROKER_STANDARD;
    std::vector<unsigned char> pixels;
    if (cursor.type == blink::WebCursorInfo::TypeCustom) {
        descriptor.kind = WKE_CURSOR_BROKER_UNSUPPORTED;
        const SkBitmap& image = cursor.customImage.getSkBitmap();
        if (!cursor.externalHandle && image.width() > 0 && image.height() > 0
            && image.width() <= WKE_CURSOR_BROKER_MAX_SIDE && image.height() <= WKE_CURSOR_BROKER_MAX_SIDE
            && cursor.hotSpot.x >= 0 && cursor.hotSpot.y >= 0
            && cursor.hotSpot.x < image.width() && cursor.hotSpot.y < image.height()) {
            SkBitmap bitmap;
            if (bitmap.tryAllocN32Pixels(image.width(), image.height())) {
                bitmap.eraseColor(SK_ColorTRANSPARENT);
                SkCanvas canvas(bitmap);
                canvas.drawBitmap(image, 0, 0);
                descriptor.kind = WKE_CURSOR_BROKER_CUSTOM_BGRA8;
                descriptor.width = image.width();
                descriptor.height = image.height();
                descriptor.stride = descriptor.width * 4;
                descriptor.hotspotX = cursor.hotSpot.x;
                descriptor.hotspotY = cursor.hotSpot.y;
                descriptor.pixelBytes = descriptor.stride * descriptor.height;
                pixels.resize(descriptor.pixelBytes);
                for (unsigned int y = 0; y < descriptor.height; ++y) {
                    for (unsigned int x = 0; x < descriptor.width; ++x) {
                        const uint32_t pixel = *bitmap.getAddr32(x, y);
                        const size_t index = y * descriptor.stride + x * 4;
                        pixels[index] = static_cast<unsigned char>(SkGetPackedB32(pixel));
                        pixels[index + 1] = static_cast<unsigned char>(SkGetPackedG32(pixel));
                        pixels[index + 2] = static_cast<unsigned char>(SkGetPackedR32(pixel));
                        pixels[index + 3] = static_cast<unsigned char>(SkGetPackedA32(pixel));
                    }
                }
                descriptor.pixels = pixels.data();
            }
        }
    }
    control->engineCallbacks.fetch_add(1);
    try { control->hooks.onDescriptor(control->hooks.userData, control->token, &descriptor); }
    catch (...) { control->failed = true; }
    control->engineCallbacks.fetch_sub(1);
    return true;
}
catch (...) {
    try { brokerRecordFailure(view); } catch (...) { }
    return true; // An owned cursor never falls back to engine handle selection.
}
static bool postUiTaskInternal(wkeWebView view, unsigned int kind, std::function<void(bool)>&& body,
    bool terminal, bool uiCleanup) try
{
    auto control = findView(view);
    if (!control || control->destroying || (!uiCleanup && !ownerEngine()) || (uiCleanup && !actualOwner(*control))
        || !body || (control->failed && !terminal)
        || (control->closing && !terminal)) return false;
    std::unique_ptr<UiTask> task(new UiTask());
    task->control = control;
    task->body = std::move(body);
    task->terminal = terminal;
    task->receipt.structSize = sizeof(task->receipt);
    task->receipt.kind = kind;
    task->receipt.endpoint = control->token;
    task->receipt.taskSequence = nextSequence(control->taskSequence);
    if (!task->receipt.taskSequence) { control->failed = true; return false; }
    std::lock_guard<std::mutex> lock(s_mutex);
    unsigned int total = static_cast<unsigned int>(s_uiTasks.size());
    for (const auto& endpoint : s_endpoints) total += endpoint->running;
    unsigned int terminalCount = 0;
    for (const auto& pending : s_closeContinuations)
        if (pending->control == control) ++terminalCount;
    if ((!terminal && total >= s_maxTasks)
        || (terminal && terminalCount >= kCloseContinuationsPerEndpoint))
        return false;
    task->receipt.identity = control->identity;
    task->epoch = control->epoch;
    // SetTimer doesn't invoke callbacks inline. Holding the lock makes an
    // immediate owner message wait until the payload has been registered.
    if (!SetTimer(control->window, kBrokerTaskTimer, 1, timerProc)) return false;
    if (terminal)
        s_closeContinuations.push_back(std::move(task));
    else
        s_uiTasks.push_back(std::move(task));
    control->queued.fetch_add(1); // Only an actually registered payload counts.
    return true;
}
catch (...) {
    try { brokerRecordFailure(view); } catch (...) { }
    return false; // Caller can undo its source flags; local payload was disposed.
}
bool brokerPostUiTask(wkeWebView view, unsigned int kind, std::function<void(bool)>&& body, bool terminal)
{
    return postUiTaskInternal(view, kind, std::move(body), terminal, false);
}
bool brokerPostUiCleanupRetry(wkeWebView view, std::function<void(bool)>&& body)
{
    if (!brokerClosing(view)) return false;
    return postUiTaskInternal(view, WKE_CURSOR_BROKER_NATIVE_MENU, std::move(body), true, true);
}
bool brokerHoldNativeResource(wkeWebView view, unsigned int kind, void* handle)
{
    auto c = findView(view);
    if (!c) return false;
    // All valid calls follow a successful typed factory on the actual owner.
    // A violated authority/factory contract is not a clean drain receipt.
    auto reject = [&c] { c->failed = true; c->nativeResourceAdmissionFault = true; return false; };
    if (!actualOwner(*c) || !handle) return reject();
    if (kind == BrokerNativeWindow) {
        DWORD pid = 0;
        if (GetWindowThreadProcessId(static_cast<HWND>(handle), &pid) != c->ownerThread
            || pid != GetCurrentProcessId()) return reject();
    } else if (kind != BrokerNativeMenu || !IsMenu(static_cast<HMENU>(handle))) return reject();
    std::lock_guard<std::mutex> lock(s_mutex);
    for (const auto& owner : c->nativeResourceOwners)
        if (owner.handle == handle && owner.kind == kind) return reject();
    for (auto& owner : c->nativeResourceOwners) {
        if (!owner.handle) {
            owner.kind = kind;
            owner.handle = handle;
            c->nativeResources.fetch_add(1);
            return true;
        }
    }
    return reject();
}
bool brokerReleaseNativeResource(wkeWebView view, unsigned int kind, void* handle)
{
    auto c = findView(view);
    if (!c || GetCurrentThreadId() != c->ownerThread || !handle) return false;
    std::lock_guard<std::mutex> lock(s_mutex);
    for (auto& owner : c->nativeResourceOwners) {
        if (owner.kind == kind && owner.handle == handle) {
            owner.handle = nullptr;
            owner.kind = 0;
            c->nativeResources.fetch_sub(1);
            return true;
        }
    }
    c->failed = true;
    return false;
}
bool brokerPostEngineTask(wkeWebView view, std::function<void()>&& body)
{
    auto control = findView(view);
    if (!control || !body) return false;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (control->closing || control->failed) return false;
        unsigned int total = 0;
        for (const auto& endpoint : s_endpoints) total += endpoint->engineCallbacks;
        if (total >= s_maxTasks) return false;
        control->engineCallbacks.fetch_add(1);
    }
    std::unique_ptr<EngineTask> task;
    bool constructed = false;
    try {
        task.reset(new EngineTask(control, std::move(body)));
        constructed = true;
        blink::Platform::current()->mainThread()->postTask(FROM_HERE, task.release());
        // WebThread's contract consumes at post, including cancel-before-run.
    } catch (...) {
        if (!constructed) control->engineCallbacks.fetch_sub(1);
        control->failed = true;
        return false;
    }
    return true;
}
std::function<bool()> brokerUiCancelPredicate(wkeWebView view)
{
    auto control = findView(view);
    const auto epoch = control ? control->epoch.load() : 0;
    return [control, epoch] { return !control || control->closing || control->failed
        || control->epoch != epoch; };
}
std::function<bool()> brokerUiClosingPredicate(wkeWebView view)
{
    auto control = findView(view);
    return [control] { return !control || control->closing || control->failed; };
}
bool brokerUiCancelled(wkeWebView view) { auto c = findView(view); return !c || c->closing || c->failed; }
void brokerSetOleRegistered(wkeWebView view, bool value) { auto c = findView(view); if (c) c->oleRegistered = value; }
void brokerAddComReference(wkeWebView view)
{
    auto c = findView(view);
    if (c) c->comReferences.fetch_add(1);
}
void brokerReleaseComReference(wkeWebView view)
{
    auto c = findView(view);
    if (c && c->comReferences.fetch_sub(1) == 0) {
        c->comReferences.fetch_add(1);
        c->failed = true; // Underflow is not a drain receipt.
    }
}
void brokerAddCloseAction(wkeWebView view, std::function<void()>&& action) try
{
    auto c = findView(view);
    if (!c || !ownerEngine() || c->closing || !action
        || c->closeActions.size() >= kCloseContinuationsPerEndpoint) {
        if (c) c->failed = true;
        return;
    }
    BrokerActionOwner owner = std::make_shared<const std::function<void()>>(std::move(action));
    c->closeActions.reserve(c->closeActions.size() + 1);
    c->closeActions.push_back(std::move(owner));
}
catch (...) {
    try { brokerRecordFailure(view); } catch (...) { }
}
void brokerAddIdentityAction(wkeWebView view, std::function<void()>&& action) try
{
    auto c = findView(view);
    if (!c || !ownerEngine() || c->closing || !action
        || c->identityActions.size() >= kIdentityActionsPerEndpoint) {
        if (c) c->failed = true;
        return;
    }
    BrokerActionOwner owner = std::make_shared<const std::function<void()>>(std::move(action));
    c->identityActions.reserve(c->identityActions.size() + 1);
    c->identityActions.push_back(std::move(owner));
}
catch (...) {
    try { brokerRecordFailure(view); } catch (...) { }
}
bool brokerAddMenuActions(wkeWebView view, std::function<void()>&& close,
    std::function<void()>&& cancelIdentity) try
{
    auto c = findView(view);
    if (!c || !ownerEngine() || c->closing || !close || !cancelIdentity
        || c->closeActions.size() >= kCloseContinuationsPerEndpoint
        || c->identityActions.size() >= kIdentityActionsPerEndpoint) {
        if (c) c->failed = true;
        return false;
    }
    BrokerActionOwner closeOwner = std::make_shared<const std::function<void()>>(std::move(close));
    BrokerActionOwner identityOwner = std::make_shared<const std::function<void()>>(std::move(cancelIdentity));
    // Both reserves precede either logical registration; moves below cannot
    // allocate. No UI task or callback is admitted during this transaction.
    c->closeActions.reserve(c->closeActions.size() + 1);
    c->identityActions.reserve(c->identityActions.size() + 1);
    c->closeActions.push_back(std::move(closeOwner));
    c->identityActions.push_back(std::move(identityOwner));
    return true;
}
catch (...) {
    try { brokerRecordFailure(view); } catch (...) { }
    return false;
}
bool brokerPostUiCancellation(wkeWebView view, unsigned int ownerSlot, std::function<void(bool)>&& body) try
{
    auto c = findView(view);
    if (!c || !ownerEngine() || c->closing || ownerSlot >= 2 || !body) return false;
    std::unique_ptr<UiTask> task(new UiTask());
    task->control = c;
    task->cancellationBodies[ownerSlot] = std::move(body);
    task->terminal = true;
    task->cancellation = true;
    task->receipt.structSize = sizeof(task->receipt);
    task->receipt.endpoint = c->token;
    task->receipt.kind = WKE_CURSOR_BROKER_NATIVE_TASK;
    task->receipt.taskSequence = nextSequence(c->taskSequence);
    if (!task->receipt.taskSequence) { c->failed = true; return false; }
    std::function<void(bool)> displaced;
    bool coalesced = false;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        // One counted slot per endpoint, with two bounded independent owners.
        for (auto& pending : s_cancelContinuations) {
            if (pending->control == c) {
                displaced = std::move(pending->cancellationBodies[ownerSlot]);
                pending->cancellationBodies[ownerSlot] = std::move(task->cancellationBodies[ownerSlot]);
                coalesced = true;
                break;
            }
        }
        if (!coalesced) {
            task->receipt.identity = c->identity;
            if (!SetTimer(c->window, kBrokerTaskTimer, 1, timerProc)) return false;
            s_cancelContinuations.push_back(std::move(task));
            c->queued.fetch_add(1);
        }
    }
    // Captured owner destruction may re-enter the registry. Never dispose it
    // while holding the registry mutex.
    displaced = nullptr;
    return true;
}
catch (...) {
    try { brokerRecordFailure(view); } catch (...) { }
    return false;
}
void brokerDocumentCommitted(wkeWebView view, bool mainDocument)
{
    auto c = findView(view);
    if (!c || !ownerEngine() || c->closing || c->failed) return;
    if (mainDocument && c->inspectorResources) {
        ++c->inspectorDocuments;
        c->inspectorResources->documentCommitted();
    }
    stage0DocumentCommitted(view, mainDocument);
    ActionCalloutLease lease(c);
    if (!lease.entered()) return;
    std::array<BrokerActionOwner, kIdentityActionsPerEndpoint> actions = {};
    if (!snapshotActions(c, c->identityActions, actions)) return;
    const auto epoch = nextSequence(c->epoch);
    if (!epoch) { c->failed = true; return; }
    if (mainDocument) {
        // Clear the old latest slot immediately, then withhold new CSS cursor
        // pixels until the SDK acknowledges a strictly newer document epoch.
        // The old engine cache must not be republished under the new SDK epoch
        // if the new document has not emitted any cursor yet.
        c->pendingDocumentGeneration = c->identity.documentGeneration;
        c->documentPending = true;
        view->webPage()->webPageImpl()->m_cursor = blink::WebCursorInfo(blink::WebCursorInfo::TypeNone);
    }
    // Drag leave can synchronously run script. Establish revocation before
    // calling out, so a nested Document/ACK cannot be overwritten afterwards.
    invokeActions(c, actions);
    // A nested identity change or close owns its newer state. The raw view is
    // used only after rechecking the still-registered control and its epoch.
    if (mainDocument && !c->closing && !c->failed && c->epoch.load() == epoch
        && c->view == view && findView(view).get() == c.get())
        brokerPublishCursor(view, view->webPage()->webPageImpl()->m_cursor);
}
BrokerNativeScope::BrokerNativeScope(wkeWebView view, unsigned int kind, bool independentInvocation)
    : m_control(findView(view)), m_receipt{}, m_entered(false)
{
    if (!m_control) { m_entered = !s_mode; return; }
    if (!actualOwner(*m_control)
        || (m_control->failed && kind != WKE_CURSOR_BROKER_NATIVE_OLE_REVOKE)
        || (m_control->closing && kind != WKE_CURSOR_BROKER_NATIVE_OLE_REVOKE)) return;
    m_receipt.structSize = sizeof(m_receipt);
    m_receipt.kind = kind;
    m_receipt.endpoint = m_control->token;
    m_receipt.taskSequence = !independentInvocation && s_uiTaskLineage
        && equalToken(s_uiTaskLineage->endpoint, m_control->token)
        ? s_uiTaskLineage->taskSequence : nextSequence(m_control->taskSequence);
    m_receipt.scopeSequence = nextSequence(m_control->scopeSequence);
    if (!m_receipt.taskSequence || !m_receipt.scopeSequence) { m_control->failed = true; return; }
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        m_receipt.identity = m_control->identity;
    }
    m_control->scopes.fetch_add(1);
    try { m_entered = m_control->hooks.onNativeEnter(m_control->hooks.userData, &m_receipt) == 1; }
    catch (...) { m_control->failed = true; }
    if (!m_entered) m_control->scopes.fetch_sub(1);
}
BrokerNativeScope::~BrokerNativeScope()
{
    if (!m_control || !m_entered) return;
    try { m_control->hooks.onNativeLeave(m_control->hooks.userData, &m_receipt); }
    catch (...) { m_control->failed = true; }
    m_control->scopes.fetch_sub(1);
}
// Private stage-0 ABI mirror. Keep in step with the experiment header; never
// add these declarations to the portable public wke/SDK headers.
enum Stage0Status { Stage0Ok, Stage0Invalid, Stage0WrongThread, Stage0Stale,
    Stage0Busy, Stage0Closing, Stage0Failed, Stage0Limit };
struct Stage0Input {
    unsigned int structSize, kind, message, flags;
    int x, y, delta, key;
};
struct Stage0Snapshot {
    unsigned int structSize, abiVersion;
    unsigned __int64 session;
    unsigned int attached, closing, detached, failed;
    unsigned int pauseDepth, dispatchDepth, queued, pinsHeld, pendingCallbacks;
    unsigned __int64 pauseEntries, pauseExits, microtaskCheckpoints;
    unsigned __int64 posted, delivered, discarded, closeRequests;
    unsigned int resourceActive, resourceQueued, resourceClosing, resourceDispatcherAlive;
    unsigned __int64 resourceHits, resourceDenied, resourceBytes, resourceManagerEscapes, resourceUnsupported;
};
struct Stage0Api {
    unsigned int structSize, abiVersion;
    unsigned int (WKE_CALL_TYPE* open)(wkeCursorBrokerEndpoint, const wkeCursorBrokerIdentity*,
        wkeCursorBrokerEndpoint, const wkeCursorBrokerIdentity*, unsigned __int64*);
    unsigned int (WKE_CALL_TYPE* snapshot)(unsigned __int64, Stage0Snapshot*);
    unsigned int (WKE_CALL_TYPE* input)(unsigned __int64, const Stage0Input*);
    unsigned int (WKE_CALL_TYPE* command)(unsigned __int64, unsigned int);
    unsigned int (WKE_CALL_TYPE* requestClose)(unsigned __int64);
    unsigned int (WKE_CALL_TYPE* detach)(unsigned __int64);
    unsigned int (WKE_CALL_TYPE* release)(unsigned __int64);
    unsigned int (WKE_CALL_TYPE* createFrontendResources)(wkeCursorBrokerRealm,
        const wkeCursorBrokerEndpointConfig*, const blink::InspectorSnapshotConfig*,
        wkeCursorBrokerEndpointReceipt*, blink::InspectorSnapshotReceipt*);
};
namespace {
constexpr UINT kStage0Wake = WM_APP + 0x33A;
constexpr size_t kStage0QueueCapacity = 256;
constexpr size_t kStage0MessageBytes = 1024 * 1024;
constexpr size_t kStage0QueueBytes = 8 * 1024 * 1024;
class Stage0Session;
std::mutex s_stage0Mutex;
std::shared_ptr<Stage0Session> s_stage0Session;

class Stage0Session final : public content::ControlledDevToolsSession,
    public std::enable_shared_from_this<Stage0Session> {
public:
    enum Kind { ToAgent, ToClient, Attach, Input, Command };
    struct Entry {
        Kind kind;
        int number = 0;
        std::string message, state;
        Stage0Input input = {};
        size_t bytes() const { return message.size() + state.size(); }
    };
    Stage0Session(std::shared_ptr<CursorBrokerControl> target,
        std::shared_ptr<CursorBrokerControl> frontend)
        : m_target(target), m_frontend(frontend), m_targetIdentity(target->identity),
          m_frontendIdentity(frontend->identity) {
        do { base::RandBytes(&m_id, sizeof(m_id)); } while (!m_id);
    }
    ~Stage0Session() override {
        // Explicit release owns HWND destruction and pin retirement. An
        // unfinished session must remain in the process-lifetime registry.
        RELEASE_ASSERT(!m_window && !m_pinsHeld);
    }
    bool initialize() {
        WNDCLASSW type = {};
        type.lpfnWndProc = windowProc;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"SOUI.MiniBlink.Stage0.Control";
        if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        m_window = CreateWindowExW(0, type.lpszClassName, L"", 0, 0, 0, 0, 0,
            HWND_MESSAGE, nullptr, type.hInstance, this);
        if (!m_window) return false;
        m_target->devToolsBorrows.fetch_add(1);
        m_frontend->devToolsBorrows.fetch_add(1);
        m_pinsHeld = true;
        return true;
    }
    void bind() {
        m_agent = m_target->view->webPage()->webPageImpl()->createOrGetDevToolsAgent();
        m_client = m_frontend->view->webPage()->webPageImpl()->createOrGetDevToolsClient();
        const auto session = shared_from_this();
        m_agent->setControlledSession(session);
        m_client->setControlledSession(session);
        m_agent->setDevToolsClient(m_client);
        m_client->setDevToolsAgent(m_agent);
    }
    unsigned __int64 id() const { return m_id; }
    bool enqueueToAgent(const std::string& message) override {
        if (message.size() > kStage0MessageBytes) { m_failed = true; requestClose(); return false; }
        Entry entry; entry.kind = ToAgent; entry.message = message;
        return enqueue(std::move(entry)) == Stage0Ok;
    }
    bool enqueueToClient(int callId, const std::string& response, const std::string& state) override {
        if (response.size() > kStage0MessageBytes || state.size() > kStage0MessageBytes
            || response.size() + state.size() > kStage0MessageBytes) {
            m_failed = true; requestClose(); return false;
        }
        Entry entry; entry.kind = ToClient; entry.number = callId;
        entry.message = response; entry.state = state;
        return enqueue(std::move(entry)) == Stage0Ok;
    }
    bool enqueueAttach(int clientId) override {
        Entry entry; entry.kind = Attach; entry.number = clientId;
        return enqueue(std::move(entry)) == Stage0Ok;
    }
    void attached() override { m_attached = true; }
    void detached() override { m_attached = false; }
    void enterPause() override {
        m_pauseDepth.fetch_add(1); m_pauseEntries.fetch_add(1);
        if (m_closing) wake();
    }
    void exitPause() override {
        if (!m_pauseDepth) { m_failed = true; return; }
        m_pauseDepth.fetch_sub(1); m_pauseExits.fetch_add(1);
        // Do not detach or release: WebKit and the original JS invocation
        // still borrow both sides after returning from this callback.
    }
    void pausedMicrotaskCheckpoint() override { m_microtasks.fetch_add(1); }
    bool reserveInterrupt(size_t bytes) override {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_closing || m_detached) return false;
        if (m_pendingInterrupts >= kStage0QueueCapacity || bytes > kStage0MessageBytes
            || bytes > kStage0QueueBytes - m_interruptBytes) {
            m_failed = true; m_closing = true; wake(); return false;
        }
        m_interruptBytes += bytes; m_pendingInterrupts.fetch_add(1); return true;
    }
    void interruptDisposed(size_t bytes) override {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        RELEASE_ASSERT(m_pendingInterrupts && bytes <= m_interruptBytes);
        m_interruptBytes -= bytes; m_pendingInterrupts.fetch_sub(1);
    }
    bool acceptsInterrupt() override { return !m_closing && !m_detached && current(); }
    unsigned int input(const Stage0Input* input) {
        if (!input || input->structSize != sizeof(*input) || input->kind < 1 || input->kind > 7)
            return Stage0Invalid;
        if (input->kind == 1 && input->message != WM_MOUSEMOVE && input->message != WM_LBUTTONDOWN
            && input->message != WM_LBUTTONUP && input->message != WM_LBUTTONDBLCLK
            && input->message != WM_RBUTTONDOWN && input->message != WM_RBUTTONUP
            && input->message != WM_MBUTTONDOWN && input->message != WM_MBUTTONUP)
            return Stage0Invalid;
        if (input->kind == 6 && (input->x < 0 || input->y < 0 || input->x > 16384 || input->y > 16384))
            return Stage0Invalid;
        if (input->kind >= 3 && input->kind <= 5
            && (!content::inspectorKeyboardRecordValid(input->kind, input->message, input->x, input->y, input->delta)
                || input->key < 0 || input->key > 0xffff))
            return Stage0Invalid;
        Entry entry; entry.kind = Input; entry.input = *input;
        return enqueue(std::move(entry));
    }
    unsigned int command(unsigned int command) {
        if (command < 1 || command > 8) return Stage0Invalid;
        Entry entry; entry.kind = Command; entry.number = static_cast<int>(command);
        return enqueue(std::move(entry));
    }
    unsigned int requestClose() {
        if (m_detached) return Stage0Closing;
        m_closeRequests.fetch_add(1);
        m_closing = true;
        if (m_frontend->inspectorResources) m_frontend->inspectorResources->close();
        // Dedicated coalesced close flag has no ordinary-queue capacity cost.
        // Every pause entry posts a wake too, so a racing pause cannot hide it.
        return wake() ? Stage0Ok : Stage0Failed;
    }
    void documentCommitted(wkeWebView view, bool mainDocument) {
        if (!mainDocument) return;
        if (view == m_target->view) requestClose();
        // Binding must precede the trusted frontend's first resource load.
        // Any later frontend navigation invalidates this experimental session.
        if (view == m_frontend->view) {
            if (m_frontendDocuments.fetch_add(1)) requestClose();
        }
    }
    void snapshot(Stage0Snapshot* out) {
        out->abiVersion = content::InspectorControlledApiVersion; out->session = m_id;
        out->attached = m_attached ? 1U : 0U; out->closing = m_closing ? 1U : 0U;
        out->detached = m_detached ? 1U : 0U; out->failed = m_failed ? 1U : 0U;
        out->pauseDepth = m_pauseDepth; out->dispatchDepth = m_dispatchDepth;
        out->pinsHeld = m_pinsHeld ? 1U : 0U;
        out->pendingCallbacks = m_pendingInterrupts;
        { std::lock_guard<std::mutex> lock(m_queueMutex); out->queued = static_cast<unsigned int>(m_queue.size()); }
        out->pauseEntries = m_pauseEntries; out->pauseExits = m_pauseExits;
        out->microtaskCheckpoints = m_microtasks;
        out->posted = m_posted; out->delivered = m_delivered;
        out->discarded = m_discarded; out->closeRequests = m_closeRequests;
        const auto resources = m_frontend->inspectorResources;
        out->resourceActive = resources->active(); out->resourceQueued = resources->queued();
        out->resourceClosing = resources->closed(); out->resourceDispatcherAlive = resources->dispatcherAlive();
        out->resourceHits = resources->hits; out->resourceDenied = resources->denied;
        out->resourceBytes = resources->deliveredBytes; out->resourceManagerEscapes = resources->managerEscapes;
        out->resourceUnsupported = resources->unsupported;
    }
    unsigned int detach() {
        if (!ownerEngine()) return Stage0WrongThread;
        if (!m_closing) return Stage0Invalid;
        if (m_pauseDepth || m_dispatchDepth) return Stage0Busy;
        if (m_pendingInterrupts && m_agent) m_agent->discardControlledInterrupts();
        if (m_pendingInterrupts) return Stage0Busy;
        const auto resources = m_frontend->inspectorResources;
        bool finished = false;
        {
            ActionCalloutLease resourceLease(m_frontend);
            if (!resourceLease.entered()) return Stage0Busy;
            if (!blink::finishAndRetireInspectorResourceOwner(resources, finished)) return Stage0Busy;
        }
        if (!finished) return Stage0Busy;
        if (m_detached) return Stage0Ok;
        discardQueue();
        // Root invokes this only from the outer engine dispatch after its
        // original JS/native invocation has returned, not from a pause hook.
        if (m_agent) m_agent->onDetach();
        if (m_agent) { m_agent->setDevToolsClient(nullptr); m_agent->setControlledSession(nullptr); }
        if (m_client) { m_client->setDevToolsAgent(nullptr); m_client->setControlledSession(nullptr); }
        m_agent = nullptr; m_client = nullptr;
        m_attached = false; m_detached = true;
        return Stage0Ok;
    }
    unsigned int release() {
        if (!ownerEngine()) return Stage0WrongThread;
        if (!m_detached || m_pauseDepth || m_dispatchDepth || m_pendingInterrupts) return Stage0Busy;
        discardQueue();
        if (m_window) {
            if (!DestroyWindow(m_window)) { m_failed = true; return Stage0Failed; }
            m_window = nullptr;
        }
        if (m_pinsHeld) {
            m_frontend->devToolsBorrows.fetch_sub(1);
            m_target->devToolsBorrows.fetch_sub(1);
            m_pinsHeld = false;
        }
        return Stage0Ok;
    }
private:
    bool current() {
        std::lock_guard<std::mutex> lock(s_mutex);
        return !m_target->closing && !m_target->failed && !m_frontend->closing && !m_frontend->failed
            && m_frontend->inspectorResources && !m_frontend->inspectorResources->closed()
            && !m_target->documentPending
            && m_target->view && m_frontend->view
            && m_target->identity.viewGeneration == m_targetIdentity.viewGeneration
            && m_target->identity.documentGeneration == m_targetIdentity.documentGeneration
            && m_frontend->identity.viewGeneration == m_frontendIdentity.viewGeneration
            && m_frontend->identity.documentGeneration == m_frontendIdentity.documentGeneration;
    }
    unsigned int enqueue(Entry&& entry) {
        if (m_closing || m_detached) return Stage0Closing;
        if (m_failed) return Stage0Failed;
        const auto bytes = entry.bytes();
        if (bytes > kStage0MessageBytes) { m_failed = true; return Stage0Limit; }
        try {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_closing || m_detached) return Stage0Closing;
            if (m_queue.size() >= kStage0QueueCapacity || bytes > kStage0QueueBytes - m_queueBytes) {
                m_failed = true; m_closing = true; wake();
                return Stage0Limit;
            }
            m_queue.push_back(std::move(entry)); m_queueBytes += bytes; m_posted.fetch_add(1);
        } catch (...) { m_failed = true; return Stage0Failed; }
        return wake() ? Stage0Ok : Stage0Failed;
    }
    bool wake() {
        bool expected = false;
        if (!m_wakePending.compare_exchange_strong(expected, true)) return true;
        if (!m_window || !PostMessageW(m_window, kStage0Wake, 0, 0)) {
            m_wakePending = false; m_failed = true; return false;
        }
        return true;
    }
    void discardQueue() {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_discarded.fetch_add(m_queue.size()); m_queue.clear(); m_queueBytes = 0;
    }
    static const char* commandText(int command) {
        switch (command) {
        case 1: return "{\"id\":2000000000,\"method\":\"Debugger.enable\"}";
        case 2: return "{\"id\":2000000000,\"method\":\"Debugger.pause\"}";
        case 3: return "{\"id\":2000000000,\"method\":\"Debugger.resume\"}";
        case 4: return "{\"id\":2000000000,\"method\":\"Debugger.stepOver\"}";
        case 5: return "{\"id\":2000000000,\"method\":\"Debugger.stepInto\"}";
        case 6: return "{\"id\":2000000000,\"method\":\"Debugger.stepOut\"}";
        case 7: return "{\"id\":2000000000,\"method\":\"Debugger.setPauseOnExceptions\",\"params\":{\"state\":\"all\"}}";
        case 8: return "{\"id\":2000000000,\"method\":\"Debugger.setPauseOnExceptions\",\"params\":{\"state\":\"none\"}}";
        default: return nullptr;
        }
    }
    void dispatch(const Entry& entry) {
        if (!m_agent || !m_client) { m_failed = true; return; }
        switch (entry.kind) {
        case ToAgent: m_agent->onMessageReceivedFromFronEnd(&entry.message); break;
        case ToClient: m_client->onMessageReceivedFromEmbedder(entry.number, &entry.message, &entry.state); break;
        case Attach: m_agent->onAttach(std::to_string(entry.number)); break;
        case Command: {
            const char* text = commandText(entry.number);
            if (text) { const std::string message(text); m_agent->onMessageReceivedFromFronEnd(&message); }
            break;
        }
        case Input: {
            const auto& input = entry.input; const auto view = m_frontend->view;
            switch (input.kind) {
            case 1: wkeFireMouseEvent(view, input.message, input.x, input.y, input.flags); break;
            case 2: wkeFireMouseWheelEvent(view, input.x, input.y, input.delta, input.flags); break;
            case 3: case 4: case 5: {
                const content::InspectorKeyboardSnapshot keyboard = {
                    static_cast<unsigned int>(input.x), input.flags };
                auto* page = view->webPage()->webPageImpl();
                const LPARAM keyData = static_cast<LPARAM>(static_cast<LONG>(input.flags));
                if (input.kind == 3)
                    page->fireKeyDownEvent(m_frontend->window, input.message, input.key, keyData, &keyboard);
                else if (input.kind == 4)
                    page->fireKeyUpEvent(m_frontend->window, input.message, input.key, keyData, &keyboard);
                else
                    page->fireKeyPressEvent(m_frontend->window, input.message, input.key, keyData, &keyboard);
                break;
            }


            case 6: wkeResize(view, input.x, input.y); break;
            case 7: wkeSetFocus(view); break;
            }
            break;
        }
        }
    }
    void pump() {
        const auto keepAlive = shared_from_this();
        if (!ownerEngine()) { m_failed = true; return; }
        // Nested debugger loops must be able to consume resume/step messages.
        // The counter is a retirement guard, not a ban on this reentrancy.
        m_dispatchDepth.fetch_add(1);
        struct Exit { Stage0Session* session; ~Exit() { session->m_dispatchDepth.fetch_sub(1); } } exit{this};
        if (m_closing) {
            discardQueue();
            if (m_pauseDepth && m_agent) {
                const std::string resume(commandText(3));
                m_agent->onMessageReceivedFromFronEnd(&resume);
            }
            return;
        }
        for (unsigned count = 0; count < 32; ++count) {
            Entry entry;
            {
                std::lock_guard<std::mutex> lock(m_queueMutex);
                if (m_queue.empty()) break;
                entry = std::move(m_queue.front()); m_queue.pop_front(); m_queueBytes -= entry.bytes();
            }
            if (m_closing || !current()) { m_discarded.fetch_add(1); requestClose(); break; }
            try { dispatch(entry); m_delivered.fetch_add(1); }
            catch (...) { m_failed = true; requestClose(); break; }
        }
        bool pending;
        { std::lock_guard<std::mutex> lock(m_queueMutex); pending = !m_queue.empty(); }
        if (pending || m_closing) wake();
    }
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* session = reinterpret_cast<Stage0Session*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            session = static_cast<Stage0Session*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(session));
        }
        if (message == kStage0Wake && session) {
            session->m_wakePending = false;
            try { session->pump(); } catch (...) { session->m_failed = true; }
            return 0;
        }
        if (message == WM_NCDESTROY) SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wparam, lparam);
    }
    std::shared_ptr<CursorBrokerControl> m_target, m_frontend;
    wkeCursorBrokerIdentity m_targetIdentity, m_frontendIdentity;
    content::DevToolsAgent* m_agent = nullptr;
    content::DevToolsClient* m_client = nullptr;
    unsigned __int64 m_id = 0;
    std::atomic<HWND> m_window{nullptr}; // UI wake may race owner-side release.
    std::mutex m_queueMutex;
    std::deque<Entry> m_queue;
    size_t m_queueBytes = 0;
    size_t m_interruptBytes = 0;
    std::atomic<bool> m_attached{false}, m_closing{false}, m_detached{false}, m_failed{false}, m_pinsHeld{false};
    std::atomic<bool> m_wakePending{false};
    std::atomic<unsigned int> m_pauseDepth{0}, m_dispatchDepth{0};
    std::atomic<unsigned int> m_frontendDocuments{0};
    std::atomic<unsigned int> m_pendingInterrupts{0};
    std::atomic<unsigned __int64> m_pauseEntries{0}, m_pauseExits{0}, m_microtasks{0};
    std::atomic<unsigned __int64> m_posted{0}, m_delivered{0}, m_discarded{0}, m_closeRequests{0};
};
void stage0DocumentCommitted(wkeWebView view, bool mainDocument) {
    std::shared_ptr<Stage0Session> session;
    { std::lock_guard<std::mutex> lock(s_stage0Mutex); session = s_stage0Session; }
    if (session) session->documentCommitted(view, mainDocument);
}
std::shared_ptr<Stage0Session> findStage0(unsigned __int64 id) {
    std::lock_guard<std::mutex> lock(s_stage0Mutex);
    return s_stage0Session && s_stage0Session->id() == id ? s_stage0Session : nullptr;
}
// Failure receipts remain cleanup-only owned endpoints. Never erase a token,
// destroy a view, invoke close actions, wait, or allocate while unwinding.
void failFrontendResources(const std::shared_ptr<CursorBrokerControl>& control,
    const std::shared_ptr<blink::InspectorResourceState>& resources) noexcept {
    if (control) {
        control->failed = true;
        // Keep the tombstone/dispatcher owner even when construction threw
        // before the factory's normal state assignment.
        if (!control->inspectorResources) control->inspectorResources = resources;
    }
    if (!resources) return;
    resources->close(); // Atomic admission close; callbacks cannot reopen it.
    try {
        if (control) {
            ActionCalloutLease resourceLease(control);
            if (!resourceLease.entered()) return; // Already failed/closed; retain for normal drain retry.
            bool finished = false;
            blink::finishAndRetireInspectorResourceOwner(resources, finished);
        } else {
            // No registered control can count a release callout. Keep the old
            // dispatcher-only cleanup; the final State owns its fallback.
            resources->finishCloseOnOwner();
        }
    }
    catch (...) { /* Remain closed/owned; normal broker drain can retry. */ }
}
struct FrontendResourceFactoryGuard {
    std::shared_ptr<CursorBrokerControl> control;
    std::shared_ptr<blink::InspectorResourceState> resources;
    blink::InspectorSnapshotReceipt* receipt;
    bool committed = false;
    explicit FrontendResourceFactoryGuard(blink::InspectorSnapshotReceipt* value) noexcept : receipt(value) { }
    ~FrontendResourceFactoryGuard() noexcept {
        if (committed) return;
        failFrontendResources(control, resources);
        std::memset(receipt->virtualBase, 0, sizeof(receipt->virtualBase));
    }
};
unsigned int WKE_CALL_TYPE stage0CreateFrontendResources(wkeCursorBrokerRealm realm,
    const wkeCursorBrokerEndpointConfig* endpointConfig, const blink::InspectorSnapshotConfig* snapshotConfig,
    wkeCursorBrokerEndpointReceipt* endpointReceipt, blink::InspectorSnapshotReceipt* snapshotReceipt) {
    if (!ownerEngine()) return Stage0WrongThread;
    if (!endpointConfig || endpointConfig->structSize != sizeof(*endpointConfig)
        || !endpointReceipt || endpointReceipt->structSize != sizeof(*endpointReceipt)
        || !snapshotConfig || !snapshotReceipt || snapshotReceipt->structSize != sizeof(*snapshotReceipt)) return Stage0Invalid;
    // Even an early broker failure must not leave caller-supplied stale tokens
    // that could be mistaken for a newly created endpoint by this factory.
    endpointReceipt->endpoint = {}; endpointReceipt->webView = nullptr;
    snapshotReceipt->abiVersion = 2; std::memset(snapshotReceipt->virtualBase, 0, sizeof(snapshotReceipt->virtualBase));
    FrontendResourceFactoryGuard factory(snapshotReceipt);
    try {
        unsigned char random[16]; base::RandBytes(random, sizeof(random));
        const char* hex = "0123456789abcdef"; char nonce[33] = {};
        for (unsigned int i = 0; i < sizeof(random); ++i) {
            nonce[2 * i] = hex[random[i] >> 4]; nonce[2 * i + 1] = hex[random[i] & 15];
        }
        factory.resources = blink::InspectorResourceState::create(*snapshotConfig, nonce);
        if (!factory.resources) return Stage0Invalid;
        // Capture the owner before registration/handoff, so exception cleanup
        // never depends on a second registry lookup or lock succeeding.
        const auto created = createEndpointImpl(realm, endpointConfig, endpointReceipt,
            EndpointPresentation::CallbackOnly, &factory.control);
        if (created != WKE_CURSOR_BROKER_OK || !factory.control || !factory.control->view) return Stage0Failed;
        factory.control->inspectorResources = factory.resources;
        auto* view = factory.control->view->webPage()->webViewImpl();
        auto* page = view ? view->page() : nullptr;
        if (!page || !blink::InspectorResourcePolicy::install(*page, factory.resources)) return Stage0Failed;
        page->settings().setPluginsEnabled(false);
        const auto& base = factory.resources->base();
        if (base.size() >= sizeof(snapshotReceipt->virtualBase)) return Stage0Failed;
        std::memcpy(snapshotReceipt->virtualBase, base.c_str(), base.size() + 1);
        factory.committed = true;
        return Stage0Ok;
    } catch (...) { return Stage0Failed; }
}
unsigned int WKE_CALL_TYPE stage0Open(wkeCursorBrokerEndpoint target, const wkeCursorBrokerIdentity* targetIdentity,
    wkeCursorBrokerEndpoint frontend, const wkeCursorBrokerIdentity* frontendIdentity, unsigned __int64* id) {
    if (!ownerEngine()) return Stage0WrongThread;
    if (!id || !targetIdentity || !frontendIdentity || !validIdentity(*targetIdentity) || !validIdentity(*frontendIdentity))
        return Stage0Invalid;
    *id = 0;
    { std::lock_guard<std::mutex> lock(s_stage0Mutex); if (s_stage0Session) return Stage0Busy; }
    const auto targetControl = findToken(target), frontendControl = findToken(frontend);
    if (!targetControl || !frontendControl || targetControl == frontendControl) return Stage0Stale;
    if (targetControl->inspectorResources || !frontendControl->inspectorResources
        || frontendControl->inspectorDocuments || frontendControl->inspectorResources->closed()
        || frontendControl->inspectorResources->started) return Stage0Invalid;
    if (targetControl->closing || targetControl->failed || frontendControl->closing || frontendControl->failed
        || targetControl->documentPending
        || !targetControl->view || !frontendControl->view || !sameIdentity(targetControl->identity, *targetIdentity)
        || !sameIdentity(frontendControl->identity, *frontendIdentity)) return Stage0Stale;
    const auto oldAgent = targetControl->view->webPage()->webPageImpl()->m_devToolsAgent;
    if (oldAgent && oldAgent->isDevToolsClientConnet()) return Stage0Busy;
    try {
        auto* frontendView = frontendControl->view->webPage()->webViewImpl();
        auto* frontendPage = frontendView ? frontendView->page() : nullptr;
        auto* actualPolicy = blink::InspectorResourcePolicy::from(frontendPage);
        if (!actualPolicy || actualPolicy->state() != frontendControl->inspectorResources) {
            failFrontendResources(frontendControl, frontendControl->inspectorResources);
            return Stage0Failed;
        }
        if (!frontendControl->inspectorResources->arm()) return Stage0Invalid;
        auto session = std::make_shared<Stage0Session>(targetControl, frontendControl);
        if (!session->initialize()) {
            failFrontendResources(frontendControl, frontendControl->inspectorResources);
            return Stage0Failed;
        }
        { std::lock_guard<std::mutex> lock(s_stage0Mutex); s_stage0Session = session; }
        *id = session->id(); // Once handed off, failure still owns these pins.
        session->bind();
        return Stage0Ok;
    } catch (...) {
        failFrontendResources(frontendControl, frontendControl->inspectorResources);
        return Stage0Failed;
    }
}
unsigned int WKE_CALL_TYPE stage0Snapshot(unsigned __int64 id, Stage0Snapshot* out) {
    if (!out || out->structSize != sizeof(*out)) return Stage0Invalid;
    auto session = findStage0(id); if (!session) return Stage0Stale;
    session->snapshot(out); return Stage0Ok;
}
unsigned int WKE_CALL_TYPE stage0Input(unsigned __int64 id, const Stage0Input* input) {
    auto session = findStage0(id); if (!session) return Stage0Stale;
    return session->input(input);
}
unsigned int WKE_CALL_TYPE stage0Command(unsigned __int64 id, unsigned int command) {
    auto session = findStage0(id); if (!session) return Stage0Stale;
    return session->command(command);
}
unsigned int WKE_CALL_TYPE stage0Close(unsigned __int64 id) {
    auto session = findStage0(id); return session ? session->requestClose() : Stage0Stale;
}
unsigned int WKE_CALL_TYPE stage0Detach(unsigned __int64 id) {
    auto session = findStage0(id); return session ? session->detach() : Stage0Stale;
}
unsigned int WKE_CALL_TYPE stage0Release(unsigned __int64 id) {
    auto session = findStage0(id); if (!session) return Stage0Stale;
    const auto result = session->release();
    if (result == Stage0Ok) { std::lock_guard<std::mutex> lock(s_stage0Mutex); s_stage0Session.reset(); }
    return result;
}
} // namespace
} // namespace wke

extern "C" __declspec(dllexport) const wke::Stage0Api* WKE_CALL_TYPE wkeStage0GetDevToolsApi(unsigned int version) {
    static const wke::Stage0Api api = { sizeof(api), content::InspectorControlledApiVersion, wke::stage0Open, wke::stage0Snapshot,
        wke::stage0Input, wke::stage0Command, wke::stage0Close, wke::stage0Detach, wke::stage0Release,
        wke::stage0CreateFrontendResources };
    return version == content::InspectorControlledApiVersion ? &api : nullptr;
}
