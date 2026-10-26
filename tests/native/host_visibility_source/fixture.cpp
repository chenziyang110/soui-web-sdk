// Production-bound regression SOURCE; dependency fakes are explicit below.
#define NDEBUG 1
#include "content/browser/HostVisibilityPolicy.h"
#include "content/common/InputFrameTrace.h"
#include "content/common/PaintLifetime.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>
#include <limits>
#include <mutex>
#include <new>
#include <vector>
#include <functional>

namespace fixture { int failures = 0; bool fail_nothrow = false; double now = 10; }
void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept
{ if (fixture::fail_nothrow) return nullptr; try { return ::operator new(bytes); } catch (...) { return nullptr; } }
void operator delete(void* value, const std::nothrow_t&) noexcept { ::operator delete(value); }
namespace fixture {
void require(bool value) { if (!value) { ++failures; std::fputs("host visibility source failure\n", stderr); } }
}
inline void atomicIncrement(int* value) { ++*value; }
inline void atomicDecrement(int* value) { --*value; }
inline void InterlockedExchange(volatile long* value, long next) { *value = next; }
inline void freeV8TempObejctOnOneFrameBefore() { }
namespace WTF {
using Mutex = std::recursive_mutex;
template<class T> class Locker {
public: explicit Locker(T& value) : m_value(value) { value.lock(); } ~Locker() { m_value.unlock(); }
private: T& m_value;
};
double monotonicallyIncreasingTime() { return fixture::now; }
}
namespace blink {
struct WebTraceLocation { };
class WebThread {
public:
    class Task { public: virtual ~Task() = default; virtual void run() = 0; };
    struct Entry { long long delay; std::unique_ptr<Task> task; };
    bool reject = false, owner = true;
    std::vector<Entry> tasks;
    bool isCurrentThread() const { return owner; }
    void postTask(WebTraceLocation, Task* task) { post(task, 0); }
    void postDelayedTask(WebTraceLocation, Task* task, long long delay) { post(task, delay); }
    void post(Task* task, long long delay)
    { std::unique_ptr<Task> owned(task); if (!reject) tasks.push_back({delay, std::move(owned)}); }
    void runAt(size_t index)
    { auto task = std::move(tasks.at(index).task); tasks.erase(tasks.begin() + index); task->run(); }
};
class Platform {
public: static Platform* instance; WebThread owner; static Platform* current() { return instance; }
    WebThread* mainThread() { return &owner; }
};
Platform* Platform::instance = nullptr;
}
#define FROM_HERE blink::WebTraceLocation{}
using WebThreadImpl = blink::WebThread;
struct WebBeginFrameArgs { WebBeginFrameArgs(double, double, double) { } };
struct SkRect { static SkRect MakeWH(int, int) { return {}; } };
namespace content { class WebPageImpl; }
namespace cc {
namespace trace = content::input_frame_trace;
trace::event renderTraceContext() { return {}; }
trace::event& currentRenderTraceContext() { static trace::event value; return value; }
struct RenderTraceContextScope { explicit RenderTraceContextScope(const trace::event&) { } };
struct RenderDurationScope { explicit RenderDurationScope(trace::duration_metric) { } };
void recordRenderStage(trace::stage, const trace::event& = {}) { }
double nextFrameDeadline(double, double now, double interval) { return now + interval; }
void addAndMergeDirty(std::vector<SkRect>* values, const SkRect& value) { if (values->empty()) values->push_back(value); }
struct Rect { bool isEmpty() const { return false; } int width() const { return 100; } int height() const { return 100; } };
struct RetryState { std::mutex mutex; struct Policy { unsigned invalidations = 0; void invalidate() { ++invalidations; } } policy; };
class LayerTreeHost {
public:
    enum class RecordBlockReason { InFlightScene, Destroying, MissingAction, Raster };
    WTF::Mutex m_compositeMutex;
    std::atomic<bool> m_visible{true};
    double m_nextCompositeFrameTime = 50, m_lastPaintTime = 50;
    std::shared_ptr<RetryState> m_uiPaintRetry = std::make_shared<RetryState>();
    std::shared_ptr<content::PaintLifetimeState> activity = std::make_shared<content::PaintLifetimeState>();
    Rect m_clientRect; std::vector<SkRect> m_dirtyRectsForComposite, m_dirtyRectsForUi;
    struct Client { content::WebPageImpl* page = nullptr; void onLayerTreeSetNeedsCommit(); } client;
    Client* m_hostClient = &client;
    bool ready = true, waiting = false;
    unsigned records = 0, drawRequests = 0, transactions = 0;
    void setVisible(bool);
    std::shared_ptr<content::PaintLifetimeState> paintLifetimeState() { return activity; }
    void retryPendingOwnerWork() { }
    void requestDrawFrameToRunIntoCompositeThread() { if (m_visible.load()) ++drawRequests; }
    bool canRecordActions() const { return ready; }
    RecordBlockReason recordBlockReason() const { return RecordBlockReason::Raster; }
    void registerResourceWait() { waiting = true; }
    void clearResourceWait() { waiting = false; }
    void beginRecordActions(bool) { ++transactions; }
    void endRecordActions() { --transactions; }
    trace::event traceContextForFrame() { return {}; }
    void recordDraw() { ++records; }
    void setMainFrameDeadline(uint64_t, double, double) { }
};
#include "host_visible.inc"
}
namespace content {
struct HostVisibilityWakeState;
enum { pageInited = 1, pageDestroyed = 2 };
struct FakeWebView {
    unsigned layouts = 0, frames = 0;
    std::function<void()> duringLayout;
    void beginFrame(const WebBeginFrameArgs&) { ++frames; }
    void layout() { ++layouts; if (duringLayout) { auto call = std::move(duringLayout); call(); } }
};
class WebPageImpl {
public:
    struct DestroyNotif { virtual ~DestroyNotif() = default; virtual void destroy() = 0; };
    std::vector<DestroyNotif*> notices;
    void registerDestroyNotif(DestroyNotif* value) { notices.push_back(value); }
    void unregisterDestroyNotif(DestroyNotif* value)
    { auto at = std::find(notices.begin(), notices.end(), value); if (at != notices.end()) notices.erase(at); }
    std::shared_ptr<HostVisibilityWakeState> m_hostVisibility;
    cc::LayerTreeHost host; cc::LayerTreeHost* m_layerTreeHost = &host;
    FakeWebView web; FakeWebView* m_webViewImpl = &web;
    int m_state = pageInited, m_commitCount = 0, m_needsLayout = 1, m_layerDirty = 1;
    int m_executeMainFrameCount = 0, m_autoRecordActionsCount = 0;
    bool m_isEnterDebugLoop = false, m_isDragging = false;
    double m_nextMainFrameTime = 0, m_mainFrameInterval = 0, m_lastFrameTimeMonotonic = 0;
    uint64_t m_renderFrameId = 0;
    WebPageImpl(); ~WebPageImpl(); void close();
    bool setHostVisible(bool); bool scheduleVisibilityRestore(); bool hasVisibilityOwnerRetry() const;
    void retryVisibilityOwnerWork(); void scheduleCommit(long long); void setNeedsCommit();
    void setNeedsCommitAndNotLayout(); void beginMainFrame(); void executeMainFrame();
};
#include "visibility_state.inc"
#include "auto_record.inc"
#include "commit_task.inc"
#include "visibility_owner.inc"
#include "main_frame.inc"
WebPageImpl::WebPageImpl() : m_hostVisibility(std::make_shared<HostVisibilityWakeState>(this)) { host.client.page = this; }
void WebPageImpl::close()
{
    // Same owner tombstone/notification sequence as doClose; no fake fence readiness.
    m_hostVisibility->page = nullptr; m_hostVisibility->policy.close();
    for (auto* notice : notices) notice->destroy();
    m_state = pageDestroyed;
}
WebPageImpl::~WebPageImpl() { close(); }
}
void cc::LayerTreeHost::Client::onLayerTreeSetNeedsCommit() { page->setNeedsCommit(); }
namespace content { namespace input_frame_trace { void note_causal_loss(uint64_t) noexcept { } } }

