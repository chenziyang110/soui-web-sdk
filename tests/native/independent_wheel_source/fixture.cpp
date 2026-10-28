// Production-bound regression SOURCE. No native window/compositor is created.
#include "cc/trees/ScrollTreeSnapshot.h"
#include "cc/trees/DrawFrameWakePolicy.h"
#include "cc/trees/RenderDiagnosticTrace.h"
#include "cc/trees/LegacyPaintRetryPolicy.h"
#include "cc/trees/ResourceNotificationPolicy.h"
#include "content/common/PaintLifetime.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <vector>

namespace fixture { int fail_after = -1, failures = 0; }
void* operator new(std::size_t bytes)
{
    if (fixture::fail_after == 0) throw std::bad_alloc();
    if (fixture::fail_after > 0) --fixture::fail_after;
    if (void* result = std::malloc(bytes ? bytes : 1)) return result;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept { try { return ::operator new(bytes); } catch (...) { return nullptr; } }
void operator delete(void* value, const std::nothrow_t&) noexcept { std::free(value); }
namespace fixture {
void require(bool value) { if (!value) { ++failures; std::fputs("independent wheel source failure\n", stderr); } }
}
// Explicit dependency fake for the recorder's allocation-free TLS API. No
// producer registration, record or clock is used by these admission cases.
namespace content { namespace input_frame_trace {
record_result record(event value) noexcept
{
    fixture::require(value.point == stage::interaction_diagnostic && value.value && value.value <= 20);
    fixture::require(!value.qpc && !value.view_id && !value.view_generation && !value.document_generation
        && !value.geometry_generation && !value.input_first && !value.input_last && !value.task_id
        && !value.frame_id && !value.action_first && !value.action_last && !value.related_id && !value.flags
        && value.why == reason::none && value.outcome == terminal::none);
    return record_result::scalar_only;
}
static thread_local event fixture_context;
static thread_local bool fixture_context_valid = false;
bool current_context(event& value) noexcept { if (!fixture_context_valid) return false; value = fixture_context; return true; }
bool set_current_context(const event& value) noexcept { fixture_context = value; fixture_context_valid = true; return true; }
void clear_current_context() noexcept { fixture_context_valid = false; }
context_scope::context_scope(const event& value) noexcept
{
    previous_valid_ = current_context(previous_);
    installed_ = set_current_context(value);
}
context_scope::~context_scope() { if (installed_) { if (previous_valid_) set_current_context(previous_); else clear_current_context(); } }
} }
namespace WTF {
using Mutex = std::recursive_mutex;
template<class T> class Locker {
public: explicit Locker(T& mutex) : m_mutex(mutex) { mutex.lock(); } ~Locker() { m_mutex.unlock(); }
private: T& m_mutex;
};
}
namespace blink {
struct WebTraceLocation {};
class WebThread { public: class Task { public: virtual ~Task() = default; virtual void run() = 0; }; };
}
#define FROM_HERE blink::WebTraceLocation{}
inline void atomicIncrement(int* value) { ++*value; }
inline void atomicDecrement(int* value) { --*value; }
namespace fixture {
struct Scheduler {
    bool reject = false;
    uint64_t posted_input = 0;
    std::vector<std::unique_ptr<blink::WebThread::Task>> tasks;
    void postTask(blink::WebTraceLocation, blink::WebThread::Task* value)
    {
        std::unique_ptr<blink::WebThread::Task> owned(value);
        content::input_frame_trace::event context;
        if (content::input_frame_trace::current_context(context)) posted_input = context.input_first;
        if (reject) return;
        try { tasks.push_back(std::move(owned)); } catch (...) { }
    }
    void postDelayedTask(blink::WebTraceLocation location, blink::WebThread::Task* value, long long delay)
    { require(delay > 0); postTask(location, value); }
    void runOne()
    {
        if (tasks.empty()) return;
        auto task = std::move(tasks.front()); tasks.erase(tasks.begin()); task->run();
    }
};
struct Composite { Scheduler owner; Scheduler& platformThread() { return owner; } };
}
namespace cc {
namespace trace = content::input_frame_trace;
struct UiPaintRetryState;
struct CompositorScrollInput;
struct CompositorScrollReceipt { uint64_t sceneVersion = 0, inputSequence = 0; double offsetX = 0, offsetY = 0; };
class LayerTreeHost {
public:
    bool m_isDestroying = false;
    std::atomic<bool> m_visible{true};
    WTF::Mutex m_compositeMutex, m_compositorScrollInputMutex;
    std::shared_ptr<content::PaintLifetimeState> m_paintLifetime = std::make_shared<content::PaintLifetimeState>();
    ScrollTreeSnapshot* m_activeInputScrollTree = nullptr;
    ScrollTreeSnapshot* m_pendingCompositorScrollTree = nullptr;
    std::vector<CompositorScrollInput*> m_pendingCompositorScrollInputs;
    uint64_t m_nextCompositorInputSequence = 0;
    ScrollNodeKey m_latchedScrollNode;
    double m_lastCompositorWheelTime = 0;
    std::atomic<bool> m_hasRejectedDrawWake{false};
    std::shared_ptr<UiPaintRetryState> m_uiPaintRetry;
    fixture::Composite thread;
    fixture::Composite* m_compositeThread = &thread;
    unsigned processed = 0;
    LayerTreeHost(); ~LayerTreeHost();
    bool handleCompositorWheelInput(int, int, double, double, double, bool,
        CompositorScrollReceipt*, const content::input_frame_trace::event*);
    void requestDrawFrameToRunIntoCompositeThread(); void requestDrawFrameAfterDelay(long long);
    void queueDrawFrameWake(long long); void retryPendingOwnerWork();
    void queueLegacyPaintLocked() { }
    void drawFrameInCompositeThread();
};
#include "retry_state.inc"
#include "input_payload.inc"
#include "draw_task.inc"
bool featureEnabledByDefault(const char*) { return true; }
#include "draw_wake.inc"
#include "owner_retry.inc"
#include "wheel.inc"
LayerTreeHost::LayerTreeHost() : m_uiPaintRetry(std::make_shared<UiPaintRetryState>(this)) { }
LayerTreeHost::~LayerTreeHost()
{
    m_uiPaintRetry->host = nullptr; m_uiPaintRetry->drawWake.close(); thread.owner.tasks.clear();
    for (auto* input : m_pendingCompositorScrollInputs) delete input;
    delete m_activeInputScrollTree; delete m_pendingCompositorScrollTree;
}
void LayerTreeHost::drawFrameInCompositeThread()
{
    --m_uiPaintRetry->drawQueuedCount; --m_uiPaintRetry->drawFinishCount;
    // Draw/Skia is a dependency fake; actual wake/admission methods are bound above.
    for (auto* input : m_pendingCompositorScrollInputs) { ++processed; delete input; }
    m_pendingCompositorScrollInputs.clear();
}
}
namespace fixture {
content::input_frame_trace::event identity()
{
    content::input_frame_trace::event value;
    value.view_id = value.view_generation = value.document_generation = value.geometry_generation = 1;
    value.input_first = value.input_last = 1000;
    return value;
}
void setup(cc::LayerTreeHost& host)
{
    auto* tree = new cc::ScrollTreeSnapshot(); tree->setSceneVersion(1); tree->setInputIdentity(identity());
    cc::ScrollNodeSnapshot node; node.key = cc::ScrollNodeKey(1, 1, 7, 1);
    node.sceneVersion = node.scrollEpoch = 1;
    node.minimumOffset = node.baseOffset = cc::ScrollVector(0, 0);
    node.maximumOffset = node.coverageMaximum = cc::ScrollVector(0, 1000);
    node.coverageMinimum = cc::ScrollVector(0, 0);
    node.viewportClip = cc::ScrollRect(0, 0, 100, 100);
    node.inputAxes = cc::ScrollInputAxisVertical; node.coverageComplete = true;
    node.mainThreadReasons = cc::ScrollMainThreadReasonNone; node.setInputPrediction(node.baseOffset);
    tree->addNode(node); tree->addHitTestRegion(cc::ScrollHitTestRegion(node.viewportClip, node.key));
    host.m_activeInputScrollTree = tree;
}
double prediction(cc::LayerTreeHost& host) { return host.m_activeInputScrollTree->nodes()[0].inputPrediction.y; }
}
int main()
{
    auto input = fixture::identity();
    cc::CompositorScrollReceipt receipt;
    {
        cc::LayerTreeHost host; fixture::setup(host);
        fixture::fail_after = 0; // Payload allocation.
        const bool accepted = host.handleCompositorWheelInput(5, 5, 0, 10, 100, false, &receipt, &input);
        fixture::fail_after = -1;
        fixture::require(!accepted && !receipt.inputSequence && fixture::prediction(host) == 0);
        fixture::fail_after = 1; // Payload succeeds, vector reserve fails.
        const bool reserved = host.handleCompositorWheelInput(5, 5, 0, 10, 100, false, &receipt, &input);
        fixture::fail_after = -1;
        fixture::require(!reserved && fixture::prediction(host) == 0 && host.m_pendingCompositorScrollInputs.empty());
        host.m_nextCompositorInputSequence = (std::numeric_limits<uint64_t>::max)();
        fixture::require(!host.handleCompositorWheelInput(5, 5, 0, 10, 100, false, &receipt, &input));
        fixture::require(fixture::prediction(host) == 0);
    }
    for (bool rejectPost : {false, true}) {
        cc::LayerTreeHost host; fixture::setup(host); host.m_pendingCompositorScrollInputs.reserve(16);
        host.thread.owner.reject = rejectPost;
        if (!rejectPost) fixture::fail_after = 1; // Accepted payload, then wake Task OOM.
        const bool accepted = host.handleCompositorWheelInput(5, 5, 0, 10, 100, false, &receipt, &input);
        fixture::fail_after = -1;
        fixture::require(accepted && fixture::prediction(host) == 10 && host.m_pendingCompositorScrollInputs.size() == 1);
        fixture::require(host.m_hasRejectedDrawWake && !host.m_uiPaintRetry->drawQueuedCount && host.m_uiPaintRetry->drawWake.needsRetry());
        host.thread.owner.reject = false; host.retryPendingOwnerWork(); host.thread.owner.runOne();
        fixture::require(host.processed == 1 && fixture::prediction(host) == 10 && !host.m_uiPaintRetry->drawQueuedCount);
    }
    {
        cc::LayerTreeHost host; fixture::setup(host);
        fixture::require(host.handleCompositorWheelInput(5, 5, 0, 10, 100, false, &receipt, &input));
        auto second = input; ++second.input_first; ++second.input_last;
        fixture::require(host.handleCompositorWheelInput(5, 5, 0, 10, 100, false, &receipt, &second));
        fixture::require(host.thread.owner.tasks.size() == 1 && host.thread.owner.posted_input == input.input_first);
        host.thread.owner.runOne(); fixture::require(host.processed == 2 && fixture::prediction(host) == 20);
    }
    {
        cc::LayerTreeHost host; fixture::setup(host);
        auto newer = input; ++newer.geometry_generation;
        fixture::require(!host.handleCompositorWheelInput(5, 5, 0, 10, 100, false, &receipt, &newer));
        fixture::require(fixture::prediction(host) == 0);
        fixture::require(host.handleCompositorWheelInput(5, 5, 0, 10, 100, false, &receipt, &input));
        fixture::require(host.thread.owner.posted_input == input.input_first);
        host.thread.owner.tasks.clear(); // Accepted wake cancelled before execution/registration.
        fixture::require(!host.m_uiPaintRetry->drawQueuedCount && host.m_uiPaintRetry->drawWake.needsRetry());
        host.retryPendingOwnerWork(); host.thread.owner.runOne(); fixture::require(host.processed == 1);
        host.requestDrawFrameToRunIntoCompositeThread();
        host.m_uiPaintRetry->host = nullptr; host.m_uiPaintRetry->drawWake.close();
        fixture::require(host.m_uiPaintRetry->drawQueuedCount == 1 && host.m_uiPaintRetry->drawFinishCount == 1);
        host.thread.owner.tasks.clear(); // Actual disposal retires the queued obligation after close.
        fixture::require(host.m_uiPaintRetry->drawQueuedCount == 0 && host.m_uiPaintRetry->drawFinishCount == 0);
    }
    {
        cc::LayerTreeHost host; fixture::setup(host);
        fixture::require(host.handleCompositorWheelInput(5, 5, 0, 10, 100, false, &receipt, &input));
        const auto closing = host.m_uiPaintRetry;
        fixture::require(closing->drawQueuedCount == 1 && closing->drawFinishCount == 1);
        closing->host = nullptr; closing->drawWake.close();
        host.thread.owner.runOne(); // Closed queued task runs no host code, then is actually disposed.
        fixture::require(closing->drawQueuedCount == 0 && closing->drawFinishCount == 0);
        fixture::require(host.processed == 0 && host.m_pendingCompositorScrollInputs.size() == 1);
    }
    return fixture::failures ? 1 : 0;
}
