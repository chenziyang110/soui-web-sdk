#ifndef LayerTreeHost_h
#define LayerTreeHost_h

#include "third_party/WebKit/public/platform/WebLayerTreeView.h"
#include "cc/trees/ScrollTreeSnapshot.h"
#include "cc/trees/LegacyPaintRetryPolicy.h"
#include "cc/trees/FrameBufferLeasePolicy.h"
#include "cc/trees/InputFrameCoverage.h"
#include "cc/trees/ViewTraceIdentity.h"
#include "cc/raster/RasterCandidateBudget.h"
#include <memory>
#include <atomic>
#include "third_party/WebKit/public/platform/WebThread.h"
#include "third_party/WebKit/Source/platform/geometry/IntRect.h"
#include "third_party/WebKit/Source/platform/geometry/IntSize.h"
#include "third_party/WebKit/Source/wtf/HashMap.h"
#include "third_party/WebKit/Source/wtf/Vector.h"
#include "third_party/WebKit/Source/wtf/ThreadingPrimitives.h"
#include "third_party/WebKit/Source/wtf/HashSet.h"
#include "third_party/skia/include/core/SkRect.h"

namespace blink {
class WebViewClient;
class WebGestureCurveTarget;
class IntRect;
struct WebFloatSize;
class WebThread;
class WebThreadSupportingGC;
}

namespace cc_blink {
class WebCompositorAnimationImpl;
class WebLayerImpl;
}

namespace WTF {
class Mutex;
}

namespace content {
class WebPageImpl;
}

class SkCanvas;

namespace cc {

class DirtyLayers;
class Tile;
class TileGrid;
class LayerChangeAction;
class CompositingLayer;
class ActionsFrameGroup;
struct UiPaintRetryState;
class UiPaintRetryTask;
class ResourceProgressTask;
struct CompositorAnimationState;
struct CompositorAnimationFinishedTask;
struct CompositorScrollState;
struct CompositorScrollInput;
struct RetiredCompositorScrollState;
struct ScrollRasterCoverageRequest;

struct CompositorScrollReceipt {
    CompositorScrollReceipt()
        : sceneVersion(0)
        , inputSequence(0)
        , offsetX(0)
        , offsetY(0)
    {
    }

    uint64_t sceneVersion;
    uint64_t inputSequence;
    double offsetX;
    double offsetY;
};
struct CompositorScrollUpdateTask;
class LayerTreeHostClent;
#if defined(SOUI_GPU_COMPOSITOR)
class GpuSkiaSurface;
#endif

class LayerTreeHostUiThreadClient {
public:
    virtual void paintToMemoryCanvasInUiThread(SkCanvas* canvas, const blink::IntRect& paintRect) = 0;
    virtual void* nativeWindowHandle() const { return nullptr; }
};

class LayerTreeHost : public blink::WebLayerTreeView {
public:
    LayerTreeHost(LayerTreeHostClent* hostClient, LayerTreeHostUiThreadClient* uiThreadClient);
    ~LayerTreeHost();

    void registerLayer(cc_blink::WebLayerImpl* layer);
    void unregisterLayer(cc_blink::WebLayerImpl* layer);
    cc_blink::WebLayerImpl* getLayerById(int id);

    void gc();

    bool isDestroying() const;

    //void updateLayers(SkCanvas* canvas, const blink::IntRect& clip, bool needsFullTreeSync);
    void recordDraw();
    bool drawToCanvas(SkCanvas* canvas, const SkRect& clip);
    bool drawCompositorScrollbarLayer(int layerId, SkCanvas* canvas,
        const SkRect& localDirtyRect);
    void updateLayersDrawProperties(ScrollTreeSnapshot* scrollTree,
        std::vector<ScrollRasterCoverageRequest>* coverageRequests);

    //void setNeedsCommit();
    //void setNeedsFullTreeSync();
    //void didUpdateLayout();

    void requestRepaint(const blink::IntRect& repaintRect);

    // 浠庡厜鏍呭寲绾跨▼鍙戝嚭锛岄€氱煡娓叉煋鏂扮殑涓€甯с€傚綋鐒朵篃鍙兘浠庝富绾跨▼鍙戝嚭锛屽鏋滄病鏈夊厜鏍呭寲锛屼絾鍙堟湁婊氬姩绛夋儏鍐?
    void requestDrawFrameLocked(DirtyLayers* dirtyLayers, Vector<Tile*>* tilesToUIThreadRelease);
    bool preDrawFrame();
    void postDrawFrame();
    bool applyActions(bool needCheck);

    void scrollBy(const blink::WebFloatSize& delta, const blink::WebFloatSize& velocity);

    // 閫氬父姣攄eviceViewportSize瑕佸ぇ涓€鐐?
    blink::IntSize canDrawSize();

    // WebLayerTreeView
    
