#include "config.h"
#include "cc/blink/WebCompositorAnimationImpl.h"

#include <atomic>
#include <cmath>
#include <limits>

namespace cc_blink {

namespace {

std::atomic<int> g_nextAnimationId(1);
std::atomic<int> g_nextAnimationGroupId(1);

AnimationDirection toNativeDirection(blink::WebCompositorAnimation::Direction direction)
{
    switch (direction) {
    case blink::WebCompositorAnimation::DirectionReverse:
        return AnimationDirectionReverse;
    case blink::WebCompositorAnimation::DirectionAlternate:
        return AnimationDirectionAlternate;
    case blink::WebCompositorAnimation::DirectionAlternateReverse:
        return AnimationDirectionAlternateReverse;
    case blink::WebCompositorAnimation::DirectionNormal:
    default:
        return AnimationDirectionNormal;
    }
}

AnimationFillMode toNativeFillMode(blink::WebCompositorAnimation::FillMode fillMode)
{
    switch (fillMode) {
    case blink::WebCompositorAnimation::FillModeForwards:
        return AnimationFillModeForwards;
    case blink::WebCompositorAnimation::FillModeBackwards:
        return AnimationFillModeBackwards;
    case blink::WebCompositorAnimation::FillModeBoth:
        return AnimationFillModeBoth;
    case blink::WebCompositorAnimation::FillModeNone:
    default:
        return AnimationFillModeNone;
    }
}

} // namespace

WebFloatAnimationCurveImpl::WebFloatAnimationCurveImpl()
{
}

WebFloatAnimationCurveImpl::~WebFloatAnimationCurveImpl()
{
}

blink::WebCompositorAnimationCurve::AnimationCurveType WebFloatAnimationCurveImpl::type() const
{
    return blink::WebCompositorAnimationCurve::AnimationCurveTypeFloat;
}

TimingFunction WebFloatAnimationCurveImpl::convertTimingFunction(TimingFunctionType type)
{
    switch (type) {
    case TimingFunctionTypeEase:
        return TimingFunction::ease();
    case TimingFunctionTypeEaseIn:
        return TimingFunction::easeIn();
    case TimingFunctionTypeEaseOut:
        return TimingFunction::easeOut();
    case TimingFunctionTypeEaseInOut:
        return TimingFunction::easeInOut();
    case TimingFunctionTypeLinear:
    default:
        return TimingFunction::linear();
    }
}

void WebFloatAnimationCurveImpl::add(const blink::WebFloatKeyframe& keyframe)
{
    m_curve.addKeyframe(keyframe.time, keyframe.value, TimingFunction::ease());
}

void WebFloatAnimationCurveImpl::add(const blink::WebFloatKeyframe& keyframe,
    TimingFunctionType timing)
{
    m_curve.addKeyframe(keyframe.time, keyframe.value, convertTimingFunction(timing));
}

void WebFloatAnimationCurveImpl::add(const blink::WebFloatKeyframe& keyframe,
    double x1, double y1, double x2, double y2)
{
    m_curve.addKeyframe(keyframe.time, keyframe.value,
        TimingFunction::cubicBezier(x1, y1, x2, y2));
}

void WebFloatAnimationCurveImpl::add(const blink::WebFloatKeyframe& keyframe,
    int steps, float stepsStartOffset)
{
    m_curve.addKeyframe(keyframe.time, keyframe.value,
        TimingFunction::steps(steps, stepsStartOffset));
}

void WebFloatAnimationCurveImpl::setLinearTimingFunction()
{
    m_curve.setGlobalTimingFunction(TimingFunction::linear());
}

void WebFloatAnimationCurveImpl::setCubicBezierTimingFunction(TimingFunctionType timing)
{
    m_curve.setGlobalTimingFunction(convertTimingFunction(timing));
}

void WebFloatAnimationCurveImpl::setCubicBezierTimingFunction(
    double x1, double y1, double x2, double y2)
{
    m_curve.setGlobalTimingFunction(TimingFunction::cubicBezier(x1, y1, x2, y2));
}

void WebFloatAnimationCurveImpl::setStepsTimingFunction(int numberOfSteps,
    float stepsStartOffset)
{
    m_curve.setGlobalTimingFunction(TimingFunction::steps(numberOfSteps, stepsStartOffset));
}

float WebFloatAnimationCurveImpl::getValue(double time) const
{
    return m_curve.valueAt(time);
}

WebTransformOperationsImpl::WebTransformOperationsImpl()
{
}

WebTransformOperationsImpl::~WebTransformOperationsImpl()
{
}

bool WebTransformOperationsImpl::canBlendWith(
    const blink::WebTransformOperations& other) const
{
    const WebTransformOperationsImpl& otherImpl =
        static_cast<const WebTransformOperationsImpl&>(other);
    return m_operations.canBlendWith(otherImpl.m_operations);
}

void WebTransformOperationsImpl::appendTranslate(double x, double y, double z)
{
    m_operations.appendTranslate(x, y, z);
}

void WebTransformOperationsImpl::appendRotate(double, double, double, double)
{
    m_operations.appendUnsupported();
}

void WebTransformOperationsImpl::appendScale(double x, double y, double z)
{
    m_operations.appendScale(x, y, z);
}

void WebTransformOperationsImpl::appendSkew(double, double)
{
    m_operations.appendUnsupported();
}

void WebTransformOperationsImpl::appendPerspective(double)
{
    m_operations.appendUnsupported();
}

void WebTransformOperationsImpl::appendMatrix(const SkMatrix44&)
{
    m_operations.appendUnsupported();
}

void WebTransformOperationsImpl::appendIdentity()
{
    m_operations.appendIdentity();
}

bool WebTransformOperationsImpl::isIdentity() const
{
    return m_operations.isIdentity();
}

WebTransformAnimationCurveImpl::WebTransformAnimationCurveImpl()
{
}

WebTransformAnimationCurveImpl::~WebTransformAnimationCurveImpl()
{
}

blink::WebCompositorAnimationCurve::AnimationCurveType
WebTransformAnimationCurveImpl::type() const
{
    return blink::WebCompositorAnimationCurve::AnimationCurveTypeTransform;
}

TimingFunction WebTransformAnimationCurveImpl::convertTimingFunction(
    TimingFunctionType type)
{
    switch (type) {
    case TimingFunctionTypeEase:
        return TimingFunction::ease();
    case TimingFunctionTypeEaseIn:
        return TimingFunction::easeIn();
    case TimingFunctionTypeEaseOut:
        return TimingFunction::easeOut();
    case TimingFunctionTypeEaseInOut:
        return TimingFunction::easeInOut();
    case TimingFunctionTypeLinear:
    default:
        return TimingFunction::linear();
    }
}

const TransformOperations& WebTransformAnimationCurveImpl::operationsOf(
    const blink::WebTransformKeyframe& keyframe)
{
    const WebTransformOperationsImpl& operations =
        static_cast<const WebTransformOperationsImpl&>(keyframe.value());
    return operations.nativeOperations();
}

void WebTransformAnimationCurveImpl::add(
    const blink::WebTransformKeyframe& keyframe)
{
    m_curve.addKeyframe(keyframe.time(), operationsOf(keyframe),
        TimingFunction::ease());
}

void WebTransformAnimationCurveImpl::add(
    const blink::WebTransformKeyframe& keyframe, TimingFunctionType timing)
{
    m_curve.addKeyframe(keyframe.time(), operationsOf(keyframe),
        convertTimingFunction(timing));
}

void WebTransformAnimationCurveImpl::add(
    const blink::WebTransformKeyframe& keyframe,
    double x1, double y1, double x2, double y2)
{
    m_curve.addKeyframe(keyframe.time(), operationsOf(keyframe),
        TimingFunction::cubicBezier(x1, y1, x2, y2));
}

void WebTransformAnimationCurveImpl::add(
    const blink::WebTransformKeyframe& keyframe, int steps,
    float stepsStartOffset)
{
    m_curve.addKeyframe(keyframe.time(), operationsOf(keyframe),
        TimingFunction::steps(steps, stepsStartOffset));
}

void WebTransformAnimationCurveImpl::setLinearTimingFunction()
{
    m_curve.setGlobalTimingFunction(TimingFunction::linear());
}

void WebTransformAnimationCurveImpl::setCubicBezierTimingFunction(
    TimingFunctionType timing)
{
    m_curve.setGlobalTimingFunction(convertTimingFunction(timing));
}

void WebTransformAnimationCurveImpl::setCubicBezierTimingFunction(
    double x1, double y1, double x2, double y2)
{
    m_curve.setGlobalTimingFunction(
        TimingFunction::cubicBezier(x1, y1, x2, y2));
}

void WebTransformAnimationCurveImpl::setStepsTimingFunction(
    int numberOfSteps, float stepsStartOffset)
{
    m_curve.setGlobalTimingFunction(
        TimingFunction::steps(numberOfSteps, stepsStartOffset));
}

WebCompositorAnimationImpl::WebCompositorAnimationImpl(const FloatAnimationCurve& curve,
    TargetProperty targetProperty, int groupId, int animationId)
    : m_id(animationId ? animationId : g_nextAnimationId.fetch_add(1))
    , m_group(groupId ? groupId : g_nextAnimationGroupId.fetch_add(1))
    , m_targetProperty(targetProperty)
{
    AnimationPlaybackSpec playback;
    playback.startTime = std::numeric_limits<double>::quiet_NaN();
    m_floatTimeline = CompositorFloatAnimationTimeline(curve, playback);
}

WebCompositorAnimationImpl::WebCompositorAnimationImpl(
    const TransformAnimationCurve& curve, TargetProperty targetProperty,
    int groupId, int animationId)
    : m_id(animationId ? animationId : g_nextAnimationId.fetch_add(1))
    , m_group(groupId ? groupId : g_nextAnimationGroupId.fetch_add(1))
    , m_targetProperty(targetProperty)
{
    AnimationPlaybackSpec playback;
    playback.startTime = std::numeric_limits<double>::quiet_NaN();
    m_transformTimeline = CompositorTransformAnimationTimeline(curve, playback);
}

WebCompositorAnimationImpl::~WebCompositorAnimationImpl()
{
}

int WebCompositorAnimationImpl::id() { return m_id; }
int WebCompositorAnimationImpl::group() { return m_group; }
blink::WebCompositorAnimation::TargetProperty WebCompositorAnimationImpl::targetProperty() const { return m_targetProperty; }

const AnimationPlaybackSpec& WebCompositorAnimationImpl::playback() const
{
    return m_targetProperty == TargetPropertyTransform
        ? m_transformTimeline.playback() : m_floatTimeline.playback();
}

AnimationPlaybackSpec& WebCompositorAnimationImpl::mutablePlayback()
{
    return m_targetProperty == TargetPropertyTransform
        ? m_transformTimeline.mutablePlayback() : m_floatTimeline.mutablePlayback();
}

double WebCompositorAnimationImpl::iterations() const { return playback().iterations; }
void WebCompositorAnimationImpl::setIterations(double value) { mutablePlayback().iterations = value; }
double WebCompositorAnimationImpl::startTime() const { return playback().startTime; }
void WebCompositorAnimationImpl::setStartTime(double value) { mutablePlayback().startTime = value; }
double WebCompositorAnimationImpl::timeOffset() const { return playback().timeOffset; }
void WebCompositorAnimationImpl::setTimeOffset(double value) { mutablePlayback().timeOffset = value; }

blink::WebCompositorAnimation::Direction WebCompositorAnimationImpl::direction() const
{
    return static_cast<Direction>(playback().direction);
}

void WebCompositorAnimationImpl::setDirection(Direction value)
{
    mutablePlayback().direction = toNativeDirection(value);
}

double WebCompositorAnimationImpl::playbackRate() const { return playback().playbackRate; }
void WebCompositorAnimationImpl::setPlaybackRate(double value) { mutablePlayback().playbackRate = value; }

blink::WebCompositorAnimation::FillMode WebCompositorAnimationImpl::fillMode() const
{
    return static_cast<FillMode>(playback().fillMode);
}

void WebCompositorAnimationImpl::setFillMode(FillMode value)
{
    mutablePlayback().fillMode = toNativeFillMode(value);
}

double WebCompositorAnimationImpl::iterationStart() const { return playback().iterationStart; }
void WebCompositorAnimationImpl::setIterationStart(double value) { mutablePlayback().iterationStart = value; }

void WebCompositorAnimationImpl::resolveStartTime(double monotonicTime)
{
    if (std::isnan(playback().startTime))
        mutablePlayback().startTime = monotonicTime;
}

void WebCompositorAnimationImpl::pauseAt(double timeOffset)
{
    if (m_targetProperty == TargetPropertyTransform)
        m_transformTimeline.pauseAt(timeOffset);
    else
        m_floatTimeline.pauseAt(timeOffset);
}

FloatAnimationSample WebCompositorAnimationImpl::sample(double monotonicTime) const
{
    return m_floatTimeline.sample(monotonicTime);
}

TransformAnimationSample WebCompositorAnimationImpl::sampleTransform(
    double monotonicTime) const
{
    return m_transformTimeline.sample(monotonicTime);
}

bool WebCompositorAnimationImpl::isSupported() const
{
    return m_targetProperty == TargetPropertyOpacity
        || (m_targetProperty == TargetPropertyTransform
            && m_transformTimeline.curve().isSupported());
}

} // namespace cc_blink