int main()
{
    blink::Platform platform; blink::Platform::instance = &platform;
    using fixture::require;
    {
        content::WebPageImpl page;
        page.m_mainFrameInterval = .5; page.m_nextMainFrameTime = fixture::now + .5;
        page.setNeedsCommit(); require(page.m_commitCount == 1 && platform.owner.tasks.size() == 1);
        require(platform.owner.tasks[0].delay > 0);
        page.host.m_dirtyRectsForUi.push_back({}); // Retained legacy damage, not a new independent copy.
        require(page.setHostVisible(false));
        for (int n = 0; n != 5; ++n) { page.setNeedsCommit(); page.beginMainFrame(); }
        page.host.ready = false; page.host.waiting = true; page.host.ready = true;
        page.setNeedsCommit(); // Real completion eligibility while hidden adds no owner work.
        require(platform.owner.tasks.size() == 1 && page.host.records == 0 && page.m_needsLayout);
        require(page.setHostVisible(true));
        require(platform.owner.tasks.size() == 2 && platform.owner.tasks[1].delay == 0);
        require(page.m_nextMainFrameTime == 0 && page.host.m_nextCompositeFrameTime == 0);
        platform.owner.runAt(1); // Fresh wake overtakes old delayed commit.
        require(page.host.records == 1 && page.web.layouts == 1 && page.m_commitCount == 1);
        require(!page.host.m_dirtyRectsForComposite.empty() && !page.host.m_dirtyRectsForUi.empty());
        require(page.setHostVisible(true)); platform.owner.runAt(1);
        require(page.host.records == 2); // Same true must refresh after a coalesced transition.
        platform.owner.runAt(0); require(page.m_commitCount == 0);
    }
    {
        content::WebPageImpl page; platform.owner.reject = true;
        require(!page.setHostVisible(true) && page.hasVisibilityOwnerRetry());
        platform.owner.reject = false; fixture::fail_nothrow = true;
        page.retryVisibilityOwnerWork(); require(page.hasVisibilityOwnerRetry());
        fixture::fail_nothrow = false; page.retryVisibilityOwnerWork();
        require(platform.owner.tasks.size() == 1); page.host.ready = false;
        platform.owner.runAt(0); require(page.host.records == 0 && page.host.waiting);
        require(platform.owner.tasks.empty() && !page.hasVisibilityOwnerRetry());
        page.host.ready = true; page.setNeedsCommit();
        require(platform.owner.tasks.size() == 1); platform.owner.runAt(0);
        require(page.host.records == 1);
        platform.owner.reject = true; page.setNeedsCommit();
        require(page.m_commitCount == 0); platform.owner.reject = false;
    }
    {
        content::WebPageImpl page;
        page.web.duringLayout = [&] { require(page.setHostVisible(false)); };
        require(page.setHostVisible(true)); platform.owner.runAt(0);
        require(page.host.records == 0 && page.m_needsLayout && page.m_layerDirty);
        require(page.host.transactions == 0 && platform.owner.tasks.empty());
        require(page.setHostVisible(true)); platform.owner.runAt(0); require(page.host.records == 1);
        bool closePending = false;
        page.web.duringLayout = [&] {
            auto& runtime = content::paintLifetimeRuntime(); std::lock_guard<std::mutex> lock(runtime.mutex);
            closePending = !page.host.activity->policy.request(content::PaintCloseKind::view);
            require(page.host.activity->policy.active > 0);
        };
        require(page.setHostVisible(true)); platform.owner.runAt(0);
        require(closePending && page.host.activity->policy.active == 0);
        // The actual close owner can now consume its retained request.
        require(page.host.activity->policy.request(content::PaintCloseKind::view));
        page.host.activity->policy.finish(content::PaintCloseKind::view);
    }
    {
        content::WebPageImpl page;
        platform.owner.owner = false;
        require(!page.setHostVisible(false) && page.m_hostVisibility->policy.visible());
        platform.owner.owner = true;
        require(page.setHostVisible(false) && page.setHostVisible(false));
        require(platform.owner.tasks.empty());
        require(page.setHostVisible(true)); auto state = page.m_hostVisibility;
        page.close(); platform.owner.tasks.clear(); // Actual canceled receipt disposal after tombstone.
        require(!state->page && !state->policy.retry());
    }
    {
        auto page = std::make_unique<content::WebPageImpl>();
        require(page->setHostVisible(true)); auto state = page->m_hostVisibility;
        page.reset(); platform.owner.runAt(0);
        require(!state->page && !state->policy.visible() && !state->policy.retry());
    }
    return fixture::failures ? 1 : 0;
}