    // Sets the root of the tree. The root is set by way of the constructor.
    virtual void setRootLayer(const blink::WebLayer&) override;
    CompositingLayer* getRootCCLayer();
    virtual void clearRootLayer() override;
    virtual void setViewportSize(const blink::WebSize& deviceViewportSize) override;

    // Gives the viewport size in physical device pixels.
    virtual blink::WebSize deviceViewportSize() const override;

    virtual void setDeviceScaleFactor(float) override;
    virtual float deviceScaleFactor() const override;

    // Sets the background color for the viewport.
    virtual void setBackgroundColor(blink::WebColor) override;
    blink::WebColor getBackgroundColor() const;

    // Sets the background transparency for the viewport. The default is 'false'.
    virtual void setHasTransparentBackground(bool) override;
    bool getHasTransparentBackground() const;

    virtual void registerForAnimations(blink::WebLayer* layer) override;

    // Sets whether this view is visible. In threaded mode, a view that is not visible will not
    // composite or trigger updateAnimations() or layout() calls until it becomes visible.
    virtual void setVisible(bool) override;

    // Sets the current page scale factor and minimum / maximum limits. Both limits are initially 1 (no page scale allowed).
    virtual void setPageScaleFactorAndLimits(float pageScaleFactor, float minimum, float maximum) override;

    // Starts an animation of the page scale to a target scale factor and scroll offset.
    // If useAnchor is true, destination is a point on the screen that will remain fixed for the duration of the animation.
    // If useAnchor is false, destination is the final top-left scroll position.
    virtual void startPageScaleAnimation(const blink::WebPoint& destination, bool useAnchor, float newPageScale, double durationSec) override;

    virtual void setNeedsAnimate() override;

    virtual void finishAllRendering() override;

    void showDebug();

    void setWebGestureCurveTarget(blink::WebGestureCurveTarget* webGestureCurveTarget);

    void setNeedTileRender(bool b) { m_needTileRender = b; }
    bool needTileRender() { return m_needTileRender; }

    void increaseNodesCount();
    void decreaseNodesCount();
    bool has3dNodes();

    WTF::Mutex* tilesMutex();

    int64 createDrawingIndex();
    int64 drawingIndex();

    void addRasteringIndex(int64 index);
    int64 frontRasteringIndex();
    void popRasteringIndex();

    enum class RecordBlockReason { None, Destroying, InFlightScene, Raster, MissingAction };
    RecordBlockReason recordBlockReason() const;
    bool canRecordActions() const;
    void registerResourceWait();
    void clearResourceWait();
    void notifyResourceProgress(bool actionDirty = false);
    RasterCandidateBudget inspectRasterCandidateScene(uint64_t* generation);
    bool matchesRasterCandidateGeneration(uint64_t generation);
    std::shared_ptr<UiPaintRetryState> resourceNotificationState() const { return m_uiPaintRetry; }
    static void notifyRasterCompletion(const std::shared_ptr<UiPaintRetryState>& state);
    void retryPendingOwnerWork();
    void beginRecordActions(bool isComefromMainframe);
    void endRecordActions();

    int64 genActionId();
    void appendLayerChangeAction(LayerChangeAction* action);
    void registerCCLayer(CompositingLayer* layer);
    void unregisterCCLayer(CompositingLayer* layer);
    CompositingLayer* getCCLayerById(int id);
    bool isRootCCLayerEmpty() const { return !m_rootCCLayer; }

    struct FrameBuffer {
        FrameBuffer(std::unique_ptr<SkCanvas> value, uint64_t sizeGeneration, int w, int h,
            bool alphaValue);
        ~FrameBuffer();
        std::unique_ptr<SkCanvas> canvas;
        uint64_t generation;
        uint64_t bytes;
        int width;
        int height;
        bool alpha;
        content::input_frame_trace::event traceContext;
    };
    struct FrameBufferLease {
        FrameBufferLease();
        FrameBufferLease(std::shared_ptr<FrameBuffer> value,
            std::shared_ptr<UiPaintRetryState> state);
        ~FrameBufferLease();
        FrameBufferLease(FrameBufferLease&& other) noexcept;
        FrameBufferLease& operator=(FrameBufferLease&& other) noexcept;
        FrameBufferLease(const FrameBufferLease&) = delete;
        FrameBufferLease& operator=(const FrameBufferLease&) = delete;
        explicit operator bool() const { return !!buffer; }
        SkCanvas* canvas() const;
        std::shared_ptr<FrameBuffer> buffer;
        std::shared_ptr<UiPaintRetryState> completionState;
    };
    FrameBufferLease acquireFrameBufferLease();
    bool independentPresentationEnabled();
    SkCanvas* getMemoryCanvasLocked();
    void releaseMemoryCanvasLocked();

    bool isDrawDirty();
    void paintToBit(void* bits, int pitch);

