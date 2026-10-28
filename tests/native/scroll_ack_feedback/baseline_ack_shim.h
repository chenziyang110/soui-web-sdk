#ifndef SOUI_TEST_BASELINE_ACK_SHIM_H_
#define SOUI_TEST_BASELINE_ACK_SHIM_H_
#include "cc/input/ScrollController.h"

namespace blink {
struct WebDoublePoint {
    double x, y;
    WebDoublePoint(double a = 0, double b = 0) : x(a), y(b) { }
    bool operator==(const WebDoublePoint& b) const { return x == b.x && y == b.y; }
    bool operator!=(const WebDoublePoint& b) const { return !(*this == b); }
};
}
namespace cc {
// Only unrelated platform operations are replaced. The two mutation method
// bodies are extracted without rewriting from the explicitly selected source.
class WebLayerImpl {
    struct ScrollClient {
        WebLayerImpl* owner;
        void didScroll() { owner->setScrollPositionDouble(owner->m_callbackCanonical); }
    };
public:
    WebLayerImpl() : m_compositorScrollEpoch(1), m_acknowledgedCompositorSample(0),
        m_client{this}, m_webLayerScrollClient(&m_client) { }
    uint64_t epoch() const { return m_compositorScrollEpoch; }
    uint64_t sample() const { return m_acknowledgedCompositorSample; }
    ScrollVector total() const { return ScrollVector(m_acknowledgedCompositorTotal.x, m_acknowledgedCompositorTotal.y); }
    bool accept(uint64_t epoch, uint64_t sample, const ScrollVector& total,
        const ScrollVector& visual, const ScrollVector& canonical) {
        m_callbackCanonical = blink::WebDoublePoint(canonical.x, canonical.y);
        return setScrollPositionFromCompositor(epoch, sample, total.x, total.y,
            blink::WebDoublePoint(visual.x, visual.y));
    }
    void setScrollPositionDouble(blink::WebDoublePoint);
    bool setScrollPositionFromCompositor(uint64_t, uint64_t, double, double,
        const blink::WebDoublePoint&);
private:
    void setNeedsCommit(bool) { }
    blink::WebDoublePoint m_scrollPositionDouble;
    uint64_t m_compositorScrollEpoch;
    uint64_t m_acknowledgedCompositorSample;
    blink::WebDoublePoint m_acknowledgedCompositorTotal;
    blink::WebDoublePoint m_callbackCanonical;
    ScrollClient m_client;
    ScrollClient* m_webLayerScrollClient;
};
}
#endif
