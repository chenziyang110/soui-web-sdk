#include "cc/input/ScrollController.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cc {

namespace {

const double kCriticalDampingFrequency = 35.0;
const double kMaximumIntegrationStep = 0.25;
const double kMovementEpsilon = 0.000001;

bool isFinite(double value)
{
    return std::isfinite(value) != 0;
}

bool isFinite(const ScrollVector& value)
{
    return isFinite(value.x) && isFinite(value.y);
}

double clampValue(double value, double minimum, double maximum)
{
    return std::max(minimum, std::min(maximum, value));
}

bool canAcceptAxis(double delta, double prediction, double base,
    double minimum, double maximum, double coverageMinimum,
    double coverageMaximum, double viewportLength)
{
    if (delta == 0.0 || viewportLength <= 0.0)
        return false;

    const double target = clampValue(prediction + delta, minimum, maximum);
    // A temporary raster edge must not turn an already accepted target into
    // a main-thread fallback. Require room to make producer progress from
    // the committed base, rather than clipping the predicted target there.
    // Use the sampling tolerance so a settled rounding remainder at a real
    // content edge is not mistaken for pending motion forever.
    if (target - base >= kMovementEpsilon)
        return coverageMaximum - base >= kMovementEpsilon;
    if (base - target >= kMovementEpsilon)
        return base - coverageMinimum >= kMovementEpsilon;
    return std::fabs(prediction - base) >= kMovementEpsilon;
}

struct AxisCoverage {
    double minimum;
    double maximum;
};

AxisCoverage computeAxisCoverage(double base, double minimum, double maximum,
    double viewportLength, double direction, double prefetchRatio,
    bool scrollable)
{
    AxisCoverage result;
    const double lower = std::min(minimum, maximum);
    const double upper = std::max(minimum, maximum);
    minimum = lower;
    maximum = upper;
    base = clampValue(base, minimum, maximum);
    result.minimum = base;
    result.maximum = base;
    if (!scrollable || viewportLength <= 0.0 || prefetchRatio <= 0.0)
        return result;

    const double budget = std::min(maximum - minimum,
        viewportLength * prefetchRatio);
    const double beforeRatio = direction > kMovementEpsilon ? 0.25
        : direction < -kMovementEpsilon ? 0.75 : 0.5;
    const double afterRatio = 1.0 - beforeRatio;
    double before = std::min(base - minimum, budget * beforeRatio);
    double after = std::min(maximum - base, budget * afterRatio);
    double remaining = budget - before - after;

    if (direction >= 0.0) {
        const double addAfter = std::min(remaining,
            maximum - base - after);
        after += addAfter;
        remaining -= addAfter;
        before += std::min(remaining, base - minimum - before);
    } else {
        const double addBefore = std::min(remaining,
            base - minimum - before);
        before += addBefore;
        remaining -= addBefore;
        after += std::min(remaining, maximum - base - after);
    }

    result.minimum = base - before;
    result.maximum = base + after;
    return result;
}

ScrollRect computeScrollbarThumbRect(
    const CompositorScrollbarSnapshot& scrollbar, const ScrollRect& track,
    double visualOffset, double minimumOffset, double maximumOffset,
    double viewportLength)
{
    if (!scrollbar.enabled || !scrollbar.isValid()
        || !isFinite(visualOffset) || !isFinite(minimumOffset)
        || !isFinite(maximumOffset) || !isFinite(viewportLength)
        || viewportLength <= 0.0) {
        return ScrollRect();
    }

    const double trackStart = scrollbar.vertical ? track.y : track.x;
    const double trackLength = scrollbar.vertical
        ? track.height : track.width;
    const double range = std::max(0.0, maximumOffset - minimumOffset);
    double thumbLength = trackLength;
    if (range > 0.0) {
        thumbLength = trackLength * viewportLength
            / (viewportLength + range);
        thumbLength = clampValue(thumbLength,
            std::min(scrollbar.minimumThumbLength, trackLength), trackLength);
    }
    const double ratio = range > 0.0
        ? clampValue((visualOffset - minimumOffset) / range, 0.0, 1.0)
        : 0.0;
    const double thumbStart = trackStart
        + (trackLength - thumbLength) * ratio;
    return scrollbar.vertical
        ? ScrollRect(track.x, thumbStart, track.width, thumbLength)
        : ScrollRect(thumbStart, track.y, thumbLength, track.height);
}

} // namespace

