#ifndef CC_TREES_FRAME_DEADLINE_POLICY_H_
#define CC_TREES_FRAME_DEADLINE_POLICY_H_

#include <cmath>

namespace cc {

// Absolute fractional seconds stay in the vendor's monotonic clock domain.
// A delayed frame advances directly to the first future phase.
inline double nextFrameDeadline(double previous, double now, double interval)
{
    if (!(interval > 0) || !std::isfinite(interval))
        return now;
    if (!(previous > 0))
        return now + interval;
    if (previous > now)
        return previous;
    const double next = previous + (std::floor((now - previous) / interval) + 1) * interval;
    return next > now ? next : now + interval;
}

} // namespace cc
#endif // CC_TREES_FRAME_DEADLINE_POLICY_H_
