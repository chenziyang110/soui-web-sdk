#ifndef CC_TREES_LEGACY_PAINT_RETRY_POLICY_H_
#define CC_TREES_LEGACY_PAINT_RETRY_POLICY_H_

#include <cmath>
#include <cstdint>

namespace cc {

// Owner supplies synchronization. A cancelled queue entry retains its ticket
// until disposal: resize must not manufacture a second outstanding wake.
class LegacyPaintRetryPolicy {
public:
    struct Ticket {
        uint64_t sequence = 0;
        uint64_t generation = 0;
        double deadline = 0;
    };

    Ticket arm(double now, double lastPaint, double interval)
    {
        if (m_closed || m_queued)
            return Ticket();
        m_queued = true;
        ++m_sequence;
        if (!m_sequence)
            ++m_sequence;
        const double due = lastPaint > 0 ? lastPaint + interval : now;
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

    void invalidate() { ++m_generation; }
    void close() { m_closed = true; invalidate(); }
    bool queued() const { return m_queued; }
    uint64_t generation() const { return m_generation; }

    static long long delayMilliseconds(double now, double deadline)
    {
        return deadline > now
            ? static_cast<long long>(std::ceil((deadline - now) * 1000.0)) : 0;
    }

private:
    uint64_t m_sequence = 0;
    uint64_t m_generation = 1;
    bool m_queued = false;
    bool m_closed = false;
};

} // namespace cc

#endif // CC_TREES_LEGACY_PAINT_RETRY_POLICY_H_
