#ifndef CC_RASTER_RASTER_SCHEDULING_POLICY_H_
#define CC_RASTER_RASTER_SCHEDULING_POLICY_H_

#include <cstdint>
#include <cmath>

namespace cc {
class RasterTaskReceipt {
public:
    bool start() { if (m_state != State::Pending) return false; m_state = State::Running; return true; }
    bool finish() { if (m_state != State::Running) return false; m_state = State::Completed; return true; }
    bool cancelUnstarted() { if (m_state != State::Pending) return false; m_state = State::Cancelled; return true; }
    bool failRunning() { if (m_state != State::Running) return false; m_state = State::Cancelled; return true; }
private:
    enum class State { Pending, Running, Completed, Cancelled };
    State m_state = State::Pending;
};
class RasterSchedulingPolicy {
public:
    static const unsigned maximumWorkers = 4;
    static const unsigned maximumQueuedTasks = 64;
    static const uint64_t maximumQueuedBytes = 64ULL * 1024 * 1024;
    static unsigned workers(unsigned requested)
    {
        return requested >= 1 && requested <= maximumWorkers ? requested : 1;
    }
    static uint64_t estimatedBytes(double width, double height)
    {
        if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0)
            return maximumQueuedBytes + 1;
        const double bytes = std::ceil(width) * std::ceil(height) * 4;
        return bytes <= maximumQueuedBytes ? static_cast<uint64_t>(bytes)
            : maximumQueuedBytes + 1;
    }
    bool reserve(uint64_t bytes)
    {
        if (m_tasks == maximumQueuedTasks || bytes > maximumQueuedBytes - m_bytes)
            return false;
        ++m_tasks;
        m_bytes += bytes;
        return true;
    }
    void release(uint64_t bytes)
    {
        if (m_tasks && bytes <= m_bytes) {
            --m_tasks;
            m_bytes -= bytes;
        }
    }
    unsigned tasks() const { return m_tasks; }
    uint64_t bytes() const { return m_bytes; }
private:
    unsigned m_tasks = 0;
    uint64_t m_bytes = 0;
};
} // namespace cc
#endif
