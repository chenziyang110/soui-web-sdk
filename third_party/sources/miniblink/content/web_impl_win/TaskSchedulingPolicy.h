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

namespace detail {

inline bool preferReadyTask(const ReadyTask& candidate, size_t candidateIndex,
    const ReadyTask& selected, size_t selectedIndex, double fireTime, bool draining)
{
    const double priorityGraceSeconds = 0.050;
    const bool candidateAged = !draining && fireTime - candidate.nextFireTime >= priorityGraceSeconds;
    const bool selectedAged = !draining && fireTime - selected.nextFireTime >= priorityGraceSeconds;
    if (candidateAged != selectedAged)
        return candidateAged;
    if (candidateAged && candidate.nextFireTime != selected.nextFireTime)
        return candidate.nextFireTime < selected.nextFireTime;
    if (candidate.priority != selected.priority)
        return candidate.priority > selected.priority;
    if (candidate.nextFireTime != selected.nextFireTime)
        return candidate.nextFireTime < selected.nextFireTime;
    return candidateIndex < selectedIndex; // Preserve the original linear scan tie-break.
}

template<class Timer, class Describe>
void selectReadySubtree(const std::vector<Timer*>& timers, size_t index,
    double fireTime, bool draining, Describe& describe, size_t& selectedIndex)
{
    const ReadyTask candidate = describe(timers[index]);
    // A deadline heap's descendants cannot be earlier than their parent. Most
    // future timers never need to be touched; recursion depth is logarithmic.
    if (!draining && candidate.nextFireTime > fireTime)
        return;
    const ReadyTask selected = describe(timers[selectedIndex]);
    if (!draining && fireTime - selected.nextFireTime >= 0.050
        && candidate.nextFireTime > selected.nextFireTime)
        return; // No younger descendant can outrank an already aged oldest task.
    if (preferReadyTask(candidate, index, selected, selectedIndex, fireTime, draining))
        selectedIndex = index;
    if (index >= timers.size() / 2)
        return;
    const size_t left = index * 2 + 1;
    selectReadySubtree(timers, left, fireTime, draining, describe, selectedIndex);
    if (left + 1 < timers.size())
        selectReadySubtree(timers, left + 1, fireTime, draining, describe, selectedIndex);
}

template<class Timer, class Describe>
bool hasReadySubtree(const std::vector<Timer*>& timers, size_t index,
    double fireTime, int minimumPriority, Describe& describe)
{
    const ReadyTask candidate = describe(timers[index]);
    if (candidate.nextFireTime > fireTime)
        return false;
    if (candidate.priority >= minimumPriority)
        return true;
    if (index >= timers.size() / 2)
        return false;
    const size_t left = index * 2 + 1;
    return hasReadySubtree(timers, left, fireTime, minimumPriority, describe)
        || (left + 1 < timers.size() && hasReadySubtree(timers, left + 1, fireTime, minimumPriority, describe));
}

} // namespace detail

// Requires a deadline heap and an eligible front element, as before.
template<class Timer, class Describe>
size_t selectReadyTimer(const std::vector<Timer*>& timers, double fireTime, bool draining, Describe describe)
{
    size_t selectedIndex = 0;
    if (!timers.empty())
        detail::selectReadySubtree(timers, 0, fireTime, draining, describe, selectedIndex);
    return selectedIndex;
}

template<class Timer, class Describe>
bool hasReadyTimerAtOrAbove(const std::vector<Timer*>& timers, double fireTime, int minimumPriority, Describe describe)
{
    return !timers.empty() && detail::hasReadySubtree(timers, 0, fireTime, minimumPriority, describe);
}

} // namespace task_scheduling
} // namespace content

#endif
