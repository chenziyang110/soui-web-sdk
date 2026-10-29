#include <content/web_impl_win/TaskSchedulingPolicy.h>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <cstdint>

struct timer {
    double m_nextFireTime;
    int m_priority;
};

unsigned failed_checks = 0;
void require(bool value, const char* message) {
    if (!value) { ++failed_checks; std::cerr << message << '\n'; }
}

int main() {
    try {
        const auto selectReadyTimer = [](const std::vector<timer*>& timers, double now, bool draining) {
            return content::task_scheduling::selectReadyTimer(timers, now, draining, [](const timer* value) {
                return content::task_scheduling::ReadyTask{value->m_nextFireTime, value->m_priority};
            });
        };
        unsigned checks = 0;
        timer loading{1.0, 100}, normal{1.01, 200}, future{10.0, 300};
        std::vector<timer*> queue{&loading, &normal, &future};
        require(selectReadyTimer(queue, 1.02, false) == 1, "Fresh higher-priority work must retain priority"); ++checks;
        normal.m_priority = 100;
        require(selectReadyTimer(queue, 1.02, false) == 0, "Equal priorities must retain earliest deadline"); ++checks;
        normal.m_nextFireTime = 1.0;
        require(selectReadyTimer(queue, 1.02, false) == 0, "Equal keys must preserve heap order"); ++checks;
        require(selectReadyTimer(queue, 1.02, true) == 2, "Exit drain must retain its original priority rule"); ++checks;
        loading = {0.0, 100}; normal = {0.049, 200};
        require(selectReadyTimer(queue, 0.0499, false) == 1, "Priority grace must apply before 50 ms"); ++checks;
        require(selectReadyTimer(queue, 0.050, false) == 0, "Aging must begin at 50 ms"); ++checks;
        loading = {1.0, 100};
        normal = {1.09, 200};
        require(selectReadyTimer(queue, 1.10, false) == 0, "Overdue loading task starved behind fresh normal work"); ++checks;
        normal = {1.01, 200};
        require(selectReadyTimer(queue, 1.10, false) == 0, "Among aged tasks the oldest deadline must win"); ++checks;
        normal = {1.0, 200};
        require(selectReadyTimer(queue, 1.10, false) == 1, "Aged equal deadlines must retain priority tie break"); ++checks;
        loading = {1.0, 0}; normal = {1.09, 300};
        require(selectReadyTimer(queue, 1.10, false) == 0, "Aged idle work must not starve indefinitely"); ++checks;
        loading = {1.0, 100}; normal = {1.0, 200};
        bool loading_ran = false;
        for (unsigned step = 0; step < 200; ++step) {
            const double now = 1.0 + step * 0.001;
            if (selectReadyTimer(queue, now, false) == 0) { loading_ran = true; break; }
            normal.m_nextFireTime = now; // Continuous animation immediately posts its next default task.
        }
        require(loading_ran, "Continuous animation starved a ready loading task"); ++checks;
        queue = {&loading, &future};
        require(selectReadyTimer(queue, 1.10, false) == 0, "Future work must not be pulled forward by its priority"); ++checks;
        // Compare with the previous linear policy over many real deadline heaps.
        // This protects aging and exact tie choices while traversal order changes.
        std::uint32_t random = 0x50937u;
        for (unsigned iteration = 0; iteration != 500; ++iteration) {
            std::vector<timer> storage(127);
            for (auto& item : storage) {
                random = random * 1664525u + 1013904223u;
                item = {1.0 + (random % 200) / 1000.0, static_cast<int>((random >> 16) % 4) * 100};
            }
            storage[0].m_nextFireTime = 1.0;
            std::stable_sort(storage.begin(), storage.end(), [](const timer& a, const timer& b) {
                return a.m_nextFireTime < b.m_nextFireTime;
            });
            queue.clear();
            for (auto& item : storage) queue.push_back(&item);
            const double now = 1.0 + (iteration % 250) / 1000.0;
            for (bool draining : {false, true}) {
                size_t expected = 0;
                for (size_t i = 1; i < queue.size(); ++i) {
                    const auto& candidate = *queue[i];
                    const auto& selected = *queue[expected];
                    if (!draining && candidate.m_nextFireTime > now) continue;
                    const bool candidateAged = !draining && now - candidate.m_nextFireTime >= .050;
                    const bool selectedAged = !draining && now - selected.m_nextFireTime >= .050;
                    bool prefer = candidate.m_priority > selected.m_priority
                        || (candidate.m_priority == selected.m_priority && candidate.m_nextFireTime < selected.m_nextFireTime);
                    if (candidateAged != selectedAged) prefer = candidateAged;
                    else if (candidateAged && candidate.m_nextFireTime != selected.m_nextFireTime)
                        prefer = candidate.m_nextFireTime < selected.m_nextFireTime;
                    if (prefer) expected = i;
                }
                require(selectReadyTimer(queue, now, draining) == expected, "Pruned traversal changed the original ready policy"); ++checks;
            }
        }
        std::vector<timer> futureStorage(16383, timer{10.0, 300});
        futureStorage[0] = {1.0, 100};
        queue.clear();
        for (auto& item : futureStorage) queue.push_back(&item);
        unsigned descriptions = 0;
        const auto selected = content::task_scheduling::selectReadyTimer(queue, 1.1, false, [&](const timer* item) {
            ++descriptions;
            return content::task_scheduling::ReadyTask{item->m_nextFireTime, item->m_priority};
        });
        require(selected == 0 && descriptions <= 5, "Future-only subtrees must be skipped without inspecting the backlog"); ++checks;
        require(!content::task_scheduling::hasReadyTimerAtOrAbove(queue, 1.1, 200, [](const timer* item) {
            return content::task_scheduling::ReadyTask{item->m_nextFireTime, item->m_priority};
        }), "Priority probe must not run future work early"); ++checks;
        std::cout << "Scheduler policy checks: " << checks << "; failures: " << failed_checks << '\n';
        return failed_checks ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
