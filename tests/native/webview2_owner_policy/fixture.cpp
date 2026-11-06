#include "../../../sdk/renderers/webview2/owner_output_policy.h"
#include "../../../sdk/renderers/webview2/owner_callout_policy.h"
#include "../../../sdk/renderers/webview2/owner_output_wake.h"
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <limits>
#include <thread>
using budget = soui::sdk::detail::owner_output_budget;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

namespace {
void empty_output_scope_is_lazy() {
  std::mutex mutex;
  bool active = false;
  std::deque<unsigned> outputs;
  unsigned notifications = 0;
  auto notify = [&]() noexcept { ++notifications; };
  // A non-lazy constructor/destructor would deadlock on this actual mutex.
  std::lock_guard<std::mutex> lock(mutex);
  { soui::sdk::detail::owner_output_drain_scope scope(active, mutex, outputs, notify); }
  require(!active && notifications == 0, "empty output pump acquired notification ownership");
}

void same_slice_consumption_needs_no_output_wake() {
  std::mutex mutex;
  bool active = false;
  std::deque<unsigned> outputs{1};
  unsigned notifications = 0;
  auto notify = [&]() noexcept { ++notifications; };
  {
    soui::sdk::detail::owner_output_drain_scope scope(active, mutex, outputs, notify);
    {
      std::lock_guard<std::mutex> lock(mutex);
      scope.enter_locked(); outputs.pop_front();
    }
    // Producer admission while the callout/drain is active. The same slice
    // consumes it, so neither producer nor tail owes another output notice.
    {
      std::lock_guard<std::mutex> lock(mutex);
      outputs.push_back(2); if (!active) notify();
      require(active && notifications == 0, "active same-slice enqueue posted redundant output notice");
      scope.enter_locked(); outputs.pop_front();
    }
  }
  require(!active && outputs.empty() && notifications == 0, "fully consumed slice posted a redundant tail notice");
}

void budget_remainder_has_one_tail_owner() {
  std::mutex mutex;
  bool active = false;
  std::deque<unsigned> outputs{1, 2, 3};
  unsigned notifications = 0;
  auto notify = [&]() noexcept { ++notifications; };
  {
    soui::sdk::detail::owner_output_drain_scope scope(active, mutex, outputs, notify);
    std::lock_guard<std::mutex> lock(mutex);
    scope.enter_locked(); outputs.pop_front();
  }
  require(!active && outputs.size() == 2 && outputs.front() == 2 && notifications == 1,
      "budget remainder lost FIFO or had no unique tail notification");
}

void threaded_enqueue_responsibility(bool before_exit) {
  std::mutex mutex;
  bool active = false;
  std::deque<unsigned> outputs{1};
  unsigned notifications = 0;
  auto notify = [&]() noexcept { ++notifications; };
  std::promise<void> release;
  auto allowed = release.get_future();
  std::thread producer([&] {
    allowed.wait();
    std::lock_guard<std::mutex> lock(mutex);
    outputs.push_back(2); if (!active) notify();
  });
  {
    soui::sdk::detail::owner_output_drain_scope scope(active, mutex, outputs, notify);
    {
      std::lock_guard<std::mutex> lock(mutex);
      scope.enter_locked(); outputs.pop_front();
    }
    if (before_exit) { release.set_value(); producer.join(); }
  }
  if (!before_exit) { release.set_value(); producer.join(); }
  require(!active && outputs.size() == 1 && outputs.front() == 2 && notifications == 1,
      before_exit ? "enqueue before drain exit lost/duplicated tail responsibility"
                  : "enqueue after drain exit lost/duplicated producer responsibility");
}

void exception_exit_returns_output_responsibility() {
  struct injected_callout_failure {};
  std::mutex mutex;
  bool active = false;
  std::deque<unsigned> outputs{1, 2};
  unsigned notifications = 0;
  auto notify = [&]() noexcept { ++notifications; };
  try {
    soui::sdk::detail::owner_output_drain_scope scope(active, mutex, outputs, notify);
    {
      std::lock_guard<std::mutex> lock(mutex);
      scope.enter_locked(); outputs.pop_front();
    }
    throw injected_callout_failure{};
  } catch (const injected_callout_failure&) {}
  require(!active && outputs.size() == 1 && notifications == 1, "exception left active output drain or stranded its remainder");
}

void rejected_reentry_cannot_retire_outer_output_scope() {
  std::mutex mutex;
  bool active = false, running = false;
  std::deque<unsigned> outputs{1, 2};
  unsigned notifications = 0;
  auto notify = [&]() noexcept { ++notifications; };
  {
    const soui::sdk::detail::owner_pump_guard outer(running);
    require(static_cast<bool>(outer), "outer pump failed to acquire its actual guard");
    {
      soui::sdk::detail::owner_output_drain_scope scope(active, mutex, outputs, notify);
      {
        std::lock_guard<std::mutex> lock(mutex);
        scope.enter_locked(); outputs.pop_front();
      }
      {
        const soui::sdk::detail::owner_pump_guard nested(running);
        require(!static_cast<bool>(nested), "reentry obtained a second actual owner guard");
        // It cannot enter a second drain. An unused lazy scope must not clear
        // the outer bit or acquire its tail-notification responsibility.
        soui::sdk::detail::owner_output_drain_scope rejected(active, mutex, outputs, notify);
      }
      require(active && running && notifications == 0, "rejected reentry retired the outer drain");
    }
    require(!active && running && notifications == 1, "output scope did not retire before the owner guard");
  }
  require(!running, "outer owner guard remained active after output-scope retirement");
}

void navigation_clear_and_refill_keep_outer_responsibility() {
  std::mutex mutex;
  bool active = false;
  std::deque<unsigned> outputs{1, 2};
  unsigned notifications = 0;
  auto notify = [&]() noexcept { ++notifications; };
  {
    soui::sdk::detail::owner_output_drain_scope scope(active, mutex, outputs, notify);
    std::lock_guard<std::mutex> lock(mutex);
    scope.enter_locked(); outputs.pop_front();
    outputs.clear(); // Navigation/stop never resets the outer active bit.
    outputs.push_back(101); if (!active) notify();
    require(active && notifications == 0, "new document stole active-drain notification ownership");
  }
  require(!active && outputs.size() == 1 && outputs.front() == 101 && notifications == 1,
      "navigation refill was lost, duplicated or replaced by old output");
  {
    soui::sdk::detail::owner_output_drain_scope scope(active, mutex, outputs, notify);
    std::lock_guard<std::mutex> lock(mutex);
    scope.enter_locked(); outputs.clear();
  }
  require(!active && notifications == 1, "stop-cleared queue posted a redundant output notice");
}

void non_output_notifications_are_independent() {
  std::mutex mutex;
  bool active = false, lifecycle_pending = false;
  std::deque<unsigned> outputs{1};
  unsigned notifications = 0;
  auto notify = [&]() noexcept { ++notifications; lifecycle_pending = true; };
  {
    soui::sdk::detail::owner_output_drain_scope scope(active, mutex, outputs, notify);
    std::lock_guard<std::mutex> lock(mutex);
    scope.enter_locked(); outputs.pop_front();
    notify(); // The existing non-output path does not consult output active.
  }
  require(!active && lifecycle_pending && notifications == 1,
      "empty output tail suppressed or consumed an independent lifecycle notice");
}

void empty_first_pop_retains_normal_tail_retry() {
  std::mutex mutex;
  bool active = false, notification_posted = false;
  std::deque<unsigned> outputs;
  unsigned attempts = 0;
  // Dependency seam only: the first native notification attempt fails,
  // leaving no posted notice; the retained normal tail can attempt it again.
  auto notify = [&]() noexcept { if (++attempts > 1) notification_posted = true; };
  {
    soui::sdk::detail::owner_output_drain_scope scope(active, mutex, outputs, notify);
    {
      std::lock_guard<std::mutex> lock(mutex);
      require(outputs.empty(), "empty-first-pop setup had output");
    }
    std::thread producer([&] {
      std::lock_guard<std::mutex> lock(mutex);
      outputs.push_back(1); if (!active) notify();
    });
    producer.join();
    require(!scope.entered() && !active && attempts == 1 && !notification_posted,
        "late producer did not preserve the empty-pop/failed-notification precondition");
    // The production caller retains this old normal tail only for an unused
    // scope. It is not a new retry loop or a claim that all Post failures heal.
    if (!scope.entered()) {
      std::lock_guard<std::mutex> lock(mutex);
      if (!outputs.empty()) notify();
    }
  }
  require(!active && outputs.size() == 1 && attempts == 2 && notification_posted,
      "normal empty-pump tail lost the original retry after a failed producer notification");
}

void output_wake_contracts() {
  empty_output_scope_is_lazy();
  same_slice_consumption_needs_no_output_wake();
  budget_remainder_has_one_tail_owner();
  threaded_enqueue_responsibility(true);
  threaded_enqueue_responsibility(false);
  exception_exit_returns_output_responsibility();
  rejected_reentry_cannot_retire_outer_output_scope();
  navigation_clear_and_refill_keep_outer_responsibility();
  non_output_notifications_are_independent();
  empty_first_pop_retains_normal_tail_retry();
  std::cout << "WebView2 output wake: 10 cases passed\n";
}
} // namespace