ScrollVector::ScrollVector()
    : x(0.0)
    , y(0.0)
{
}

ScrollVector::ScrollVector(double vectorX, double vectorY)
    : x(vectorX)
    , y(vectorY)
{
}

ScrollRect::ScrollRect()
    : x(0.0)
    , y(0.0)
    , width(0.0)
    , height(0.0)
{
}

ScrollRect::ScrollRect(double rectX, double rectY,
    double rectWidth, double rectHeight)
    : x(rectX)
    , y(rectY)
    , width(rectWidth)
    , height(rectHeight)
{
}

bool ScrollRect::contains(double pointX, double pointY) const
{
    return pointX >= x && pointX < x + width
        && pointY >= y && pointY < y + height;
}

ScrollCoverageWindow::ScrollCoverageWindow()
{
}

ScrollCoverageWindow computeScrollCoverageWindow(
    const ScrollVector& baseOffset,
    const ScrollVector& minimumOffset,
    const ScrollVector& maximumOffset,
    const ScrollVector& viewportSize,
    const ScrollVector& direction,
    uint32_t inputAxes)
{
    const bool horizontal = (inputAxes & ScrollInputAxisHorizontal) != 0;
    const bool vertical = (inputAxes & ScrollInputAxisVertical) != 0;
    const double perAxisRatio = horizontal && vertical ? 0.4 : 1.0;
    const AxisCoverage x = computeAxisCoverage(baseOffset.x,
        minimumOffset.x, maximumOffset.x, viewportSize.x, direction.x,
        perAxisRatio, horizontal);
    const AxisCoverage y = computeAxisCoverage(baseOffset.y,
        minimumOffset.y, maximumOffset.y, viewportSize.y, direction.y,
        perAxisRatio, vertical);

    ScrollCoverageWindow result;
    result.minimumOffset = ScrollVector(x.minimum, y.minimum);
    result.maximumOffset = ScrollVector(x.maximum, y.maximum);
    result.rasterRect = ScrollRect(x.minimum, y.minimum,
        std::max(0.0, x.maximum - x.minimum + viewportSize.x),
        std::max(0.0, y.maximum - y.minimum + viewportSize.y));
    return result;
}

CompositorScrollbarSnapshot::CompositorScrollbarSnapshot()
    : enabled(false)
    , vertical(true)
    , overlay(false)
    , customStyle(false)
    , layerId(0)
    , minimumThumbLength(18.0)
{
}

bool CompositorScrollbarSnapshot::isValid() const
{
    if (!enabled)
        return true;
    return layerId > 0
        && isFinite(trackRect.x) && isFinite(trackRect.y)
        && isFinite(trackRect.width) && isFinite(trackRect.height)
        && trackRect.width > 0.0 && trackRect.height > 0.0
        && isFinite(localTrackRect.x) && isFinite(localTrackRect.y)
        && isFinite(localTrackRect.width) && isFinite(localTrackRect.height)
        && localTrackRect.width > 0.0 && localTrackRect.height > 0.0
        && isFinite(minimumThumbLength) && minimumThumbLength > 0.0;
}

bool CompositorScrollbarSnapshot::isSupported() const
{
    return !enabled || (isValid() && !overlay && !customStyle);
}

bool CompositorScrollbarSnapshot::belongsToLayer(
    int candidateLayerId) const
{
    return enabled && layerId > 0 && layerId == candidateLayerId;
}

