#include "wke/CursorDescriptorBroker.h"
#include "wke/wkeWebView.h"
#include "wke/wkeGlobalVar.h"
#include "content/browser/WebPage.h"
#include "content/browser/WebPageImpl.h"
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
        && !control.scopes && !control.engineCallbacks && !control.oleRegistered
        && !control.comReferences && !control.nativeResources
        && !control.nativeResourceAdmissionFault
        && (!control.view || control.view->paintCallbacksDrained());
}
void receipt(const CursorBrokerControl& control, wkeCursorBrokerDrainReceipt* out)
{
    out->closing = control.closing ? 1 : 0;
    out->endpoint = control.token;
    out->queuedUiTasks = control.queued;
    out->runningUiTasks = control.running;
    out->nativeScopes = control.scopes;
    out->engineCallbacks = control.engineCallbacks;
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
unsigned int WKE_CALL_TYPE createEndpoint(wkeCursorBrokerRealm realm, const wkeCursorBrokerEndpointConfig* config, wkeCursorBrokerEndpointReceipt* out)
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
        control->view->webPage()->setNeedAutoDrawToHwnd(false);
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
    if (control->view) control->view->quiescePaintCallbacks();
    const bool alreadyClosing = control->closing.exchange(true);
    if (!alreadyClosing) {
        if (control->publishSequence == (std::numeric_limits<unsigned __int64>::max)())
            control->failed = true;
        else
            control->closeSequence = ++control->publishSequence;
    } else {
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
} // namespace wke
