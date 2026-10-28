#ifndef TaskSchedulingPolicy_h
#define TaskSchedulingPolicy_h

#include <cstddef>
#include <vector>

namespace content {
namespace task_scheduling {

struct ReadyTask {
    double nextFireTime;
    int priority;
};

// The caller's heap-front check guarantees at least one eligible task.
// Normal oracle: aged (>= 50 ms) precedes fresh; aged deadline then priority;
// fresh priority then deadline. Drain ignores aging/eligibility, priority first.
// Exact keys retain current vector encounter order, which is not a FIFO promise.
template<class Timer, class Describe>
size_t selectReadyTimer(const std::vector<Timer*>& timers, double fireTime, bool draining, Describe describe)
{
    size_t selectedIndex = 0;
    for (size_t i = 1; i < timers.size(); ++i) {
        const ReadyTask candidate = describe(timers[i]);
        if (!draining && candidate.nextFireTime > fireTime)
            continue;
        const ReadyTask selected = describe(timers[selectedIndex]);
        const double priorityGraceSeconds = 0.050;
        const bool candidateAged = !draining && fireTime - candidate.nextFireTime >= priorityGraceSeconds;
        const bool selectedAged = !draining && fireTime - selected.nextFireTime >= priorityGraceSeconds;
        bool preferCandidate = candidate.priority > selected.priority
            || (candidate.priority == selected.priority
                && candidate.nextFireTime < selected.nextFireTime);
        if (candidateAged != selectedAged)
            preferCandidate = candidateAged;
        else if (candidateAged && candidate.nextFireTime != selected.nextFireTime)
            preferCandidate = candidate.nextFireTime < selected.nextFireTime;
        if (preferCandidate) {
            selectedIndex = i;
        }
    }
    return selectedIndex;
}

} // namespace task_scheduling
} // namespace content

#endif