ScrollRect CompositorScrollbarSnapshot::thumbRect(double visualOffset,
    double minimumOffset, double maximumOffset, double viewportLength) const
{
    return computeScrollbarThumbRect(*this, trackRect, visualOffset,
        minimumOffset, maximumOffset, viewportLength);
}

ScrollRect CompositorScrollbarSnapshot::localThumbRect(double visualOffset,
    double minimumOffset, double maximumOffset, double viewportLength) const
{
    return computeScrollbarThumbRect(*this, localTrackRect, visualOffset,
        minimumOffset, maximumOffset, viewportLength);
}

ScrollNodeKey::ScrollNodeKey()
    : viewId(0)
    , viewGeneration(0)
    , nodeId(0)
    , nodeGeneration(0)
{
}

ScrollNodeKey::ScrollNodeKey(uint64_t view, uint64_t viewGenerationValue,
    uint64_t node, uint64_t nodeGenerationValue)
    : viewId(view)
    , viewGeneration(viewGenerationValue)
    , nodeId(node)
    , nodeGeneration(nodeGenerationValue)
{
}

bool ScrollNodeKey::isValid() const
{
    return viewId && viewGeneration && nodeId && nodeGeneration;
}

bool ScrollNodeKey::operator==(const ScrollNodeKey& other) const
{
    return viewId == other.viewId
        && viewGeneration == other.viewGeneration
        && nodeId == other.nodeId
        && nodeGeneration == other.nodeGeneration;
}

ScrollNodeSnapshot::ScrollNodeSnapshot()
    : sceneVersion(0)
    , scrollEpoch(0)
    , acknowledgedSample(0)
    , mainThreadReasons(ScrollMainThreadReasonNone)
    , inputAxes(ScrollInputAxisNone)
    , coverageComplete(false)
{
}

bool ScrollNodeSnapshot::isValid() const
{
    if (!key.isValid() || !sceneVersion || !scrollEpoch)
        return false;
    if (!isFinite(acknowledgedTotal) || !isFinite(baseOffset)
        || !isFinite(inputPrediction)
        || !isFinite(minimumOffset) || !isFinite(maximumOffset)
        || !isFinite(coverageMinimum) || !isFinite(coverageMaximum)) {
        return false;
    }
    if (!isFinite(viewportClip.x) || !isFinite(viewportClip.y)
        || !isFinite(viewportClip.width) || !isFinite(viewportClip.height)
        || viewportClip.width < 0.0 || viewportClip.height < 0.0) {
        return false;
    }
    if (!verticalScrollbar.isValid() || !horizontalScrollbar.isValid())
        return false;
    return minimumOffset.x <= maximumOffset.x
        && minimumOffset.y <= maximumOffset.y
        && coverageMinimum.x <= coverageMaximum.x
        && coverageMinimum.y <= coverageMaximum.y
        && std::max(minimumOffset.x, coverageMinimum.x)
            <= std::min(maximumOffset.x, coverageMaximum.x)
        && std::max(minimumOffset.y, coverageMinimum.y)
            <= std::min(maximumOffset.y, coverageMaximum.y);
}

bool ScrollNodeSnapshot::compositorEligible() const
{
    return isValid() && coverageComplete
        && mainThreadReasons == ScrollMainThreadReasonNone
        && inputAxes != ScrollInputAxisNone
        && viewportClip.width > 0.0 && viewportClip.height > 0.0
        && verticalScrollbar.isSupported()
        && horizontalScrollbar.isSupported();
}

