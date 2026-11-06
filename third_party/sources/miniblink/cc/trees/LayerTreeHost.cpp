#include <atomic>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>
#include <new>

#include <algorithm>
#include "cc/trees/LayerTreeHost.h"
#include "cc/trees/RenderDiagnosticTrace.h"
#include "cc/trees/FrameDeadlinePolicy.h"
#include "cc/trees/DirtyRegionPolicy.h"
#include "cc/trees/ResourceNotificationPolicy.h"
#include "cc/trees/RenderTrace.h"
#if defined(SOUI_GPU_COMPOSITOR)
#include "cc/trees/GpuSkiaSurface.h"
#endif

#include "SkCanvas.h"
#include "skia/ext/bitmap_platform_device_win.h"
#include "skia/ext/platform_canvas.h"

#include "cc/blink/WebLayerImpl.h"
#include "cc/blink/WebLayerImplClient.h"
#include "cc/blink/WebScrollbarLayerImpl.h"
#include "cc/blink/WebCompositorAnimationImpl.h"
#include "cc/base/MathUtil.h"
#include "cc/trees/DrawProperties.h"
#include "cc/trees/LayerSorter.h"
#include "cc/trees/ActionsFrameGroup.h"
#include "cc/trees/LayerTreeHostClient.h"
#include "cc/tiles/Tile.h"
#include "cc/tiles/TileGrid.h"
#include "cc/raster/RasterTask.h"
#include "cc/layers/CompositingLayer.h"
#include "cc/playback/LayerChangeAction.h"

#include "platform/RuntimeEnabledFeatures.h"
#include "third_party/WebKit/public/web/WebViewClient.h"
#include "third_party/WebKit/public/platform/WebFloatSize.h"
#include "third_party/WebKit/public/platform/WebGestureCurveTarget.h"
#include "third_party/WebKit/public/platform/Platform.h"
#include "third_party/WebKit/public/platform/WebThread.h"
#include "third_party/WebKit/Source/platform/WebThreadSupportingGC.h"
#include "third_party/WebKit/Source/platform/graphics/GraphicsContext.h" // TODO
#include "third_party/WebKit/Source/platform/geometry/win/IntRectWin.h"
#include "wke/wkeWebView.h"
#include "wke/RendererDiagnosticsState.h"
#include "wke/RendererModelState.h"

extern DWORD g_nowTime;

using namespace blink;

namespace cc {

struct UiPaintRetryState {
    explicit UiPaintRetryState(LayerTreeHost* target) : host(target) { }
    std::mutex mutex;
    LayerTreeHost* host;
    LegacyPaintRetryPolicy policy;
    ResourceNotificationPolicy resource;
    DrawFrameWakePolicy drawWake;
    std::atomic<int> drawQueuedCount{0};
    std::atomic<int> drawFinishCount{0};
    bool recordWaiting = false;
    bool rerasterRequired = false;
    bool drawWaiting = false;
    bool notificationFailed = false;
};

// The queue owns this object. Rejection/cancellation disposes only its ticket;
// dirty rectangles remain owned by LayerTreeHost until a successful dispatch.
class UiPaintRetryTask : public blink::WebThread::Task {
public:
    UiPaintRetryTask(const std::shared_ptr<UiPaintRetryState>& state,
        const LegacyPaintRetryPolicy::Ticket& ticket)
        : m_state(state), m_ticket(ticket) { }
    ~UiPaintRetryTask() override
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        if (!m_ran && m_state->host && m_state->policy.accepts(m_ticket))
            m_state->notificationFailed = true;
        m_state->policy.dispose(m_ticket);
    }
    void run() override
    {
        m_ran = true;
        LayerTreeHost* host;
        {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            host = m_state->host;
        }
        // Host creation, destruction, resize and this callback share the
        // Blink owner. The retained state remains valid after host teardown.
        if (host)
            host->runLegacyPaintRetry(m_ticket);
    }
private:
    std::shared_ptr<UiPaintRetryState> m_state;
    LegacyPaintRetryPolicy::Ticket m_ticket;
    bool m_ran = false;
};

class DrawFrameTask final : public blink::WebThread::Task {
public:
    DrawFrameTask(const std::shared_ptr<UiPaintRetryState>& state, uint64_t ticket)
        : m_state(state), m_ticket(ticket) { }
    ~DrawFrameTask() override
    {
        if (m_ran)
            return;
        std::lock_guard<std::mutex> lock(m_state->mutex);
        const bool rejected = m_state->drawWake.reject(m_ticket);
        // Every constructed/posted task owns one actual counter obligation.
        // Retire it even after close clears the raw host and invalidates tickets.
        m_state->drawQueuedCount.fetch_sub(1);
        m_state->drawFinishCount.fetch_sub(1);
        if (rejected && m_state->host)
            m_state->host->m_hasRejectedDrawWake.store(true);
    }
    void run() override
    {
        LayerTreeHost* host = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            if (m_state->drawWake.claim(m_ticket)) {
                host = m_state->host;
                m_ran = host != nullptr;
                if (host)
                    host->m_hasRejectedDrawWake.store(false);
            }
        }
        // Only the composite owner executes this task; host teardown joins it.
        if (host)
            host->drawFrameInCompositeThread();
    }
private:
    std::shared_ptr<UiPaintRetryState> m_state;
    uint64_t m_ticket;
    bool m_ran = false;
};

static void queueResourceProgress(const std::shared_ptr<UiPaintRetryState>& state,
    bool failureRetry = false);

class ResourceProgressTask : public blink::WebThread::Task {
public:
    ResourceProgressTask(const std::shared_ptr<UiPaintRetryState>& state,
        uint64_t ticket, uint64_t generation, bool failureRetry)
        : m_state(state), m_ticket(ticket), m_generation(generation)
        , m_failureRetry(failureRetry) { }
    ~ResourceProgressTask() override
    {
        bool retry = false;
        {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            if (m_state->resource.dispose(m_ticket)) {
                retry = !m_ran && !m_failureRetry && m_state->host;
                if (!m_ran && m_failureRetry && m_state->host)
                    m_state->notificationFailed = true;
            }
        }
        // One exceptional 100ms fallback after a rejected/cancelled post.
        // A second rejection retains intent for the next owner service call.
        if (retry)
            queueResourceProgress(m_state, true);
    }
    void run() override
    {
        m_ran = true;
        LayerTreeHost* host;
        {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            host = m_state->host;
        }
        if (host)
            host->runResourceProgress(m_ticket, m_generation);
    }
private:
    std::shared_ptr<UiPaintRetryState> m_state;
    uint64_t m_ticket;
    uint64_t m_generation;
    bool m_failureRetry;
    bool m_ran = false;
};

static void queueResourceProgress(const std::shared_ptr<UiPaintRetryState>& state,
    bool failureRetry)
{
    uint64_t ticket = 0;
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->host)
            return;
        const auto armed = state->resource.arm(state->policy.generation());
        ticket = armed.sequence;
        generation = armed.generation;
        if (!ticket)
            return;
    }
    ResourceProgressTask* task = new (std::nothrow) ResourceProgressTask(
        state, ticket, generation, failureRetry);
    if (!task) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->resource.dispose(ticket);
        if (state->host) state->notificationFailed = true;
        return;
    }
    if (!Platform::current() || !Platform::current()->mainThread()) {
        delete task;
        return;
    }
    if (failureRetry)
        Platform::current()->mainThread()->postDelayedTask(FROM_HERE, task, 100);
    else
        Platform::current()->mainThread()->postTask(FROM_HERE, task);
}

struct CompositorAnimationState {
    CompositorAnimationState(int targetLayerId,
        cc_blink::WebCompositorAnimationImpl* compositorAnimation)
        : layerId(targetLayerId)
        , animation(compositorAnimation)
        , cancelled(false)
        , overlayApplied(false)
        , completed(false)
    {
    }

    ~CompositorAnimationState()
    {
        delete animation;
    }

    int layerId;
    cc_blink::WebCompositorAnimationImpl* animation;
    bool cancelled;
    bool overlayApplied;
    bool completed;
};

struct CompositorAnimationFinishedTask {
    CompositorAnimationFinishedTask(LayerTreeHost* targetHost, int targetLayerId,
        int animationGroup, double finishedTime)
        : host(targetHost)
        , layerId(targetLayerId)
        , group(animationGroup)
        , monotonicTime(finishedTime)
    {
    }

    void dispatch()
    {
        LayerTreeHost* targetHost = host;
        if (targetHost) {
            WTF::Locker<WTF::Mutex> locker(targetHost->m_compositeMutex);
            targetHost->m_animationFinishedTasks.remove(this);
        }

        if (targetHost)
            targetHost->dispatchAnimationFinishedOnMainThread(layerId, group, monotonicTime);
        delete this;
    }

    LayerTreeHost* host;
    int layerId;
    int group;
    double monotonicTime;
};

struct CompositorScrollState {
    explicit CompositorScrollState(int targetLayerId)
        : layerId(targetLayerId)
    {
    }

    int layerId;
    ScrollController controller;
};

struct RetiredCompositorScrollState {
    RetiredCompositorScrollState(const ScrollBarrier& retiredBarrier,
        uint64_t retirementScene)
        : barrier(retiredBarrier)
        , retiredAtScene(retirementScene)
    {
    }

    ScrollBarrier barrier;
    uint64_t retiredAtScene;
};

static const size_t kMaximumRetiredCompositorScrollStates = 32;

static void rememberRetiredCompositorScrollState(
    WTF::Vector<RetiredCompositorScrollState*>& retiredStates,
    const ScrollBarrier& barrier, uint64_t retiredAtScene)
{
    if (!barrier.valid)
        return;
    for (size_t i = 0; i < retiredStates.size();) {
        RetiredCompositorScrollState* retired = retiredStates[i];
        if (retired->barrier.key == barrier.key
            && retired->barrier.scrollEpoch == barrier.scrollEpoch) {
            delete retired;
            retiredStates.remove(i);
        } else {
            ++i;
        }
    }
    retiredStates.append(new RetiredCompositorScrollState(
        barrier, retiredAtScene));
    while (retiredStates.size() > kMaximumRetiredCompositorScrollStates) {
        delete retiredStates[0];
        retiredStates.remove(0);
    }
}

struct CompositorScrollInput {
    CompositorScrollInput(const ScrollNodeKey& targetNode,
        uint64_t sequence, const ScrollVector& inputDelta,
        double inputTime, bool useSmoothMotion, uint64_t targetEpoch,
        const content::input_frame_trace::event* identity)
        : node(targetNode)
        , inputSequence(sequence)
        , delta(inputDelta)
        , monotonicTime(inputTime)
        , smooth(useSmoothMotion)
        , epoch(targetEpoch)
        , hasTrace(identity != nullptr)
    {
        if (identity)
            traceIdentity = *identity;
    }

    ScrollNodeKey node;
    uint64_t inputSequence;
    ScrollVector delta;
    double monotonicTime;
    bool smooth;
    uint64_t epoch;
    bool hasTrace;
    content::input_frame_trace::event traceIdentity;
};

static CompositorScrollState* findCompositorScrollState(
    WTF::Vector<CompositorScrollState*>& states, const ScrollNodeKey& key);
static void addCompositorScrollbarDamage(Vector<SkRect>* dirtyRects,
    const ScrollNodeSnapshot& node);

static bool drawCompositorScrollbarInLayer(SkCanvas* canvas,
    const SkRect& dirtyRect, const CompositorScrollbarSnapshot& scrollbar,
    const ScrollNodeSnapshot& node, const ScrollVector& visualOffset)
{
    if (!scrollbar.enabled || !scrollbar.isSupported())
        return false;

    const SkRect fullTrack = SkRect::MakeXYWH(scrollbar.localTrackRect.x,
        scrollbar.localTrackRect.y, scrollbar.localTrackRect.width,
        scrollbar.localTrackRect.height);
    SkRect clippedTrack = fullTrack;
    if (!clippedTrack.intersect(dirtyRect))
        return false;

    SkPaint trackPaint;
    trackPaint.setColor(0xfff1f1f1);
    trackPaint.setXfermodeMode(SkXfermode::kSrcOver_Mode);
    canvas->drawRect(fullTrack, trackPaint);

    const double visual = scrollbar.vertical
        ? visualOffset.y : visualOffset.x;
    const double minimum = scrollbar.vertical
        ? node.minimumOffset.y : node.minimumOffset.x;
    const double maximum = scrollbar.vertical
        ? node.maximumOffset.y : node.maximumOffset.x;
    const double viewportLength = scrollbar.vertical
        ? node.viewportClip.height : node.viewportClip.width;
    const ScrollRect thumb = scrollbar.localThumbRect(visual, minimum,
        maximum, viewportLength);
    if (thumb.width <= 0.0 || thumb.height <= 0.0)
        return true;

    SkPaint thumbPaint;
    thumbPaint.setColor(0xff8f8f8f);
    thumbPaint.setAntiAlias(true);
    thumbPaint.setXfermodeMode(SkXfermode::kSrcOver_Mode);
    canvas->drawRoundRect(SkRect::MakeXYWH(thumb.x, thumb.y,
        thumb.width, thumb.height), 3.0f, 3.0f, thumbPaint);
    return true;
}

struct CompositorScrollUpdateTask {
    CompositorScrollUpdateTask(LayerTreeHost* targetHost, int targetLayerId,
        const ScrollSample& latestSample, const ScrollVector& latestVisualOffset)
        : host(targetHost)
        , layerId(targetLayerId)
        , sample(latestSample)
        , visualOffset(latestVisualOffset)
    {
    }

    void update(const ScrollSample& latestSample,
        const ScrollVector& latestVisualOffset)
    {
        sample = latestSample;
        visualOffset = latestVisualOffset;
    }

    void dispatch()
    {
        LayerTreeHost* targetHost = host;
        ScrollSample sampleToDispatch;
        ScrollVector visualToDispatch;
        if (targetHost) {
            WTF::Locker<WTF::Mutex> locker(targetHost->m_compositeMutex);
            WTF::HashMap<int, CompositorScrollUpdateTask*>::iterator it =
                targetHost->m_scrollUpdateTasks.find(layerId);
            if (it != targetHost->m_scrollUpdateTasks.end()
                && it->value == this) {
                targetHost->m_scrollUpdateTasks.remove(it);
            }
            targetHost = host;
            sampleToDispatch = sample;
            visualToDispatch = visualOffset;
        }

        if (targetHost) {
            targetHost->dispatchCompositorScrollOnMainThread(layerId,
                sampleToDispatch, visualToDispatch);
        }
        delete this;
    }

    LayerTreeHost* host;
    int layerId;
    ScrollSample sample;
    ScrollVector visualOffset;
};

LayerTreeHost* gLayerTreeHost = nullptr;

static void initializeCompositeThread(blink::WebThreadSupportingGC* webThreadSupportingGC)
{
    webThreadSupportingGC->initialize();
}

static bool featureEnabledByDefault(const char* environmentName)
{
    char option[8] = {};
    const DWORD length = GetEnvironmentVariableA(environmentName, option,
        sizeof(option));
    return !(length == 1 && option[0] == '0');
}

#if defined(SOUI_GPU_COMPOSITOR)
// The direct presenter does not need a CPU copy. Keep the scanline readback
// only for diagnostics because glReadPixels plus a temporary buffer adds work
// to every frame and can hide the benefit of the WGL/Ganesh path.
static bool gpuDiagnosticReadbackEnabled()
{
    char option[2] = {};
    return GetEnvironmentVariableA("SOUI_GPU_DIAGNOSTIC_READBACK", option, sizeof(option)) == 1 && option[0] == '1';
}
#endif

// A CPU compositor should not run the historical 3 ms draw throttle. That
// cadence is useful while a real GL surface is presenting at the display rate,
// but on a software-only machine it repeatedly enters raster/paint work that
// cannot be shown before the next 60 Hz pulse. Keep the default CPU budget
// aligned with the host display clock, while allowing a measured experiment to
// override it without another source build.
static double configuredDrawMinInterval()
{
    char overrideValue[32] = {};
    const DWORD length = GetEnvironmentVariableA("SOUI_CPU_DRAW_INTERVAL_MS", overrideValue,
                                                 sizeof(overrideValue));
    if (length > 0 && length < sizeof(overrideValue)) {
        const int milliseconds = std::atoi(overrideValue);
        if (milliseconds >= 0 && milliseconds <= 1000)
            return static_cast<double>(milliseconds) / 1000.0;
    }

#if defined(SOUI_GPU_COMPOSITOR)
    char gpuOption[2] = {};
    const bool gpuRequested = GetEnvironmentVariableA("SOUI_GPU_COMPOSITOR", gpuOption,
                                                       sizeof(gpuOption)) == 1 && gpuOption[0] == '1';
    if (gpuRequested)
        return 0.003;
#endif
    return 1.0 / 60.0;
}

LayerTreeHost::LayerTreeHost(LayerTreeHostClent* hostClient, LayerTreeHostUiThreadClient* uiThreadClient)
{
    m_paintLifetime = std::make_shared<content::PaintLifetimeState>();
    m_rootLayer = nullptr;
    m_rootCCLayer = nullptr;
    m_deviceScaleFactor = 1.0f;
    m_backgroundColor = 0xff00ffff;
    m_hasTransparentBackground = false;
    m_independentPresentationEnabled = false;
    m_visible = true;
    m_pageScaleFactor = 1.0f;
    m_minimum = 1.0f;
    m_maximum = 1.0f;
    m_hostClient = hostClient;
    m_uiThreadClient = uiThreadClient;
    //m_needsFullTreeSync = true;
    m_needTileRender = true;
    m_layerTreeDirty = true;
    m_3dNodesCount = 0;
    m_tilesMutex = new WTF::Mutex();
    m_rasterNotifMutex = new WTF::Mutex();
    m_newestDrawingIndex = 0;
    m_drawingIndex = 1;
    m_actionsFrameGroup = new ActionsFrameGroup(this);
    m_isDestroying = false;
    
    m_memoryCanvas = nullptr;
    m_currentFrameBufferSlot = 0;
    m_frameBufferGeneration = 1;
    m_canvasPaintBlocked = false;
    m_uiPaintRetry = std::make_shared<UiPaintRetryState>(this);
#if defined(SOUI_GPU_COMPOSITOR)
    m_gpuSurface = nullptr;
    m_gpuInitAttempted = false;
    char gpuOption[2] = {};
    m_gpuExperiment = GetEnvironmentVariableA("SOUI_GPU_COMPOSITOR", gpuOption, sizeof(gpuOption)) == 1 && gpuOption[0] == '1';
#endif
    m_drawMinInterval = configuredDrawMinInterval();

    m_isDrawDirty = true;
    m_lastCompositeTime = 0.0;
    m_lastPaintTime = 0.0;
    m_nextCompositeFrameTime = 0.0;
    m_pendingMainFrameId = 0;
    m_presentedMainFrameId = 0;
    m_pendingMainFrameDeadline = 0;
    m_unifiedFrameDeadline = false;
    m_requestApplyActionsCount = 0;
    m_requestApplyActionsFinishCount = 0;
    m_postpaintMessageCount = 0;
    m_hasResize = false;
    m_pendingCompositorScrollTree = nullptr;
    m_activeInputScrollTree = nullptr;
    m_activeCompositorScrollVersion = 0;
    m_nextScrollSceneVersion = 0;
    m_nextCompositorInputSequence = 0;
    m_lastCompositorWheelTime = 0.0;
    m_compositeThread = nullptr;
    if (m_uiThreadClient) {
        m_compositeThread = blink::WebThreadSupportingGC::create("CompositeThread");
        m_compositeThread->platformThread().postTask(FROM_HERE, WTF::bind(&initializeCompositeThread, m_compositeThread.get()));
    }
    gLayerTreeHost = this;
#if defined(ENABLE_WKE) && ENABLE_WKE == 1
    wke::diagnostics::renderer_created();
#endif
}

