#include "cc/blink/CompositorAnimationTimeline.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cc_blink {

namespace {

double clampUnit(double value)
{
    return std::max(0.0, std::min(1.0, value));
}

double cubicCoordinate(double t, double p1, double p2)
{
    const double oneMinusT = 1.0 - t;
    return 3.0 * oneMinusT * oneMinusT * t * p1
        + 3.0 * oneMinusT * t * t * p2
        + t * t * t;
}

double cubicDerivative(double t, double p1, double p2)
{
    const double oneMinusT = 1.0 - t;
    return 3.0 * oneMinusT * oneMinusT * p1
        + 6.0 * oneMinusT * t * (p2 - p1)
        + 3.0 * t * t * (1.0 - p2);
}

double solveCubicBezier(double progress, double x1, double x2)
{
    double parameter = progress;
    for (int i = 0; i < 8; ++i) {
        const double error = cubicCoordinate(parameter, x1, x2) - progress;
        if (std::fabs(error) < 1e-7)
            return parameter;

        const double derivative = cubicDerivative(parameter, x1, x2);
        if (std::fabs(derivative) < 1e-7)
            break;
        parameter = clampUnit(parameter - error / derivative);
    }

    double low = 0.0;
    double high = 1.0;
    for (int i = 0; i < 20; ++i) {
        parameter = (low + high) * 0.5;
        if (cubicCoordinate(parameter, x1, x2) < progress)
            low = parameter;
        else
            high = parameter;
    }
    return (low + high) * 0.5;
}

bool fillsBackwards(AnimationFillMode fillMode)
{
    return fillMode == AnimationFillModeBackwards || fillMode == AnimationFillModeBoth;
}

bool fillsForwards(AnimationFillMode fillMode)
{
    return fillMode == AnimationFillModeForwards || fillMode == AnimationFillModeBoth;
}

} // namespace

TimingFunction::TimingFunction()
    : type(TimingFunctionLinear)
    , x1(0.0)
    , y1(0.0)
    , x2(1.0)
    , y2(1.0)
    , stepCount(1)
    , stepStartOffset(0.0f)
{
}

TimingFunction TimingFunction::linear()
{
    return TimingFunction();
}

TimingFunction TimingFunction::ease()
{
    return cubicBezier(0.25, 0.1, 0.25, 1.0);
}

TimingFunction TimingFunction::easeIn()
{
    return cubicBezier(0.42, 0.0, 1.0, 1.0);
}

TimingFunction TimingFunction::easeOut()
{
    return cubicBezier(0.0, 0.0, 0.58, 1.0);
}

TimingFunction TimingFunction::easeInOut()
{
    return cubicBezier(0.42, 0.0, 0.58, 1.0);
}

TimingFunction TimingFunction::cubicBezier(double controlX1, double controlY1,
    double controlX2, double controlY2)
{
    TimingFunction result;
    result.type = TimingFunctionCubicBezier;
    result.x1 = controlX1;
    result.y1 = controlY1;
    result.x2 = controlX2;
    result.y2 = controlY2;
    return result;
}

TimingFunction TimingFunction::steps(int count, float startOffset)
{
    TimingFunction result;
    result.type = TimingFunctionSteps;
    result.stepCount = std::max(1, count);
    result.stepStartOffset = std::max(0.0f, std::min(1.0f, startOffset));
    return result;
}

double TimingFunction::transform(double progress) const
{
    progress = clampUnit(progress);
    if (type != TimingFunctionSteps && (progress == 0.0 || progress == 1.0))
        return progress;

    if (type == TimingFunctionCubicBezier) {
        const double parameter = solveCubicBezier(progress, x1, x2);
        return cubicCoordinate(parameter, y1, y2);
    }

    if (type == TimingFunctionSteps) {
        const double scaled = progress * stepCount;
        const double stepped = std::floor(scaled + stepStartOffset);
        return clampUnit(stepped / stepCount);
    }

    return progress;
}

FloatAnimationKeyframe::FloatAnimationKeyframe(double keyTime, float keyValue,
    const TimingFunction& keyTiming)
    : time(keyTime)
    , value(keyValue)
    , timing(keyTiming)
{
}