bool ScrollNodeSnapshot::canConsumeDelta(const ScrollVector& delta) const
{
    if (!compositorEligible() || !isFinite(delta))
        return false;
    const bool canConsumeX = (inputAxes & ScrollInputAxisHorizontal)
        && canAcceptAxis(delta.x, inputPrediction.x, baseOffset.x,
            minimumOffset.x, maximumOffset.x, coverageMinimum.x,
            coverageMaximum.x, viewportClip.width);
    const bool canConsumeY = (inputAxes & ScrollInputAxisVertical)
        && canAcceptAxis(delta.y, inputPrediction.y, baseOffset.y,
            minimumOffset.y, maximumOffset.y, coverageMinimum.y,
            coverageMaximum.y, viewportClip.height);
    return canConsumeX || canConsumeY;
}

bool ScrollNodeSnapshot::advanceInputPrediction(const ScrollVector& delta)
{
    if (!canConsumeDelta(delta))
        return false;

    if (inputAxes & ScrollInputAxisHorizontal) {
        inputPrediction.x = clampValue(inputPrediction.x + delta.x,
            minimumOffset.x, maximumOffset.x);
    }
    if (inputAxes & ScrollInputAxisVertical) {
        inputPrediction.y = clampValue(inputPrediction.y + delta.y,
            minimumOffset.y, maximumOffset.y);
    }
    return true;
}

void ScrollNodeSnapshot::setInputPrediction(const ScrollVector& offset)
{
    inputPrediction.x = clampValue(offset.x, minimumOffset.x, maximumOffset.x);
    inputPrediction.y = clampValue(offset.y, minimumOffset.y, maximumOffset.y);
}

ScrollSample::ScrollSample()
    : valid(false)
    , scrollEpoch(0)
    , sampleSequence(0)
    , throughInputSequence(0)
{
}

ScrollBarrier::ScrollBarrier()
    : valid(false)
    , sceneVersion(0)
    , scrollEpoch(0)
    , sampleSequence(0)
    , throughInputSequence(0)
{
}

ScrollController::MotionAxis::MotionAxis()
    : m_position(0.0)
    , m_target(0.0)
    , m_velocity(0.0)
    , m_lastTime(0.0)
    , m_minimum(-std::numeric_limits<double>::max())
    , m_maximum(std::numeric_limits<double>::max())
    , m_drawableMinimum(-std::numeric_limits<double>::max())
    , m_drawableMaximum(std::numeric_limits<double>::max())
    , m_initialized(false)
    , m_reanchorTime(false)
{
}

void ScrollController::MotionAxis::reset(double position, double monotonicTime)
{
    m_position = clampDrawable(position);
    m_target = m_position;
    m_velocity = 0.0;
    m_lastTime = monotonicTime;
    m_initialized = true;
    m_reanchorTime = false;
}

void ScrollController::MotionAxis::setBounds(double minimum, double maximum,
    double drawableMinimum, double drawableMaximum)
{
    const bool wasParked = m_initialized && !isActive()
        && std::fabs(m_target - m_position) >= kMovementEpsilon;
    m_minimum = std::min(minimum, maximum);
    m_maximum = std::max(minimum, maximum);
    m_drawableMinimum = std::max(m_minimum,
        std::min(drawableMinimum, drawableMaximum));
    m_drawableMaximum = std::min(m_maximum,
        std::max(drawableMinimum, drawableMaximum));
    m_position = clampDrawable(m_position);
    m_target = clamp(m_target);
    if (m_position == m_drawableMinimum || m_position == m_drawableMaximum)
        m_velocity = 0.0;
    if (wasParked && isActive())
        m_reanchorTime = true;
}

void ScrollController::MotionAxis::correctPosition(double position,
    double monotonicTime)
{
    m_position = clampDrawable(position);
    m_velocity = 0.0;
    m_lastTime = monotonicTime;
    m_reanchorTime = false;
}

void ScrollController::MotionAxis::addDelta(double delta,
    double monotonicTime, bool smooth)
{
    advance(monotonicTime);
    m_target = clamp(m_target + delta);
    if (smooth)
        return;

    m_position = drawableTarget();
    m_velocity = 0.0;
}