static void shutdownCompositeThread(blink::WebThreadSupportingGC* webThread,
#if defined(SOUI_GPU_COMPOSITOR)
    GpuSkiaSurface* gpuSurface,
#endif
    std::atomic<int>* waitCount)
{
#if defined(SOUI_GPU_COMPOSITOR)
    // The GL context was made current on this thread. Release Skia and WGL
    // before the thread exits, rather than trying to destroy them on the UI
    // thread after shutdown.
    delete gpuSurface;
#endif
    webThread->shutdown();
    waitCount->fetch_sub(1, std::memory_order_release);
}

LayerTreeHost::~LayerTreeHost()
{
    m_isDestroying = true;
    // Join any pool notification before the shared host state is withdrawn.
    RasterTaskWorkerThreadPool::shared()->unregisterCompletionWaiter(this);

    while (0 != RasterTaskWorkerThreadPool::shared()->getPendingRasterTaskNum()) { ::Sleep(20); }
    requestApplyActionsToRunIntoCompositeThread(true);

    m_compositeMutex.lock();
    trace::event destroyedInput;
    while (m_independentInputTrace.takeAny(&destroyedInput)) {
        destroyedInput.flags = 0; destroyedInput.outcome = trace::terminal::cancelled;
        destroyedInput.why = trace::reason::destroyed;
        recordRenderStage(trace::stage::input_terminal, destroyedInput);
    }
    {
        std::lock_guard<std::mutex> retryLock(m_uiPaintRetry->mutex);
        m_uiPaintRetry->host = nullptr;
        m_uiPaintRetry->policy.close();
        m_uiPaintRetry->drawWake.close();
    }
    for (auto it : m_animationFinishedTasks)
        it->host = nullptr;
    m_animationFinishedTasks.clear();
    for (WTF::HashMap<int, CompositorScrollUpdateTask*>::iterator it =
        m_scrollUpdateTasks.begin(); it != m_scrollUpdateTasks.end(); ++it) {
        it->value->host = nullptr;
    }
    m_scrollUpdateTasks.clear();
    m_compositeMutex.unlock();

    waitForApplyActions();

    ASSERT(0 == m_actionsFrameGroup->getFramesSize());
    if (m_compositeThread) {
        std::atomic<int> waitCount(1);
        blink::WebThreadSupportingGC* webThread = m_compositeThread.leakPtr();
#if defined(SOUI_GPU_COMPOSITOR)
        GpuSkiaSurface* gpuSurface = m_gpuSurface;
        m_gpuSurface = nullptr;
        webThread->platformThread().postTask(FROM_HERE,
            WTF::bind(&shutdownCompositeThread, webThread, gpuSurface, &waitCount));
#else
        webThread->platformThread().postTask(FROM_HERE, WTF::bind(&shutdownCompositeThread, webThread, &waitCount));
#endif
        while (waitCount.load(std::memory_order_acquire) != 0) {
            ::Sleep(100);
        }
        delete webThread;
    }

    m_memoryCanvas = nullptr;
    for (size_t index = 0; index < FrameBufferLeasePolicy::maximumSlots; ++index)
        m_frameBuffers[index].reset();
#if defined(SOUI_GPU_COMPOSITOR)
    // A surface is normally owned and released by the compositor thread.
    // Keep this fallback for hosts that never created that thread.
    delete m_gpuSurface;
    m_gpuSurface = nullptr;
#endif

    for (size_t i = 0; i < m_compositorAnimations.size(); ++i)
        delete m_compositorAnimations[i];
    m_compositorAnimations.clear();
    for (size_t i = 0; i < m_compositorScrollStates.size(); ++i)
        delete m_compositorScrollStates[i];
    m_compositorScrollStates.clear();
    for (size_t i = 0; i < m_retiredCompositorScrollStates.size(); ++i)
        delete m_retiredCompositorScrollStates[i];
    m_retiredCompositorScrollStates.clear();

    {
        WTF::Locker<WTF::Mutex> locker(m_compositorScrollInputMutex);
        for (size_t i = 0; i < m_pendingCompositorScrollInputs.size(); ++i) {
            auto* input = m_pendingCompositorScrollInputs[i];
            if (input->hasTrace) {
                auto value = input->traceIdentity;
                value.flags = 0; value.outcome = trace::terminal::cancelled;
                value.why = trace::reason::destroyed;
                recordRenderStage(trace::stage::input_terminal, value);
            }
            delete input;
        }
        m_pendingCompositorScrollInputs.clear();
        delete m_pendingCompositorScrollTree;
        m_pendingCompositorScrollTree = nullptr;
        delete m_activeInputScrollTree;
        m_activeInputScrollTree = nullptr;
    }
    
    for (WTF::HashMap<int, cc_blink::WebLayerImpl*>::iterator it = m_liveLayers.begin(); m_liveLayers.end() != it; ++it) {
        cc_blink::WebLayerImpl* layer = it->value;
        layer->setLayerTreeHost(nullptr);
    }
    m_liveLayers.clear();

    ASSERT(0 == m_actionsFrameGroup->getFramesSize());

    for (WTF::HashMap<int, CompositingLayer*>::iterator it = m_liveCCLayers.begin(); m_liveCCLayers.end() != it; ++it) {
        CompositingLayer* ccLayer = it->value;
        ccLayer->setParent(nullptr);
        delete ccLayer;
    }
    m_liveCCLayers.clear();

    ASSERT(0 == m_actionsFrameGroup->getFramesSize());
    
    for (size_t i = 0; i < m_dirtyLayersGroup.size(); ++i) {
        DirtyLayers* dirtyLayers = m_dirtyLayersGroup[i];
        delete dirtyLayers;
    }
    m_dirtyLayersGroup.clear();

    ASSERT(0 == m_actionsFrameGroup->getFramesSize());

    delete m_rasterNotifMutex;
    m_rasterNotifMutex = nullptr;

    ASSERT(0 == m_actionsFrameGroup->getFramesSize());

    delete m_tilesMutex;
    m_tilesMutex = nullptr;

    waitForApplyActions();
    ASSERT(0 == m_actionsFrameGroup->getFramesSize());

    delete m_actionsFrameGroup;
    m_actionsFrameGroup = nullptr;
#if defined(ENABLE_WKE) && ENABLE_WKE == 1
    wke::diagnostics::renderer_closed();
#endif
}

void LayerTreeHost::waitForDrawFrame()
{
    int finishCount = 0;
    do {
        m_compositeMutex.lock();
        finishCount = m_uiPaintRetry->drawFinishCount;
        m_compositeMutex.unlock();
        Sleep(20);
    } while (0 != finishCount);
}

void LayerTreeHost::waitForApplyActions()
{
    waitForDrawFrame();

    int finishCount = 0;
    do {
        m_compositeMutex.lock();
        finishCount = m_requestApplyActionsFinishCount;
        m_compositeMutex.unlock();
        Sleep(20);
    } while (0 != finishCount);
}

bool LayerTreeHost::isDestroying() const
{
    return m_isDestroying;
}

void LayerTreeHost::registerLayer(cc_blink::WebLayerImpl* layer)
{
    layer->setBackgroundColor(getRealColor(m_hasTransparentBackground, m_backgroundColor));
    m_liveLayers.add(layer->id(), layer);
}

void LayerTreeHost::unregisterLayer(cc_blink::WebLayerImpl* layer)
{
    m_liveLayers.remove(layer->id());
}

void LayerTreeHost::registerCCLayer(CompositingLayer* layer)
{
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    m_liveCCLayers.add(layer->id(), layer);
}

void LayerTreeHost::unregisterCCLayer(CompositingLayer* layer)
{
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    m_liveCCLayers.remove(layer->id());
}

cc_blink::WebLayerImpl* LayerTreeHost::getLayerById(int id)
{
    WTF::HashMap<int, cc_blink::WebLayerImpl*>::iterator it = m_liveLayers.find(id);
    if (m_liveLayers.end() != it)
        return it->value;

    return nullptr;
}

CompositingLayer* LayerTreeHost::getCCLayerById(int id)
{
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    WTF::HashMap<int, CompositingLayer*>::iterator it = m_liveCCLayers.find(id);
    if (m_liveCCLayers.end() != it)
        return it->value;

    return nullptr;
}

void LayerTreeHost::gc()
{
    for (auto it = m_liveLayers.begin(); it != m_liveLayers.end(); ++it) {
        cc_blink::WebLayerImpl* layer = it->value;
        layer->gc();
    }
}

void LayerTreeHost::increaseNodesCount()
{
    ++m_3dNodesCount;
}

void LayerTreeHost::decreaseNodesCount()
{
    --m_3dNodesCount;
}

bool LayerTreeHost::has3dNodes()
{
    return m_3dNodesCount > 0;
}

WTF::Mutex* LayerTreeHost::tilesMutex()
{
    return m_tilesMutex;
}

int64 LayerTreeHost::createDrawingIndex()
{
    ++m_newestDrawingIndex;
    return m_newestDrawingIndex;
}

int64 LayerTreeHost::drawingIndex()
{
    return m_drawingIndex;
}

void LayerTreeHost::addRasteringIndex(int64 index)
{
    WTF::MutexLocker locker(*m_tilesMutex);
    m_rasteringIndexs.append(index);
}

int64 LayerTreeHost::frontRasteringIndex()
{
    WTF::MutexLocker locker(*m_tilesMutex);
    ASSERT(0 != m_rasteringIndexs.size());
    if (0 == m_rasteringIndexs.size())
        return -1;
    return m_rasteringIndexs[0];
}

void LayerTreeHost::popRasteringIndex()
{
    WTF::MutexLocker locker(*m_tilesMutex);
    if (0 < m_rasteringIndexs.size())
        m_rasteringIndexs.remove(0);
}

void LayerTreeHost::setWebGestureCurveTarget(blink::WebGestureCurveTarget* webGestureCurveTarget)
{
    m_webGestureCurveTarget = webGestureCurveTarget;
}

// void LayerTreeHost::setNeedsCommit() // 暂时被废弃，由setLayerTreeDirty代替
// {
//     // 由光栅化线程来提起脏区域，所以这里直接指定需要开始下一帧，光删化完毕后由光栅化线程通过requestRepaint发起重绘
//     m_hostClient->scheduleAnimation();
// }

void LayerTreeHost::setLayerTreeDirty()
{
    m_layerTreeDirty = true;
    m_hostClient->onLayerTreeDirty();
}

bool LayerTreeHost::isLayerTreeDirty() const
{
    return m_layerTreeDirty;
}

// void LayerTreeHost::setNeedsFullTreeSync()
// {
//     m_needsFullTreeSync = true;
//     //m_hostClient->scheduleAnimation();
// }

void LayerTreeHost::requestRepaint(const blink::IntRect& repaintRect)
{
    if (WTF::isMainThread()) {
        m_hostClient->onLayerTreeInvalidateRect(blink::WebRect(repaintRect));
        return;
    }
    // Action application is compositor-owned. Publish damage directly and
    // wake the owner only through the merged completion path.
    postPaintMessage(SkRect::MakeXYWH(repaintRect.x(), repaintRect.y(),
        repaintRect.width(), repaintRect.height()));
    requestDrawFrameToRunIntoCompositeThread();
    notifyResourceProgress();
}

void LayerTreeHost::requestDrawFrameLocked(DirtyLayers* dirtyLayers, Vector<Tile*>* tilesToUIThreadRelease)
{
    DebugBreak();
}

static bool compareDirtyLayer(DirtyLayers*& left, DirtyLayers*& right)
{
    return left->drawingIndex() < right->drawingIndex();
}

static bool compareAction(LayerChangeAction*& left, LayerChangeAction*& right)
{
    return left->actionId() < right->actionId();
}

LayerTreeHost::RecordBlockReason LayerTreeHost::recordBlockReason() const
{
    if (m_isDestroying)
        return RecordBlockReason::Destroying;
    if (hasInFlightScene())
        return RecordBlockReason::InFlightScene;
    if (!m_actionsFrameGroup)
        return RecordBlockReason::MissingAction;
    if (!m_actionsFrameGroup->containComefromMainframeLocked())
        return RecordBlockReason::None;
    if (RasterTaskWorkerThreadPool::shared()->getPendingRasterTaskNum() > 10)
        return RecordBlockReason::Raster;
    if (m_actionsFrameGroup->getFramesSize() > 30)
        return RecordBlockReason::MissingAction;
    return RecordBlockReason::None;
}

bool LayerTreeHost::canRecordActions() const
{
    return recordBlockReason() == RecordBlockReason::None;
}

void LayerTreeHost::registerResourceWait()
{
    {
        std::lock_guard<std::mutex> lock(m_uiPaintRetry->mutex);
        m_uiPaintRetry->recordWaiting = true;
        m_uiPaintRetry->resource.wait();
    }
    // The pool threshold is global: another view may own every pending task.
    if (!RasterTaskWorkerThreadPool::shared()->registerCompletionWaiter(this)) {
        std::lock_guard<std::mutex> lock(m_uiPaintRetry->mutex);
        m_uiPaintRetry->notificationFailed = true;
    }
}

void LayerTreeHost::clearResourceWait()
{
    RasterTaskWorkerThreadPool::shared()->unregisterCompletionWaiter(this);
    std::lock_guard<std::mutex> lock(m_uiPaintRetry->mutex);
    m_uiPaintRetry->recordWaiting = false;
    m_uiPaintRetry->resource.clearWait();
    if (m_uiPaintRetry->drawWaiting)
        m_uiPaintRetry->resource.wait();
}

void LayerTreeHost::notifyRecordResourcesChanged()
{
    // Called under the pool membership guard: only enqueue retained owner work.
    queueResourceProgress(m_uiPaintRetry);
}

const char* LayerTreeHost::recordResourceWakeStatus() const
{
    std::lock_guard<std::mutex> lock(m_uiPaintRetry->mutex);
    if (m_uiPaintRetry->notificationFailed) return "notification-failed";
    return m_uiPaintRetry->recordWaiting || m_uiPaintRetry->drawWaiting ? "waiting" : "idle";
}

void LayerTreeHost::notifyResourceProgress(bool actionDirty)
{
    {
        std::lock_guard<std::mutex> lock(m_uiPaintRetry->mutex);
        if (!m_uiPaintRetry->host)
            return;
        m_uiPaintRetry->resource.notify(actionDirty);
    }
    queueResourceProgress(m_uiPaintRetry);
}

RasterCandidateBudget LayerTreeHost::inspectRasterCandidateScene(uint64_t* generation)
{
    RasterCandidateBudget budget;
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    if (generation)
        *generation = m_frameBufferGeneration;
    if (m_isDestroying || hasInFlightScene()
        || RasterTaskWorkerThreadPool::shared()->getPendingRasterTaskNum() != 0) {
        budget.reject(RasterCandidateBudget::Failure::ExistingWork);
        return budget;
    }
    if (m_liveCCLayers.size() > RasterCandidateBudget::maximumLayers) {
        budget.reject(RasterCandidateBudget::Failure::LayerLimit);
        return budget;
    }
    for (auto it = m_liveCCLayers.begin(); it != m_liveCCLayers.end(); ++it) {
        if (!it->value->inspectRasterCandidateResidentBudget(&budget))
            break;
    }
    return budget;
}
bool LayerTreeHost::matchesRasterCandidateGeneration(uint64_t generation)
{
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    return !m_isDestroying && generation == m_frameBufferGeneration;
}

void LayerTreeHost::notifyRasterCompletion(const std::shared_ptr<UiPaintRetryState>& state)
{
    // Safe after the pending-task count reaches zero: this handle pins only
    // cancellation state, and never dereferences a potentially destroyed host.
    queueResourceProgress(state);
}

void LayerTreeHost::runResourceProgress(uint64_t ticket, uint64_t generation)
{
    bool dirty = false;
    bool waiting = false;
    bool rerasterRequired = false;
    bool stale = false;
    bool drawWaiting = false;
    {
        std::lock_guard<std::mutex> lock(m_uiPaintRetry->mutex);
        const auto intent = m_uiPaintRetry->resource.consume(
            ResourceNotificationPolicy::Ticket{ticket, generation},
            m_uiPaintRetry->policy.generation());
        stale = intent.stale;
        dirty = intent.dirty;
        waiting = intent.waiting && m_uiPaintRetry->recordWaiting;
        drawWaiting = intent.waiting && m_uiPaintRetry->drawWaiting;
        if (!stale) {
            m_uiPaintRetry->recordWaiting = false;
            m_uiPaintRetry->drawWaiting = false;
            rerasterRequired = m_uiPaintRetry->rerasterRequired;
            m_uiPaintRetry->rerasterRequired = false;
        }
    }
    if (!stale && waiting)
        RasterTaskWorkerThreadPool::shared()->unregisterCompletionWaiter(this);
    trace::add(trace::counter::resource_notifications);
    if (stale) {
        queueResourceProgress(m_uiPaintRetry);
        return;
    }
    if (m_isDestroying)
        return;
    retryPendingOwnerWork();
    if (drawWaiting)
        requestDrawFrameToRunIntoCompositeThread();
    if (rerasterRequired) {
        // Owner-side full invalidation reinstates every content tile whose
        // dirty state was consumed by the rejected raster group.
        for (auto it = m_liveLayers.begin(); it != m_liveLayers.end(); ++it)
            it->value->invalidate();
        {
            WTF::Locker<WTF::Mutex> sceneLock(m_sceneTransactionMutex);
            m_rasterRecoveryNeedsInvalidation = false;
        }
        postPaintMessage(SkRect::MakeWH(m_clientRect.width(), m_clientRect.height()));
    }
    if (dirty || rerasterRequired)
        setLayerTreeDirty();
    else if (waiting)
        m_hostClient->onLayerTreeSetNeedsCommit();
}

