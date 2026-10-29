#include <content/web_impl_win/TaskSchedulingIndex.h>
#include <content/web_impl_win/TaskSchedulingPolicy.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>
namespace { std::atomic<bool> fail_next_allocation{false}; }
void* operator new(std::size_t bytes) {
  if (fail_next_allocation.exchange(false)) throw std::bad_alloc();
  if (void* value = std::malloc(bytes ? bytes : 1)) return value;
  throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
using index_type = content::task_scheduling::DeadlinePriorityIndex;
struct timer { index_type::Entry entry; bool active = false; };
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void check(index_type& index, std::array<timer, 512>& timers, double now) {
  std::vector<timer*> oracle;
  for (auto& value : timers) if (value.active) oracle.push_back(&value);
  std::stable_sort(oracle.begin(), oracle.end(), [](const auto* a, const auto* b) { return a->entry.deadline < b->entry.deadline; });
  for (bool draining : {false, true}) {
    auto* selected = index.select(now, draining);
    if (oracle.empty() || (!draining && oracle.front()->entry.deadline > now)) { require(!selected, "future ran early"); continue; }
    const auto expected = content::task_scheduling::selectReadyTimer(oracle, now, draining,
      [](const timer* value) { return content::task_scheduling::ReadyTask{value->entry.deadline, value->entry.priority}; });
    // Equal-key identities were formerly heap encounter order; compare exact
    // policy keys, and test the new explicit entry-order tie separately.
    require(selected && selected->deadline == oracle[expected]->entry.deadline && selected->priority == oracle[expected]->entry.priority,
            "indexed priority/deadline/50ms aging differs from baseline oracle");
  }
  for (int priority : {-1000, 0, 100, 200, 300, 1000}) {
    const bool expected = std::any_of(oracle.begin(), oracle.end(), [&](const auto* value) { return value->entry.priority >= priority && value->entry.deadline <= now; });
    require(index.hasReadyAtOrAbove(priority, now) == expected, "ready summary false positive/negative");
  }
  require(index.statistics().entries == oracle.size(), "non-owning index cardinality drift");
}
int main() {
  try {
    index_type index;
    std::array<timer, 512> timers;
    std::mt19937 random(20261026);
    double now = 100;
    for (unsigned operation = 0; operation < 100000; ++operation) {
      auto& value = timers[random() % timers.size()];
      if (value.active) { index.erase(value.entry); value.active = false; }
      if (random() % 4) {
        value.entry.value = &value;
        value.entry.deadline = now + (static_cast<int>(random() % 200) - 60) / 1000.0;
        value.entry.priority = static_cast<int>(random() % 801) - 400;
        index.prepare(value.entry.priority); index.insert(value.entry); value.active = true;
      }
      now += 0.0001;
      check(index, timers, now);
    }
    for (auto& value : timers) if (value.active) { index.erase(value.entry); value.active = false; }
    for (unsigned order = 0; order < 128; ++order) {
      auto& value = timers[order]; value.entry = {}; value.entry.value = &value; value.entry.deadline = now; value.entry.priority = 200;
      index.prepare(200); index.insert(value.entry); value.active = true;
    }
    for (unsigned order = 0; order < 128; ++order) {
      require(index.select(now, false) == &timers[order].entry, "explicit equal-key queue-entry order changed");
      index.erase(timers[order].entry); timers[order].active = false;
    }
    require(index.statistics().entries == 0 && index.statistics().nodes <= 4, "empty arbitrary priority nodes leaked");
    auto& preserved = timers[0].entry; preserved = {}; preserved.deadline = now; preserved.priority = 200;
    index.prepare(200); index.insert(preserved);
    fail_next_allocation = true;
    bool rejected = false;
    try { index.prepare(123456); } catch (const std::bad_alloc&) { rejected = true; }
    fail_next_allocation = false;
    require(rejected && index.select(now, false) == &preserved && index.statistics().entries == 1,
            "failed priority-node preparation destroyed existing tree");
    index.erase(preserved);
    std::cout << "indexed scheduler source contract complete\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