double ScrollController::MotionAxis::advance(double monotonicTime)
{
    if (!m_initialized) {
        reset(0.0, monotonicTime);
        return m_position;
    }

    if (m_reanchorTime) {
        m_lastTime = monotonicTime;
        m_reanchorTime = false;
        return m_position;
    }

    double elapsed = monotonicTime - m_lastTime;
    if (!isFinite(elapsed) || elapsed <= 0.0)
        return m_position;
    elapsed = std::min(elapsed, kMaximumIntegrationStep);
    m_lastTime = monotonicTime;

    const double target = drawableTarget();
    const double displacement = m_position - target;
    const double velocityTerm = m_velocity
        + kCriticalDampingFrequency * displacement;
    const double decay = std::exp(-kCriticalDampingFrequency * elapsed);
    m_position = target
        + (displacement + velocityTerm * elapsed) * decay;
    m_velocity = (m_velocity
        - kCriticalDampingFrequency * velocityTerm * elapsed) * decay;

    if (std::fabs(m_position - target) < kMovementEpsilon
        && std::fabs(m_velocity) < kMovementEpsilon) {
        m_position = target;
        m_velocity = 0.0;
    }

    const double clamped = clampDrawable(m_position);
    if (clamped != m_position) {
        m_position = clamped;
        m_velocity = 0.0;
    }
    return m_position;
}

bool ScrollController::MotionAxis::isActive() const
{
    return m_initialized
        && (std::fabs(m_position - drawableTarget()) >= kMovementEpsilon
            || std::fabs(m_velocity) >= kMovementEpsilon);
}

double ScrollController::MotionAxis::clamp(double value) const
{
    return clampValue(value, m_minimum, m_maximum);
}

double ScrollController::MotionAxis::drawableTarget() const
{
    return clampDrawable(m_target);
}

double ScrollController::MotionAxis::clampDrawable(double value) const
{
    return clampValue(value, m_drawableMinimum, m_drawableMaximum);
}

ScrollController::ScrollController()
    : m_hasActiveNode(false)
    , m_sampleSequence(0)
    , m_throughInputSequence(0)
{
}

bool ScrollController::activateScene(const ScrollNodeSnapshot& node)
{
    if (!node.isValid())
        return false;

    const bool sameEpoch = m_hasActiveNode
        && node.key == m_node.key
        && node.scrollEpoch == m_node.scrollEpoch;
    if (sameEpoch) {
        if (node.sceneVersion <= m_node.sceneVersion
            || node.acknowledgedSample < m_node.acknowledgedSample
            || node.acknowledgedSample > m_sampleSequence) {
            return false;
        }

        m_node = node;
        m_visualOffset = reconciledVisualOffset(m_consumedTotal);
        updateMotionBounds();
        return true;
    }

    m_node = node;
    m_hasActiveNode = true;
    m_consumedTotal = node.acknowledgedTotal;
    m_sampleSequence = node.acknowledgedSample;
    m_throughInputSequence = 0;
    m_visualOffset.x = clampValue(node.baseOffset.x,
        std::max(node.minimumOffset.x, node.coverageMinimum.x),
        std::min(node.maximumOffset.x, node.coverageMaximum.x));
    m_visualOffset.y = clampValue(node.baseOffset.y,
        std::max(node.minimumOffset.y, node.coverageMinimum.y),
        std::min(node.maximumOffset.y, node.coverageMaximum.y));
    updateMotionBounds();
    m_motionX.reset(m_consumedTotal.x, 0.0);
    m_motionY.reset(m_consumedTotal.y, 0.0);
    return true;
}