void LayerTreeHost::retryPendingOwnerWork()
{
    // Cheap owner service entry: retained dirty after an exceptional rejection
    // is retried here, without a permanent timer or an engine drain loop.
    bool retryDraw = false;
    {
        WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
        if (!m_visible.load()) return;
        queueLegacyPaintLocked();
        std::lock_guard<std::mutex> lock(m_uiPaintRetry->mutex);
        retryDraw = m_uiPaintRetry->drawWake.needsRetry();
    }
    if (retryDraw)
        requestDrawFrameToRunIntoCompositeThread();
}

void LayerTreeHost::beginRecordActions(bool isComefromMainframe)
{
    if (m_isDestroying)
        return;
    m_actionsFrameGroup->beginRecordActions(isComefromMainframe);
}

void LayerTreeHost::endRecordActions()
{
    if (m_isDestroying)
        return;
    m_actionsFrameGroup->endRecordActions();
    requestApplyActionsToRunIntoCompositeThread(false);
    // A final append may have been consumed before endId was sealed.
    requestDrawFrameToRunIntoCompositeThread();
    notifyResourceProgress();
}

void LayerTreeHost::appendLayerChangeAction(LayerChangeAction* action)
{
    m_actionsFrameGroup->saveLayerChangeAction(action);
    // Producer completion wakes the compositor and owner once each. Never
    // call WebPageImpl/JS directly from a raster worker.
    requestApplyActionsToRunIntoCompositeThread(false);
    notifyResourceProgress(true);
}

int64 LayerTreeHost::genActionId()
{
    return m_actionsFrameGroup->genActionId();
}

bool LayerTreeHost::preDrawFrame()
{
    return applyActions(m_isDestroying);
}

bool LayerTreeHost::applyActions(bool needCheck)
{
    ASSERT(!m_compositeThread || !isMainThread());
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    return m_actionsFrameGroup->applyActions(needCheck);
}

void LayerTreeHost::postDrawFrame()
{
    ASSERT(!m_compositeThread || !isMainThread());
}

void LayerTreeHost::scrollBy(const blink::WebFloatSize& delta, const blink::WebFloatSize& velocity)
{

}

blink::IntSize LayerTreeHost::canDrawSize()
{
    blink::IntSize outSize = deviceViewportSize();
    outSize.scale(1.2);
    return outSize;
}

static blink::WebDoublePoint getEffectiveTotalScrollOffset(cc_blink::WebLayerImpl* layer) {
    //     if (layer->DrawIgnoreScrollOffset())
    //         return gfx::Vector2dF();
    blink::WebDoublePoint offset = layer->scrollPositionDouble();
    // The scroll parent's total scroll offset (scroll offset + scroll delta)
    // can't be used because its scroll offset has already been applied to the
    // scroll children's positions by the main thread layer positioning code.
    //     if (layer->scrollParent())
    //         offset += layer->scroll_parent()->ScrollDelta();
    return offset;
}

// static void updateLayer(cc_blink::WebLayerImpl* layer, SkCanvas* canvas, const blink::IntRect& clip)
// {
//     blink::WebFloatPoint position = layer->position();
//     blink::WebSize size = layer->bounds();
//     blink::IntRect childClip(0, 0, size.width, size.height);
// 
// //     childClip.intersect(clip);
// //     if (childClip.isEmpty())
// //         return;
//     layer->updataAndPaintContents(canvas, childClip);
// }
//
// static void updateLayerChildren(cc_blink::WebLayerImpl* layer, SkCanvas* canvas, const blink::IntRect& clip, bool needsFullTreeSync)
// {
//     blink::WebFloatPoint currentLayerPosition = layer->position();
//     blink::WebDoublePoint effectiveTotalScrollOffset = getEffectiveTotalScrollOffset(layer);
//     blink::WebFloatPoint currentLayerPositionScrolled(currentLayerPosition.x - effectiveTotalScrollOffset.x, currentLayerPosition.y - effectiveTotalScrollOffset.y);
// 
//     blink::WebFloatPoint3D transformOrigin = layer->transformOrigin();
// 
//     SkMatrix44 combinedTransform(SkMatrix44::kIdentity_Constructor);
//     combinedTransform.preTranslate(currentLayerPositionScrolled.x + transformOrigin.x, currentLayerPositionScrolled.y + transformOrigin.y, currentLayerPosition.x);
//     combinedTransform.preConcat(layer->transform());
//     combinedTransform.preTranslate(-transformOrigin.x, -transformOrigin.y, -transformOrigin.z);
// 
//     if (!combinedTransform.isIdentity()) {
//         canvas->save();
//         canvas->concat(combinedTransform);
//     }
// 
//     blink::WebSize size = layer->bounds();
//     blink::IntRect layerRect(0, 0, size.width, size.height);
// 
//     bool layerClip = 0 != layerRect.width() && 0 != layerRect.height() && layer->masksToBounds();
//     if (layerClip) {
//         canvas->save();
//         canvas->clipRect(layerRect, SkRegion::kIntersect_Op, false);
//     }
// 
//     if (needsFullTreeSync)
//         updateLayer(layer, canvas, clip);
// 
//     blink::IntRect clipInChildCoordinate = clip;
//     clipInChildCoordinate.move(-currentLayerPositionScrolled.x, -currentLayerPositionScrolled.y);
// 
//     WTF::Vector<cc_blink::WebLayerImpl*>& children = layer->children();
//     for (size_t i = 0; i < children.size(); ++i) {
//         cc_blink::WebLayerImpl* child = children[i];
// #if 1
//         if (!(child->dirty() || child->childrenDirty() || needsFullTreeSync))
//             continue;
// #endif
// 
//         child->clearChildrenDirty();
//         updateLayerChildren(child, canvas, clipInChildCoordinate, needsFullTreeSync);
//     }
// 
//     if (layerClip)
//         canvas->restore();
// 
//     if (!combinedTransform.isIdentity())
//         canvas->restore();
// }
// 
// void LayerTreeHost::updateLayers(SkCanvas* canvas, const blink::IntRect& clip, bool needsFullTreeSync)
// {
//     if (!m_rootLayer)
//         return;
// 
//     updateLayerChildren(m_rootLayer, canvas, clip, m_needsFullTreeSync || needsFullTreeSync);
//     m_needsFullTreeSync = false;
// }

static void flattenTo2d(SkMatrix44& matrix)
{
    matrix.set(2, 0, 0.0);
    matrix.set(2, 1, 0.0);
    matrix.set(0, 2, 0.0);
    matrix.set(1, 2, 0.0);
    matrix.set(2, 2, 1.0);
    matrix.set(3, 2, 0.0);
    matrix.set(2, 3, 0.0);
}

// Kept only while recording one scene: the refresh must inspect exactly the
// window requested before TileGrid::update(), which consumes that request.
struct ScrollRasterCoverageRequest {
    int layerId;
    ScrollNodeKey key;
    uint64_t scrollEpoch;
    ScrollCoverageWindow coverage;
    blink::IntRect rasterRect;
};

static void refreshScrollRasterCoverage(LayerTreeHost* host,
    ScrollTreeSnapshot* scrollTree,
    const std::vector<ScrollRasterCoverageRequest>& coverageRequests,
    bool hasResourceFence);

void LayerTreeHost::recordDraw()
{
    m_recordedTraceActionFirst = m_recordedTraceActionLast = 0;
    if (!m_visible.load() || blink::RuntimeEnabledFeatures::headlessEnabled())
        return;
    if (!m_rootLayer)
        return;

    ScrollTreeSnapshot scrollTree;
    std::vector<ScrollRasterCoverageRequest> coverageRequests;
    updateLayersDrawProperties(&scrollTree, &coverageRequests);
    {
        WTF::Locker<WTF::Mutex> identityLock(m_compositeMutex);
        scrollTree.setInputIdentity(m_viewTraceIdentity.value());
    }
    const int64 actionBefore = m_actionsFrameGroup->newestActionId();

    cc::RasterTaskGroup* taskGroup = RasterTaskWorkerThreadPool::shared()->beginPostRasterTask(this);

    for (size_t i = 0; i < m_pendingRepaintRectsInRootLayerCoordinate.size(); ++i) {
        SkRect r = m_pendingRepaintRectsInRootLayerCoordinate[i];
        if (!r.isEmpty())
            taskGroup->appendPendingInvalidateRect(r);
    }

    m_pendingRepaintRectsInRootLayerCoordinate.clear();

    m_rootLayer->recordDrawChildren(taskGroup, 0);
    const bool noRasterCommit = taskGroup->endPostRasterTask();
    const int64 actionAfter = m_actionsFrameGroup->newestActionId();
    const bool hasActions = actionAfter > actionBefore;
    m_recordedTraceActionFirst = hasActions ? actionBefore + 1 : 0;
    m_recordedTraceActionLast = hasActions ? actionAfter : 0;
    const bool resourcesReady = noRasterCommit || !hasActions;
    refreshScrollRasterCoverage(this, &scrollTree, coverageRequests,
        !resourcesReady);
    submitSceneTransaction(std::move(scrollTree),
        hasActions ? actionBefore + 1 : 0,
        hasActions ? actionAfter : 0,
        resourcesReady);
}

void printTrans(const SkMatrix44& transform, int deep)
{
    SkMScalar total = 0.0;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            total += transform.get(i, j);
        }
    }
    
    String outString = String::format("LayerTreeHost::printTrans:%d, %f \n", deep, total);
    OutputDebugStringW(outString.charactersWithNullTermination().data());
}

bool LayerTreeHost::drawToCanvas(SkCanvas* canvas, const SkRect& dirtyRect)
{
    if (dirtyRect.isEmpty())
        return false;

    canvas->save();
    canvas->clipRect(dirtyRect);
       
    SkPaint clearColorPaint;
    clearColorPaint.setColor(getRealColor(m_hasTransparentBackground, m_backgroundColor));

    // http://blog.csdn.net/to_be_designer/article/details/48530921
    clearColorPaint.setXfermodeMode(SkXfermode::kSrcOver_Mode); // SkXfermode::kSrcOver_Mode
    canvas->drawRect((SkRect)dirtyRect, clearColorPaint);

    bool b = false;
    WTF::Locker<WTF::Mutex> locker(m_rootCCLayerMutex);
    if (getRootCCLayer())
        b = m_rootCCLayer->drawToCanvasChildren(this, canvas, dirtyRect, 1, 0);
    canvas->restore();

    return b;
}

static bool isIdentityOrIntegerTranslation(const SkMatrix44& transform)
{
    if (!transform.isTranslate())
        return false;

    bool noFractionalTranslation =
        static_cast<int>(transform.get(0, 3)) == transform.get(0, 3) &&
        static_cast<int>(transform.get(1, 3)) == transform.get(1, 3) &&
        static_cast<int>(transform.get(2, 3)) == transform.get(2, 3);

    return noFractionalTranslation;
}

struct DrawPropertiesFromAncestor {
    DrawPropertiesFromAncestor()
    {
        transform = SkMatrix44(SkMatrix44::kIdentity_Constructor);
        opacity = 1.0;
        subtreeCanUseLcdText = true;
    }

    SkMatrix44 transform;
    float opacity;
    bool subtreeCanUseLcdText;
};

static ScrollNodeKey scrollNodeKeyForLayer(LayerTreeHost* host,
    const cc_blink::WebLayerImpl* layer)
{
    const uint64_t viewId = static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(host));
    return ScrollNodeKey(viewId ? viewId : 1, 1,
        static_cast<uint64_t>(layer->id()), 1);
}

bool LayerTreeHost::drawCompositorScrollbarLayer(int layerId,
    SkCanvas* canvas, const SkRect& localDirtyRect)
{
    if (!canvas || layerId <= 0)
        return false;

    bool drew = false;
    for (size_t i = 0; i < m_compositorScrollStates.size(); ++i) {
        const ScrollController& controller =
            m_compositorScrollStates[i]->controller;
        if (!controller.compositorEligible())
            continue;
        const ScrollNodeSnapshot& node = controller.activeNode();
        const ScrollVector visual = controller.visualOffset();
        if (node.verticalScrollbar.belongsToLayer(layerId)) {
            drew |= drawCompositorScrollbarInLayer(canvas, localDirtyRect,
                node.verticalScrollbar, node, visual);
        }
        if (node.horizontalScrollbar.belongsToLayer(layerId)) {
            drew |= drawCompositorScrollbarInLayer(canvas, localDirtyRect,
                node.horizontalScrollbar, node, visual);
        }
    }
    return drew;
}

static ScrollNodeKey appendScrollNodeSnapshot(LayerTreeHost* host,
    cc_blink::WebLayerImpl* layer, uint64_t sceneVersion,
    ScrollTreeSnapshot* scrollTree,
    std::vector<ScrollRasterCoverageRequest>* coverageRequests)
{
    if (!layer->scrollable())
        return ScrollNodeKey();

    ScrollNodeSnapshot node;
    node.key = scrollNodeKeyForLayer(host, layer);
    node.sceneVersion = sceneVersion;
    node.scrollEpoch = layer->compositorScrollEpoch();
    node.acknowledgedSample = layer->acknowledgedCompositorSample();
    const blink::WebDoublePoint acknowledged =
        layer->acknowledgedCompositorTotal();
    node.acknowledgedTotal = ScrollVector(acknowledged.x, acknowledged.y);
    const blink::WebDoublePoint base = layer->scrollPositionDouble();
    node.baseOffset = ScrollVector(base.x, base.y);
    node.canonicalAdjustment = layer->compositorCanonicalAdjustment();
    node.minimumOffset = ScrollVector(0.0, 0.0);

    cc_blink::WebLayerImpl* clipLayer =
        host->getLayerById(layer->scrollClipLayerId());
    if (!clipLayer) {
        node.mainThreadReasons |= ScrollMainThreadReasonUnknownRelationship;
        node.maximumOffset = node.minimumOffset;
        node.coverageMinimum = node.minimumOffset;
        node.coverageMaximum = node.minimumOffset;
        node.setInputPrediction(node.baseOffset);
        scrollTree->addNode(node);
        return node.key;
    }

    node.maximumOffset = ScrollVector(
        std::max(0, layer->bounds().width - clipLayer->bounds().width),
        std::max(0, layer->bounds().height - clipLayer->bounds().height));
    node.inputAxes = ScrollInputAxisNone;
    if (layer->userScrollableHorizontal() && node.maximumOffset.x > 0.0)
        node.inputAxes |= ScrollInputAxisHorizontal;
    if (layer->userScrollableVertical() && node.maximumOffset.y > 0.0)
        node.inputAxes |= ScrollInputAxisVertical;

    const blink::FloatRect localClip(0, 0, clipLayer->bounds().width,
        clipLayer->bounds().height);
    const blink::FloatRect mappedClip = MathUtil::MapClippedRect(
        clipLayer->drawProperties()->screenSpaceTransform, localClip);
    node.viewportClip = ScrollRect(mappedClip.x(), mappedClip.y(),
        mappedClip.width(), mappedClip.height());

    if (layer->haveWheelEventHandlers())
        node.mainThreadReasons |= ScrollMainThreadReasonWheelHandler;
    if (layer->shouldScrollOnMainThread())
        node.mainThreadReasons |= ScrollMainThreadReasonUnknownRelationship;
    if (!layer->nonFastScrollableRegion().isEmpty())
        node.mainThreadReasons |= ScrollMainThreadReasonNonFastRegion;
    if (!layer->touchEventHandlerRegion().isEmpty())
        node.mainThreadReasons |= ScrollMainThreadReasonTouchHandler;
    if (!layer->drawProperties()->currentTransform.isTranslate()
        || !layer->drawProperties()->parentScreenSpaceTransform.isTranslate()) {
        node.mainThreadReasons |= ScrollMainThreadReasonComplexTransform;
    }
    if (host->has3dNodes())
        node.mainThreadReasons |= ScrollMainThreadReasonComplexTransform;

    cc_blink::WebLayerImpl* scrollParent = layer->scrollParent();
    if (scrollParent && scrollParent->scrollable())
        node.parent = scrollNodeKeyForLayer(host, scrollParent);

    const ScrollCoverageWindow coverage = computeScrollCoverageWindow(
        node.baseOffset, node.minimumOffset, node.maximumOffset,
        ScrollVector(clipLayer->bounds().width, clipLayer->bounds().height),
        node.acknowledgedTotal, node.inputAxes);
    const double rasterRight = coverage.rasterRect.x
        + coverage.rasterRect.width;
    const double rasterBottom = coverage.rasterRect.y
        + coverage.rasterRect.height;
    const int rasterX = static_cast<int>(std::floor(coverage.rasterRect.x));
    const int rasterY = static_cast<int>(std::floor(coverage.rasterRect.y));
    const blink::IntRect requiredRasterCoverage(rasterX, rasterY,
        std::max(0, static_cast<int>(std::ceil(rasterRight)) - rasterX),
        std::max(0, static_cast<int>(std::ceil(rasterBottom)) - rasterY));
    ScrollRasterCoverageRequest request;
    request.layerId = layer->id();
    request.key = node.key;
    request.scrollEpoch = node.scrollEpoch;
    request.coverage = coverage;
    request.rasterRect = requiredRasterCoverage;
    coverageRequests->push_back(request);
    if (layer->tileGrid())
        layer->tileGrid()->setRasterCoverageWindow(requiredRasterCoverage);
    node.coverageComplete = layer->tileGrid()
        && layer->tileGrid()->hasReadyCoverage(requiredRasterCoverage);
    if (node.coverageComplete) {
        node.coverageMinimum = coverage.minimumOffset;
        node.coverageMaximum = coverage.maximumOffset;
    } else {
        node.coverageMinimum = node.baseOffset;
        node.coverageMaximum = node.baseOffset;
        node.mainThreadReasons |= ScrollMainThreadReasonIncompleteCoverage;
    }
    node.setInputPrediction(node.baseOffset);
    scrollTree->addNode(node);
    if (node.viewportClip.width > 0.0 && node.viewportClip.height > 0.0)
        scrollTree->addHitTestRegion(ScrollHitTestRegion(
            node.viewportClip, node.key));
    return node.key;
}

