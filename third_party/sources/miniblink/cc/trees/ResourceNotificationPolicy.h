#ifndef CC_TREES_RESOURCE_NOTIFICATION_POLICY_H_
#define CC_TREES_RESOURCE_NOTIFICATION_POLICY_H_

#include <cstdint>
#include <limits>

namespace cc {

// Synchronized by the containing lifetime state. Intent survives queue
// cancellation; only a successful current-generation owner dispatch clears it.
class ResourceNotificationPolicy {
public:
    struct Ticket { uint64_t sequence = 0; uint64_t generation = 0; };
    struct Intent { bool dirty = false; bool waiting = false; bool stale = false; };
    void wait() { m_waiting = true; }
    void clearWait() { m_waiting = false; }
    void notify(bool dirty) { m_dirty |= dirty; }
    Ticket arm(uint64_t generation)
    {
        if (m_queued || (!m_waiting && !m_dirty)
            || m_sequence == (std::numeric_limits<uint64_t>::max)())
            return Ticket();
        m_queued = true;
        return Ticket{++m_sequence, generation};
    }
    bool dispose(uint64_t sequence)
    {
        if (m_sequence != sequence || !m_queued)
            return false;
        m_queued = false;
        return true;
    }
    Intent consume(const Ticket& ticket, uint64_t generation)
    {
        if (!dispose(ticket.sequence))
            return Intent();
        if (ticket.generation != generation)
            return Intent{false, false, true};
        const Intent intent{m_dirty, m_waiting, false};
        m_dirty = false;
        m_waiting = false;
        return intent;
    }
private:
    uint64_t m_sequence = 0;
    bool m_queued = false;
    bool m_waiting = false;
    bool m_dirty = false;
};
} // namespace cc
#endif