FloatAnimationCurve::FloatAnimationCurve()
{
}

void FloatAnimationCurve::addKeyframe(double time, float value, const TimingFunction& timing)
{
    FloatAnimationKeyframe keyframe(time, value, timing);
    std::vector<FloatAnimationKeyframe>::iterator insertion = m_keyframes.begin();
    while (insertion != m_keyframes.end() && insertion->time <= time)
        ++insertion;
    m_keyframes.insert(insertion, keyframe);
}

void FloatAnimationCurve::setGlobalTimingFunction(const TimingFunction& timing)
{
    m_globalTiming = timing;
}

double FloatAnimationCurve::duration() const
{
    if (m_keyframes.size() < 2)
        return 0.0;
    return std::max(0.0, m_keyframes.back().time - m_keyframes.front().time);
}

bool FloatAnimationCurve::isEmpty() const
{
    return m_keyframes.empty();
}

float FloatAnimationCurve::valueAt(double time) const
{
    if (m_keyframes.empty())
        return 0.0f;
    if (m_keyframes.size() == 1 || time <= m_keyframes.front().time)
        return m_keyframes.front().value;
    if (time >= m_keyframes.back().time)
        return m_keyframes.back().value;

    const double curveDuration = duration();
    if (curveDuration > 0.0) {
        const double globalProgress = (time - m_keyframes.front().time) / curveDuration;
        time = m_keyframes.front().time
            + m_globalTiming.transform(globalProgress) * curveDuration;
    }

    for (size_t i = 1; i < m_keyframes.size(); ++i) {
        const FloatAnimationKeyframe& upper = m_keyframes[i];
        if (time > upper.time)
            continue;

        const FloatAnimationKeyframe& lower = m_keyframes[i - 1];
        const double interval = upper.time - lower.time;
        if (interval <= 0.0)
            return upper.value;

        double progress = (time - lower.time) / interval;
        progress = lower.timing.transform(progress);
        return static_cast<float>(lower.value + (upper.value - lower.value) * progress);
    }

    return m_keyframes.back().value;
}

TransformOperationValue::TransformOperationValue()
    : type(Identity)
    , x(0.0)
    , y(0.0)
    , z(0.0)
{
}

TransformOperationValue::TransformOperationValue(Type operationType,
    double operationX, double operationY, double operationZ)
    : type(operationType)
    , x(operationX)
    , y(operationY)
    , z(operationZ)
{
}

TransformMatrix2D::TransformMatrix2D()
    : m11(1.0)
    , m12(0.0)
    , m21(0.0)
    , m22(1.0)
    , tx(0.0)
    , ty(0.0)
    , valid(true)
{
}

void TransformMatrix2D::mapPoint(double x, double y,
    double* mappedX, double* mappedY) const
{
    if (mappedX)
        *mappedX = m11 * x + m21 * y + tx;
    if (mappedY)
        *mappedY = m12 * x + m22 * y + ty;
}

TransformOperations::TransformOperations()
    : m_supported(true)
{
}

void TransformOperations::appendTranslate(double x, double y, double z)
{
    m_operations.push_back(TransformOperationValue(
        TransformOperationValue::Translate, x, y, z));
    if (z != 0.0)
        m_supported = false;
}

void TransformOperations::appendScale(double x, double y, double z)
{
    m_operations.push_back(TransformOperationValue(
        TransformOperationValue::Scale, x, y, z));
    if (z != 1.0)
        m_supported = false;
}

void TransformOperations::appendIdentity()
{
    m_operations.push_back(TransformOperationValue(
        TransformOperationValue::Identity, 0.0, 0.0, 0.0));
}

void TransformOperations::appendUnsupported()
{
    m_operations.push_back(TransformOperationValue(
        TransformOperationValue::Unsupported, 0.0, 0.0, 0.0));
    m_supported = false;
}

