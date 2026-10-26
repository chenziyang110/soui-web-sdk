#ifndef CC_TREES_RENDER_TRACE_H_
#define CC_TREES_RENDER_TRACE_H_

#include "content/common/InputFrameTrace.h"

namespace cc {
namespace trace = content::input_frame_trace;

inline trace::event& currentRenderTraceContext()
{
    static thread_local trace::event context;
    return context;
}
inline trace::event renderTraceContext()
{
    trace::event value = currentRenderTraceContext();
    if (!value.view_id)
        (void)trace::current_context(value);
    return value;
}
class RenderTraceContextScope {
public:
    explicit RenderTraceContextScope(const trace::event& context)
        : m_previous(currentRenderTraceContext()) { currentRenderTraceContext() = context; }
    ~RenderTraceContextScope() { currentRenderTraceContext() = m_previous; }
private:
    trace::event m_previous;
};
inline void recordRenderStage(trace::stage point, trace::event value = renderTraceContext())
{
    value.point = point;
    value.qpc = 0;
    value.thread_id = 0;
    (void)trace::record(value);
}
class RenderDurationScope {
public:
    explicit RenderDurationScope(trace::duration_metric metric)
        : m_metric(metric), m_start(trace::now_qpc()) { }
    ~RenderDurationScope()
    {
        const uint64_t end = trace::now_qpc();
        if (m_start && end >= m_start)
            trace::observe_duration(m_metric, end - m_start);
    }
private:
    trace::duration_metric m_metric;
    uint64_t m_start;
};
template<class Mutex> class RenderMutexLock {
public:
    explicit RenderMutexLock(Mutex& mutex) : m_mutex(mutex)
    {
        const auto start = trace::now_qpc();
        if (!m_mutex.tryLock()) {
            trace::add(trace::counter::lock_contentions);
            m_mutex.lock();
        }
        m_start = trace::now_qpc();
        if (start && m_start >= start)
            trace::observe_duration(trace::duration_metric::lock_wait, m_start - start);
    }
    ~RenderMutexLock() { unlock(); }
    void unlock()
    {
        if (!m_locked)
            return;
        const auto end = trace::now_qpc();
        if (m_start && end >= m_start)
            trace::observe_duration(trace::duration_metric::lock_hold, end - m_start);
        m_locked = false;
        m_mutex.unlock();
    }
private:
    Mutex& m_mutex;
    uint64_t m_start = 0;
    bool m_locked = true;
};
} // namespace cc
#endif
