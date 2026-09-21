#ifndef CC_INPUT_SCROLL_CONTROLLER_H_
#define CC_INPUT_SCROLL_CONTROLLER_H_

#include <cstdint>

namespace cc {

struct ScrollVector {
    ScrollVector();
    ScrollVector(double vectorX, double vectorY);

    double x;
    double y;
};

struct ScrollRect {
    ScrollRect();
    ScrollRect(double rectX, double rectY, double rectWidth, double rectHeight);

    bool contains(double pointX, double pointY) const;

    double x;
    double y;
    double width;
    double height;
};

struct CompositorScrollbarSnapshot {
    CompositorScrollbarSnapshot();

    bool isValid() const;
    bool isSupported() const;
    bool belongsToLayer(int candidateLayerId) const;
    ScrollRect thumbRect(double visualOffset, double minimumOffset,
        double maximumOffset, double viewportLength) const;
    ScrollRect localThumbRect(double visualOffset, double minimumOffset,
        double maximumOffset, double viewportLength) const;

    bool enabled;
    bool vertical;
    bool overlay;
    bool customStyle;
    int layerId;
    ScrollRect trackRect;
    ScrollRect localTrackRect;
    double minimumThumbLength;
};

struct ScrollNodeKey {
    ScrollNodeKey();
    ScrollNodeKey(uint64_t view, uint64_t viewGenerationValue,
        uint64_t node, uint64_t nodeGenerationValue);

    bool isValid() const;
    bool operator==(const ScrollNodeKey& other) const;
    bool operator!=(const ScrollNodeKey& other) const { return !(*this == other); }

    uint64_t viewId;
    uint64_t viewGeneration;
    uint64_t nodeId;
    uint64_t nodeGeneration;
};

enum ScrollInputAxis {
    ScrollInputAxisNone = 0,
    ScrollInputAxisHorizontal = 1 << 0,
    ScrollInputAxisVertical = 1 << 1
};

struct ScrollCoverageWindow {
    ScrollCoverageWindow();

    ScrollRect rasterRect;
    ScrollVector minimumOffset;
    ScrollVector maximumOffset;
};

// Computes a bounded raster window around the current viewport. A one-axis
// scroller is limited to two viewport areas; a two-axis scroller is limited
// to 1.96 viewport areas. Direction reserves more of the budget ahead while
// retaining enough content behind for small reversals.
ScrollCoverageWindow computeScrollCoverageWindow(
    const ScrollVector& baseOffset,
    const ScrollVector& minimumOffset,
    const ScrollVector& maximumOffset,
    const ScrollVector& viewportSize,
    const ScrollVector& direction,
    uint32_t inputAxes);

enum ScrollMainThreadReason {
    ScrollMainThreadReasonNone = 0,
    ScrollMainThreadReasonWheelHandler = 1 << 0,
    ScrollMainThreadReasonTouchHandler = 1 << 1,
    ScrollMainThreadReasonNonFastRegion = 1 << 2,
    ScrollMainThreadReasonComplexTransform = 1 << 3,
    ScrollMainThreadReasonUnknownRelationship = 1 << 4,
    ScrollMainThreadReasonCrossFrame = 1 << 5,
    ScrollMainThreadReasonScrollLinkedEffect = 1 << 6,
    ScrollMainThreadReasonIncompleteCoverage = 1 << 7,
    ScrollMainThreadReasonUnsupportedScrollbar = 1 << 8
};

struct ScrollNodeSnapshot {
    ScrollNodeSnapshot();

    bool isValid() const;
    bool compositorEligible() const;
    bool canConsumeDelta(const ScrollVector& delta) const;
    bool advanceInputPrediction(const ScrollVector& delta);
    void setInputPrediction(const ScrollVector& offset);

    ScrollNodeKey key;
    ScrollNodeKey parent;
    uint64_t sceneVersion;
    uint64_t scrollEpoch;
    uint64_t acknowledgedSample;
    ScrollVector acknowledgedTotal;
    ScrollVector baseOffset;
    // Input-thread prediction of the smooth-scroll target. It is deliberately
    // separate from baseOffset, which remains the owner's committed position.
    ScrollVector inputPrediction;
    ScrollVector minimumOffset;
    ScrollVector maximumOffset;
    ScrollVector coverageMinimum;
    ScrollVector coverageMaximum;
    ScrollRect viewportClip;
    CompositorScrollbarSnapshot verticalScrollbar;
    CompositorScrollbarSnapshot horizontalScrollbar;
    uint32_t mainThreadReasons;
    uint32_t inputAxes;
    bool coverageComplete;
};

struct ScrollSample {
    ScrollSample();

    bool valid;
    ScrollNodeKey key;
    uint64_t scrollEpoch;
    uint64_t sampleSequence;
    uint64_t throughInputSequence;
    ScrollVector consumedTotal;
};

struct ScrollBarrier {
    ScrollBarrier();

    bool valid;
    ScrollNodeKey key;
    uint64_t sceneVersion;
    uint64_t scrollEpoch;
    uint64_t sampleSequence;
    uint64_t throughInputSequence;
    ScrollVector consumedTotal;
    ScrollVector visualOffset;
};

class ScrollController {
public:
    ScrollController();

    // Activates scroll metadata atomically. Older scenes and impossible
    // acknowledgements are rejected without modifying the active state.
    bool activateScene(const ScrollNodeSnapshot& node);

    // Input and visual sample sequence numbers intentionally have different
    // domains: one wheel input can produce several visual samples.
    bool enqueueDelta(uint64_t inputSequence, const ScrollVector& delta,
        double monotonicTime, bool smooth);
    ScrollSample sample(double monotonicTime);
    ScrollBarrier captureBarrier() const;
    bool synchronizeBarrier(const ScrollBarrier& barrier,
        ScrollSample* synchronizedSample);

    void revokeEligibility(uint32_t reason);
    bool hasActiveNode() const { return m_hasActiveNode; }
    bool compositorEligible() const;
    bool needsAnimationFrame() const;
    ScrollVector visualOffset() const { return m_visualOffset; }
    const ScrollNodeSnapshot& activeNode() const { return m_node; }

private:
    class MotionAxis {
    public:
        MotionAxis();

        void reset(double position, double monotonicTime);
        void setBounds(double minimum, double maximum);
        void addDelta(double delta, double monotonicTime, bool smooth);
        double advance(double monotonicTime);
        bool isActive() const;

    private:
        double clamp(double value) const;

        double m_position;
        double m_target;
        double m_velocity;
        double m_lastTime;
        double m_minimum;
        double m_maximum;
        bool m_initialized;
    };

    void updateMotionBounds();
    ScrollVector reconciledVisualOffset(const ScrollVector& consumed) const;

    bool m_hasActiveNode;
    ScrollNodeSnapshot m_node;
    ScrollVector m_consumedTotal;
    ScrollVector m_visualOffset;
    MotionAxis m_motionX;
    MotionAxis m_motionY;
    uint64_t m_sampleSequence;
    uint64_t m_throughInputSequence;
};

} // namespace cc

#endif // CC_INPUT_SCROLL_CONTROLLER_H_