bool TransformOperations::isIdentity() const
{
    for (size_t i = 0; i < m_operations.size(); ++i) {
        const TransformOperationValue& operation = m_operations[i];
        if (operation.type == TransformOperationValue::Identity)
            continue;
        if (operation.type == TransformOperationValue::Translate
            && operation.x == 0.0 && operation.y == 0.0 && operation.z == 0.0)
            continue;
        if (operation.type == TransformOperationValue::Scale
            && operation.x == 1.0 && operation.y == 1.0 && operation.z == 1.0)
            continue;
        return false;
    }
    return true;
}

bool TransformOperations::isSupported() const
{
    return m_supported;
}

bool TransformOperations::canBlendWith(const TransformOperations& other) const
{
    if (!m_supported || !other.m_supported)
        return false;
    if (isIdentity() || other.isIdentity())
        return true;
    if (m_operations.size() != other.m_operations.size())
        return false;

    for (size_t i = 0; i < m_operations.size(); ++i) {
        const TransformOperationValue::Type fromType = m_operations[i].type;
        const TransformOperationValue::Type toType = other.m_operations[i].type;
        if (fromType != toType
            && fromType != TransformOperationValue::Identity
            && toType != TransformOperationValue::Identity)
            return false;
    }
    return true;
}

static TransformOperationValue neutralOperation(
    TransformOperationValue::Type type)
{
    if (type == TransformOperationValue::Scale)
        return TransformOperationValue(type, 1.0, 1.0, 1.0);
    if (type == TransformOperationValue::Translate)
        return TransformOperationValue(type, 0.0, 0.0, 0.0);
    return TransformOperationValue();
}

static TransformOperationValue blendOperation(
    const TransformOperationValue& from,
    const TransformOperationValue& to,
    double progress)
{
    TransformOperationValue::Type type = from.type;
    if (type == TransformOperationValue::Identity)
        type = to.type;

    const TransformOperationValue actualFrom = from.type == TransformOperationValue::Identity
        ? neutralOperation(type) : from;
    const TransformOperationValue actualTo = to.type == TransformOperationValue::Identity
        ? neutralOperation(type) : to;
    return TransformOperationValue(type,
        actualFrom.x + (actualTo.x - actualFrom.x) * progress,
        actualFrom.y + (actualTo.y - actualFrom.y) * progress,
        actualFrom.z + (actualTo.z - actualFrom.z) * progress);
}

TransformOperations TransformOperations::blend(const TransformOperations& to,
    double progress) const
{
    TransformOperations result;
    if (!canBlendWith(to)) {
        result.appendUnsupported();
        return result;
    }

    if (isIdentity() && to.isIdentity()) {
        result.appendIdentity();
        return result;
    }

    if (isIdentity()) {
        for (size_t i = 0; i < to.m_operations.size(); ++i) {
            const TransformOperationValue neutral = neutralOperation(to.m_operations[i].type);
            result.m_operations.push_back(blendOperation(neutral, to.m_operations[i], progress));
        }
        return result;
    }

    if (to.isIdentity()) {
        for (size_t i = 0; i < m_operations.size(); ++i) {
            const TransformOperationValue neutral = neutralOperation(m_operations[i].type);
            result.m_operations.push_back(blendOperation(m_operations[i], neutral, progress));
        }
        return result;
    }

    for (size_t i = 0; i < m_operations.size(); ++i)
        result.m_operations.push_back(blendOperation(m_operations[i], to.m_operations[i], progress));
    return result;
}

TransformMatrix2D TransformOperations::matrix2d() const
{
    TransformMatrix2D result;
    for (size_t i = 0; i < m_operations.size(); ++i) {
        const TransformOperationValue& operation = m_operations[i];
        switch (operation.type) {
        case TransformOperationValue::Identity:
            break;
        case TransformOperationValue::Translate:
            if (operation.z != 0.0) {
                result.valid = false;
                return result;
            }
            result.tx += result.m11 * operation.x + result.m21 * operation.y;
            result.ty += result.m12 * operation.x + result.m22 * operation.y;
            break;
        case TransformOperationValue::Scale:
            if (operation.z != 1.0) {
                result.valid = false;
                return result;
            }
            result.m11 *= operation.x;
            result.m12 *= operation.x;
            result.m21 *= operation.y;
            result.m22 *= operation.y;
            break;
        case TransformOperationValue::Unsupported:
            result.valid = false;
            return result;
        }
    }
    return result;
}

