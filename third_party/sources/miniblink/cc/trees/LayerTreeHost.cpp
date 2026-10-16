#include <atomic>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

#include <algorithm>
#include "cc/trees/LayerTreeHost.h"
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
        double inputTime, bool useSmoothMotion)
        : node(targetNode)
        , inputSequence(sequence)
        , delta(inputDelta)
        , monotonicTime(inputTime)
        , smooth(useSmoothMotion)
    {
    }

    ScrollNodeKey node;
    uint64_t inputSequence;
    ScrollVector delta;
    double monotonicTime;
    bool smooth;
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

static void appendCompositorScrollTrace(const char* reason,
    const ScrollTreeSnapshot* tree, int x, int y, double deltaX,
    double deltaY, bool hasPendingTree, size_t pendingInputCount)
{
    char path[MAX_PATH] = {};
    const DWORD pathLength = GetEnvironmentVariableA(
        "SOUI_INTERACTION_TRACE", path, sizeof(path));
    if (!pathLength || pathLength >= sizeof(path))
        return;

    const ScrollNodeSnapshot* hitNode = nullptr;
    bool hitRegion = false;
    if (tree) {
        const std::vector<ScrollHitTestRegion>& regions =
            tree->hitTestRegions();
        for (size_t index = regions.size(); index > 0; --index) {
            const ScrollHitTestRegion& region = regions[index - 1];
            if (!region.bounds.contains(x, y))
                continue;
            hitRegion = true;
            hitNode = tree->findNode(region.target);
            break;
        }
    }

    char line[1024] = {};
    const int length = std::snprintf(line, sizeof(line),
        "reason=%s x=%d y=%d dx=%.3f dy=%.3f tree=%u pending_tree=%u pending_inputs=%u hit_region=%u hit_node=%u eligible=%u coverage=%u reasons=%u axes=%u scene=%llu min=(%.1f,%.1f) max=(%.1f,%.1f) coverage_min=(%.1f,%.1f) coverage_max=(%.1f,%.1f) prediction=(%.1f,%.1f)\r\n",
        reason, x, y, deltaX, deltaY, tree ? 1U : 0U,
        hasPendingTree ? 1U : 0U,
        static_cast<unsigned>(pendingInputCount), hitRegion ? 1U : 0U,
        hitNode ? 1U : 0U,
        hitNode && hitNode->compositorEligible() ? 1U : 0U,
        hitNode && hitNode->coverageComplete ? 1U : 0U,
        hitNode ? hitNode->mainThreadReasons : 0U,
        hitNode ? hitNode->inputAxes : 0U,
        hitNode ? static_cast<unsigned long long>(hitNode->sceneVersion) : 0ULL,
        hitNode ? hitNode->minimumOffset.x : 0.0,
        hitNode ? hitNode->minimumOffset.y : 0.0,
        hitNode ? hitNode->maximumOffset.x : 0.0,
        hitNode ? hitNode->maximumOffset.y : 0.0,
        hitNode ? hitNode->coverageMinimum.x : 0.0,
        hitNode ? hitNode->coverageMinimum.y : 0.0,
        hitNode ? hitNode->coverageMaximum.x : 0.0,
        hitNode ? hitNode->coverageMaximum.y : 0.0,
        hitNode ? hitNode->inputPrediction.x : 0.0,
        hitNode ? hitNode->inputPrediction.y : 0.0);
    if (length <= 0)
        return;

    HANDLE file = CreateFileA(path, FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(file, line,
        static_cast<DWORD>(std::min(length,
            static_cast<int>(sizeof(line) - 1))), &written, nullptr);
    if (tree) {
        const std::vector<ScrollNodeSnapshot>& nodes = tree->nodes();
        for (size_t index = 0; index < nodes.size(); ++index) {
            const ScrollNodeSnapshot& node = nodes[index];
            if (!node.viewportClip.contains(x, y))
                continue;
            const int nodeLength = std::snprintf(line, sizeof(line),
                "  node[%u] id=%llu parent=%llu eligible=%u coverage=%u reasons=%u axes=%u scene=%llu clip=(%.1f,%.1f,%.1f,%.1f) min=(%.1f,%.1f) max=(%.1f,%.1f)\r\n",
                static_cast<unsigned>(index),
                static_cast<unsigned long long>(node.key.nodeId),
                static_cast<unsigned long long>(node.parent.nodeId),
                node.compositorEligible() ? 1U : 0U,
                node.coverageComplete ? 1U : 0U,
                node.mainThreadReasons, node.inputAxes,
                static_cast<unsigned long long>(node.sceneVersion),
                node.viewportClip.x, node.viewportClip.y,
                node.viewportClip.width, node.viewportClip.height,
                node.minimumOffset.x, node.minimumOffset.y,
                node.maximumOffset.x, node.maximumOffset.y);
            if (nodeLength > 0) {
                WriteFile(file, line,
                    static_cast<DWORD>(std::min(nodeLength,
                        static_cast<int>(sizeof(line) - 1))), &written,
                    nullptr);
            }
        }
    }
    CloseHandle(file);
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
    m_paintToMemoryCanvasInUiThreadTaskCount = 0;
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
    m_lastRecordTime = 0.0;
    m_drawFrameCount = 0;
    m_drawFrameFinishCount = 0;
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

    while (0 != RasterTaskWorkerThreadPool::shared()->getPendingRasterTaskNum()) { ::Sleep(20); }
    requestApplyActionsToRunIntoCompositeThread(true);

    m_compositeMutex.lock();
    for (auto it : m_wrapSelfForUiThreads) {
        it->m_host = nullptr;
    }
    m_wrapSelfForUiThreads.clear();
    for (auto it : m_animationFinishedTasks)
        it->host = nullptr;
    m_animationFinishedTasks.clear();
    for (WTF::HashMap<int, CompositorScrollUpdateTask*>::iterator it =
        m_scrollUpdateTasks.begin(); it != m_scrollUpdateTasks.end(); ++it) {
        it->value->host = nullptr;
    }
    m_scrollUpdateTasks.clear();
    m_compositeMutex.unlock();

    //while (0 != m_paintToMemoryCanvasInUiThreadTaskCount) { ::Sleep(20); }
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

    if (m_memoryCanvas)
        delete m_memoryCanvas;
    m_memoryCanvas = nullptr;
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
        for (size_t i = 0; i < m_pendingCompositorScrollInputs.size(); ++i)
            delete m_pendingCompositorScrollInputs[i];
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
        finishCount = m_drawFrameFinishCount;
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
    m_hostClient->onLayerTreeInvalidateRect(blink::WebRect(repaintRect));
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

bool LayerTreeHost::canRecordActions() const
{
    if (hasInFlightScene())
        return false;
    if (!m_actionsFrameGroup->containComefromMainframeLocked())
        return true;

    if (RasterTaskWorkerThreadPool::shared()->getPendingRasterTaskNum() > 10)
        return false;

    if (!m_actionsFrameGroup || m_actionsFrameGroup->getFramesSize() > 30)
        return false;
    
    double lastRecordTime = WTF::monotonicallyIncreasingTime();
    double detTime = lastRecordTime - m_lastRecordTime;
    if (detTime < m_drawMinInterval)
        return false;
    m_lastRecordTime = lastRecordTime;

    return true;
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
}

void LayerTreeHost::appendLayerChangeAction(LayerChangeAction* action)
{
    m_actionsFrameGroup->saveLayerChangeAction(action);

    setLayerTreeDirty();
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

void LayerTreeHost::recordDraw()
{
    if (blink::RuntimeEnabledFeatures::headlessEnabled())
        return;
    if (!m_rootLayer)
        return;

    ScrollTreeSnapshot scrollTree;
    updateLayersDrawProperties(&scrollTree);
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
    submitSceneTransaction(scrollTree,
        hasActions ? actionBefore + 1 : 0,
        hasActions ? actionAfter : 0,
        noRasterCommit || !hasActions);
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
    ScrollTreeSnapshot* scrollTree)
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
    ScrollTreeSnapshot* scrollTree)
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
            host, child, sceneVersion, scrollTree);
        if (childScrollTarget.isValid())
            scrollTarget = childScrollTarget;
        else
            appendLayerHitTestRegion(child, scrollTarget, scrollTree);

        prop.subtreeCanUseLcdText = subtreeCanUseLcdText;
        updateChildLayersDrawProperties(child, host, layerSorter, prop,
            deep + 1, sceneVersion, scrollTarget, scrollTree);
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

void LayerTreeHost::updateLayersDrawProperties(ScrollTreeSnapshot* output)
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
        m_nextScrollSceneVersion, ScrollNodeKey(), &scrollTree);
    attachCompositorScrollbarSnapshots(m_rootLayer, this, &scrollTree);
    if (output)
        *output = scrollTree;
}