static void refreshScrollRasterCoverage(LayerTreeHost* host,
    ScrollTreeSnapshot* scrollTree,
    const std::vector<ScrollRasterCoverageRequest>& coverageRequests,
    bool hasResourceFence)
{
    for (size_t i = 0; i < coverageRequests.size(); ++i) {
        const ScrollRasterCoverageRequest& request = coverageRequests[i];
        cc_blink::WebLayerImpl* layer = host->getLayerById(request.layerId);
        const bool coveragePrepared = layer
            && scrollNodeKeyForLayer(host, layer) == request.key
            && layer->compositorScrollEpoch() == request.scrollEpoch
            && layer->tileGrid()
            && layer->tileGrid()->hasReadyCoverage(request.rasterRect);
        scrollTree->refreshCoverage(request.key, request.scrollEpoch,
            request.coverage, coveragePrepared, hasResourceFence);
    }
}

static void appendLayerHitTestRegion(cc_blink::WebLayerImpl* layer,
    const ScrollNodeKey& scrollTarget, ScrollTreeSnapshot* scrollTree)
{
    if (!layer->drawsContent() || layer->opacity() <= 0.0f
        || layer->bounds().width <= 0 || layer->bounds().height <= 0) {
        return;
    }

    const blink::FloatRect localBounds(0, 0, layer->bounds().width,
        layer->bounds().height);
    blink::FloatRect mappedBounds = MathUtil::MapClippedRect(
        layer->drawProperties()->screenSpaceTransform, localBounds);
    if (scrollTarget.isValid()) {
        const ScrollNodeSnapshot* owner = scrollTree->findNode(scrollTarget);
        if (!owner)
            return;
        mappedBounds.intersect(blink::FloatRect(owner->viewportClip.x,
            owner->viewportClip.y, owner->viewportClip.width,
            owner->viewportClip.height));
    }
    if (mappedBounds.isEmpty())
        return;
    scrollTree->addHitTestRegion(ScrollHitTestRegion(
        ScrollRect(mappedBounds.x(), mappedBounds.y(), mappedBounds.width(),
            mappedBounds.height()), scrollTarget));
}

static void updateChildLayersDrawProperties(cc_blink::WebLayerImpl* layer,
    LayerTreeHost* host, LayerSorter& layerSorter,
    const DrawPropertiesFromAncestor& propFromAncestor, int deep,
    uint64_t sceneVersion, const ScrollNodeKey& inheritedScrollTarget,
    ScrollTreeSnapshot* scrollTree,
    std::vector<ScrollRasterCoverageRequest>* coverageRequests)
{
    WTF::Vector<cc_blink::WebLayerImpl*>& children = layer->children();
    
    for (size_t i = 0; i < children.size(); ++i) {
        cc_blink::WebLayerImpl* child = children[i];
        DrawProps* drawProperties = child->drawProperties();

        blink::WebFloatPoint currentLayerPosition = child->position();
        blink::WebDoublePoint effectiveTotalScrollOffset = getEffectiveTotalScrollOffset(child);
        blink::WebFloatPoint currentLayerPositionScrolled(currentLayerPosition.x - effectiveTotalScrollOffset.x, currentLayerPosition.y - effectiveTotalScrollOffset.y);
        blink::WebFloatPoint3D transformOrigin = child->transformOrigin();

        SkMatrix44 currentTransform(SkMatrix44::kIdentity_Constructor);
        SkMatrix44 childTransform = child->transform();

        if (!childTransform.isIdentity()) {
            currentTransform.preTranslate(currentLayerPositionScrolled.x + transformOrigin.x, currentLayerPositionScrolled.y + transformOrigin.y, transformOrigin.z);
            currentTransform.preConcat(childTransform);
            currentTransform.preTranslate(-transformOrigin.x, -transformOrigin.y, -transformOrigin.z);
        } else {
            currentTransform.preTranslate(currentLayerPositionScrolled.x, currentLayerPositionScrolled.y, 0);
        }

        // Flatten to 2D if the layer doesn't preserve 3D.
        SkMatrix44 combinedTransform = propFromAncestor.transform;
        combinedTransform.preConcat(currentTransform);

        SkMatrix44 transformToAncestorIfFlatten = combinedTransform;
        if (child->shouldFlattenTransform())
            flattenTo2d(transformToAncestorIfFlatten);

        drawProperties->screenSpaceTransform = combinedTransform;
        if (!combinedTransform.invert(&drawProperties->inverseScreenSpaceTransform))
            drawProperties->inverseScreenSpaceTransform = SkMatrix44(SkMatrix44::kIdentity_Constructor);
        drawProperties->targetSpaceTransform = combinedTransform;
        drawProperties->parentScreenSpaceTransform = propFromAncestor.transform;
        drawProperties->currentTransform = currentTransform;
        drawProperties->position = currentLayerPositionScrolled;
        drawProperties->transformOrigin = child->transformOrigin();
        drawProperties->shouldFlattenTransform = child->shouldFlattenTransform();

        bool layerCanUseLcdText = true;
        bool subtreeCanUseLcdText = true;

        DrawPropertiesFromAncestor prop;
        prop.transform = transformToAncestorIfFlatten;
        prop.opacity = propFromAncestor.opacity * child->opacity();

        // To avoid color fringing, LCD text should only be used on opaque layers with just integral translation.
        subtreeCanUseLcdText = /*propFromAncestor.subtreeCanUseLcdText &&*/ prop.opacity == 1.f && isIdentityOrIntegerTranslation(propFromAncestor.transform);
        // Also disable LCD text locally for non-opaque content.
        layerCanUseLcdText = subtreeCanUseLcdText && child->opaque();
        drawProperties->layerCanUseLcdText = layerCanUseLcdText;

        ScrollNodeKey scrollTarget = inheritedScrollTarget;
        cc_blink::WebLayerImpl* explicitScrollParent = child->scrollParent();
        if (explicitScrollParent && explicitScrollParent->scrollable())
            scrollTarget = scrollNodeKeyForLayer(host, explicitScrollParent);
        const ScrollNodeKey childScrollTarget = appendScrollNodeSnapshot(
            host, child, sceneVersion, scrollTree, coverageRequests);
        if (childScrollTarget.isValid())
            scrollTarget = childScrollTarget;
        else
            appendLayerHitTestRegion(child, scrollTarget, scrollTree);

        prop.subtreeCanUseLcdText = subtreeCanUseLcdText;
        updateChildLayersDrawProperties(child, host, layerSorter, prop,
            deep + 1, sceneVersion, scrollTarget, scrollTree, coverageRequests);
    }

    if (children.size() && layer->layerTreeHost()->has3dNodes() && layer->is3dSorted()) {
        layerSorter.Sort(children.begin(), children.end());
    }
}


static void attachCompositorScrollbarSnapshots(
    cc_blink::WebLayerImpl* layer, LayerTreeHost* host,
    ScrollTreeSnapshot* scrollTree)
{
    WTF::Vector<cc_blink::WebLayerImpl*>& children = layer->children();
    for (size_t i = 0; i < children.size(); ++i) {
        cc_blink::WebLayerImpl* child = children[i];
        cc_blink::WebLayerImplClient* client =
            const_cast<cc_blink::WebLayerImplClient*>(child->client());
        if (client && client->type()
                == cc_blink::WebLayerImplClient::ScrollbarLayerType) {
            const cc_blink::WebScrollbarLayerImpl* scrollbarLayer =
                static_cast<const cc_blink::WebScrollbarLayerImpl*>(client);
            cc_blink::WebLayerImpl* scrollLayer =
                host->getLayerById(scrollbarLayer->scrollLayerId());
            ScrollNodeSnapshot* node = scrollLayer
                ? scrollTree->findMutableNode(
                    scrollNodeKeyForLayer(host, scrollLayer)) : nullptr;
            if (node) {
                const blink::WebScrollbar* webScrollbar =
                    scrollbarLayer->scrollbar();
                if (!webScrollbar) {
                    node->mainThreadReasons |=
                        ScrollMainThreadReasonUnsupportedScrollbar;
                } else {
                    const blink::FloatRect localTrack(0, 0,
                        child->bounds().width, child->bounds().height);
                    const blink::FloatRect mappedTrack =
                        MathUtil::MapClippedRect(
                            child->drawProperties()->screenSpaceTransform,
                            localTrack);
                    if (mappedTrack.isEmpty()) {
                        node->mainThreadReasons |=
                            ScrollMainThreadReasonUnsupportedScrollbar;
                        attachCompositorScrollbarSnapshots(child, host,
                            scrollTree);
                        continue;
                    }
                    CompositorScrollbarSnapshot snapshot;
                    snapshot.enabled = true;
                    snapshot.vertical = webScrollbar->orientation()
                        == blink::WebScrollbar::Vertical;
                    snapshot.overlay = webScrollbar->isOverlay();
                    snapshot.customStyle = webScrollbar->isCustomScrollbar();
                    snapshot.layerId = child->id();
                    snapshot.trackRect = ScrollRect(mappedTrack.x(),
                        mappedTrack.y(), mappedTrack.width(),
                        mappedTrack.height());
                    snapshot.localTrackRect = ScrollRect(0.0, 0.0,
                        child->bounds().width, child->bounds().height);
                    if (snapshot.vertical)
                        node->verticalScrollbar = snapshot;
                    else
                        node->horizontalScrollbar = snapshot;
                    if (!snapshot.isSupported()) {
                        node->mainThreadReasons |=
                            ScrollMainThreadReasonUnsupportedScrollbar;
                    }
                }
            }
        }
        attachCompositorScrollbarSnapshots(child, host, scrollTree);
    }
}

void LayerTreeHost::updateLayersDrawProperties(ScrollTreeSnapshot* output,
    std::vector<ScrollRasterCoverageRequest>* coverageRequests)
{
    LayerSorter layerSorter;
    SkMatrix44 transform(SkMatrix44::kIdentity_Constructor);

    DrawPropertiesFromAncestor prop;
    ++m_nextScrollSceneVersion;
    if (!m_nextScrollSceneVersion)
        ++m_nextScrollSceneVersion;
    ScrollTreeSnapshot scrollTree;
    scrollTree.setSceneVersion(m_nextScrollSceneVersion);
    updateChildLayersDrawProperties(m_rootLayer, this, layerSorter, prop, 0,
        m_nextScrollSceneVersion, ScrollNodeKey(), &scrollTree, coverageRequests);
    attachCompositorScrollbarSnapshots(m_rootLayer, this, &scrollTree);
    if (output)
        *output = std::move(scrollTree);
}

bool LayerTreeHost::hasInFlightScene() const
{
    WTF::Locker<WTF::Mutex> locker(m_sceneTransactionMutex);
    return m_sceneCoordinator.hasPending() || m_sceneCoordinator.hasReady();
}

bool LayerTreeHost::submitSceneTransaction(ScrollTreeSnapshot&& scrollTree,
    int64 firstActionId, int64 lastActionId, bool resourcesReady)
{
    const uint64_t sceneVersion = scrollTree.sceneVersion();
    bool accepted = false;
    {
        WTF::Locker<WTF::Mutex> locker(m_sceneTransactionMutex);
        if (m_rasterRecoveryPending && m_rasterRecoveryNeedsInvalidation)
            return false;
        SceneTransaction transaction;
        transaction.baseVersion = m_sceneCoordinator.activeVersion();
        transaction.newVersion = scrollTree.sceneVersion();
        transaction.firstActionId = firstActionId;
        transaction.lastActionId = lastActionId;
        transaction.resourcesReady = resourcesReady;
        transaction.scrollTree = std::move(scrollTree);
        accepted = m_sceneCoordinator.submit(std::move(transaction));
        if (accepted) {
            m_pendingSceneTraceContext = renderTraceContext();
            m_pendingSceneTraceContext.input_first = m_pendingSceneTraceContext.input_last = 0;
            m_pendingSceneTraceContext.action_first = firstActionId > 0 ? firstActionId : 0;
            m_pendingSceneTraceContext.action_last = lastActionId > 0 ? lastActionId : 0;
            m_pendingSceneTraceContext.related_id = sceneVersion;
        }
    }
    if (accepted) {
        trace::add(trace::counter::scene_pending);
        auto value = renderTraceContext();
        value.action_first = firstActionId > 0 ? firstActionId : 0;
        value.action_last = lastActionId > 0 ? lastActionId : 0;
        value.related_id = sceneVersion;
        recordRenderStage(trace::stage::scene_pending, value);
    }
    if (accepted && resourcesReady)
        requestDrawFrameToRunIntoCompositeThread();
    return accepted;
}

void LayerTreeHost::onRasterActionCancelled(int64 actionId)
{
    {
        WTF::Locker<WTF::Mutex> locker(m_sceneTransactionMutex);
        const uint64_t failedScene = m_sceneCoordinator.hasPending()
            ? m_sceneCoordinator.pendingScene().newVersion : m_sceneCoordinator.activeVersion();
        m_sceneCoordinator.cancelPendingForAction(actionId);
        m_rasterRecoveryPending = true;
        m_rasterRecoveryNeedsInvalidation = true;
        if (failedScene > m_rasterRecoveryAfterScene)
            m_rasterRecoveryAfterScene = failedScene;
    }
    {
        WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
        for (size_t index = 0; index < m_unpresentedInputTrace.size(); ++index) {
            if (!m_pendingInputTrace.append(m_unpresentedInputTrace.at(index))) {
                trace::add(trace::counter::causal_ranges_dropped);
                trace::note_causal_loss();
            }
        }
        m_unpresentedInputTrace.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_uiPaintRetry->mutex);
        m_uiPaintRetry->rerasterRequired = true;
        m_uiPaintRetry->resource.notify(true);
    }
    queueResourceProgress(m_uiPaintRetry);
}

void LayerTreeHost::onActionFrameApplied(int64 throughActionId)
{
    bool ready = false;
    {
        WTF::Locker<WTF::Mutex> locker(m_sceneTransactionMutex);
        ready = m_sceneCoordinator.markResourcesReadyThroughAction(
            throughActionId);
    }
    if (ready) {
        trace::event value;
        {
            WTF::Locker<WTF::Mutex> locker(m_sceneTransactionMutex);
            value = m_pendingSceneTraceContext;
        }
        value.action_last = throughActionId > 0 ? throughActionId : 0;
        recordRenderStage(trace::stage::resources_ready, value);
        requestDrawFrameToRunIntoCompositeThread();
    }
    notifyResourceProgress();
}

inline SkRect unionRect(const SkRect& a, const SkRect& b)
{
    SkRect c = a;
    c.join(b);
    return c;
}

static SkScalar rectArea(const SkRect& r)
{
    return (r.width()) * (r.height());
}

static SkScalar unionArea(const SkRect& r1, const SkRect& r2)
{
    SkRect area;
    area = unionRect(r1, r2);
    return rectArea(area);
}

static void mergeDirty(Vector<SkRect>* dirtyRects)
{
    do {
        int nDirty = (int)dirtyRects->size();
        if (nDirty < 1)
            break;

        SkScalar bestDelta = 0;
        int mergeA = 0;
        int mergeB = 0;
        for (int i = 0; i < nDirty - 1; i++) {
            for (int j = i + 1; j < nDirty; j++) {
                SkScalar delta = unionArea(dirtyRects->at(i), dirtyRects->at(j)) - rectArea(dirtyRects->at(i)) - rectArea(dirtyRects->at(j));
                if (bestDelta >= delta) {
                    mergeA = i;
                    mergeB = j;
                    bestDelta = delta;
                }
            }
        }

        if (mergeA == mergeB)
            break;

        dirtyRects->at(mergeA).join(dirtyRects->at(mergeB));
        for (int i = mergeB + 1; i < nDirty; i++)
            dirtyRects->at(i - 1) = dirtyRects->at(i);

        dirtyRects->removeLast();
    } while (true);
}

static void addAndMergeDirty(Vector<SkRect>* dirtyRects, const SkRect& r)
{
    if (r.isEmpty())
        return;

    size_t dirtySize = dirtyRects->size();
    if (0 == dirtySize) {
        dirtyRects->append(r);
        return;
    }

    // Bound the number of UI/compositor regions. Keeping every tiny invalid
    // rectangle can cost more in task dispatch and GDI calls than the pixels
    // it saves; merging the least-expensive pair preserves coverage while
    // keeping the per-frame work predictable.
    const size_t kMaxDirtyRects = 8;
    if (dirtySize >= kMaxDirtyRects) {
        const dirty_region::MergeChoice choice = dirty_region::leastCostMerge(
            dirtyRects->data(), dirtySize, r, rectArea, unionArea);
        if (choice.second == dirtySize) {
            dirtyRects->at(choice.first).join(r);
            return;
        }
        dirtyRects->at(choice.first).join(dirtyRects->at(choice.second));
        for (size_t i = choice.second + 1; i < dirtySize; ++i)
            dirtyRects->at(i - 1) = dirtyRects->at(i);
        dirtyRects->removeLast();
        dirtySize = dirtyRects->size();
    }

    SkScalar bestDelta = 0;
    size_t mergePos = (size_t)-1;
    for (size_t i = 0; i < dirtySize; i++) {
        SkScalar delta = unionArea(dirtyRects->at(i), r) - rectArea(dirtyRects->at(i)) - rectArea(r);
        if (delta > bestDelta)
            continue;
        bestDelta = delta;
        mergePos = i;
    }

    if (mergePos == (size_t)-1)
        dirtyRects->append(r);
    else
        dirtyRects->at(mergePos).join(r);
}

void LayerTreeHost::appendPendingRepaintRect(const SkRect& r)
{
    addAndMergeDirty(&m_pendingRepaintRectsInRootLayerCoordinate, r);
}

static void showDebugChildren(cc_blink::WebLayerImpl* layer, int deep)
{
    Vector<LChar> blankSpaceString;
    blankSpaceString.resize(deep);
    blankSpaceString.fill(' ');

    WTF::Vector<cc_blink::WebLayerImpl*>& children = layer->children();
    for (size_t i = 0; i < children.size(); ++i) {
        cc_blink::WebLayerImpl* child = children[i];

        blink::WebFloatPoint position = child->position();
        blink::WebSize bounds = child->bounds();
        
        String msg = String::format("%p, %d %d %d %d - %d, (%f %f)\n", child,
            (int)position.x, (int)position.y, bounds.width, bounds.height, child->id(), (child->transform().get(0, 3)), (child->transform().get(1, 3)));
        msg.insert(blankSpaceString.data(), blankSpaceString.size(), 0);
        OutputDebugStringA(msg.utf8().data());

        showDebugChildren(child, deep + 1);
    }
}

void LayerTreeHost::showDebug()
{
    if (!m_rootLayer)
        return;
    showDebugChildren(m_rootLayer, 1);
}

// Sets the root of the tree. The root is set by way of the constructor.
void LayerTreeHost::setRootLayer(const blink::WebLayer& layer)
{
    m_rootLayer = (cc_blink::WebLayerImpl*)&layer;
    m_rootLayer->setLayerTreeHost(this);

    requestApplyActionsToRunIntoCompositeThread(false);

    getRootCCLayer();

    //setNeedsFullTreeSync();
}