bool ScrollController::enqueueDelta(uint64_t inputSequence,
    const ScrollVector& delta, double monotonicTime, bool smooth)
{
    if (!compositorEligible() || !inputSequence
        || inputSequence <= m_throughInputSequence
        || !isFinite(delta) || !isFinite(monotonicTime)) {
        return false;
    }

    ScrollVector eligibleDelta = delta;
    if (!(m_node.inputAxes & ScrollInputAxisHorizontal))
        eligibleDelta.x = 0.0;
    if (!(m_node.inputAxes & ScrollInputAxisVertical))
        eligibleDelta.y = 0.0;
    if (eligibleDelta.x == 0.0 && eligibleDelta.y == 0.0)
        return false;

    if (eligibleDelta.x != 0.0)
        m_motionX.addDelta(eligibleDelta.x, monotonicTime, smooth);
    if (eligibleDelta.y != 0.0)
        m_motionY.addDelta(eligibleDelta.y, monotonicTime, smooth);
    m_throughInputSequence = inputSequence;
    return true;
}

ScrollSample ScrollController::sample(double monotonicTime)
{
    ScrollSample result;
    if (!compositorEligible() || !isFinite(monotonicTime))
        return result;

    const ScrollVector desired(m_motionX.advance(monotonicTime),
        m_motionY.advance(monotonicTime));
    const ScrollVector nextVisual = reconciledVisualOffset(desired);
    const ScrollVector realized(nextVisual.x - m_visualOffset.x,
        nextVisual.y - m_visualOffset.y);
    if (std::fabs(realized.x) < kMovementEpsilon
        && std::fabs(realized.y) < kMovementEpsilon) {
        return result;
    }

    m_consumedTotal.x += realized.x;
    m_consumedTotal.y += realized.y;
    m_visualOffset = nextVisual;

    // Reconciliation adds/subtracts the nonzero base and acknowledged offsets.
    // Its rounding error is not a real clamp and must not discard the smooth
    // target or velocity. A material position correction must also preserve
    // any accepted target still waiting for prepared coverage.
    if (std::fabs(m_consumedTotal.x - desired.x) >= kMovementEpsilon)
        m_motionX.correctPosition(m_consumedTotal.x, monotonicTime);
    if (std::fabs(m_consumedTotal.y - desired.y) >= kMovementEpsilon)
        m_motionY.correctPosition(m_consumedTotal.y, monotonicTime);

    result.valid = true;
    result.key = m_node.key;
    result.scrollEpoch = m_node.scrollEpoch;
    result.sampleSequence = ++m_sampleSequence;
    result.throughInputSequence = m_throughInputSequence;
    result.consumedTotal = m_consumedTotal;
    return result;
}

ScrollBarrier ScrollController::captureBarrier() const
{
    ScrollBarrier barrier;
    if (!m_hasActiveNode || !m_node.isValid())
        return barrier;

    barrier.valid = true;
    barrier.key = m_node.key;
    barrier.sceneVersion = m_node.sceneVersion;
    barrier.scrollEpoch = m_node.scrollEpoch;
    barrier.sampleSequence = m_sampleSequence;
    barrier.throughInputSequence = m_throughInputSequence;
    barrier.consumedTotal = m_consumedTotal;
    barrier.visualOffset = m_visualOffset;
    return barrier;
}