bool LayerTreeHost::hasInFlightScene() const
{
    WTF::Locker<WTF::Mutex> locker(m_sceneTransactionMutex);
    return m_sceneCoordinator.hasPending() || m_sceneCoordinator.hasReady();
}

bool LayerTreeHost::submitSceneTransaction(const ScrollTreeSnapshot& scrollTree,
    int64 firstActionId, int64 lastActionId, bool resourcesReady)
{
    bool accepted = false;
    {
        WTF::Locker<WTF::Mutex> locker(m_sceneTransactionMutex);
        SceneTransaction transaction;
        transaction.baseVersion = m_sceneCoordinator.activeVersion();
        transaction.newVersion = scrollTree.sceneVersion();
        transaction.firstActionId = firstActionId;
        transaction.lastActionId = lastActionId;
        transaction.resourcesReady = resourcesReady;
        transaction.scrollTree = scrollTree;
        accepted = m_sceneCoordinator.submit(transaction);
    }
    if (accepted && resourcesReady)
        requestDrawFrameToRunIntoCompositeThread();
    return accepted;
}

void LayerTreeHost::onActionFrameApplied(int64 throughActionId)
{
    bool ready = false;
    {
        WTF::Locker<WTF::Mutex> locker(m_sceneTransactionMutex);
        ready = m_sceneCoordinator.markResourcesReadyThroughAction(
            throughActionId);
    }
    if (ready)
        requestDrawFrameToRunIntoCompositeThread();
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
        SkScalar bestDelta = std::numeric_limits<SkScalar>::max();
        size_t mergeA = 0;
        size_t mergeB = 1;
        for (size_t i = 0; i + 1 < dirtySize; ++i) {
            for (size_t j = i + 1; j < dirtySize; ++j) {
                const SkScalar delta = unionArea(dirtyRects->at(i), dirtyRects->at(j)) -
                    rectArea(dirtyRects->at(i)) - rectArea(dirtyRects->at(j));
                if (delta < bestDelta) {
                    bestDelta = delta;
                    mergeA = i;
                    mergeB = j;
                }
            }
        }
        dirtyRects->at(mergeA).join(dirtyRects->at(mergeB));
        for (size_t i = mergeB + 1; i < dirtySize; ++i)
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
    m_deviceScaleFactor = scale;
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
    m_visible = visible;
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
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    if (!m_compositeThread) {
        RELEASE_ASSERT(!m_uiThreadClient);
        return;
    }
    if (0 != m_drawFrameCount)
        return;

    atomicIncrement(&m_drawFrameCount);
    atomicIncrement(&m_drawFrameFinishCount);
    m_compositeThread->platformThread().postTask(FROM_HERE, WTF::bind(&LayerTreeHost::drawFrameInCompositeThread, this));
}