CompositingLayer* LayerTreeHost::getRootCCLayer()
{
    WTF::Locker<WTF::Mutex> locker(m_rootCCLayerMutex);
    if (m_rootCCLayer)
        return m_rootCCLayer;
    if (!m_rootLayer)
        return nullptr;

    m_rootCCLayer = getCCLayerById(m_rootLayer->id());
    return m_rootCCLayer;
}

void LayerTreeHost::clearRootLayer()
{
    m_rootLayer = nullptr;

    while (0 != RasterTaskWorkerThreadPool::shared()->getPendingRasterTaskNum()) { ::Sleep(20); }

    requestApplyActionsToRunIntoCompositeThread(false);

    m_rootCCLayerMutex.lock();
    m_rootCCLayer = nullptr;
    m_rootCCLayerMutex.unlock();
}

void LayerTreeHost::setViewportSize(const blink::WebSize& deviceViewportSize)
{
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    m_deviceViewportSize = deviceViewportSize;
    m_clientRect = blink::IntRect(blink::IntPoint(), m_deviceViewportSize);
    m_hasResize = true;
    if (m_frameBufferGeneration != (std::numeric_limits<uint64_t>::max)())
        ++m_frameBufferGeneration;
    m_nextCompositeFrameTime = 0;
    {
        std::lock_guard<std::mutex> retryLock(m_uiPaintRetry->mutex);
        m_uiPaintRetry->policy.invalidate();
    }
    // Old-size damage cannot be painted into the replacement canvas. Its
    // first compositor draw publishes full current-size damage instead.
    m_dirtyRectsForUi.clear();
#if defined(SOUI_GPU_COMPOSITOR)
    // A size change is a safe point to retry a GPU context that was
    // unavailable during startup (for example after a display driver reset).
    m_gpuInitAttempted = false;
#endif
}

// Gives the viewport size in physical device pixels.
blink::WebSize LayerTreeHost::deviceViewportSize() const
{
    return blink::WebSize(m_deviceViewportSize.width(), m_deviceViewportSize.height());
}

void LayerTreeHost::setDeviceScaleFactor(float scale)
{
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    if (m_deviceScaleFactor == scale)
        return;
    m_deviceScaleFactor = scale;
    m_nextCompositeFrameTime = 0;
    m_hasResize = true;
    if (m_frameBufferGeneration != (std::numeric_limits<uint64_t>::max)())
        ++m_frameBufferGeneration;
}

float LayerTreeHost::deviceScaleFactor() const
{
    return m_deviceScaleFactor;
}

static void recursiveSetColor(WTF::HashMap<int, cc_blink::WebLayerImpl*>* layers, bool hasTransparentBackground, SkColor backgroundColor)
{
    for (auto it = layers->begin(); it != layers->end(); ++it) {
        cc_blink::WebLayerImpl* layer = it->value;
        layer->setBackgroundColor(getRealColor(hasTransparentBackground, backgroundColor));
    }
}

void LayerTreeHost::setBackgroundColor(blink::WebColor color)
{
    m_backgroundColor = color;
    recursiveSetColor(&m_liveLayers, m_hasTransparentBackground, m_backgroundColor);
}

blink::WebColor LayerTreeHost::getBackgroundColor() const
{
    return m_backgroundColor;
}

void LayerTreeHost::setHasTransparentBackground(bool b)
{
    if (m_hasTransparentBackground == b)
        return;
    m_hasTransparentBackground = b;
    {
        WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
        m_hasResize = true;
        if (m_frameBufferGeneration != (std::numeric_limits<uint64_t>::max)())
            ++m_frameBufferGeneration;
    }
    recursiveSetColor(&m_liveLayers, m_hasTransparentBackground, m_backgroundColor);
}

bool LayerTreeHost::getHasTransparentBackground() const
{ 
    return m_hasTransparentBackground;
}

void LayerTreeHost::registerForAnimations(blink::WebLayer* layer)
{
    // 不能在这里设置LayerTreeHost，因为popup 类型的会把新窗口的layer调用本接口到老的host来。
//     cc_blink::WebLayerImpl* layerImpl = (cc_blink::WebLayerImpl*)layer;
//     layerImpl->setLayerTreeHost(this);
}

// Sets whether this view is visible. In threaded mode, a view that is not visible will not
// composite or trigger updateAnimations() or layout() calls until it becomes visible.
void LayerTreeHost::setVisible(bool visible)
{
    {
        WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
        m_visible.store(visible);
        m_nextCompositeFrameTime = 0;
        m_lastPaintTime = 0;
        {
            std::lock_guard<std::mutex> retryLock(m_uiPaintRetry->mutex);
            m_uiPaintRetry->policy.invalidate();
        }
        // Repeated true is a real restore intent after UI transition coalescing.
        if (visible && !m_clientRect.isEmpty()) {
            const SkRect full = SkRect::MakeWH(m_clientRect.width(), m_clientRect.height());
            addAndMergeDirty(&m_dirtyRectsForComposite, full);
        }
    }
    if (visible) {
        m_hostClient->onLayerTreeSetNeedsCommit();
        requestDrawFrameToRunIntoCompositeThread();
    }
}

// Sets the current page scale factor and minimum / maximum limits. Both limits are initially 1 (no page scale allowed).
void LayerTreeHost::setPageScaleFactorAndLimits(float pageScaleFactor, float minimum, float maximum)
{
    m_pageScaleFactor = pageScaleFactor;
    m_minimum = minimum;
    m_maximum = maximum;
}

// Starts an animation of the page scale to a target scale factor and scroll offset.
// If useAnchor is true, destination is a point on the screen that will remain fixed for the duration of the animation.
// If useAnchor is false, destination is the final top-left scroll position.
void LayerTreeHost::startPageScaleAnimation(const blink::WebPoint& destination, bool useAnchor, float newPageScale, double durationSec)
{

}

void LayerTreeHost::setNeedsAnimate()
{
    m_hostClient->onLayerTreeSetNeedsCommit();
}

void LayerTreeHost::finishAllRendering()
{
    OutputDebugStringW(L"LayerTreeHost::finishAllRendering");
}

//////////////////////////////////////////////////////////////////////////

blink::IntRect LayerTreeHost::getClientRect()
{
    blink::IntRect clientRect;
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    clientRect = m_clientRect;
    return clientRect;
}

void LayerTreeHost::requestDrawFrameToRunIntoCompositeThread()
{
    queueDrawFrameWake(0);
}

void LayerTreeHost::requestDrawFrameAfterDelay(long long delayMs)
{
    queueDrawFrameWake(std::max<long long>(1, delayMs));
}

void LayerTreeHost::queueDrawFrameWake(long long delayMs)
{
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    if (!m_compositeThread || m_isDestroying || !m_visible.load())
        return;
    if (m_uiPaintRetry->drawQueuedCount)
        return;
    uint64_t ticket;
    {
        std::lock_guard<std::mutex> lock(m_uiPaintRetry->mutex);
        m_uiPaintRetry->drawWake.request();
        ticket = m_uiPaintRetry->drawWake.arm();
    }
    if (!ticket) {
        m_hasRejectedDrawWake.store(true);
        return;
    }
    auto* task = new (std::nothrow) DrawFrameTask(m_uiPaintRetry, ticket);
    if (!task) {
        std::lock_guard<std::mutex> lock(m_uiPaintRetry->mutex);
        m_uiPaintRetry->drawWake.reject(ticket);
        m_hasRejectedDrawWake.store(true);
        return;
    }
    m_uiPaintRetry->drawQueuedCount.fetch_add(1);
    m_uiPaintRetry->drawFinishCount.fetch_add(1);
    m_hasRejectedDrawWake.store(false);
    // WebThread owns/disposes the task on every admission/timer failure.
    // Its receipt clears only this wake, preserving published input and damage.
    if (delayMs > 0)
        m_compositeThread->platformThread().postDelayedTask(FROM_HERE, task, delayMs);
    else
        m_compositeThread->platformThread().postTask(FROM_HERE, task);
}

bool LayerTreeHost::addCompositorAnimation(int layerId,
    cc_blink::WebCompositorAnimationImpl* animation)
{
    if (!featureEnabledByDefault("SOUI_COMPOSITOR_ANIMATION") || !animation
        || !animation->isSupported()
        || (animation->targetProperty() != blink::WebCompositorAnimation::TargetPropertyOpacity
            && animation->targetProperty() != blink::WebCompositorAnimation::TargetPropertyTransform))
        return false;

    {
        WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
        if (!m_compositeThread || m_isDestroying)
            return false;
        m_compositorAnimations.append(new CompositorAnimationState(layerId, animation));
    }

    requestDrawFrameToRunIntoCompositeThread();
    return true;
}

void LayerTreeHost::removeCompositorAnimation(int layerId, int animationId,
    int targetProperty)
{
    bool found = false;
    {
        WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
        for (size_t i = 0; i < m_compositorAnimations.size(); ++i) {
            CompositorAnimationState* state = m_compositorAnimations[i];
            if (state->layerId != layerId)
                continue;
            if (animationId >= 0 && state->animation->id() != animationId)
                continue;
            if (targetProperty >= 0
                && static_cast<int>(state->animation->targetProperty()) != targetProperty)
                continue;
            state->cancelled = true;
            found = true;
        }
    }

    if (found)
        requestDrawFrameToRunIntoCompositeThread();
}

void LayerTreeHost::pauseCompositorAnimation(int layerId, int animationId,
    double timeOffset)
{
    bool found = false;
    {
        WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
        for (size_t i = 0; i < m_compositorAnimations.size(); ++i) {
            CompositorAnimationState* state = m_compositorAnimations[i];
            if (state->layerId == layerId && state->animation->id() == animationId) {
                state->animation->pauseAt(timeOffset);
                found = true;
            }
        }
    }

    if (found)
        requestDrawFrameToRunIntoCompositeThread();
}

bool LayerTreeHost::hasCompositorAnimation(int layerId)
{
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    for (size_t i = 0; i < m_compositorAnimations.size(); ++i) {
        const CompositorAnimationState* state = m_compositorAnimations[i];
        if (state->layerId == layerId && !state->cancelled)
            return true;
    }
    return false;
}

bool LayerTreeHost::submitCompositorScrollTree(
    const ScrollTreeSnapshot& scrollTree)
{
    if (m_isDestroying || !scrollTree.isValid())
        return false;

    {
        WTF::Locker<WTF::Mutex> locker(m_compositorScrollInputMutex);
        if (m_pendingCompositorScrollTree
            && scrollTree.sceneVersion()
                <= m_pendingCompositorScrollTree->sceneVersion()) {
            return false;
        }
        delete m_pendingCompositorScrollTree;
        m_pendingCompositorScrollTree = new ScrollTreeSnapshot(scrollTree);
    }
    requestDrawFrameToRunIntoCompositeThread();
    return true;
}

bool LayerTreeHost::handleCompositorWheelInput(int x, int y,
    double deltaX, double deltaY, double monotonicTime, bool smooth,
    CompositorScrollReceipt* receipt,
    const content::input_frame_trace::event* inputIdentity)
{
    if (receipt)
        *receipt = CompositorScrollReceipt();
    content::PaintActivityScope inputActivity(m_paintLifetime);
    if (!inputActivity)
        return false;
    if (!featureEnabledByDefault("SOUI_COMPOSITOR_SCROLL")
        || m_isDestroying || !std::isfinite(deltaX) || !std::isfinite(deltaY)
        || !std::isfinite(monotonicTime)) {
        recordInteractionDiagnostic(trace::interaction_diagnostic_code::disabled_or_invalid);
        return false;
    }

    bool accepted = false;
    {
        WTF::Locker<WTF::Mutex> locker(m_compositorScrollInputMutex);
        if (!m_activeInputScrollTree || m_pendingCompositorScrollTree
            || (inputIdentity && !m_activeInputScrollTree->matchesInputIdentity(*inputIdentity))
            || m_pendingCompositorScrollInputs.size() >= 512) {
            recordInteractionDiagnostic(trace::interaction_diagnostic_code::scene_unavailable);
            return false;
        }

        ScrollNodeSnapshot* target = nullptr;
        const double latchTimeoutSeconds = 0.120;
        if (m_latchedScrollNode.isValid()
            && monotonicTime - m_lastCompositorWheelTime
                <= latchTimeoutSeconds) {
            ScrollNodeSnapshot* latched =
                m_activeInputScrollTree->findMutableNode(m_latchedScrollNode);
            if (latched && latched->compositorEligible())
                target = latched;
        }

        if (!target) {
            const ScrollNodeSnapshot* hit =
                m_activeInputScrollTree->scrollTargetAt(x, y);
            if (hit)
                target = m_activeInputScrollTree->findMutableNode(hit->key);
        }

        if (!target) {
            recordInteractionDiagnostic(trace::interaction_diagnostic_code::no_eligible_hit);
            return false;
        }
        if (m_nextCompositorInputSequence == (std::numeric_limits<uint64_t>::max)())
            return false;
        const uint64_t sequence = m_nextCompositorInputSequence + 1;
        std::unique_ptr<CompositorScrollInput> payload(new (std::nothrow) CompositorScrollInput(
            target->key, sequence, ScrollVector(deltaX, deltaY), monotonicTime,
            smooth, target->scrollEpoch, inputIdentity));
        if (!payload)
            return false;
        try {
            if (m_pendingCompositorScrollInputs.size() == m_pendingCompositorScrollInputs.capacity()) {
                const size_t capacity = m_pendingCompositorScrollInputs.capacity();
                m_pendingCompositorScrollInputs.reserve(std::min<size_t>(512, capacity ? capacity * 2 : 16));
            }
        } catch (...) {
            return false; // No prediction/receipt mutation; original Engine fallback remains exact.
        }
        if (!target->advanceInputPrediction(ScrollVector(deltaX, deltaY))) {
            recordInteractionDiagnostic(trace::interaction_diagnostic_code::delta_not_consumed);
            return false;
        }

        m_pendingCompositorScrollInputs.push_back(payload.release()); // Reserved, pointer-only, nonthrowing.
        m_nextCompositorInputSequence = sequence;
        if (receipt) {
            receipt->sceneVersion = target->sceneVersion;
            receipt->inputSequence = m_nextCompositorInputSequence;
            receipt->offsetX = target->inputPrediction.x;
            receipt->offsetY = target->inputPrediction.y;
        }
        m_latchedScrollNode = target->key;
        m_lastCompositorWheelTime = monotonicTime;
        accepted = true;
        recordInteractionDiagnostic(trace::interaction_diagnostic_code::accepted);
    }

    if (accepted) {
        if (inputIdentity && InputFrameCoverage::validRange(*inputIdentity)) {
            trace::context_scope acceptedInput(*inputIdentity); // TLS only; the actual post hook records its own event.
            requestDrawFrameToRunIntoCompositeThread();
        } else {
            requestDrawFrameToRunIntoCompositeThread();
        }
    }
    return accepted;
}

bool LayerTreeHost::captureCompositorScrollBarrier(int x, int y,
    ScrollBarrier* barrier, const content::input_frame_trace::event* inputIdentity)
{
    content::PaintActivityScope inputActivity(m_paintLifetime);
    if (!inputActivity)
        return false;
    if (!barrier || m_isDestroying) {
        recordInteractionDiagnostic(trace::interaction_diagnostic_code::barrier_capture_invalid);
        return false;
    }

    ScrollNodeKey targetKey;
    uint64_t targetSceneVersion = 0;
    uint64_t targetScrollEpoch = 0;
    {
        WTF::Locker<WTF::Mutex> locker(m_compositorScrollInputMutex);
        if (!m_activeInputScrollTree
            || (inputIdentity && !m_activeInputScrollTree->matchesInputIdentity(*inputIdentity))) {
            recordInteractionDiagnostic(trace::interaction_diagnostic_code::barrier_capture_no_tree);
            return false;
        }
        const ScrollNodeSnapshot* target =
            m_activeInputScrollTree->scrollTargetAt(x, y);
        if (!target || !target->compositorEligible()) {
            recordInteractionDiagnostic(trace::interaction_diagnostic_code::barrier_capture_no_target);
            return false;
        }
        targetKey = target->key;
        targetSceneVersion = target->sceneVersion;
        targetScrollEpoch = target->scrollEpoch;
    }

    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    CompositorScrollState* state = findCompositorScrollState(
        m_compositorScrollStates, targetKey);
    if (!state) {
        recordInteractionDiagnostic(trace::interaction_diagnostic_code::barrier_capture_no_state);
        return false;
    }
    *barrier = state->controller.captureBarrier();
    // Hit testing and controller sampling use different locks. A scene can
    // advance between them, so bind the barrier to the scene in which the
    // physical point was resolved while retaining the latest visual sample
    // from the same node/epoch.
    if (!barrier->valid || barrier->key != targetKey
        || barrier->scrollEpoch != targetScrollEpoch
        || barrier->sceneVersion < targetSceneVersion) {
        *barrier = ScrollBarrier();
    } else {
        barrier->sceneVersion = targetSceneVersion;
    }
    recordInteractionDiagnostic(barrier->valid
            ? trace::interaction_diagnostic_code::barrier_capture_accepted : trace::interaction_diagnostic_code::barrier_capture_rejected);
    return barrier->valid;
}