    struct BitInfo {
        uint32_t* pixels;
        FrameBufferLease lease;
        int width;
        int height;
    };
    BitInfo* getBitBegin();
    static void getBitEnd(const BitInfo* bitInfo);

    void requestDrawFrameToRunIntoCompositeThread();
    void requestApplyActionsToRunIntoCompositeThread(bool needCheck);
    //void setUseLayeredBuffer(bool b);
    //bool getIsUseLayeredBuffer() const { return m_useLayeredBuffer; }
    static void clearCanvas(SkCanvas* canvas, const SkRect& rect, bool useLayeredBuffer);

    void setDrawMinInterval(double drawMinInterval);
    void setMainFrameDeadline(uint64_t frameId, double target, double interval);
    bool setViewTraceIdentity(const content::input_frame_trace::event& value);
    content::input_frame_trace::event traceContextForFrame();
    void noteProcessedInputTrace(const content::input_frame_trace::event& value);
    void setIndependentPresentationEnabled(bool enabled);
    
    void postPaintMessage(const SkRect& paintRect);
    void firePaintEvent(HDC hdc, const RECT* paintRect);
    blink::IntRect getClientRect();

    void setLayerTreeDirty();
    bool isLayerTreeDirty() const;

    void disablePaint();
    void enablePaint();

    cc_blink::WebLayerImpl* getRootLayer() { return m_rootLayer; }
    const cc_blink::WebLayerImpl* getConstRootLayer() { return m_rootLayer; }

    void appendPendingRepaintRect(const SkRect& r);

    bool addCompositorAnimation(int layerId, cc_blink::WebCompositorAnimationImpl* animation);
    void removeCompositorAnimation(int layerId, int animationId, int targetProperty = -1);
    void pauseCompositorAnimation(int layerId, int animationId, double timeOffset);
    bool hasCompositorAnimation(int layerId);
    bool submitCompositorScrollTree(const ScrollTreeSnapshot& scrollTree);
    void onActionFrameApplied(int64 throughActionId);
    void onRasterActionCancelled(int64 actionId);
    bool handleCompositorWheelInput(int x, int y, double deltaX,
        double deltaY, double monotonicTime, bool smooth,
        CompositorScrollReceipt* receipt = nullptr);
    bool captureCompositorScrollBarrier(int x, int y,
        ScrollBarrier* barrier);
    bool synchronizeCompositorScrollBarrier(const ScrollBarrier& barrier);

private:
    void requestPaintToMemoryCanvasToUiThread(const SkRect& r);
    void queueLegacyPaintLocked();
    void runLegacyPaintRetry(const LegacyPaintRetryPolicy::Ticket& ticket);
    friend class UiPaintRetryTask;
    friend class ResourceProgressTask;
    void runResourceProgress(uint64_t ticket, uint64_t generation);
    std::shared_ptr<UiPaintRetryState> m_uiPaintRetry;
    void onApplyActionsInCompositeThread(bool needCheck);
    void waitForApplyActions();
    void waitForDrawFrame();
    void drawFrameInCompositeThread();
    void requestDrawFrameAfterDelay(long long delayMs);
    bool tickCompositorAnimations(double monotonicTime, Vector<SkRect>* dirtyRects);
    bool tickCompositorScroll(double monotonicTime, Vector<SkRect>* dirtyRects);
    void postCompositorScrollUpdateToMainThread(int layerId,
        const ScrollSample& sample, const ScrollVector& visualOffset);
    bool dispatchCompositorScrollOnMainThread(int layerId,
        const ScrollSample& sample, const ScrollVector& visualOffset);
    void postAnimationFinishedToUiThread(int layerId, int group, double monotonicTime);
    void dispatchAnimationFinishedOnMainThread(int layerId, int group, double monotonicTime);
    void paintToMemoryCanvasInUiThread(const SkRect& paintRect);
    uint64_t paintToMemoryCanvasInCompositeThread(const SkRect& r);
    bool submitSceneTransaction(ScrollTreeSnapshot&& scrollTree,
        int64 firstActionId, int64 lastActionId, bool resourcesReady);
    bool hasInFlightScene() const;
    
    std::atomic<bool> m_isDestroying;

    LayerTreeHostClent* m_hostClient;
    LayerTreeHostUiThreadClient* m_uiThreadClient;
    blink::WebGestureCurveTarget * m_webGestureCurveTarget;

    cc_blink::WebLayerImpl* m_rootLayer;
    CompositingLayer* m_rootCCLayer;

    blink::IntSize m_deviceViewportSize;
    blink::IntRect m_clientRect;
    float m_deviceScaleFactor;
    blink::WebColor m_backgroundColor;
    bool m_hasTransparentBackground;
    bool m_independentPresentationEnabled;
    bool m_visible;
    float m_pageScaleFactor;
    float m_minimum;
    float m_maximum;
    bool m_needTileRender;
    bool m_layerTreeDirty; // 闇€瑕乄ebPageImpl.recordDraw