void LayerTreeHost::requestDrawFrameAfterDelay(long long delayMs)
{
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    if (!m_compositeThread || m_isDestroying)
        return;
    if (0 != m_drawFrameCount)
        return;

    atomicIncrement(&m_drawFrameCount);
    atomicIncrement(&m_drawFrameFinishCount);
    m_compositeThread->platformThread().postDelayedTask(FROM_HERE,
        WTF::bind(&LayerTreeHost::drawFrameInCompositeThread, this),
        std::max<long long>(1, delayMs));
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
    CompositorScrollReceipt* receipt)
{
    if (!featureEnabledByDefault("SOUI_COMPOSITOR_SCROLL")
        || m_isDestroying || !std::isfinite(deltaX) || !std::isfinite(deltaY)
        || !std::isfinite(monotonicTime)) {
        appendCompositorScrollTrace("disabled-or-invalid", nullptr, x, y,
            deltaX, deltaY, false, 0);
        return false;
    }

    bool accepted = false;
    {
        WTF::Locker<WTF::Mutex> locker(m_compositorScrollInputMutex);
        if (!m_activeInputScrollTree || m_pendingCompositorScrollTree
            || m_pendingCompositorScrollInputs.size() >= 512) {
            appendCompositorScrollTrace("scene-unavailable",
                m_activeInputScrollTree, x, y, deltaX, deltaY,
                m_pendingCompositorScrollTree != nullptr,
                m_pendingCompositorScrollInputs.size());
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
            appendCompositorScrollTrace("no-eligible-hit",
                m_activeInputScrollTree, x, y, deltaX, deltaY, false,
                m_pendingCompositorScrollInputs.size());
            return false;
        }
        if (!target->advanceInputPrediction(ScrollVector(deltaX, deltaY))) {
            appendCompositorScrollTrace("delta-not-consumed",
                m_activeInputScrollTree, x, y, deltaX, deltaY, false,
                m_pendingCompositorScrollInputs.size());
            return false;
        }

        ++m_nextCompositorInputSequence;
        if (!m_nextCompositorInputSequence)
            ++m_nextCompositorInputSequence;
        m_pendingCompositorScrollInputs.append(new CompositorScrollInput(
            target->key, m_nextCompositorInputSequence,
            ScrollVector(deltaX, deltaY), monotonicTime, smooth));
        if (receipt) {
            receipt->sceneVersion = target->sceneVersion;
            receipt->inputSequence = m_nextCompositorInputSequence;
            receipt->offsetX = target->inputPrediction.x;
            receipt->offsetY = target->inputPrediction.y;
        }
        m_latchedScrollNode = target->key;
        m_lastCompositorWheelTime = monotonicTime;
        accepted = true;
        appendCompositorScrollTrace("accepted", m_activeInputScrollTree,
            x, y, deltaX, deltaY, false,
            m_pendingCompositorScrollInputs.size());
    }

    if (accepted)
        requestDrawFrameToRunIntoCompositeThread();
    return accepted;
}