const TransformOperationValue& TransformOperations::operationAt(size_t index) const
{
    static const TransformOperationValue unsupported(
        TransformOperationValue::Unsupported, 0.0, 0.0, 0.0);
    return index < m_operations.size() ? m_operations[index] : unsupported;
}

TransformAnimationKeyframe::TransformAnimationKeyframe(double keyTime,
    const TransformOperations& keyValue, const TimingFunction& keyTiming)
    : time(keyTime)
    , value(keyValue)
    , timing(keyTiming)
{
}

TransformAnimationCurve::TransformAnimationCurve()
{
}

void TransformAnimationCurve::addKeyframe(double time,
    const TransformOperations& value, const TimingFunction& timing)
{
    TransformAnimationKeyframe keyframe(time, value, timing);
    std::vector<TransformAnimationKeyframe>::iterator insertion = m_keyframes.begin();
    while (insertion != m_keyframes.end() && insertion->time <= time)
        ++insertion;
    m_keyframes.insert(insertion, keyframe);
}

void TransformAnimationCurve::setGlobalTimingFunction(const TimingFunction& timing)
{
    m_globalTiming = timing;
}

double TransformAnimationCurve::duration() const
{
    if (m_keyframes.size() < 2)
        return 0.0;
    return std::max(0.0, m_keyframes.back().time - m_keyframes.front().time);
}

bool TransformAnimationCurve::isEmpty() const
{
    return m_keyframes.empty();
}

bool TransformAnimationCurve::isSupported() const
{
    if (m_keyframes.empty())
        return false;
    for (size_t i = 0; i < m_keyframes.size(); ++i) {
        if (!m_keyframes[i].value.isSupported())
            return false;
        if (i && !m_keyframes[i - 1].value.canBlendWith(m_keyframes[i].value))
            return false;
    }
    return true;
}

TransformOperations TransformAnimationCurve::valueAt(double time) const
{
    if (m_keyframes.empty()) {
        TransformOperations unsupported;
        unsupported.appendUnsupported();
        return unsupported;
    }
    if (m_keyframes.size() == 1 || time <= m_keyframes.front().time)
        return m_keyframes.front().value;
    if (time >= m_keyframes.back().time)
        return m_keyframes.back().value;

    const double curveDuration = duration();
    if (curveDuration > 0.0) {
        const double globalProgress = (time - m_keyframes.front().time) / curveDuration;
        time = m_keyframes.front().time
            + m_globalTiming.transform(globalProgress) * curveDuration;
    }

    for (size_t i = 1; i < m_keyframes.size(); ++i) {
        const TransformAnimationKeyframe& upper = m_keyframes[i];
        if (time > upper.time)
            continue;

        const TransformAnimationKeyframe& lower = m_keyframes[i - 1];
        const double interval = upper.time - lower.time;
        if (interval <= 0.0)
            return upper.value;

        double progress = (time - lower.time) / interval;
        progress = lower.timing.transform(progress);
        return lower.value.blend(upper.value, progress);
    }

    return m_keyframes.back().value;
}

AnimationPlaybackSpec::AnimationPlaybackSpec()
    : startTime(0.0)
    , timeOffset(0.0)
    , iterations(1.0)
    , iterationStart(0.0)
    , playbackRate(1.0)
    , direction(AnimationDirectionNormal)
    , fillMode(AnimationFillModeNone)
{
}

static void terminalIterationState(const AnimationPlaybackSpec& playback,
    unsigned* iteration, double* progress)
{
    const double terminalPosition = std::max(0.0, playback.iterationStart)
        + std::max(0.0, playback.iterations);
    if (terminalPosition <= 0.0) {
        *iteration = 0;
        *progress = 0.0;
        return;
    }

    const double wholeIterations = std::floor(terminalPosition);
    const double fractionalProgress = terminalPosition - wholeIterations;
    if (fractionalProgress == 0.0) {
        *iteration = static_cast<unsigned>(wholeIterations - 1.0);
        *progress = 1.0;
        return;
    }

    *iteration = static_cast<unsigned>(wholeIterations);
    *progress = fractionalProgress;
}