int main() {
  try {
    const budget::clock::time_point start{};
    budget legacy(0, start), timed(1000, start), invalid(5001, start);
    require(legacy.allows_next(15, start + std::chrono::seconds(1)) && !legacy.allows_next(16, start), "legacy16-item boundary");
    require(timed.allows_next(0, start + std::chrono::seconds(1)), "at least one output must progress");
    require(timed.allows_next(1, start + std::chrono::microseconds(999)) && !timed.allows_next(1, start + std::chrono::microseconds(1000)), "elapsed candidate deadline");
    require(invalid.allows_next(15, start + std::chrono::seconds(1)), "invalid config changed default policy");
    require(budget::may_cache(32, 16, 32 + 16 * sizeof(wchar_t)), "exact prepared byte bound");
    require(!budget::may_cache(33, 16, 32 + 16 * sizeof(wchar_t)), "prepared representation exceeds existing lane byte bound");
    require(!budget::may_cache(65, 0, 64), "oversized raw underflowed capacity");
    require(!budget::may_cache(0, (std::numeric_limits<std::size_t>::max)(), 64), "prepared byte calculation overflowed");
    require(budget::parse_microseconds(L"1000", 4) == 1000 && budget::parse_microseconds(L"100x", 4) == 0
        && budget::parse_microseconds(L"9999999999", 10) == 0, "invalid owner budget was accepted");
    require(budget::parse_microseconds(L"100", 3) == 100 && budget::parse_microseconds(L"5000", 4) == 5000
        && budget::parse_microseconds(L"99", 2) == 0 && budget::parse_microseconds(L"5001", 4) == 0
        && budget::parse_microseconds(L"-100", 4) == 0, "owner budget range/strict decimal boundary");
    struct fake_core { unsigned& retired; explicit fake_core(unsigned& value) : retired(value) {} ~fake_core() { ++retired; } };
    unsigned retired = 0, executed = 0; bool mounted = true, pending = true;
    std::uint64_t generation = 1;
    auto current = std::make_shared<fake_core>(retired);
    const auto target = current; const auto expected_generation = generation;
    const auto identity = [&] { return mounted && generation == expected_generation && current == target; };
    const bool still_current = soui::sdk::detail::owner_callout_current(identity, [&] {
      current.reset(); mounted = false; return true; // get_Source reenters close, but returns an old trusted URL.
    });
    if (still_current) { ++executed; pending = false; }
    require(!still_current && !executed && pending && !retired, "opaque getter close used a reset core or retired the actual target early");
    mounted = true; current = target; generation = 1;
    const bool old_document = soui::sdk::detail::owner_callout_current(identity, [&] { ++generation; return true; });
    if (old_document) pending = false;
    require(!old_document && pending, "old completion changed the new document diagnostic flag");
    bool running = false;
    {
      const soui::sdk::detail::owner_pump_guard outer(running), nested(running);
      require(static_cast<bool>(outer) && !static_cast<bool>(nested) && running, "opaque COM reentry opened a second owner batch");
    }
    require(!running, "owner batch guard did not retire after return");
    output_wake_contracts();
    std::cout << "WebView2 owner policy source contract complete\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