bool LayerTreeHost::captureCompositorScrollBarrier(int x, int y,
    ScrollBarrier* barrier)
{
    if (!barrier || m_isDestroying) {
        appendCompositorScrollTrace("barrier-capture-invalid", nullptr,
            x, y, 0.0, 0.0, false, 0);
        return false;
    }

    ScrollNodeKey targetKey;
    uint64_t targetSceneVersion = 0;
    uint64_t targetScrollEpoch = 0;
    {
        WTF::Locker<WTF::Mutex> locker(m_compositorScrollInputMutex);
        if (!m_activeInputScrollTree) {
            appendCompositorScrollTrace("barrier-capture-no-tree", nullptr,
                x, y, 0.0, 0.0, false, 0);
            return false;
        }
        const ScrollNodeSnapshot* target =
            m_activeInputScrollTree->scrollTargetAt(x, y);
        if (!target || !target->compositorEligible()) {
            appendCompositorScrollTrace("barrier-capture-no-target",
                m_activeInputScrollTree, x, y, 0.0, 0.0, false, 0);
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
        appendCompositorScrollTrace("barrier-capture-no-state", nullptr,
            x, y, 0.0, 0.0, false, 0);
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
    appendCompositorScrollTrace(barrier->valid
            ? "barrier-capture-accepted" : "barrier-capture-rejected",
        nullptr, x, y, barrier->visualOffset.x,
        barrier->visualOffset.y, false,
        static_cast<size_t>(barrier->sampleSequence));
    return barrier->valid;
}

bool LayerTreeHost::synchronizeCompositorScrollBarrier(
    const ScrollBarrier& barrier)
{
    if (m_isDestroying || !barrier.valid) {
        appendCompositorScrollTrace("barrier-sync-invalid", nullptr,
            0, 0, barrier.visualOffset.x, barrier.visualOffset.y,
            false, static_cast<size_t>(barrier.sampleSequence));
        return false;
    }

    if (barrier.key.nodeId > static_cast<uint64_t>(INT_MAX)) {
        appendCompositorScrollTrace("barrier-sync-invalid-layer-id", nullptr,
            0, 0, barrier.visualOffset.x, barrier.visualOffset.y,
            false, static_cast<size_t>(barrier.sampleSequence));
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
            appendCompositorScrollTrace("barrier-sync-future-scene",
                m_activeInputScrollTree, 0, 0, barrier.visualOffset.x,
                barrier.visualOffset.y, false,
                static_cast<size_t>(barrier.sampleSequence));
            return false;
        }
        activeInputScene = m_activeInputScrollTree->sceneVersion();
        ScrollNodeSnapshot* active =
            m_activeInputScrollTree->findMutableNode(barrier.key);
        if (active && active->sceneVersion < barrier.sceneVersion) {
            appendCompositorScrollTrace("barrier-sync-tree-mismatch",
                m_activeInputScrollTree, 0, 0, barrier.visualOffset.x,
                barrier.visualOffset.y, false,
                static_cast<size_t>(barrier.sampleSequence));
            return false;
        }
        requiresRetiredStateProof = (!active
                && activeInputScene >= barrier.sceneVersion)
            || (active && active->scrollEpoch != barrier.scrollEpoch);
        if (!ownerMatches && !requiresRetiredStateProof) {
            appendCompositorScrollTrace("barrier-sync-owner-mismatch",
                m_activeInputScrollTree, layerId, 0,
                barrier.visualOffset.x, barrier.visualOffset.y, false,
                static_cast<size_t>(barrier.sampleSequence));
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
                delete input;
                m_pendingCompositorScrollInputs.remove(i);
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
            appendCompositorScrollTrace("barrier-sync-owner-rejected", nullptr,
                layerId, 0, barrier.visualOffset.x, barrier.visualOffset.y,
                false, static_cast<size_t>(barrier.sampleSequence));
            return false;
        }
    } else if (!retiredStateProvesReconciliation) {
        appendCompositorScrollTrace("barrier-sync-owner-rejected", nullptr,
            layerId, 0, barrier.visualOffset.x, barrier.visualOffset.y,
            false, static_cast<size_t>(barrier.sampleSequence));
        return false;
    }
    appendCompositorScrollTrace(retiredStateProvesReconciliation
            ? "barrier-sync-retired-accepted"
            : (synchronizedController ? "barrier-sync-accepted"
                                      : "barrier-sync-forward-accepted"),
        nullptr,
        layerId, 0, barrier.visualOffset.x, barrier.visualOffset.y,
        false, static_cast<size_t>(barrier.sampleSequence));
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
            activatedSceneTree = new ScrollTreeSnapshot(
                m_sceneCoordinator.activeScene().scrollTree);
        }
    }

    ScrollTreeSnapshot* pendingTree = nullptr;
    WTF::Vector<CompositorScrollInput*> inputs;
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
        if (state) {
            state->controller.enqueueDelta(input->inputSequence,
                input->delta, input->monotonicTime, input->smooth);
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
                postCompositorScrollUpdateToMainThread(state->layerId,
                    sample, visual);
            }
        }
        needsAnotherFrame |= state->controller.needsAnimationFrame();
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
        const blink::WebDoublePoint acknowledgedTotal =
            layer->acknowledgedCompositorTotal();
        const blink::WebDoublePoint position = layer->scrollPositionDouble();
        const double epsilon = 0.01;
        return std::fabs(acknowledgedTotal.x - sample.consumedTotal.x)
                <= epsilon
            && std::fabs(acknowledgedTotal.y - sample.consumedTotal.y)
                <= epsilon
            && std::fabs(position.x - visualOffset.x) <= epsilon
            && std::fabs(position.y - visualOffset.y) <= epsilon;
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
    m_drawMinInterval = drawMinInterval;
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
    m_compositeMutex.lock();
    if (!m_memoryCanvas || m_clientRect.isEmpty()) {
        m_compositeMutex.unlock();
        return;
    }
    m_compositeMutex.unlock();

