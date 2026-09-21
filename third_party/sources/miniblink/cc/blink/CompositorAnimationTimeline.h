#ifndef CC_BLINK_COMPOSITOR_ANIMATION_TIMELINE_H_
#define CC_BLINK_COMPOSITOR_ANIMATION_TIMELINE_H_

#include <cstddef>
#include <vector>

namespace cc_blink {

enum TimingFunctionType {
    TimingFunctionLinear,
    TimingFunctionCubicBezier,
    TimingFunctionSteps
};

struct TimingFunction {
    TimingFunction();

    static TimingFunction linear();
    static TimingFunction ease();
    static TimingFunction easeIn();
    static TimingFunction easeOut();
    static TimingFunction easeInOut();
    static TimingFunction cubicBezier(double x1, double y1, double x2, double y2);
    static TimingFunction steps(int count, float startOffset);

    double transform(double progress) const;

    TimingFunctionType type;
    double x1;
    double y1;
    double x2;
    double y2;
    int stepCount;
    float stepStartOffset;
};

struct FloatAnimationKeyframe {
    FloatAnimationKeyframe(double keyTime, float keyValue, const TimingFunction& keyTiming);

    double time;
    float value;
    TimingFunction timing;
};

class FloatAnimationCurve {
public:
    FloatAnimationCurve();

    void addKeyframe(double time, float value, const TimingFunction& timing);
    void setGlobalTimingFunction(const TimingFunction& timing);
    double duration() const;
    bool isEmpty() const;
    float valueAt(double time) const;

private:
    std::vector<FloatAnimationKeyframe> m_keyframes;
    TimingFunction m_globalTiming;
};

struct TransformOperationValue {
    enum Type {
        Identity,
        Translate,
        Scale,
        Unsupported
    };

    TransformOperationValue();
    TransformOperationValue(Type operationType, double operationX,
        double operationY, double operationZ);

    Type type;
    double x;
    double y;
    double z;
};

struct TransformMatrix2D {
    TransformMatrix2D();

    void mapPoint(double x, double y, double* mappedX, double* mappedY) const;

    double m11;
    double m12;
    double m21;
    double m22;
    double tx;
    double ty;
    bool valid;
};

class TransformOperations {
public:
    TransformOperations();

    void appendTranslate(double x, double y, double z);
    void appendScale(double x, double y, double z);
    void appendIdentity();
    void appendUnsupported();

    bool isIdentity() const;
    bool isSupported() const;
    bool canBlendWith(const TransformOperations&) const;
    TransformOperations blend(const TransformOperations& to, double progress) const;
    TransformMatrix2D matrix2d() const;

    size_t size() const { return m_operations.size(); }
    const TransformOperationValue& operationAt(size_t index) const;

private:
    std::vector<TransformOperationValue> m_operations;
    bool m_supported;
};

struct TransformAnimationKeyframe {
    TransformAnimationKeyframe(double keyTime, const TransformOperations& keyValue,
        const TimingFunction& keyTiming);

    double time;
    TransformOperations value;
    TimingFunction timing;
};

class TransformAnimationCurve {
public:
    TransformAnimationCurve();

    void addKeyframe(double time, const TransformOperations& value,
        const TimingFunction& timing);
    void setGlobalTimingFunction(const TimingFunction& timing);
    double duration() const;
    bool isEmpty() const;
    bool isSupported() const;
    TransformOperations valueAt(double time) const;

private:
    std::vector<TransformAnimationKeyframe> m_keyframes;
    TimingFunction m_globalTiming;
};

enum AnimationDirection {
    AnimationDirectionNormal,
    AnimationDirectionReverse,
    AnimationDirectionAlternate,
    AnimationDirectionAlternateReverse
};

enum AnimationFillMode {
    AnimationFillModeNone,
    AnimationFillModeForwards,
    AnimationFillModeBackwards,
    AnimationFillModeBoth
};

struct AnimationPlaybackSpec {
    AnimationPlaybackSpec();

    double startTime;
    double timeOffset;
    double iterations;
    double iterationStart;
    double playbackRate;
    AnimationDirection direction;
    AnimationFillMode fillMode;
};

struct FloatAnimationSample {
    FloatAnimationSample();

    bool affectsOutput;
    bool finished;
    float value;
};

class CompositorFloatAnimationTimeline {
public:
    CompositorFloatAnimationTimeline();
    CompositorFloatAnimationTimeline(const FloatAnimationCurve& curve,
        const AnimationPlaybackSpec& playback);

    FloatAnimationSample sample(double monotonicTime) const;
    void pauseAt(double timeOffset);
    void resumeAt(double monotonicTime);

    const FloatAnimationCurve& curve() const { return m_curve; }
    const AnimationPlaybackSpec& playback() const { return m_playback; }
    AnimationPlaybackSpec& mutablePlayback() { return m_playback; }
    bool isPaused() const { return m_paused; }

private:
    double directedProgress(double progress, unsigned iteration) const;
    FloatAnimationSample sampleAtActiveTime(double activeTime) const;

    FloatAnimationCurve m_curve;
    AnimationPlaybackSpec m_playback;
    bool m_paused;
    double m_pauseOffset;
};

struct TransformAnimationSample {
    TransformAnimationSample();

    bool affectsOutput;
    bool finished;
    TransformOperations value;
};

class CompositorTransformAnimationTimeline {
public:
    CompositorTransformAnimationTimeline();
    CompositorTransformAnimationTimeline(const TransformAnimationCurve& curve,
        const AnimationPlaybackSpec& playback);

    TransformAnimationSample sample(double monotonicTime) const;
    void pauseAt(double timeOffset);
    void resumeAt(double monotonicTime);

    const TransformAnimationCurve& curve() const { return m_curve; }
    const AnimationPlaybackSpec& playback() const { return m_playback; }
    AnimationPlaybackSpec& mutablePlayback() { return m_playback; }
    bool isPaused() const { return m_paused; }

private:
    double directedProgress(double progress, unsigned iteration) const;
    TransformAnimationSample sampleAtActiveTime(double activeTime) const;

    TransformAnimationCurve m_curve;
    AnimationPlaybackSpec m_playback;
    bool m_paused;
    double m_pauseOffset;
};

} // namespace cc_blink

#endif // CC_BLINK_COMPOSITOR_ANIMATION_TIMELINE_H_
