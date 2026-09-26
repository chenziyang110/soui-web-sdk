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
template<class Timer, class Describe>
size_t selectReadyTimer(const std::vector<Timer*>& timers, double fireTime, bool draining, Describe describe)
{
    size_t selectedIndex = 0;
    for (size_t i = 1; i < timers.size(); ++i) {
        const ReadyTask candidate = describe(timers[i]);
        if (!draining && candidate.nextFireTime > fireTime)
            continue;
        const ReadyTask selected = describe(timers[selectedIndex]);
        if (candidate.priority > selected.priority
            || (candidate.priority == selected.priority
                && candidate.nextFireTime < selected.nextFireTime)) {
            selectedIndex = i;
        }
    }
    return selectedIndex;
}

} // namespace task_scheduling
} // namespace content

#endif