#if 0
    HPEN hpen = CreatePen(PS_SOLID, 10, RGB(11, 22, 33));
    HBRUSH hbrush = CreateSolidBrush(RGB(0xf3, 22, 33));

    SelectObject(hdc, hpen);
    SelectObject(hdc, hbrush);

    Rectangle(hdc, paintRect->left, paintRect->top, paintRect->right, paintRect->bottom);

    DeleteObject(hpen);
    DeleteObject(hbrush);
#endif

    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);

    if (!m_hasTransparentBackground)
        skia::DrawToNativeContext(m_memoryCanvas, hdc, paintRect->left, paintRect->top, paintRect);
    else {
        RECT rc = blink::intRectToWinRect(m_clientRect);
        skia::DrawToNativeLayeredContext(m_memoryCanvas, hdc, paintRect, &rc);
    }
}

void LayerTreeHost::drawFrameInCompositeThread()
{
    m_compositeMutex.lock();
    if (1 != m_drawFrameCount)
        DebugBreak();
    atomicDecrement(&m_drawFrameCount);
    m_compositeMutex.unlock();

    double lastCompositeTime = WTF::monotonicallyIncreasingTime();
    double detTime = lastCompositeTime - m_lastCompositeTime;
    if (detTime < m_drawMinInterval && !m_isDestroying) { // 如果刷新频率太快，缓缓再画
        const long long remainingDelayMs = static_cast<long long>(
            std::ceil((m_drawMinInterval - detTime) * 1000.0));
        requestDrawFrameAfterDelay(remainingDelayMs);
        atomicDecrement(&m_drawFrameFinishCount);
        return;
    }
    m_lastCompositeTime = lastCompositeTime;

#if 0
    LARGE_INTEGER performanceCount = { 0 };
    QueryPerformanceCounter(&performanceCount);
    static DWORD gLastCount = 0;
    WTF::String outstr = String::format("LayerTreeHost::drawFrameInCompositeThread:[%d]\n", performanceCount.LowPart - gLastCount);
    OutputDebugStringA(outstr.utf8().data());
    gLastCount = performanceCount.LowPart;
#endif

    bool frameReady = preDrawFrame(); // 这里也会发起Commit

    Vector<SkRect> animationDirtyRects;
    const bool animationNeedsAnotherFrame = tickCompositorAnimations(
        lastCompositeTime, &animationDirtyRects);
    const bool scrollNeedsAnotherFrame = tickCompositorScroll(
        lastCompositeTime, &animationDirtyRects);

    m_compositeMutex.lock();
    Vector<SkRect> dirtyRects = m_dirtyRectsForComposite;
    m_dirtyRectsForComposite.clear();
    m_compositeMutex.unlock();

    for (size_t i = 0; i < animationDirtyRects.size(); ++i)
        addAndMergeDirty(&dirtyRects, animationDirtyRects[i]);

    for (size_t i = 0; i < dirtyRects.size() && !m_isDestroying; ++i) {
        const SkRect& r = dirtyRects[i];
        paintToMemoryCanvasInCompositeThread(r);
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

    postDrawFrame();
    if ((animationNeedsAnotherFrame || scrollNeedsAnotherFrame)
        && !m_isDestroying) {
        requestDrawFrameToRunIntoCompositeThread();
    } else if (!frameReady && !m_isDestroying) {
        // An incomplete transaction is waiting for producer work; do not turn
        // that wait into a zero-delay compositor spin loop.
        requestDrawFrameAfterDelay(8);
    }
    atomicDecrement(&m_drawFrameFinishCount);
}

void LayerTreeHost::paintToMemoryCanvasInUiThread(const SkRect& paintRect)
{
    if (!m_uiThreadClient)
        return;

//     DWORD nowTime = (DWORD)(WTF::currentTimeMS() * 100);
//     DWORD detTime = nowTime - g_nowTime;
//     InterlockedExchange((LONG*)&g_nowTime, nowTime);

//     String out = String::format("LayerTreeHost.paintToMemoryCanvasInUiThread: %d\n", detTime);
//     OutputDebugStringA(out.utf8().data());

    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    blink::IntRect intPaintRect(paintRect.x(), paintRect.y(), paintRect.width(), paintRect.height());
    m_uiThreadClient->paintToMemoryCanvasInUiThread(m_memoryCanvas, intPaintRect);
}

void LayerTreeHost::WrapSelfForUiThread::endPaint() {
    LayerTreeHost* host = m_host;
    WrapSelfForUiThread* next = nullptr;
    int* taskCount = nullptr;
    if (host) {
        WTF::Locker<WTF::Mutex> locker(host->m_compositeMutex);
        host->m_wrapSelfForUiThreads.remove(this);
        taskCount = &host->m_paintToMemoryCanvasInUiThreadTaskCount;
        // Keep one coalesced follow-up for dirty regions that arrived while
        // this task was painting. Do not build an unbounded UI task queue.
        if (!host->m_isDestroying && !host->m_dirtyRectsForUi.isEmpty()) {
            next = new WrapSelfForUiThread(host);
            host->m_wrapSelfForUiThreads.add(next);
            atomicIncrement(taskCount);
        }
    }

    delete this;

    if (taskCount)
        atomicDecrement(taskCount);
    if (next)
        Platform::current()->mainThread()->postTask(FROM_HERE,
            WTF::bind(&LayerTreeHost::WrapSelfForUiThread::paintInUiThread, next));
}

void LayerTreeHost::WrapSelfForUiThread::paintInUiThread()
{
    if (!m_host) {
        delete this;
        return;
    }

    double lastPaintTime = WTF::monotonicallyIncreasingTime();
    double detTime = lastPaintTime - m_host->m_lastPaintTime;
    if (detTime < m_host->m_drawMinInterval) {
        m_host->requestPaintToMemoryCanvasToUiThread(IntRect());
        endPaint();
        return;
    }
    m_host->m_lastPaintTime = lastPaintTime;

    m_host->m_compositeMutex.lock();
    Vector<SkRect> dirtyRectsForUi = m_host->m_dirtyRectsForUi;
    m_host->m_dirtyRectsForUi.clear();
    m_host->m_compositeMutex.unlock();

    for (size_t i = 0; i < dirtyRectsForUi.size(); ++i) {
        m_host->paintToMemoryCanvasInUiThread(dirtyRectsForUi[i]);
    }

    endPaint();
}

LayerTreeHost::WrapSelfForUiThread::~WrapSelfForUiThread()
{
    
}

void LayerTreeHost::WrapSelfForUiThread::willProcessTask()
{
    Platform::current()->mainThread()->removeTaskObserver(this);
    paintInUiThread();
}

void LayerTreeHost::WrapSelfForUiThread::didProcessTask()
{
    Platform::current()->mainThread()->removeTaskObserver(this);
    paintInUiThread();
}

void LayerTreeHost::requestPaintToMemoryCanvasToUiThread(const SkRect& r)
{
//     char* output = (char*)malloc(0x100);
//     sprintf(output, "LayerTreeHost::requestPaintToMemoryCanvasToUiThread: %f %f\n", r.x(), r.y());
//     OutputDebugStringA(output);
//     free(output);

    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);

    if (r.isEmpty() && m_paintToMemoryCanvasInUiThreadTaskCount > 1)
        return;

    SkRect dirtyRect(r);
    SkRect clientRect = SkRect::MakeXYWH(m_clientRect.x(), m_clientRect.y(), m_clientRect.width(), m_clientRect.height());
    if (!dirtyRect.intersect(clientRect))
        return;

    addAndMergeDirty(&m_dirtyRectsForUi, dirtyRect);

    // A single task drains the merged dirty list. Additional requests while
    // it is queued or painting are merged and scheduled by endPaint().
    if (m_paintToMemoryCanvasInUiThreadTaskCount != 0)
        return;

    WrapSelfForUiThread* wrap = new WrapSelfForUiThread(this);
    m_wrapSelfForUiThreads.add(wrap);
    atomicIncrement(&m_paintToMemoryCanvasInUiThreadTaskCount);
    Platform::current()->mainThread()->postTask(FROM_HERE, WTF::bind(&LayerTreeHost::WrapSelfForUiThread::paintInUiThread, wrap)); //addTaskObserver(wrap);
}