bool LayerTreeHost::synchronizeCompositorScrollBarrier(
    const ScrollBarrier& barrier)
{
    if (m_isDestroying || !barrier.valid) {
        recordInteractionDiagnostic(trace::interaction_diagnostic_code::barrier_sync_invalid);
        return false;
    }

    if (barrier.key.nodeId > static_cast<uint64_t>(INT_MAX)) {
        recordInteractionDiagnostic(trace::interaction_diagnostic_code::barrier_sync_invalid_layer_id);
        return false;
    }

    const int layerId = static_cast<int>(barrier.key.nodeId);
    cc_blink::WebLayerImpl* ownerLayer = getLayerById(layerId);
    const bool ownerMatches = ownerLayer
        && scrollNodeKeyForLayer(this, ownerLayer) == barrier.key
        && ownerLayer->compositorScrollEpoch() == barrier.scrollEpoch;

    uint64_t activeInputScene = 0;
    bool requiresRetiredStateProof = false;
    {
        WTF::Locker<WTF::Mutex> locker(m_compositorScrollInputMutex);
        if (!m_activeInputScrollTree
            || m_activeInputScrollTree->sceneVersion()
                < barrier.sceneVersion) {
            recordInteractionDiagnostic(trace::interaction_diagnostic_code::barrier_sync_future_scene);
            return false;
        }
        activeInputScene = m_activeInputScrollTree->sceneVersion();
        ScrollNodeSnapshot* active =
            m_activeInputScrollTree->findMutableNode(barrier.key);
        if (active && active->sceneVersion < barrier.sceneVersion) {
            recordInteractionDiagnostic(trace::interaction_diagnostic_code::barrier_sync_tree_mismatch);
            return false;
        }
        requiresRetiredStateProof = (!active
                && activeInputScene >= barrier.sceneVersion)
            || (active && active->scrollEpoch != barrier.scrollEpoch);
        if (!ownerMatches && !requiresRetiredStateProof) {
            recordInteractionDiagnostic(trace::interaction_diagnostic_code::barrier_sync_owner_mismatch);
            return false;
        }
        // An owner reconciliation may replace the tree at this or a newer
        // scene version and omit the old scroll node. The bounded retirement
        // record checked below then becomes the authoritative identity proof.
        if (active && active->scrollEpoch == barrier.scrollEpoch)
            active->setInputPrediction(barrier.visualOffset);
        for (size_t i = 0; i < m_pendingCompositorScrollInputs.size();) {
            CompositorScrollInput* input =
                m_pendingCompositorScrollInputs[i];
            if (input->node == barrier.key
                && input->inputSequence <= barrier.throughInputSequence) {
                if (input->hasTrace) {
                    auto value = input->traceIdentity;
                    value.flags = 0; value.outcome = trace::terminal::cancelled;
                    value.why = trace::reason::superseded;
                    recordRenderStage(trace::stage::input_terminal, value);
                }
                delete input;
                m_pendingCompositorScrollInputs.erase(m_pendingCompositorScrollInputs.begin() + i);
            } else {
                ++i;
            }
        }
    }

    ScrollSample synchronized;
    synchronized.valid = true;
    synchronized.key = barrier.key;
    synchronized.scrollEpoch = barrier.scrollEpoch;
    synchronized.sampleSequence = barrier.sampleSequence;
    synchronized.throughInputSequence = barrier.throughInputSequence;
    synchronized.consumedTotal = barrier.consumedTotal;
    bool synchronizedController = false;
    bool retiredStateProvesReconciliation = false;
    {
        WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
        CompositorScrollState* state = findCompositorScrollState(
            m_compositorScrollStates, barrier.key);
        if (state && state->controller.synchronizeBarrier(barrier,
                &synchronized)) {
            synchronizedController = true;
            WTF::HashMap<int, CompositingLayer*>::iterator layerIt =
                m_liveCCLayers.find(layerId);
            if (layerIt != m_liveCCLayers.end()) {
                const ScrollNodeSnapshot& node =
                    state->controller.activeNode();
                layerIt->value->setCompositorScrollDelta(
                    barrier.visualOffset.x - node.baseOffset.x,
                    barrier.visualOffset.y - node.baseOffset.y);
                addAndMergeDirty(&m_dirtyRectsForComposite,
                    SkRect::MakeXYWH(node.viewportClip.x,
                        node.viewportClip.y, node.viewportClip.width,
                        node.viewportClip.height));
                addCompositorScrollbarDamage(&m_dirtyRectsForComposite,
                    node);
            }

            WTF::HashMap<int, CompositorScrollUpdateTask*>::iterator pending =
                m_scrollUpdateTasks.find(layerId);
            if (pending != m_scrollUpdateTasks.end())
                pending->value->update(synchronized, barrier.visualOffset);
        }

        if (!synchronizedController && requiresRetiredStateProof) {
            for (size_t i = 0;
                 i < m_retiredCompositorScrollStates.size(); ++i) {
                const RetiredCompositorScrollState* retired =
                    m_retiredCompositorScrollStates[i];
                if (retired->barrier.key == barrier.key
                    && retired->barrier.scrollEpoch == barrier.scrollEpoch
                    && retired->barrier.sceneVersion >= barrier.sceneVersion
                    && retired->barrier.sampleSequence
                        >= barrier.sampleSequence
                    && retired->barrier.throughInputSequence
                        >= barrier.throughInputSequence
                    && retired->retiredAtScene <= activeInputScene) {
                    retiredStateProvesReconciliation = true;
                    break;
                }
            }
        }
    }

    // If a forward scene already consumed the node, owner acknowledgement is
    // the synchronization point. The dispatch is idempotent when that exact
    // (or a newer) sample was already applied.
    if (ownerMatches) {
        if (!dispatchCompositorScrollOnMainThread(layerId, synchronized,
                barrier.visualOffset)) {
            recordInteractionDiagnostic(trace::interaction_diagnostic_code::barrier_sync_owner_rejected);
            return false;
        }
    } else if (!retiredStateProvesReconciliation) {
        recordInteractionDiagnostic(trace::interaction_diagnostic_code::barrier_sync_owner_rejected);
        return false;
    }
    recordInteractionDiagnostic(retiredStateProvesReconciliation
            ? trace::interaction_diagnostic_code::barrier_sync_retired_accepted
            : (synchronizedController ? trace::interaction_diagnostic_code::barrier_sync_accepted
                                      : trace::interaction_diagnostic_code::barrier_sync_forward_accepted));
    if (synchronizedController)
        requestDrawFrameToRunIntoCompositeThread();
    return true;
}

static bool compositorTransformToMatrix(
    const cc_blink::TransformOperations& operations, SkMatrix44* matrix)
{
    matrix->setIdentity();
    const cc_blink::TransformMatrix2D transform = operations.matrix2d();
    if (!operations.isSupported() || !transform.valid)
        return false;

    matrix->setDouble(0, 0, transform.m11);
    matrix->setDouble(1, 0, transform.m12);
    matrix->setDouble(0, 1, transform.m21);
    matrix->setDouble(1, 1, transform.m22);
    matrix->setDouble(0, 3, transform.tx);
    matrix->setDouble(1, 3, transform.ty);
    return true;
}

static void clearCompositorAnimationOverlay(CompositingLayer* layer,
    blink::WebCompositorAnimation::TargetProperty targetProperty)
{
    if (targetProperty == blink::WebCompositorAnimation::TargetPropertyTransform)
        layer->clearCompositorTransform();
    else
        layer->clearCompositorOpacity();
}

bool LayerTreeHost::tickCompositorAnimations(double monotonicTime,
    Vector<SkRect>* dirtyRects)
{
    bool needsAnotherFrame = false;
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);

    for (size_t i = 0; i < m_compositorAnimations.size();) {
        CompositorAnimationState* state = m_compositorAnimations[i];
        CompositingLayer* layer = nullptr;
        WTF::HashMap<int, CompositingLayer*>::iterator layerIt =
            m_liveCCLayers.find(state->layerId);
        if (layerIt != m_liveCCLayers.end())
            layer = layerIt->value;

        if (state->cancelled) {
            if (layer && state->overlayApplied) {
                addAndMergeDirty(dirtyRects, layer->subtreeBoundsInRootSpace());
                clearCompositorAnimationOverlay(layer,
                    state->animation->targetProperty());
                addAndMergeDirty(dirtyRects, layer->subtreeBoundsInRootSpace());
            }
            delete state;
            m_compositorAnimations.remove(i);
            continue;
        }

        if (!layer) {
            needsAnotherFrame = true;
            ++i;
            continue;
        }

        if (state->completed) {
            ++i;
            continue;
        }

        bool affectsOutput = false;
        bool finished = false;
        if (state->animation->targetProperty()
            == blink::WebCompositorAnimation::TargetPropertyTransform) {
            const cc_blink::TransformAnimationSample sample =
                state->animation->sampleTransform(monotonicTime);
            affectsOutput = sample.affectsOutput;
            finished = sample.finished;
            if (sample.affectsOutput) {
                SkMatrix44 transform(SkMatrix44::kIdentity_Constructor);
                if (!compositorTransformToMatrix(sample.value, &transform)) {
                    state->cancelled = true;
                    needsAnotherFrame = true;
                    ++i;
                    continue;
                }
                addAndMergeDirty(dirtyRects, layer->subtreeBoundsInRootSpace());
                layer->setCompositorTransform(transform);
                addAndMergeDirty(dirtyRects, layer->subtreeBoundsInRootSpace());
                state->overlayApplied = true;
            } else if (state->overlayApplied) {
                addAndMergeDirty(dirtyRects, layer->subtreeBoundsInRootSpace());
                layer->clearCompositorTransform();
                addAndMergeDirty(dirtyRects, layer->subtreeBoundsInRootSpace());
                state->overlayApplied = false;
            }
        } else {
            const cc_blink::FloatAnimationSample sample =
                state->animation->sample(monotonicTime);
            affectsOutput = sample.affectsOutput;
            finished = sample.finished;
            if (sample.affectsOutput) {
                addAndMergeDirty(dirtyRects, layer->subtreeBoundsInRootSpace());
                layer->setCompositorOpacity(sample.value);
                state->overlayApplied = true;
            } else if (state->overlayApplied) {
                addAndMergeDirty(dirtyRects, layer->subtreeBoundsInRootSpace());
                layer->clearCompositorOpacity();
                state->overlayApplied = false;
            }
        }

        if (finished) {
            postAnimationFinishedToUiThread(state->layerId,
                state->animation->group(), monotonicTime);
            if (affectsOutput) {
                state->completed = true;
                ++i;
            } else {
                delete state;
                m_compositorAnimations.remove(i);
            }
            continue;
        }

        needsAnotherFrame = true;
        ++i;
    }

    return needsAnotherFrame;
}

static CompositorScrollState* findCompositorScrollState(
    WTF::Vector<CompositorScrollState*>& states, const ScrollNodeKey& key)
{
    for (size_t i = 0; i < states.size(); ++i) {
        if (states[i]->controller.hasActiveNode()
            && states[i]->controller.activeNode().key == key) {
            return states[i];
        }
    }
    return nullptr;
}

static void addCompositorScrollbarDamage(Vector<SkRect>* dirtyRects,
    const ScrollNodeSnapshot& node)
{
    const CompositorScrollbarSnapshot* scrollbars[] = {
        &node.verticalScrollbar, &node.horizontalScrollbar
    };
    for (size_t i = 0; i < 2; ++i) {
        const CompositorScrollbarSnapshot& scrollbar = *scrollbars[i];
        if (!scrollbar.enabled)
            continue;
        addAndMergeDirty(dirtyRects, SkRect::MakeXYWH(
            scrollbar.trackRect.x, scrollbar.trackRect.y,
            scrollbar.trackRect.width, scrollbar.trackRect.height));
    }
}

bool LayerTreeHost::tickCompositorScroll(double monotonicTime,
    Vector<SkRect>* dirtyRects)
{
    ScrollTreeSnapshot* activatedSceneTree = nullptr;
    {
        WTF::Locker<WTF::Mutex> sceneLocker(m_sceneTransactionMutex);
        if (m_sceneCoordinator.activateReady()) {
            if (m_rasterRecoveryPending && !m_rasterRecoveryNeedsInvalidation
                && m_sceneCoordinator.activeVersion() > m_rasterRecoveryAfterScene)
                m_rasterRecoveryPending = false;
            m_activeSceneTraceContext = m_pendingSceneTraceContext;
            trace::add(trace::counter::scene_activated);
            recordRenderStage(trace::stage::scene_activated, m_activeSceneTraceContext);
            activatedSceneTree = new ScrollTreeSnapshot(
                m_sceneCoordinator.activeScene().scrollTree);
        }
    }

    if (activatedSceneTree)
        notifyResourceProgress();
    ScrollTreeSnapshot* pendingTree = nullptr;
    std::vector<CompositorScrollInput*> inputs;
    {
        WTF::Locker<WTF::Mutex> locker(m_compositorScrollInputMutex);
        pendingTree = m_pendingCompositorScrollTree;
        m_pendingCompositorScrollTree = nullptr;
        if (activatedSceneTree) {
            if (!pendingTree || activatedSceneTree->sceneVersion()
                    >= pendingTree->sceneVersion()) {
                delete pendingTree;
                pendingTree = activatedSceneTree;
            } else {
                delete activatedSceneTree;
            }
        }
        inputs.swap(m_pendingCompositorScrollInputs);
    }

    bool needsAnotherFrame = false;
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);

    // Inputs accepted before an eligibility change are consumed against the
    // previously active scene. The pending scene is activated afterwards at
    // this same frame boundary.
    for (size_t i = 0; i < inputs.size(); ++i) {
        CompositorScrollInput* input = inputs[i];
        CompositorScrollState* state = findCompositorScrollState(
            m_compositorScrollStates, input->node);
        const bool enqueued = state && state->controller.enqueueDelta(input->inputSequence,
            input->delta, input->monotonicTime, input->smooth);
        if (input->hasTrace) {
            if (!enqueued || !m_viewTraceIdentity.accepts(input->traceIdentity)
                || state->controller.activeNode().scrollEpoch != input->epoch) {
                auto value = input->traceIdentity;
                value.flags = 0; value.outcome = trace::terminal::stale;
                value.why = trace::reason::stale_generation;
                recordRenderStage(trace::stage::input_terminal, value);
            } else if (!m_independentInputTrace.append(input->traceIdentity, input->node,
                input->epoch, input->inputSequence, input->monotonicTime, input->smooth)) {
                trace::add(trace::counter::causal_ranges_dropped);
                trace::note_causal_loss();
            }
        }
        delete input;
    }

    for (size_t i = 0; i < m_compositorScrollStates.size(); ++i) {
        CompositorScrollState* state = m_compositorScrollStates[i];
        const ScrollSample sample = state->controller.sample(monotonicTime);
        if (sample.valid) {
            WTF::HashMap<int, CompositingLayer*>::iterator layerIt =
                m_liveCCLayers.find(state->layerId);
            if (layerIt != m_liveCCLayers.end()) {
                CompositingLayer* layer = layerIt->value;
                const ScrollNodeSnapshot& node =
                    state->controller.activeNode();
                addAndMergeDirty(dirtyRects, SkRect::MakeXYWH(
                    node.viewportClip.x, node.viewportClip.y,
                    node.viewportClip.width, node.viewportClip.height));
                addCompositorScrollbarDamage(dirtyRects, node);
                const ScrollVector visual = state->controller.visualOffset();
                layer->setCompositorScrollDelta(
                    visual.x - node.baseOffset.x,
                    visual.y - node.baseOffset.y);
                m_independentInputTrace.adopt(sample.key, sample.scrollEpoch,
                    sample.throughInputSequence, monotonicTime, sample.valid);
                postCompositorScrollUpdateToMainThread(state->layerId,
                    sample, visual);
            }
        }
    }

    if (pendingTree
        && pendingTree->sceneVersion() > m_activeCompositorScrollVersion) {
        for (size_t i = 0; i < m_compositorScrollStates.size();) {
            CompositorScrollState* state = m_compositorScrollStates[i];
            if (pendingTree->findNode(state->controller.activeNode().key)) {
                ++i;
                continue;
            }

            WTF::HashMap<int, CompositingLayer*>::iterator layerIt =
                m_liveCCLayers.find(state->layerId);
            rememberRetiredCompositorScrollState(
                m_retiredCompositorScrollStates,
                state->controller.captureBarrier(),
                pendingTree->sceneVersion());
            if (layerIt != m_liveCCLayers.end()) {
                const ScrollNodeSnapshot& oldNode =
                    state->controller.activeNode();
                addAndMergeDirty(dirtyRects, SkRect::MakeXYWH(
                    oldNode.viewportClip.x, oldNode.viewportClip.y,
                    oldNode.viewportClip.width, oldNode.viewportClip.height));
                addCompositorScrollbarDamage(dirtyRects, oldNode);
                layerIt->value->clearCompositorScrollDelta();
            }
            trace::event discarded;
            while (m_independentInputTrace.takeNode(state->controller.activeNode().key,
                state->controller.activeNode().scrollEpoch, &discarded)) {
                discarded.flags = 0; discarded.outcome = trace::terminal::stale;
                discarded.why = trace::reason::stale_generation;
                recordRenderStage(trace::stage::input_terminal, discarded);
            }
            delete state;
            m_compositorScrollStates.remove(i);
        }

        const std::vector<ScrollNodeSnapshot>& nodes = pendingTree->nodes();
        for (size_t i = 0; i < nodes.size(); ++i) {
            const ScrollNodeSnapshot& node = nodes[i];
            if (node.key.nodeId > static_cast<uint64_t>(INT_MAX))
                continue;
            for (size_t retiredIndex = 0;
                 retiredIndex < m_retiredCompositorScrollStates.size();) {
                RetiredCompositorScrollState* retired =
                    m_retiredCompositorScrollStates[retiredIndex];
                if (retired->barrier.key == node.key
                    && retired->barrier.scrollEpoch == node.scrollEpoch) {
                    delete retired;
                    m_retiredCompositorScrollStates.remove(retiredIndex);
                } else {
                    ++retiredIndex;
                }
            }
            CompositorScrollState* state = findCompositorScrollState(
                m_compositorScrollStates, node.key);
            if (state && state->controller.hasActiveNode()
                && state->controller.activeNode().scrollEpoch
                    != node.scrollEpoch) {
                rememberRetiredCompositorScrollState(
                    m_retiredCompositorScrollStates,
                    state->controller.captureBarrier(),
                    pendingTree->sceneVersion());
                trace::event discarded;
                while (m_independentInputTrace.takeNode(state->controller.activeNode().key,
                    state->controller.activeNode().scrollEpoch, &discarded)) {
                    discarded.flags = 0; discarded.outcome = trace::terminal::stale;
                    discarded.why = trace::reason::stale_generation;
                    recordRenderStage(trace::stage::input_terminal, discarded);
                }
            }
            if (!state) {
                state = new CompositorScrollState(
                    static_cast<int>(node.key.nodeId));
                m_compositorScrollStates.append(state);
            }
            if (!state->controller.activateScene(node))
                continue;

            WTF::HashMap<int, CompositingLayer*>::iterator layerIt =
                m_liveCCLayers.find(state->layerId);
            if (layerIt == m_liveCCLayers.end())
                continue;
            const ScrollVector visual = state->controller.visualOffset();
            if (visual.x == node.baseOffset.x
                && visual.y == node.baseOffset.y) {
                layerIt->value->clearCompositorScrollDelta();
            } else {
                layerIt->value->setCompositorScrollDelta(
                    visual.x - node.baseOffset.x,
                    visual.y - node.baseOffset.y);
            }
            addAndMergeDirty(dirtyRects, SkRect::MakeXYWH(
                node.viewportClip.x, node.viewportClip.y,
                node.viewportClip.width, node.viewportClip.height));
            addCompositorScrollbarDamage(dirtyRects, node);
        }

        m_activeCompositorScrollVersion = pendingTree->sceneVersion();
        {
            WTF::Locker<WTF::Mutex> inputLocker(m_compositorScrollInputMutex);
            ScrollTreeSnapshot* nextInputTree =
                new ScrollTreeSnapshot(*pendingTree);
            if (m_activeInputScrollTree) {
                const std::vector<ScrollNodeSnapshot>& nextNodes =
                    nextInputTree->nodes();
                for (size_t i = 0; i < nextNodes.size(); ++i) {
                    const ScrollNodeSnapshot* previous =
                        m_activeInputScrollTree->findNode(nextNodes[i].key);
                    ScrollNodeSnapshot* next =
                        nextInputTree->findMutableNode(nextNodes[i].key);
                    if (previous && next
                        && previous->scrollEpoch == next->scrollEpoch) {
                        next->setInputPrediction(previous->inputPrediction);
                    }
                }
            }
            delete m_activeInputScrollTree;
            m_activeInputScrollTree = nextInputTree;
            // A scene switch may change only stacking/hit-test metadata while
            // retaining the same node identity. End the old latch so an
            // overlay can revoke fast scrolling immediately.
            m_latchedScrollNode = ScrollNodeKey();
        }
    }

    // A ready scene can release a target parked at its old raster boundary.
    // Query after activation so its continuation is scheduled without a new
    // wheel event; parked targets themselves do not request empty frames.
    for (size_t i = 0; i < m_compositorScrollStates.size(); ++i)
        needsAnotherFrame |= m_compositorScrollStates[i]->controller.needsAnimationFrame();

    delete pendingTree;
    return needsAnotherFrame;
}