FloatAnimationSample::FloatAnimationSample()
    : affectsOutput(false)
    , finished(false)
    , value(0.0f)
{
}

CompositorFloatAnimationTimeline::CompositorFloatAnimationTimeline()
    : m_paused(false)
    , m_pauseOffset(0.0)
{
}

CompositorFloatAnimationTimeline::CompositorFloatAnimationTimeline(
    const FloatAnimationCurve& curve, const AnimationPlaybackSpec& playback)
    : m_curve(curve)
    , m_playback(playback)
    , m_paused(false)
    , m_pauseOffset(0.0)
{
}

double CompositorFloatAnimationTimeline::directedProgress(double progress,
    unsigned iteration) const
{
    bool reverse = false;
    switch (m_playback.direction) {
    case AnimationDirectionReverse:
        reverse = true;
        break;
    case AnimationDirectionAlternate:
        reverse = (iteration % 2) != 0;
        break;
    case AnimationDirectionAlternateReverse:
        reverse = (iteration % 2) == 0;
        break;
    case AnimationDirectionNormal:
        break;
    }
    return reverse ? 1.0 - progress : progress;
}

FloatAnimationSample CompositorFloatAnimationTimeline::sampleAtActiveTime(double activeTime) const
{
    FloatAnimationSample result;
    const double duration = m_curve.duration();
    if (m_curve.isEmpty())
        return result;

    if (duration <= 0.0) {
        result.affectsOutput = true;
        result.finished = true;
        result.value = m_curve.valueAt(0.0);
        return result;
    }

    const double iterationPosition = activeTime / duration + m_playback.iterationStart;
    const double flooredIteration = std::floor(std::max(0.0, iterationPosition));
    const unsigned iteration = static_cast<unsigned>(flooredIteration);
    double progress = iterationPosition - flooredIteration;
    progress = directedProgress(clampUnit(progress), iteration);

    result.affectsOutput = true;
    result.value = m_curve.valueAt(progress * duration);
    return result;
}

FloatAnimationSample CompositorFloatAnimationTimeline::sample(double monotonicTime) const
{
    FloatAnimationSample result;
    if (m_curve.isEmpty())
        return result;

    const double activeTime = m_paused
        ? m_pauseOffset
        : (monotonicTime - m_playback.startTime) * m_playback.playbackRate
            + m_playback.timeOffset;
    const double duration = m_curve.duration();
    const bool infinite = m_playback.iterations < 0.0
        || !std::isfinite(m_playback.iterations);
    const double activeDuration = infinite
        ? std::numeric_limits<double>::infinity()
        : duration * std::max(0.0, m_playback.iterations);

    if (activeTime < 0.0) {
        if (!fillsBackwards(m_playback.fillMode))
            return result;
        return sampleAtActiveTime(0.0);
    }

    if (!infinite && activeTime >= activeDuration) {
        result.finished = true;
        if (!fillsForwards(m_playback.fillMode))
            return result;

        unsigned terminalIteration = 0;
        double terminalProgress = 0.0;
        terminalIterationState(m_playback, &terminalIteration, &terminalProgress);
        terminalProgress = directedProgress(terminalProgress, terminalIteration);

        result.affectsOutput = true;
        result.value = m_curve.valueAt(terminalProgress * duration);
        return result;
    }

    return sampleAtActiveTime(activeTime);
}

void CompositorFloatAnimationTimeline::pauseAt(double timeOffset)
{
    m_paused = true;
    m_pauseOffset = std::max(0.0, timeOffset);
}

void CompositorFloatAnimationTimeline::resumeAt(double monotonicTime)
{
    if (!m_paused)
        return;

    const double rate = m_playback.playbackRate == 0.0 ? 1.0 : m_playback.playbackRate;
    m_playback.startTime = monotonicTime - (m_pauseOffset - m_playback.timeOffset) / rate;
    m_paused = false;
}