void LayerTreeHost::paintToMemoryCanvasInCompositeThread(const SkRect& r)
{
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    SkRect paintRect = r;
#if defined(SOUI_GPU_COMPOSITOR)
    bool resized = m_hasResize;
#endif

    if ((!m_memoryCanvas || m_hasResize) && !m_clientRect.isEmpty()) {
        m_hasResize = false;
        paintRect = m_clientRect;

        if (m_memoryCanvas)
            delete m_memoryCanvas;
        m_memoryCanvas = skia::CreatePlatformCanvas(m_clientRect.width(), m_clientRect.height(), !m_hasTransparentBackground);
        clearCanvas(m_memoryCanvas, m_clientRect, m_hasTransparentBackground);
    }

    SkRect clientRect = SkRect::MakeXYWH(m_clientRect.x(), m_clientRect.y(), m_clientRect.width(), m_clientRect.height());
    if (!paintRect.intersect(clientRect))
        return;

    if (!m_memoryCanvas) {
        ASSERT(false);
        return;
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
                return;
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
                return;
            }
            SkIRect readRect = SkIRect::MakeXYWH(static_cast<int>(paintRect.x()),
                                                 static_cast<int>(paintRect.y()),
                                                 static_cast<int>(paintRect.width()),
                                                 static_cast<int>(paintRect.height()));
            if (m_gpuSurface->readback(m_memoryCanvas, readRect)) {
                m_gpuSurface->recordPresented();
                if (!needNotifUi)
                    return;
                requestPaintToMemoryCanvasToUiThread(paintRect);
                return;
            }
        }
    }
