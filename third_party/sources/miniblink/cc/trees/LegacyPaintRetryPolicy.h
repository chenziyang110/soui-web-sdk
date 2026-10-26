#ifndef CC_TREES_LEGACY_PAINT_RETRY_POLICY_H_
#define CC_TREES_LEGACY_PAINT_RETRY_POLICY_H_

#include <cmath>
#include <cstdint>
#include <limits>

namespace cc {

// Owner supplies synchronization. A cancelled queue entry retains its ticket
// until disposal: resize must not manufacture a second outstanding wake.
class LegacyPaintRetryPolicy {
public:
    explicit LegacyPaintRetryPolicy(uint64_t lastSequence = 0, uint64_t generation = 1)
        : m_sequence(lastSequence), m_generation(generation) { }
    struct Ticket {
        uint64_t sequence = 0;
        uint64_t generation = 0;
        double deadline = 0;
    };

    Ticket arm(double now, double lastPaint, double interval)
    {
        if (m_closed || m_queued || !std::isfinite(now)
            || !std::isfinite(lastPaint) || !std::isfinite(interval)
            || !(interval > 0) || interval > 1.0)
            return Ticket();
        if (m_sequence == (std::numeric_limits<uint64_t>::max)()) {
            m_closed = true; // Never alias a still-retained historical task.
            return Ticket();
        }
        const double due = lastPaint > 0 ? lastPaint + interval : now;
        if (!std::isfinite(due))
            return Ticket();
        m_queued = true;
        ++m_sequence;
        return Ticket{m_sequence, m_generation, due > now ? due : now};
    }

    bool accepts(const Ticket& ticket) const
    {
        return !m_closed && m_queued && ticket.sequence == m_sequence
            && ticket.generation == m_generation;
    }

    void dispose(const Ticket& ticket)
    {
        if (ticket.sequence == m_sequence)
            m_queued = false;
    }

    void invalidate()
    {
        if (m_generation == (std::numeric_limits<uint64_t>::max)())
            m_closed = true;
        else
            ++m_generation;
    }
    void close() { m_closed = true; invalidate(); }
    bool queued() const { return m_queued; }
    uint64_t generation() const { return m_generation; }

    static long long delayMilliseconds(double now, double deadline)
    {
        if (!std::isfinite(now) || !std::isfinite(deadline))
            return 1000;
        const double remaining = deadline - now;
        if (!(remaining > 0))
            return 0;
        // Supported intervals are <= 1s. An anomalous clock value is still
        // revisited by one bounded delayed wake, never an undefined cast.
        return remaining >= 1.0 ? 1000
            : static_cast<long long>(std::ceil(remaining * 1000.0));
    }

private:
    uint64_t m_sequence = 0;
    uint64_t m_generation = 1;
    bool m_queued = false;
    bool m_closed = false;
};

} // namespace cc

#endif // CC_TREES_LEGACY_PAINT_RETRY_POLICY_H_