TransformAnimationSample::TransformAnimationSample()
    : affectsOutput(false)
    , finished(false)
{
}

CompositorTransformAnimationTimeline::CompositorTransformAnimationTimeline()
    : m_paused(false)
    , m_pauseOffset(0.0)
{
}

CompositorTransformAnimationTimeline::CompositorTransformAnimationTimeline(
    const TransformAnimationCurve& curve, const AnimationPlaybackSpec& playback)
    : m_curve(curve)
    , m_playback(playback)
    , m_paused(false)
    , m_pauseOffset(0.0)
{
}

double CompositorTransformAnimationTimeline::directedProgress(double progress,
    unsigned iteration) const
{
    bool reverse = false;
    switch (m_playback.direction) {
    case AnimationDirectionReverse:
        reverse = true;
        break;
    case AnimationDirectionAlternate:
        reverse = (iteration % 2) != 0;
        break;
    case AnimationDirectionAlternateReverse:
        reverse = (iteration % 2) == 0;
        break;
    case AnimationDirectionNormal:
        break;
    }
    return reverse ? 1.0 - progress : progress;
}

TransformAnimationSample CompositorTransformAnimationTimeline::sampleAtActiveTime(
    double activeTime) const
{
    TransformAnimationSample result;
    const double duration = m_curve.duration();
    if (m_curve.isEmpty() || !m_curve.isSupported())
        return result;

    if (duration <= 0.0) {
        result.affectsOutput = true;
        result.finished = true;
        result.value = m_curve.valueAt(0.0);
        return result;
    }

    const double iterationPosition = activeTime / duration + m_playback.iterationStart;
    const double flooredIteration = std::floor(std::max(0.0, iterationPosition));
    const unsigned iteration = static_cast<unsigned>(flooredIteration);
    double progress = iterationPosition - flooredIteration;
    progress = directedProgress(clampUnit(progress), iteration);

    result.affectsOutput = true;
    result.value = m_curve.valueAt(progress * duration);
    return result;
}

TransformAnimationSample CompositorTransformAnimationTimeline::sample(
    double monotonicTime) const
{
    TransformAnimationSample result;
    if (m_curve.isEmpty() || !m_curve.isSupported())
        return result;

    const double activeTime = m_paused
        ? m_pauseOffset
        : (monotonicTime - m_playback.startTime) * m_playback.playbackRate
            + m_playback.timeOffset;
    const double duration = m_curve.duration();
    const bool infinite = m_playback.iterations < 0.0
        || !std::isfinite(m_playback.iterations);
    const double activeDuration = infinite
        ? std::numeric_limits<double>::infinity()
        : duration * std::max(0.0, m_playback.iterations);

    if (activeTime < 0.0) {
        if (!fillsBackwards(m_playback.fillMode))
            return result;
        return sampleAtActiveTime(0.0);
    }

    if (!infinite && activeTime >= activeDuration) {
        result.finished = true;
        if (!fillsForwards(m_playback.fillMode))
            return result;

        unsigned terminalIteration = 0;
        double terminalProgress = 0.0;
        terminalIterationState(m_playback, &terminalIteration, &terminalProgress);
        terminalProgress = directedProgress(terminalProgress, terminalIteration);

        result.affectsOutput = true;
        result.value = m_curve.valueAt(terminalProgress * duration);
        return result;
    }

    return sampleAtActiveTime(activeTime);
}

void CompositorTransformAnimationTimeline::pauseAt(double timeOffset)
{
    m_paused = true;
    m_pauseOffset = std::max(0.0, timeOffset);
}

void CompositorTransformAnimationTimeline::resumeAt(double monotonicTime)
{
    if (!m_paused)
        return;

    const double rate = m_playback.playbackRate == 0.0 ? 1.0 : m_playback.playbackRate;
    m_playback.startTime = monotonicTime - (m_pauseOffset - m_playback.timeOffset) / rate;
    m_paused = false;
}

} // namespace cc_blink