#endif
    if (m_hasTransparentBackground)
        clearCanvas(m_memoryCanvas, paintRect, m_hasTransparentBackground);

    bool needNotifUi = drawToCanvas(m_memoryCanvas, paintRect); // 绘制脏矩形
#if defined(ENABLE_WKE) && ENABLE_WKE == 1
    wke::diagnostics::renderer_draw_completed(WKE_RENDER_DIAGNOSTICS_DRAW_BITMAP_RASTER);
#endif

    if (!needNotifUi)
        return;

    if (m_independentPresentationEnabled) {
        blink::IntRect intPaintRect(paintRect.x(), paintRect.y(),
            paintRect.width(), paintRect.height());
        m_uiThreadClient->paintToMemoryCanvasInUiThread(
            m_memoryCanvas, intPaintRect);
        return;
    }

#if ENABLE_WKE == 1
    if (blink::RuntimeEnabledFeatures::updataInOtherThreadEnabled()) {
        blink::IntRect intPaintRect(paintRect.x(), paintRect.y(), paintRect.width(), paintRect.height());
        m_uiThreadClient->paintToMemoryCanvasInUiThread(m_memoryCanvas, intPaintRect);
        return;
    }
#endif

    requestPaintToMemoryCanvasToUiThread(paintRect);
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
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    if (!m_memoryCanvas)
        return nullptr;

    int width = m_clientRect.width();
    int height = m_clientRect.height();

    DWORD cBytes = width * height * 4;
    SkBaseDevice* device = (SkBaseDevice*)m_memoryCanvas->getTopDevice();
    if (!device)
        return nullptr;

    const SkBitmap& bitmap = device->accessBitmap(false);
    SkCanvas* tempCanvas = nullptr;
    uint32_t* pixels = nullptr;

    if (bitmap.info().width() != width || bitmap.info().height() != height) {
        tempCanvas = skia::CreatePlatformCanvas(width, height, !m_hasTransparentBackground);
        clearCanvas(tempCanvas, m_clientRect, m_hasTransparentBackground);
        tempCanvas->drawBitmap(bitmap, 0, 0, nullptr);
        device = (SkBaseDevice*)tempCanvas->getTopDevice();
        if (!device) {
            delete tempCanvas;
            return nullptr;
        }
        const SkBitmap& tempBitmap = device->accessBitmap(false);
        pixels = tempBitmap.getAddr32(0, 0);
    } else
        pixels = bitmap.getAddr32(0, 0);

    m_compositeMutex.lock();

    BitInfo* bitInfo = new BitInfo();
    bitInfo->pixels = pixels;
    bitInfo->tempCanvas = tempCanvas;
    bitInfo->width = width;
    bitInfo->height = height;
    return bitInfo;
}

