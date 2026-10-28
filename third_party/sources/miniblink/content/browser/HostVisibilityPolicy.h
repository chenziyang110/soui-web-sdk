#ifndef CONTENT_BROWSER_HOST_VISIBILITY_POLICY_H_
#define CONTENT_BROWSER_HOST_VISIBILITY_POLICY_H_
#include "cc/trees/DrawFrameWakePolicy.h"
namespace content {
// Owner-only. One wake receipt; dirty/fresh intent survives hidden or rejected work.
class HostVisibilityPolicy {
public:
    bool set(bool value)
    {
        if (m_closed) return false;
        m_visible = value;
        if (value) { m_fresh = true; m_wake.request(); }
        return true;
    }
    bool visible() const { return m_visible && !m_closed; }
    bool fresh() const { return m_fresh && !m_closed; }
    void recording() { m_fresh = false; }
    uint64_t arm() { return visible() && fresh() ? m_wake.arm() : 0; }
    bool claim(uint64_t ticket) { return m_wake.claim(ticket); }
    void reject(uint64_t ticket) { m_wake.reject(ticket); }
    bool retry() const { return visible() && fresh() && m_wake.needsRetry(); }
    void request() { if (fresh()) m_wake.request(); }
    void close() { m_closed = true; m_wake.close(); }
private:
    cc::DrawFrameWakePolicy m_wake;
    bool m_visible = true, m_fresh = false, m_closed = false;
};
} // namespace content
#endif
