#include <content/web_impl_win/TaskSchedulingPolicy.h>
#include <iostream>
#include <stdexcept>
#include <vector>

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
        std::cout << "Scheduler policy checks: " << checks << "; failures: " << failed_checks << '\n';
        return failed_checks ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