void LayerTreeHost::postCompositorScrollUpdateToMainThread(int layerId,
    const ScrollSample& sample, const ScrollVector& visualOffset)
{
    // Called with m_compositeMutex held. Coalesce samples so a busy Blink
    // owner receives the latest cumulative total without an unbounded queue.
    WTF::HashMap<int, CompositorScrollUpdateTask*>::iterator it =
        m_scrollUpdateTasks.find(layerId);
    if (it != m_scrollUpdateTasks.end()) {
        it->value->update(sample, visualOffset);
        return;
    }

    if (m_isDestroying || !Platform::current()
        || !Platform::current()->mainThread()) {
        return;
    }

    CompositorScrollUpdateTask* task = new CompositorScrollUpdateTask(
        this, layerId, sample, visualOffset);
    m_scrollUpdateTasks.add(layerId, task);
    Platform::current()->mainThread()->postTask(FROM_HERE,
        WTF::bind(&CompositorScrollUpdateTask::dispatch, task));
}

bool LayerTreeHost::dispatchCompositorScrollOnMainThread(int layerId,
    const ScrollSample& sample, const ScrollVector& visualOffset)
{
    cc_blink::WebLayerImpl* layer = getLayerById(layerId);
    if (!layer || layer->compositorScrollEpoch() != sample.scrollEpoch)
        return false;

    const uint64_t acknowledged = layer->acknowledgedCompositorSample();
    if (acknowledged >= sample.sampleSequence) {
        if (acknowledged > sample.sampleSequence)
            return true;
        return layer->matchesCompositorScrollReceipt(sample.scrollEpoch,
            sample.sampleSequence, sample.consumedTotal, visualOffset);
    }

    return layer->setScrollPositionFromCompositor(sample.scrollEpoch,
        sample.sampleSequence, sample.consumedTotal.x,
        sample.consumedTotal.y,
        blink::WebDoublePoint(visualOffset.x, visualOffset.y));
}

void LayerTreeHost::postAnimationFinishedToUiThread(int layerId, int group,
    double monotonicTime)
{
    // Called while m_compositeMutex is held by tickCompositorAnimations.
    if (m_isDestroying || !Platform::current() || !Platform::current()->mainThread())
        return;

    CompositorAnimationFinishedTask* task = new CompositorAnimationFinishedTask(
        this, layerId, group, monotonicTime);
    m_animationFinishedTasks.add(task);
    Platform::current()->mainThread()->postTask(FROM_HERE,
        WTF::bind(&CompositorAnimationFinishedTask::dispatch, task));
}

void LayerTreeHost::dispatchAnimationFinishedOnMainThread(int layerId, int group,
    double monotonicTime)
{
    cc_blink::WebLayerImpl* layer = getLayerById(layerId);
    if (layer)
        layer->notifyCompositorAnimationFinished(monotonicTime, group);
}

void LayerTreeHost::requestApplyActionsToRunIntoCompositeThread(bool needCheck)
{
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    if (!m_compositeThread) {
        RELEASE_ASSERT(!m_uiThreadClient);
        return;
    }
    if (0 != m_requestApplyActionsCount && !needCheck) // 如果needCheck==true，则表示是退出流程，必须执行一次onApply
        return;

    atomicIncrement(&m_requestApplyActionsCount);
    atomicIncrement(&m_requestApplyActionsFinishCount);
    m_compositeThread->platformThread().postTask(FROM_HERE, WTF::bind(&LayerTreeHost::onApplyActionsInCompositeThread, this, needCheck));
}

void LayerTreeHost::onApplyActionsInCompositeThread(bool needCheck)
{
    atomicDecrement(&m_requestApplyActionsCount);
    applyActions(needCheck);
    atomicDecrement(&m_requestApplyActionsFinishCount);
}

void LayerTreeHost::clearCanvas(SkCanvas* canvas, const SkRect& rect, bool useLayeredBuffer)
{
    // When using transparency mode clear the rectangle before painting.
    SkPaint clearPaint;
    if (useLayeredBuffer) {
        clearPaint.setARGB(0, 0xFF, 0xFF, 0xFF);
        clearPaint.setXfermodeMode(SkXfermode::kClear_Mode);
    } else {
        clearPaint.setARGB(0xFF, 0xFF, 0xFF, 0xFF);
        clearPaint.setXfermodeMode(SkXfermode::kSrcOver_Mode);
    }

    SkRect skrc;
    skrc.set(rect.x(), rect.y(), rect.x() + rect.width(), rect.y() + rect.height());
    canvas->drawRect(skrc, clearPaint);
}

void LayerTreeHost::setDrawMinInterval(double drawMinInterval)
{
    if (drawMinInterval < 0 || !std::isfinite(drawMinInterval))
        return;
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    m_drawMinInterval = drawMinInterval;
    m_nextCompositeFrameTime = 0;
}

void LayerTreeHost::setMainFrameDeadline(uint64_t frameId, double target, double interval)
{
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    m_unifiedFrameDeadline = interval > 0;
    if (!frameId || frameId <= m_pendingMainFrameId)
        return;
    m_pendingMainFrameId = frameId;
    m_pendingMainFrameDeadline = target;
    m_unpresentedInputTrace = m_pendingInputTrace;
    m_unpresentedInputFrameId = frameId;
    m_frameTraceContext = m_viewTraceIdentity.frameContext(renderTraceContext());
    m_frameTraceContext.input_first = m_frameTraceContext.input_last = 0;
    m_frameTraceContext.frame_id = frameId;
    m_frameTraceContext.action_first = m_recordedTraceActionFirst;
    m_frameTraceContext.action_last = m_recordedTraceActionLast;
    if (m_pendingInputTrace.size()) {
        m_frameTraceContext = m_pendingInputTrace.at(0);
        m_frameTraceContext.input_first = m_frameTraceContext.input_last = 0;
        m_frameTraceContext.frame_id = frameId;
        m_frameTraceContext.action_first = m_recordedTraceActionFirst;
        m_frameTraceContext.action_last = m_recordedTraceActionLast;
        for (size_t index = 0; index < m_pendingInputTrace.size(); ++index) {
            auto value = m_pendingInputTrace.at(index);
            value.frame_id = frameId;
            value.action_first = m_recordedTraceActionFirst;
            value.action_last = m_recordedTraceActionLast;
            value.flags = trace::coverage_valid; // Processed, not proven visible content.
            recordRenderStage(trace::stage::frame_coverage, value);
            recordRenderStage(trace::stage::commit_complete, value);
        }
        bool keepForRecovery = false;
        {
            WTF::Locker<WTF::Mutex> sceneLock(m_sceneTransactionMutex);
            keepForRecovery = m_rasterRecoveryPending;
        }
        if (!keepForRecovery)
            m_pendingInputTrace.clear();
    } else {
        m_frameTraceContext.frame_id = frameId;
        recordRenderStage(trace::stage::commit_complete, m_frameTraceContext);
    }
}

static void recordStaleInputTrace(content::input_frame_trace::event value)
{
    value.flags = 0;
    value.why = trace::reason::stale_generation;
    value.outcome = trace::terminal::stale;
    value.frame_id = value.action_first = value.action_last = value.related_id = 0;
    recordRenderStage(trace::stage::input_terminal, value);
}

bool LayerTreeHost::setViewTraceIdentity(const content::input_frame_trace::event& value)
{
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    const auto previous = m_viewTraceIdentity.value();
    if (!m_viewTraceIdentity.set(value))
        return false;
    if (InputFrameCoverage::sameGeneration(previous, value))
        return true;
    InputFrameCoverage retired;
    size_t lost = 0;
    m_viewTraceIdentity.retainCurrent(m_pendingInputTrace, retired, lost);
    m_viewTraceIdentity.retainCurrent(m_unpresentedInputTrace, retired, lost);
    if (lost) {
        trace::add(trace::counter::causal_ranges_dropped, lost);
        trace::note_causal_loss(lost);
    }
    if (!m_unpresentedInputTrace.size())
        m_unpresentedInputFrameId = 0;
    for (size_t index = 0; index < retired.size(); ++index)
        recordStaleInputTrace(retired.at(index));
    trace::event independentStale;
    while (m_independentInputTrace.takeStale(m_viewTraceIdentity.value(), &independentStale))
        recordStaleInputTrace(independentStale);
    // Scene and owning framebuffer descriptors remain immutable snapshots.
    m_frameTraceContext = m_viewTraceIdentity.value();
    return true;
}

content::input_frame_trace::event LayerTreeHost::traceContextForFrame()
{
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    return m_viewTraceIdentity.frameContext(renderTraceContext());
}

void LayerTreeHost::noteProcessedInputTrace(const content::input_frame_trace::event& value)
{
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    if (!m_viewTraceIdentity.accepts(value)) {
        recordStaleInputTrace(value);
        return;
    }
    if (!m_pendingInputTrace.append(value)) {
        trace::add(trace::counter::causal_ranges_dropped);
        trace::note_causal_loss();
    }
}

void LayerTreeHost::setIndependentPresentationEnabled(bool enabled)
{
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    m_independentPresentationEnabled = enabled;
}

void LayerTreeHost::postPaintMessage(const SkRect& paintRect)
{
    SkRect dirtyRect = paintRect;

    m_compositeMutex.lock();

    SkRect clientRect = SkRect::MakeXYWH(m_clientRect.x(), m_clientRect.y(), m_clientRect.width(), m_clientRect.height());
    if (paintRect.isEmpty() || !dirtyRect.intersect(clientRect)) {
        m_compositeMutex.unlock();
        return;
    }

//     m_dirtyRectsForComposite.append(dirtyRect);
//     mergeDirty(&m_dirtyRectsForComposite);
    addAndMergeDirty(&m_dirtyRectsForComposite, dirtyRect);
    m_compositeMutex.unlock();
}

void LayerTreeHost::firePaintEvent(HDC hdc, const RECT* paintRect)
{
    content::PaintActivityScope paintActivity(m_paintLifetime);
    if (!paintActivity)
        return;
    FrameBufferLease lease = acquireFrameBufferLease();
    if (!lease || !paintRect)
        return;
    RenderTraceContextScope frameScope(lease.buffer->traceContext);
    if (!lease.buffer->alpha)
        skia::DrawToNativeContext(lease.canvas(), hdc, paintRect->left,
            paintRect->top, paintRect);
    else {
        RECT client = {0, 0, lease.buffer->width, lease.buffer->height};
        skia::DrawToNativeLayeredContext(lease.canvas(), hdc, paintRect, &client);
    }
}

void LayerTreeHost::drawFrameInCompositeThread()
{
    content::PaintActivityScope paintActivity(m_paintLifetime);
    if (!paintActivity) {
        m_uiPaintRetry->drawQueuedCount.fetch_sub(1);
        m_uiPaintRetry->drawFinishCount.fetch_sub(1);
        return;
    }
    m_compositeMutex.lock();
    if (1 != m_uiPaintRetry->drawQueuedCount)
        DebugBreak();
    m_uiPaintRetry->drawQueuedCount.fetch_sub(1);
    m_compositeMutex.unlock();

    const double lastCompositeTime = WTF::monotonicallyIncreasingTime();
    uint64_t committedFrame = 0;
    trace::event frameContext;
    long long pacingDelay = 0;
    {
        WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
        if (!m_visible && !m_isDestroying) {
            m_uiPaintRetry->drawFinishCount.fetch_sub(1);
            return;
        }
        committedFrame = m_pendingMainFrameId;
        frameContext = m_frameTraceContext;
        const bool alreadyPaced = m_unifiedFrameDeadline
            && committedFrame > m_presentedMainFrameId
            && lastCompositeTime >= m_pendingMainFrameDeadline;
        if (!alreadyPaced && !m_isDestroying)
            pacingDelay = LegacyPaintRetryPolicy::delayMilliseconds(
                lastCompositeTime, m_nextCompositeFrameTime);
        if (!pacingDelay) {
            m_nextCompositeFrameTime = nextFrameDeadline(
                m_nextCompositeFrameTime, lastCompositeTime, m_drawMinInterval);
            m_lastCompositeTime = lastCompositeTime;
        }
    }
    if (pacingDelay > 0) {
        trace::add(trace::counter::pacing_wakes);
        requestDrawFrameAfterDelay(pacingDelay);
        m_uiPaintRetry->drawFinishCount.fetch_sub(1);
        return;
    }

#if 0
    LARGE_INTEGER performanceCount = { 0 };
    QueryPerformanceCounter(&performanceCount);
    static DWORD gLastCount = 0;
    WTF::String outstr = String::format("LayerTreeHost::drawFrameInCompositeThread:[%d]\n", performanceCount.LowPart - gLastCount);
    OutputDebugStringA(outstr.utf8().data());
    gLastCount = performanceCount.LowPart;
#endif

    RenderTraceContextScope frameTraceScope(frameContext);
    RenderDurationScope compositeDuration(trace::duration_metric::composite);
    recordRenderStage(trace::stage::composite_begin);
    bool frameReady = preDrawFrame(); // 这里也会发起Commit
    {
        WTF::Locker<WTF::Mutex> sceneLock(m_sceneTransactionMutex);
        if (m_rasterRecoveryPending && (m_rasterRecoveryNeedsInvalidation
            || !m_sceneCoordinator.hasReady())) {
            // The previous canvas remains the only visible scene while a
            // replacement reraster is outstanding. Do not consume its dirty.
            recordRenderStage(trace::stage::composite_end);
            m_uiPaintRetry->drawFinishCount.fetch_sub(1);
            return;
        }
    }

    Vector<SkRect> animationDirtyRects;
    const bool animationNeedsAnotherFrame = tickCompositorAnimations(
        lastCompositeTime, &animationDirtyRects);
    const bool scrollNeedsAnotherFrame = tickCompositorScroll(
        lastCompositeTime, &animationDirtyRects);
    {
        WTF::Locker<WTF::Mutex> sceneLocker(m_sceneTransactionMutex);
        // Unknown active identity must not inherit a newer pending frame.
        currentRenderTraceContext() = m_activeSceneTraceContext;
    }

    m_compositeMutex.lock();
    Vector<SkRect> dirtyRects = m_dirtyRectsForComposite;
    m_dirtyRectsForComposite.clear();
    m_compositeMutex.unlock();

    for (size_t i = 0; i < animationDirtyRects.size(); ++i)
        addAndMergeDirty(&dirtyRects, animationDirtyRects[i]);

    m_canvasPaintBlocked = false;
    uint64_t paintedFrame = 0;
    for (size_t i = 0; i < dirtyRects.size() && !m_isDestroying; ++i) {
        const SkRect& r = dirtyRects[i];
        const uint64_t actualFrame = paintToMemoryCanvasInCompositeThread(r);
        if (actualFrame)
            paintedFrame = actualFrame;
    }

#if defined(SOUI_GPU_COMPOSITOR)
    if (m_gpuSurface && m_gpuSurface->available() &&
        m_gpuSurface->directPresent() && !dirtyRects.isEmpty()) {
        // One SwapBuffers/vsync wait per compositor frame, regardless of how
        // many dirty rectangles were merged for that frame.
        m_gpuSurface->flush();
        if (m_gpuSurface->present() && gpuDiagnosticReadbackEnabled()) {
            const int diagnosticY = std::min(220, m_clientRect.height() - 1);
            SkIRect diagnosticRect = SkIRect::MakeXYWH(0, diagnosticY, m_clientRect.width(), 1);
            if (m_gpuSurface->readback(m_memoryCanvas, diagnosticRect))
                m_uiThreadClient->paintToMemoryCanvasInUiThread(
                    m_memoryCanvas, blink::IntRect(0, diagnosticY, m_clientRect.width(), 1));
        }
    }
#endif

    const bool paintCompleted = !m_canvasPaintBlocked && !m_isDestroying;
    {
        WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
        m_presentedMainFrameId = FrameBufferLeasePolicy::completedPaintFrame(
            m_presentedMainFrameId, paintedFrame, paintCompleted);
        if (FrameBufferLeasePolicy::retiresInputCoverage(
            m_unpresentedInputFrameId, paintedFrame, paintCompleted)) {
            m_unpresentedInputTrace.clear();
            m_unpresentedInputFrameId = 0;
        }
    }
    postDrawFrame();
    if ((animationNeedsAnotherFrame || scrollNeedsAnotherFrame)
        && !m_isDestroying) {
        requestDrawFrameToRunIntoCompositeThread();
    }
    // Incomplete action/resource transactions are resumed by producer
    // publication, not a periodic draw task. No resource fence is bypassed.
    (void)frameReady;
    recordRenderStage(trace::stage::composite_end);
    m_uiPaintRetry->drawFinishCount.fetch_sub(1);
}

