#ifndef CC_BLINK_WEB_COMPOSITOR_ANIMATION_IMPL_H_
#define CC_BLINK_WEB_COMPOSITOR_ANIMATION_IMPL_H_

#include "cc/blink/CompositorAnimationTimeline.h"
#include "third_party/WebKit/public/platform/WebCompositorAnimation.h"
#include "third_party/WebKit/public/platform/WebFloatAnimationCurve.h"
#include "third_party/WebKit/public/platform/WebTransformAnimationCurve.h"
#include "third_party/WebKit/public/platform/WebTransformOperations.h"

namespace cc_blink {

class WebFloatAnimationCurveImpl : public blink::WebFloatAnimationCurve {
public:
    WebFloatAnimationCurveImpl();
    ~WebFloatAnimationCurveImpl() override;

    blink::WebCompositorAnimationCurve::AnimationCurveType type() const override;
    void add(const blink::WebFloatKeyframe&) override;
    void add(const blink::WebFloatKeyframe&, TimingFunctionType) override;
    void add(const blink::WebFloatKeyframe&, double x1, double y1, double x2, double y2) override;
    void add(const blink::WebFloatKeyframe&, int steps, float stepsStartOffset) override;

    void setLinearTimingFunction() override;
    void setCubicBezierTimingFunction(TimingFunctionType) override;
    void setCubicBezierTimingFunction(double x1, double y1, double x2, double y2) override;
    void setStepsTimingFunction(int numberOfSteps, float stepsStartOffset) override;

    float getValue(double time) const override;
    const FloatAnimationCurve& nativeCurve() const { return m_curve; }

private:
    static TimingFunction convertTimingFunction(TimingFunctionType);

    FloatAnimationCurve m_curve;
};

class WebTransformOperationsImpl : public blink::WebTransformOperations {
public:
    WebTransformOperationsImpl();
    ~WebTransformOperationsImpl() override;

    bool canBlendWith(const blink::WebTransformOperations&) const override;
    void appendTranslate(double x, double y, double z) override;
    void appendRotate(double x, double y, double z, double degrees) override;
    void appendScale(double x, double y, double z) override;
    void appendSkew(double x, double y) override;
    void appendPerspective(double depth) override;
    void appendMatrix(const SkMatrix44&) override;
    void appendIdentity() override;
    bool isIdentity() const override;

    const TransformOperations& nativeOperations() const { return m_operations; }

private:
    TransformOperations m_operations;
};

class WebTransformAnimationCurveImpl : public blink::WebTransformAnimationCurve {
public:
    WebTransformAnimationCurveImpl();
    ~WebTransformAnimationCurveImpl() override;

    blink::WebCompositorAnimationCurve::AnimationCurveType type() const override;
    void add(const blink::WebTransformKeyframe&) override;
    void add(const blink::WebTransformKeyframe&, TimingFunctionType) override;
    void add(const blink::WebTransformKeyframe&, double x1, double y1,
        double x2, double y2) override;
    void add(const blink::WebTransformKeyframe&, int steps,
        float stepsStartOffset) override;

    void setLinearTimingFunction() override;
    void setCubicBezierTimingFunction(TimingFunctionType) override;
    void setCubicBezierTimingFunction(double x1, double y1,
        double x2, double y2) override;
    void setStepsTimingFunction(int numberOfSteps,
        float stepsStartOffset) override;

    const TransformAnimationCurve& nativeCurve() const { return m_curve; }

private:
    static TimingFunction convertTimingFunction(TimingFunctionType);
    static const TransformOperations& operationsOf(
        const blink::WebTransformKeyframe&);

    TransformAnimationCurve m_curve;
};

class WebCompositorAnimationImpl : public blink::WebCompositorAnimation {
public:
    WebCompositorAnimationImpl(const FloatAnimationCurve&, TargetProperty, int groupId, int animationId);
    WebCompositorAnimationImpl(const TransformAnimationCurve&, TargetProperty,
        int groupId, int animationId);
    ~WebCompositorAnimationImpl() override;

    int id() override;
    int group() override;
    TargetProperty targetProperty() const override;

    double iterations() const override;
    void setIterations(double) override;
    double startTime() const override;
    void setStartTime(double monotonicTime) override;
    double timeOffset() const override;
    void setTimeOffset(double) override;
    Direction direction() const override;
    void setDirection(Direction) override;
    double playbackRate() const override;
    void setPlaybackRate(double) override;
    FillMode fillMode() const override;
    void setFillMode(FillMode) override;
    double iterationStart() const override;
    void setIterationStart(double) override;

    void resolveStartTime(double monotonicTime);
    void pauseAt(double timeOffset);
    FloatAnimationSample sample(double monotonicTime) const;
    TransformAnimationSample sampleTransform(double monotonicTime) const;

    bool isSupported() const;

private:
    const AnimationPlaybackSpec& playback() const;
    AnimationPlaybackSpec& mutablePlayback();

    int m_id;
    int m_group;
    TargetProperty m_targetProperty;
    CompositorFloatAnimationTimeline m_floatTimeline;
    CompositorTransformAnimationTimeline m_transformTimeline;
};

} // namespace cc_blink

#endif // CC_BLINK_WEB_COMPOSITOR_ANIMATION_IMPL_H_
