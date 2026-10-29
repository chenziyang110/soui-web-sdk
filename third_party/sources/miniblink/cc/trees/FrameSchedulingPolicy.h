#ifndef FrameSchedulingPolicy_h
#define FrameSchedulingPolicy_h

#include <cmath>

namespace cc {
namespace frame_scheduling {

// A late wake skips elapsed slots but retains the fractional phase. It never
// replays a backlog, and an idle-to-dirty transition may draw immediately.
inline double advanceDeadline(double deadline, double now, double interval)
{
    if (!(interval > 0) || !std::isfinite(interval))
        return 0;
    if (!(deadline > 0) || !std::isfinite(deadline))
        return now + interval;
    if (deadline > now)
        return deadline;
    double next = deadline + (std::floor((now - deadline) / interval) + 1) * interval;
    if (!(next > now))
        next += interval;
    return next;
}

inline long long remainingDelayMilliseconds(double deadline, double now)
{
    if (!(deadline > now))
        return 0;
    return static_cast<long long>(std::ceil((deadline - now) * 1000.0));
}

} // namespace frame_scheduling
} // namespace cc

#endif
