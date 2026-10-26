#ifndef CC_TREES_DRAW_FRAME_WAKE_POLICY_H_
#define CC_TREES_DRAW_FRAME_WAKE_POLICY_H_
#include <cstdint>
#include <limits>
namespace cc {
// Synchronized by UiPaintRetryState. Rejection retains the publication intent.
class DrawFrameWakePolicy {
public:
    void request() { if (!m_closed) m_pending = true; }
    uint64_t arm()
    {
        if (m_closed || !m_pending || m_queued || m_serial == (std::numeric_limits<uint64_t>::max)())
            return 0;
        return m_queued = ++m_serial;
    }
    bool claim(uint64_t ticket)
    {
        if (!ticket || ticket != m_queued || m_closed)
            return false;
        m_queued = 0; m_pending = false;
        return true;
    }
    bool reject(uint64_t ticket)
    {
        if (!ticket || ticket != m_queued || m_closed)
            return false;
        m_queued = 0;
        return true;
    }
    bool needsRetry() const { return !m_closed && m_pending && !m_queued; }
    void close() { m_closed = true; m_pending = false; m_queued = 0; }
private:
    uint64_t m_serial = 0, m_queued = 0;
    bool m_pending = false, m_closed = false;
};
} // namespace cc
#endif