void LayerTreeHost::getBitEnd(const BitInfo* bitInfo)
{
    m_isDrawDirty = false;

    if (bitInfo->tempCanvas) {
        delete m_memoryCanvas;
        m_memoryCanvas = bitInfo->tempCanvas;
    }
    m_compositeMutex.unlock();

    delete bitInfo;
}

void LayerTreeHost::paintToBit(void* bits, int pitch)
{
#if 1
    BitInfo* bitInfo = getBitBegin();
    if (!bitInfo)
        return;
    uint32_t* pixels = bitInfo->pixels;;
    int width = bitInfo->width;
    int height = bitInfo->height;

    if (pitch == 0 || pitch == width * 4) {
        memcpy(bits, pixels, width * height * 4);
    } else {
        unsigned char* src = (unsigned char*)pixels;
        unsigned char* dst = (unsigned char*)bits;
        for (int i = 0; i < height; ++i) {
            memcpy(dst, src, width * 4);
            src += width * 4;
            dst += pitch;
        }
    }
    getBitEnd(bitInfo);
#else
    WTF::Locker<WTF::Mutex> locker(m_compositeMutex);
    if (!m_memoryCanvas)
        return;

    int width = m_clientRect.width();
    int height = m_clientRect.height();

    DWORD cBytes = width * height * 4;
    SkBaseDevice* device = (SkBaseDevice*)m_memoryCanvas->getTopDevice();
    if (!device)
        return;

    const SkBitmap& bitmap = device->accessBitmap(false);
    SkCanvas* tempCanvas = nullptr;
    uint32_t* pixels = nullptr;

    if (bitmap.info().width() != width || bitmap.info().height() != height) {
        tempCanvas = skia::CreatePlatformCanvas(width, height, !m_hasTransparentBackground);
        clearCanvas(tempCanvas, m_clientRect, m_hasTransparentBackground);
        tempCanvas->drawBitmap(bitmap, 0, 0, nullptr);
        device = (SkBaseDevice*)tempCanvas->getTopDevice();
        if (!device)
            return;
        const SkBitmap& tempBitmap = device->accessBitmap(false);
        pixels = tempBitmap.getAddr32(0, 0);
    } else
        pixels = bitmap.getAddr32(0, 0);

    if (pitch == 0 || pitch == width * 4) {
        memcpy(bits, pixels, width * height * 4);
    } else {
        unsigned char* src = (unsigned char*)pixels;
        unsigned char* dst = (unsigned char*)bits;
        for (int i = 0; i < height; ++i) {
            memcpy(dst, src, width * 4);
            src += width * 4;
            dst += pitch;
        }
    }

    m_isDrawDirty = false;

    if (tempCanvas) {
        delete m_memoryCanvas;
        m_memoryCanvas = tempCanvas;
    }

#endif
}

} // cc