    int m_3dNodesCount;

    WTF::Mutex* m_tilesMutex;

    int64 m_newestDrawingIndex;
    int64 m_drawingIndex;
    WTF::Vector<int64> m_rasteringIndexs;

    WTF::HashMap<int, cc_blink::WebLayerImpl*> m_liveLayers;

    WTF::Mutex* m_rasterNotifMutex;
    WTF::Vector<DirtyLayers*> m_dirtyLayersGroup;
    WTF::Vector<Tile*> m_tilesToUIThreadRelease;

    ActionsFrameGroup* m_actionsFrameGroup;
    WTF::Vector<LayerChangeAction*> m_actions;
    WTF::HashMap<int, CompositingLayer*> m_liveCCLayers;
    WTF::Vector<CompositorAnimationState*> m_compositorAnimations;
    WTF::Vector<CompositorScrollState*> m_compositorScrollStates;
    WTF::Vector<RetiredCompositorScrollState*> m_retiredCompositorScrollStates;
    WTF::Vector<CompositorScrollInput*> m_pendingCompositorScrollInputs;
    WTF::Mutex m_compositorScrollInputMutex;
    ScrollTreeSnapshot* m_pendingCompositorScrollTree;
    ScrollTreeSnapshot* m_activeInputScrollTree;
    mutable WTF::Mutex m_sceneTransactionMutex;
    SceneCoordinator m_sceneCoordinator;
    content::input_frame_trace::event m_pendingSceneTraceContext;
    content::input_frame_trace::event m_activeSceneTraceContext;
    bool m_rasterRecoveryPending = false;
    bool m_rasterRecoveryNeedsInvalidation = false;
    uint64_t m_rasterRecoveryAfterScene = 0;
    uint64_t m_activeCompositorScrollVersion;
    uint64_t m_nextScrollSceneVersion;
    uint64_t m_nextCompositorInputSequence;
    ScrollNodeKey m_latchedScrollNode;
    double m_lastCompositorWheelTime;
    //////////////////////////////////////////////////////////////////////////
    WTF::OwnPtr<blink::WebThreadSupportingGC> m_compositeThread;
    WTF::Mutex m_compositeMutex;
    WTF::Mutex m_rootCCLayerMutex;
    SkCanvas* m_memoryCanvas; // Borrowed only while m_compositeMutex is held.
    std::shared_ptr<FrameBuffer> m_frameBuffers[FrameBufferLeasePolicy::maximumSlots];
    size_t m_currentFrameBufferSlot;
    uint64_t m_frameBufferGeneration;
    bool m_canvasPaintBlocked;
    bool prepareWritableFrameBufferLocked();
    FrameBufferLease acquireFrameBufferLeaseLocked();

    double m_lastCompositeTime;
    double m_lastPaintTime;
    double m_nextCompositeFrameTime;
    uint64_t m_pendingMainFrameId;
    uint64_t m_presentedMainFrameId;
    double m_pendingMainFrameDeadline;
    bool m_unifiedFrameDeadline;
    ViewTraceIdentity m_viewTraceIdentity;
    InputFrameCoverage m_pendingInputTrace;
    InputFrameCoverage m_unpresentedInputTrace;
    uint64_t m_unpresentedInputFrameId = 0;
    content::input_frame_trace::event m_frameTraceContext;
    int64 m_recordedTraceActionFirst = 0;
    int64 m_recordedTraceActionLast = 0;
    double m_drawMinInterval;

    static const int m_paintMessageQueueSize = 200;
    Vector<SkRect> m_dirtyRectsForComposite;
    Vector<SkRect> m_dirtyRectsForUi;
    int m_postpaintMessageCount;
    int m_drawFrameCount;
    int m_drawFrameFinishCount;
    int m_requestApplyActionsCount;
    int m_requestApplyActionsFinishCount;

    friend CompositorAnimationFinishedTask;
    WTF::HashSet<CompositorAnimationFinishedTask*> m_animationFinishedTasks;
    friend CompositorScrollUpdateTask;
    WTF::HashMap<int, CompositorScrollUpdateTask*> m_scrollUpdateTasks;
    int m_paintToMemoryCanvasInUiThreadTaskCount;
#if defined(SOUI_GPU_COMPOSITOR)
    GpuSkiaSurface* m_gpuSurface;
    bool m_gpuExperiment;
    // Avoid retrying WGL/context creation on every software frame after a
    // machine has no usable GPU. A resize is an explicit retry boundary.
    bool m_gpuInitAttempted;
#endif

    bool m_isDrawDirty;
    bool m_hasResize;

    WTF::Vector<SkRect> m_pendingRepaintRectsInRootLayerCoordinate;
};

} // cc

#endif // LayerTreeHost_h