void LayerTreeHost::paintToMemoryCanvasInUiThread(const SkRect& paintRect)
{
    content::PaintActivityScope paintActivity(m_paintLifetime);
    if (!paintActivity)
        return;
    FrameBufferLease lease;
    LayerTreeHostUiThreadClient* client = nullptr;
    {
        WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
        if (!m_visible.load()) return;
        lease = acquireFrameBufferLeaseLocked();
        client = m_uiThreadClient;
    }
    if (lease && client) {
        RenderTraceContextScope frameScope(lease.buffer->traceContext);
        const blink::IntRect rect(paintRect.x(), paintRect.y(),
            paintRect.width(), paintRect.height());
        trace::add(trace::counter::client_callbacks);
        RenderDurationScope callbackDuration(trace::duration_metric::client_callback);
        client->paintToMemoryCanvasInUiThread(lease.canvas(), rect);
    }
}

void LayerTreeHost::queueLegacyPaintLocked()
{
    if (m_isDestroying || !m_visible.load() || m_dirtyRectsForUi.isEmpty() || m_hasResize || !m_memoryCanvas
        || !Platform::current() || !Platform::current()->mainThread())
        return;
    const double now = WTF::monotonicallyIncreasingTime();
    LegacyPaintRetryPolicy::Ticket ticket;
    {
        std::lock_guard<std::mutex> retryLock(m_uiPaintRetry->mutex);
        ticket = m_uiPaintRetry->policy.arm(now, m_unifiedFrameDeadline ? 0 : m_lastPaintTime,
            m_drawMinInterval);
    }
    if (!ticket.sequence)
        return;
    UiPaintRetryTask* task = new (std::nothrow) UiPaintRetryTask(m_uiPaintRetry, ticket);
    if (!task) {
        std::lock_guard<std::mutex> retryLock(m_uiPaintRetry->mutex);
        m_uiPaintRetry->policy.dispose(ticket);
        m_uiPaintRetry->notificationFailed = true;
        return;
    }
    const long long delay = LegacyPaintRetryPolicy::delayMilliseconds(now, ticket.deadline);
    if (delay > 0)
        Platform::current()->mainThread()->postDelayedTask(FROM_HERE, task, delay);
    else
        Platform::current()->mainThread()->postTask(FROM_HERE, task);
}

void LayerTreeHost::runLegacyPaintRetry(const LegacyPaintRetryPolicy::Ticket& ticket)
{
    content::PaintActivityScope paintActivity(m_paintLifetime);
    if (!paintActivity)
        return;
    Vector<SkRect> dirtyRects;
    const std::shared_ptr<UiPaintRetryState> state = m_uiPaintRetry;
    {
        WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
        bool accepted;
        {
            std::lock_guard<std::mutex> retryLock(state->mutex);
            accepted = state->policy.accepts(ticket);
            state->policy.dispose(ticket);
        }
        if (!accepted || m_isDestroying || !m_visible.load() || m_hasResize || !m_memoryCanvas) {
            queueLegacyPaintLocked();
            return;
        }
        const double now = WTF::monotonicallyIncreasingTime();
        if (now < ticket.deadline) {
            // A premature wake retains damage and schedules actual remaining
            // time. It never creates a ready-task throttle loop.
            queueLegacyPaintLocked();
            return;
        }
        m_lastPaintTime = now;
        dirtyRects.swap(m_dirtyRectsForUi);
    }
    for (size_t i = 0; i < dirtyRects.size(); ++i) {
        LayerTreeHost* host;
        {
            std::lock_guard<std::mutex> retryLock(state->mutex);
            host = state->host;
            if (state->policy.generation() != ticket.generation)
                return;
        }
        if (!host)
            return;
        host->paintToMemoryCanvasInUiThread(dirtyRects[i]);
    }
}

void LayerTreeHost::requestPaintToMemoryCanvasToUiThread(const SkRect& r)
{
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    SkRect dirtyRect(r);
    const SkRect clientRect = SkRect::MakeXYWH(m_clientRect.x(), m_clientRect.y(),
        m_clientRect.width(), m_clientRect.height());
    if (!r.isEmpty() && dirtyRect.intersect(clientRect))
        addAndMergeDirty(&m_dirtyRectsForUi, dirtyRect);
    queueLegacyPaintLocked();
}

void LayerTreeHost::publishIndependentCoverageForPaint(const content::input_frame_trace::event& actualFrame)
{
    trace::event executor;
    const bool hasExecutor = trace::current_context(executor);
    trace::event value;
    while (m_independentInputTrace.takeForPaint(actualFrame, &value)) {
        value.task_id = hasExecutor ? executor.task_id : 0;
        recordRenderStage(trace::stage::frame_coverage, value);
    }
}

uint64_t LayerTreeHost::paintToMemoryCanvasInCompositeThread(const SkRect& r)
{
    RenderMutexLock<WTF::Mutex> locker(m_compositeMutex);
    SkRect paintRect = r;
    if (!m_visible.load()) {
        addAndMergeDirty(&m_dirtyRectsForComposite, paintRect);
        m_canvasPaintBlocked = true;
        return 0;
    }
#if defined(SOUI_GPU_COMPOSITOR)
    bool resized = m_hasResize;
#endif

    if (m_hasResize || !m_memoryCanvas)
        paintRect = m_clientRect;
    if (!prepareWritableFrameBufferLocked()) {
        addAndMergeDirty(&m_dirtyRectsForComposite, paintRect);
        m_canvasPaintBlocked = true;
        std::lock_guard<std::mutex> stateLock(m_uiPaintRetry->mutex);
        m_uiPaintRetry->drawWaiting = true;
        m_uiPaintRetry->resource.wait();
        return 0;
    }

    SkRect clientRect = SkRect::MakeXYWH(m_clientRect.x(), m_clientRect.y(), m_clientRect.width(), m_clientRect.height());
    if (!paintRect.intersect(clientRect))
        return 0;

    if (!m_memoryCanvas) {
        ASSERT(false);
        return 0;
    }

    m_isDrawDirty = true;
#if defined(SOUI_GPU_COMPOSITOR)
    if (m_gpuExperiment && m_uiThreadClient) {
        HWND window = static_cast<HWND>(m_uiThreadClient->nativeWindowHandle());
        if (!m_gpuSurface) {
            wke::diagnostics::observe_renderer_factory(WKE_RENDER_MODEL_FACTORY_OFFSCREEN);
            m_gpuSurface = new GpuSkiaSurface();
        }
        if (resized)
            m_gpuInitAttempted = false;
        if (!m_gpuSurface->available() && !m_gpuInitAttempted) {
            m_gpuInitAttempted = true;
            m_gpuSurface->initialize(window, m_clientRect.width(), m_clientRect.height());
        } else if (m_gpuSurface->available() && resized)
            m_gpuSurface->resize(m_clientRect.width(), m_clientRect.height());
        if (m_gpuSurface->available()) {
            m_gpuSurface->beginFrame();
            SkCanvas* gpuCanvas = m_gpuSurface->canvas();
            if (m_hasTransparentBackground)
                clearCanvas(gpuCanvas, paintRect, m_hasTransparentBackground);
            bool needNotifUi = drawToCanvas(gpuCanvas, paintRect);
#if defined(ENABLE_WKE) && ENABLE_WKE == 1
            wke::diagnostics::renderer_draw_completed(WKE_RENDER_DIAGNOSTICS_DRAW_GPU_SKIA);
#endif
            // Direct presentation is frame-scoped. Several dirty rectangles
            // may be rendered into the same surface before the compositor
            // submits one window buffer at the end of the frame.
            if (m_gpuSurface->directPresent()) {
                (void)needNotifUi;
                return renderTraceContext().frame_id;
            }
            gpuCanvas->flush();
            if (m_gpuSurface->present()) {
                if (gpuDiagnosticReadbackEnabled()) {
                    // Keep the existing paint callback's timing/pixel
                    // diagnostics without copying the full GPU frame back.
                    const int diagnosticY = std::min(220, m_clientRect.height() - 1);
                    SkIRect diagnosticRect = SkIRect::MakeXYWH(0, diagnosticY, m_clientRect.width(), 1);
                    if (m_gpuSurface->readback(m_memoryCanvas, diagnosticRect))
                        m_uiThreadClient->paintToMemoryCanvasInUiThread(
                            m_memoryCanvas, blink::IntRect(0, diagnosticY, m_clientRect.width(), 1));
                }
                return renderTraceContext().frame_id;
            }
            SkIRect readRect = SkIRect::MakeXYWH(static_cast<int>(paintRect.x()),
                                                 static_cast<int>(paintRect.y()),
                                                 static_cast<int>(paintRect.width()),
                                                 static_cast<int>(paintRect.height()));
            if (m_gpuSurface->readback(m_memoryCanvas, readRect)) {
                m_gpuSurface->recordPresented();
                if (!needNotifUi)
                    return renderTraceContext().frame_id;
                requestPaintToMemoryCanvasToUiThread(paintRect);
                return renderTraceContext().frame_id;
            }
        }
    }
#endif
    if (m_hasTransparentBackground)
        clearCanvas(m_memoryCanvas, paintRect, m_hasTransparentBackground);

    bool needNotifUi = drawToCanvas(m_memoryCanvas, paintRect); // 绘制脏矩形
    m_frameBuffers[m_currentFrameBufferSlot]->traceContext = renderTraceContext();
    const uint64_t paintedFrame = m_frameBuffers[m_currentFrameBufferSlot]->traceContext.frame_id;
    publishIndependentCoverageForPaint(m_frameBuffers[m_currentFrameBufferSlot]->traceContext);
#if defined(ENABLE_WKE) && ENABLE_WKE == 1
    wke::diagnostics::renderer_draw_completed(WKE_RENDER_DIAGNOSTICS_DRAW_BITMAP_RASTER);
#endif

    if (!needNotifUi)
        return paintedFrame;

    bool direct = m_independentPresentationEnabled;
#if ENABLE_WKE == 1
    direct |= blink::RuntimeEnabledFeatures::updataInOtherThreadEnabled();
#endif
    if (direct) {
        FrameBufferLease lease = acquireFrameBufferLeaseLocked();
        LayerTreeHostUiThreadClient* client = m_uiThreadClient;
        const blink::IntRect rect(paintRect.x(), paintRect.y(),
            paintRect.width(), paintRect.height());
        locker.unlock();
        if (lease && client) {
            trace::add(trace::counter::client_callbacks);
            RenderTraceContextScope frameScope(lease.buffer->traceContext);
            RenderDurationScope callbackDuration(trace::duration_metric::client_callback);
            client->paintToMemoryCanvasInUiThread(lease.canvas(), rect);
        }
        return paintedFrame;
    }

    requestPaintToMemoryCanvasToUiThread(paintRect);
    return paintedFrame;
}

LayerTreeHost::FrameBuffer::FrameBuffer(std::unique_ptr<SkCanvas> value, uint64_t sizeGeneration,
    int w, int h, bool alphaValue)
    : canvas(std::move(value)), generation(sizeGeneration), bytes(static_cast<uint64_t>(w) * h * 4)
    , width(w), height(h), alpha(alphaValue) { }
LayerTreeHost::FrameBuffer::~FrameBuffer() = default;
LayerTreeHost::FrameBufferLease::FrameBufferLease() = default;
LayerTreeHost::FrameBufferLease::FrameBufferLease(std::shared_ptr<FrameBuffer> value,
    std::shared_ptr<UiPaintRetryState> state,
    const std::shared_ptr<content::PaintLifetimeState>& lifetime)
    : activity(lifetime), buffer(std::move(value)), completionState(std::move(state))
{
    if (!activity) {
        buffer.reset();
        completionState.reset();
    }
}
LayerTreeHost::FrameBufferLease::~FrameBufferLease()
{
    buffer.reset();
    if (completionState)
        LayerTreeHost::notifyRasterCompletion(completionState);
}
LayerTreeHost::FrameBufferLease::FrameBufferLease(FrameBufferLease&& other) noexcept
    : activity(std::move(other.activity)), buffer(std::move(other.buffer))
    , completionState(std::move(other.completionState)) { }
LayerTreeHost::FrameBufferLease& LayerTreeHost::FrameBufferLease::operator=(FrameBufferLease&& other) noexcept
{
    if (this == &other)
        return *this;
    content::PaintActivityScope retiredActivity(std::move(activity));
    buffer.reset();
    if (completionState)
        LayerTreeHost::notifyRasterCompletion(completionState);
    buffer = std::move(other.buffer);
    completionState = std::move(other.completionState);
    activity = std::move(other.activity);
    return *this; // retiredActivity releases last, with no later host/platform access.
}
SkCanvas* LayerTreeHost::FrameBufferLease::canvas() const
{
    return buffer ? buffer->canvas.get() : nullptr;
}
LayerTreeHost::FrameBufferLease LayerTreeHost::acquireFrameBufferLeaseLocked()
{
    if (m_isDestroying || m_hasResize || !m_memoryCanvas)
        return FrameBufferLease();
    FrameBufferLease lease(m_frameBuffers[m_currentFrameBufferSlot], m_uiPaintRetry, m_paintLifetime);
    if (lease)
        trace::add(trace::counter::buffer_leases);
    return lease;
}
LayerTreeHost::FrameBufferLease LayerTreeHost::acquireFrameBufferLease()
{
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    return acquireFrameBufferLeaseLocked();
}
bool LayerTreeHost::independentPresentationEnabled()
{
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    return m_independentPresentationEnabled;
}
bool LayerTreeHost::prepareWritableFrameBufferLocked()
{
    uint64_t required = 0;
    if (m_hasResize && m_frameBufferGeneration == (std::numeric_limits<uint64_t>::max)())
        return false;
    if (!FrameBufferLeasePolicy::bytesForSize(m_clientRect.width(),
            m_clientRect.height(), &required))
        return false;
    const int writable = FrameBufferLeasePolicy::writableSlot(m_currentFrameBufferSlot,
        m_frameBuffers[0].use_count(), m_frameBuffers[1].use_count());
    if (writable < 0)
        return false;
    std::shared_ptr<FrameBuffer>& next = m_frameBuffers[writable];
    const bool valid = next && next->generation == m_frameBufferGeneration
        && next->width == m_clientRect.width() && next->height == m_clientRect.height()
        && next->alpha == m_hasTransparentBackground;
    if (!valid) {
        // Free only an unleased slot. A retained reader always pins its own
        // old generation; at most two slots and 256MiB are retained by the pool.
        next.reset();
        if (static_cast<size_t>(writable) == m_currentFrameBufferSlot)
            m_memoryCanvas = nullptr;
        uint64_t retained = 0;
        for (size_t index = 0; index < FrameBufferLeasePolicy::maximumSlots; ++index) {
            if (m_frameBuffers[index])
                retained += m_frameBuffers[index]->bytes;
        }
        if (required > FrameBufferLeasePolicy::maximumBytes - retained) {
            for (size_t index = 0; index < FrameBufferLeasePolicy::maximumSlots; ++index) {
                if (index != static_cast<size_t>(writable) && m_frameBuffers[index]
                    && m_frameBuffers[index].use_count() == 1) {
                    retained -= m_frameBuffers[index]->bytes;
                    m_frameBuffers[index].reset();
                }
            }
            if (required > FrameBufferLeasePolicy::maximumBytes - retained)
                return false;
        }
        try {
            std::unique_ptr<SkCanvas> canvas(skia::CreatePlatformCanvas(
                m_clientRect.width(), m_clientRect.height(), !m_hasTransparentBackground));
            if (!canvas)
                return false;
            clearCanvas(canvas.get(), m_clientRect, m_hasTransparentBackground);
            next = std::make_shared<FrameBuffer>(std::move(canvas), m_frameBufferGeneration,
                m_clientRect.width(), m_clientRect.height(), m_hasTransparentBackground);
        } catch (const std::bad_alloc&) {
            return false;
        }
    }
    if (static_cast<size_t>(writable) != m_currentFrameBufferSlot) {
        const std::shared_ptr<FrameBuffer>& previous = m_frameBuffers[m_currentFrameBufferSlot];
        if (previous && previous->generation == m_frameBufferGeneration
            && previous->alpha == m_hasTransparentBackground) {
            SkBaseDevice* device = skia::GetTopDevice(*previous->canvas);
            if (device) {
                const SkBitmap& bitmap = device->accessBitmap(false);
                if (m_hasTransparentBackground && valid)
                    clearCanvas(next->canvas.get(), m_clientRect, true);
                next->canvas->drawBitmap(bitmap, 0, 0, nullptr);
                trace::add(trace::counter::copy_operations);
                trace::add(trace::counter::copy_bytes, next->bytes);
            }
        }
    }
    m_currentFrameBufferSlot = static_cast<size_t>(writable);
    m_memoryCanvas = next->canvas.get();
    m_hasResize = false;
    return true;
}

SkCanvas* LayerTreeHost::getMemoryCanvasLocked()
{
    m_compositeMutex.lock();
    return m_memoryCanvas;
}

void LayerTreeHost::releaseMemoryCanvasLocked()
{
    m_compositeMutex.unlock();
}

bool LayerTreeHost::isDrawDirty()
{
    bool isDrawDirty;
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    isDrawDirty = m_isDrawDirty;
    return isDrawDirty;
}

void LayerTreeHost::disablePaint()
{
    m_hostClient->disablePaint();
}

void LayerTreeHost::enablePaint()
{
    m_hostClient->enablePaint();
}

LayerTreeHost::BitInfo* LayerTreeHost::getBitBegin()
{
    FrameBufferLease lease = acquireFrameBufferLease();
    if (!lease)
        return nullptr;
    SkBaseDevice* device = skia::GetTopDevice(*lease.canvas());
    if (!device)
        return nullptr;
    const SkBitmap& bitmap = device->accessBitmap(false);
    BitInfo* info = new (std::nothrow) BitInfo();
    if (!info)
        return nullptr;
    info->pixels = bitmap.getAddr32(0, 0);
    info->width = lease.buffer->width;
    info->height = lease.buffer->height;
    info->lease = std::move(lease);
    {
        WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
        if (m_frameBuffers[m_currentFrameBufferSlot] == info->lease.buffer)
            m_isDrawDirty = false;
    }
    return info;
}

void LayerTreeHost::getBitEnd(const BitInfo* bitInfo)
{
    // No host dereference: a client may close/resize while retaining pixels.
    delete bitInfo;
}

void LayerTreeHost::paintToBit(void* bits, int pitch)
{
    if (!bits)
        return;
    BitInfo* info = getBitBegin();
    if (!info)
        return;
    const int rowBytes = info->width * 4;
    if (pitch == 0 || pitch == rowBytes) {
        memcpy(bits, info->pixels, static_cast<size_t>(rowBytes) * info->height);
    } else {
        const unsigned char* source = reinterpret_cast<const unsigned char*>(info->pixels);
        unsigned char* destination = static_cast<unsigned char*>(bits);
        for (int row = 0; row < info->height; ++row) {
            memcpy(destination, source, rowBytes);
            source += rowBytes;
            destination += pitch;
        }
    }
    getBitEnd(info);
}

} // cc
