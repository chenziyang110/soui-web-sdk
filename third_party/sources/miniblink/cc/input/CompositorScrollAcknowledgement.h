#ifndef CC_INPUT_COMPOSITOR_SCROLL_ACKNOWLEDGEMENT_H_
#define CC_INPUT_COMPOSITOR_SCROLL_ACKNOWLEDGEMENT_H_

#include "cc/input/ScrollController.h"

#include <cmath>

namespace cc {

// Main-thread acknowledgement state. The raw receipt belongs to the compositor
// sample; the published total belongs to Blink's canonical base coordinates.
class CompositorScrollAcknowledgement {
public:
    CompositorScrollAcknowledgement() : m_epoch(1), m_sample(0) { }

    uint64_t epoch() const { return m_epoch; }
    uint64_t sample() const { return m_sample; }
    ScrollVector total() const { return m_total; }
    ScrollVector canonicalAdjustment() const { return m_canonicalAdjustment; }

    void invalidate()
    {
        if (!++m_epoch)
            m_epoch = 1;
        m_sample = 0;
        m_total = ScrollVector();
        m_canonicalAdjustment = ScrollVector();
        m_rawTotal = ScrollVector();
        m_rawVisual = ScrollVector();
    }

    bool accept(uint64_t epoch, uint64_t sample, const ScrollVector& rawTotal,
        const ScrollVector& rawVisual, const ScrollVector& canonical)
    {
        if (epoch != m_epoch || sample <= m_sample || !finite(rawTotal)
            || !finite(rawVisual) || !finite(canonical)) {
            return false;
        }
        const ScrollVector adjustment(canonical.x - rawVisual.x,
            canonical.y - rawVisual.y);
        const ScrollVector total(rawTotal.x + adjustment.x,
            rawTotal.y + adjustment.y);
        // Preserve base - acknowledgedTotal without overflowing T + C first.
        if (!finite(adjustment) || !finite(total)
            || !std::isfinite(canonical.x - adjustment.x)
            || !std::isfinite(canonical.y - adjustment.y)
            || !std::isfinite(rawVisual.x - rawTotal.x)
            || !std::isfinite(rawVisual.y - rawTotal.y)
            || !std::isfinite(canonical.x - total.x)
            || !std::isfinite(canonical.y - total.y)) {
            return false;
        }
        m_sample = sample;
        m_total = total;
        m_canonicalAdjustment = adjustment;
        m_rawTotal = rawTotal;
        m_rawVisual = rawVisual;
        return true;
    }

    bool matchesRaw(uint64_t epoch, uint64_t sample,
        const ScrollVector& rawTotal, const ScrollVector& rawVisual) const
    {
        if (!m_sample || epoch != m_epoch || sample != m_sample
            || !finite(rawTotal) || !finite(rawVisual)) {
            return false;
        }
        const double epsilon = 0.01;
        return std::fabs(rawTotal.x - m_rawTotal.x) <= epsilon
            && std::fabs(rawTotal.y - m_rawTotal.y) <= epsilon
            && std::fabs(rawVisual.x - m_rawVisual.x) <= epsilon
            && std::fabs(rawVisual.y - m_rawVisual.y) <= epsilon;
    }

    // A scene can retire before its first visual sample. Preserve the original
    // zero-sample forward-barrier proof without manufacturing a raw receipt.
    bool matchesInitial(uint64_t epoch, uint64_t sample,
        const ScrollVector& rawTotal, const ScrollVector& rawVisual,
        const ScrollVector& base) const
    {
        if (m_sample || sample || epoch != m_epoch || !finite(rawTotal)
            || !finite(rawVisual) || !finite(base)) {
            return false;
        }
        const double epsilon = 0.01;
        return std::fabs(rawTotal.x) <= epsilon
            && std::fabs(rawTotal.y) <= epsilon
            && std::fabs(rawVisual.x - base.x) <= epsilon
            && std::fabs(rawVisual.y - base.y) <= epsilon;
    }

private:
    static bool finite(const ScrollVector& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y);
    }

    uint64_t m_epoch;
    uint64_t m_sample;
    ScrollVector m_total;
    ScrollVector m_canonicalAdjustment;
    ScrollVector m_rawTotal;
    ScrollVector m_rawVisual;
};

} // namespace cc

#endif