bool ScrollController::synchronizeBarrier(const ScrollBarrier& barrier,
    ScrollSample* synchronizedSample)
{
    if (!m_hasActiveNode || !barrier.valid
        || barrier.key != m_node.key
        // A main-thread acknowledgement can publish a newer scene before the
        // queued click reaches Blink. The captured state remains valid while
        // node identity and scroll epoch are unchanged; only a future barrier
        // is impossible here.
        || barrier.sceneVersion > m_node.sceneVersion
        || barrier.scrollEpoch != m_node.scrollEpoch
        || barrier.sampleSequence < m_node.acknowledgedSample
        || barrier.sampleSequence > m_sampleSequence
        || barrier.throughInputSequence > m_throughInputSequence
        || !isFinite(barrier.consumedTotal)
        || !isFinite(barrier.visualOffset)) {
        return false;
    }

    const ScrollVector reconciled = reconciledVisualOffset(
        barrier.consumedTotal);
    if (std::fabs(reconciled.x - barrier.visualOffset.x)
            > kMovementEpsilon
        || std::fabs(reconciled.y - barrier.visualOffset.y)
            > kMovementEpsilon) {
        return false;
    }

    m_consumedTotal = barrier.consumedTotal;
    m_visualOffset = barrier.visualOffset;
    m_throughInputSequence = barrier.throughInputSequence;
    m_sampleSequence = std::max(m_sampleSequence, barrier.sampleSequence);
    m_motionX.reset(m_consumedTotal.x, 0.0);
    m_motionY.reset(m_consumedTotal.y, 0.0);
    m_node.setInputPrediction(m_visualOffset);

    if (synchronizedSample) {
        synchronizedSample->valid = true;
        synchronizedSample->key = m_node.key;
        synchronizedSample->scrollEpoch = m_node.scrollEpoch;
        synchronizedSample->sampleSequence = ++m_sampleSequence;
        synchronizedSample->throughInputSequence = m_throughInputSequence;
        synchronizedSample->consumedTotal = m_consumedTotal;
    }
    return true;
}

void ScrollController::revokeEligibility(uint32_t reason)
{
    if (!m_hasActiveNode)
        return;
    m_node.mainThreadReasons |= reason;
    m_motionX.reset(m_consumedTotal.x, 0.0);
    m_motionY.reset(m_consumedTotal.y, 0.0);
}

bool ScrollController::compositorEligible() const
{
    return m_hasActiveNode && m_node.compositorEligible();
}

bool ScrollController::needsAnimationFrame() const
{
    return compositorEligible()
        && (m_motionX.isActive() || m_motionY.isActive());
}

void ScrollController::updateMotionBounds()
{
    const double minimumX = std::max(m_node.minimumOffset.x,
        m_node.coverageMinimum.x);
    const double maximumX = std::min(m_node.maximumOffset.x,
        m_node.coverageMaximum.x);
    const double minimumY = std::max(m_node.minimumOffset.y,
        m_node.coverageMinimum.y);
    const double maximumY = std::min(m_node.maximumOffset.y,
        m_node.coverageMaximum.y);

    m_motionX.setBounds(m_node.acknowledgedTotal.x
            + m_node.minimumOffset.x - m_node.baseOffset.x,
        m_node.acknowledgedTotal.x
            + m_node.maximumOffset.x - m_node.baseOffset.x,
        m_node.acknowledgedTotal.x
            + minimumX - m_node.baseOffset.x,
        m_node.acknowledgedTotal.x
            + maximumX - m_node.baseOffset.x);
    m_motionY.setBounds(m_node.acknowledgedTotal.y
            + m_node.minimumOffset.y - m_node.baseOffset.y,
        m_node.acknowledgedTotal.y
            + m_node.maximumOffset.y - m_node.baseOffset.y,
        m_node.acknowledgedTotal.y
            + minimumY - m_node.baseOffset.y,
        m_node.acknowledgedTotal.y
            + maximumY - m_node.baseOffset.y);
}

ScrollVector ScrollController::reconciledVisualOffset(
    const ScrollVector& consumed) const
{
    const double minimumX = std::max(m_node.minimumOffset.x,
        m_node.coverageMinimum.x);
    const double maximumX = std::min(m_node.maximumOffset.x,
        m_node.coverageMaximum.x);
    const double minimumY = std::max(m_node.minimumOffset.y,
        m_node.coverageMinimum.y);
    const double maximumY = std::min(m_node.maximumOffset.y,
        m_node.coverageMaximum.y);
    return ScrollVector(
        clampValue(m_node.baseOffset.x + consumed.x
                - m_node.acknowledgedTotal.x,
            minimumX, maximumX),
        clampValue(m_node.baseOffset.y + consumed.y
                - m_node.acknowledgedTotal.y,
            minimumY, maximumY));
}

} // namespace cc
