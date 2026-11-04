#pragma once
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <vector>

// Compile the exact production Host selection/cache/query statements. Only
// HWND/monitor/query results and surrounding mount/report state are fakes.
// This is not a Windows message, pacer-thread, provider-apply or display test.
namespace display_reference_fixture {
inline void require(bool value, const char* reason) {
  if (!value) throw std::runtime_error(reason);
}

struct monitor { unsigned rate; };
struct window { monitor* display; };
using HWND = window*;
using HMONITOR = monitor*;
constexpr unsigned MONITOR_DEFAULTTONEAREST = 2;
inline std::vector<HWND> monitor_calls, query_calls;

inline HMONITOR MonitorFromWindow(HWND reference, unsigned flags) {
  require(flags == MONITOR_DEFAULTTONEAREST, "Host monitor fallback flag changed");
  monitor_calls.push_back(reference);
  return reference ? reference->display : nullptr;
}
struct frame_pacer {
  static unsigned query_display_refresh(HWND reference) {
    query_calls.push_back(reference);
    return reference && reference->display ? reference->display->rate : 0;
  }
};

struct cached_state {
  std::uintptr_t frame_monitor{};
  int frame_display{}, frame_requested{-1}, frame_report_pulse{-1}, frame_report_desired{-1};
  std::atomic<bool> frame_display_dirty{true};
};
struct sampled_display { int selected_limit, raw_display; };
struct host_fragment {
  void* owner_window_{};
  void* webview_window_{};
  cached_state cache;

  sampled_display sample(int requested, int pulse_rate, bool unified = true) {
    auto* lifetime = &cache;
#include "frame-display-reference.inc"
    const sampled_display result{display, lifetime->frame_display};
    // The exact report-cache assignments close each simulated pump. The real
    // report's change predicate is not under test; repeated assignments here
    // are idempotent and do not trigger extra display queries.
#include "frame-display-report-inputs.inc"
    return result;
  }
};

inline void reset_calls() { monitor_calls.clear(); query_calls.clear(); }
inline void only_reference(HWND reference) {
  for (auto value : monitor_calls)
    require(value == reference, "MonitorFromWindow used a different display reference");
  for (auto value : query_calls)
    require(value == reference, "query_display_refresh used a different display reference");
}
inline int effective(int requested, int pulse, const sampled_display& value, int main = -1) {
  const auto result = soui::sdk::detail::select_frame_policy_rate(
      requested, value.selected_limit, main < 0 ? pulse : main, 30, 240);
  require(result.valid, "Projected Host produced an unsupported policy input");
  return result.effective;
}

inline void run() {
  monitor owner_monitor{100}, child_monitor{75}, other_child_monitor{144};
  window owner{&owner_monitor}, child{&child_monitor};
  host_fragment host;
  host.owner_window_ = &owner;
  host.webview_window_ = &child;
  reset_calls();
  auto observed = host.sample(120, 100);
  require(observed.raw_display == 100 && effective(120, 100, observed) == 100,
      "Owner 100 / child 75 must select the owner and preserve pulse 100");
  require(monitor_calls.size() == 1 && query_calls.size() == 1,
      "Initial Host display lookup must be bounded");
  only_reference(&owner);

  // Changing only the child's monitor must not change the owner cache key.
  child.display = &other_child_monitor;
  observed = host.sample(120, 100);
  require(observed.raw_display == 100 && monitor_calls.size() == 2 && query_calls.size() == 1,
      "Child-only monitor change must not requery an unchanged owner");
  only_reference(&owner);

  // Same owner/monitor, successful UI adoption is the existing pulse-change
  // invalidation opportunity. No setter or feedback mechanism is reimplemented.
  owner_monitor.rate = 75;
  observed = host.sample(120, 75);
  require(query_calls.size() == 2 && observed.raw_display == 75 && effective(120, 75, observed) == 75,
      "A changed actual pulse must refresh the owner's display rate");
  owner_monitor.rate = 100;
  observed = host.sample(120, 100);
  require(query_calls.size() == 3 && observed.raw_display == 100 && effective(120, 100, observed) == 100,
      "Original request must permit recovery from 75 to 100");
  observed = host.sample(120, 100);
  require(query_calls.size() == 3, "Stable pump must not enumerate display settings again");
  only_reference(&owner);

  monitor next_owner_monitor{120};
  owner.display = &next_owner_monitor;
  observed = host.sample(120, 100);
  require(query_calls.size() == 4 && observed.raw_display == 120 && effective(120, 100, observed) == 100,
      "Owner monitor change refreshes while preserving the currently selected active rate");
  observed = host.sample(120, 120);
  require(query_calls.size() == 5 && effective(120, 120, observed) == 120,
      "Next existing UI pulse converges after the owner display change");
  observed = host.sample(144, 120);
  require(query_calls.size() == 6 && host.cache.frame_requested == 144,
      "Existing requested-rate invalidation remains active");
  only_reference(&owner);

  host_fragment fallback;
  child.display = &child_monitor;
  fallback.webview_window_ = &child;
  reset_calls();
  observed = fallback.sample(120, 100);
  require(observed.raw_display == 75 && effective(120, 100, observed) == 75,
      "A null owner must retain the child fallback");
  require(monitor_calls.size() == 1 && query_calls.size() == 1,
      "Child fallback must use one cache/query reference");
  only_reference(&child);

  // Explicitly retain the existing unknown-query boundary: the raw cached
  // value stays zero, normalized policy is 60, and no automatic retry exists.
  host_fragment unknown;
  unknown.owner_window_ = &owner;
  unknown.webview_window_ = &child;
  next_owner_monitor.rate = 0;
  reset_calls();
  observed = unknown.sample(120, 100);
  require(observed.raw_display == 0 && observed.selected_limit == 0 && effective(120, 100, observed) == 60,
      "Unknown display must remain raw zero and select conservative 60");
  next_owner_monitor.rate = 100;
  observed = unknown.sample(120, 100);
  require(query_calls.size() == 1 && observed.raw_display == 0 && effective(120, 100, observed) == 60,
      "An unchanged key must not invent unknown-query recovery");
  unknown.cache.frame_display_dirty.store(true, std::memory_order_release);
  observed = unknown.sample(120, 100);
  require(query_calls.size() == 2 && observed.raw_display == 100 && effective(120, 100, observed) == 100,
      "An existing dirty opportunity must recover a successful owner query");
  only_reference(&owner);

  host_fragment legacy;
  legacy.owner_window_ = &owner;
  legacy.webview_window_ = &child;
  next_owner_monitor.rate = 75;
  reset_calls();
  observed = legacy.sample(120, 100, false);
  require(observed.raw_display == 75 && observed.selected_limit == 1000 && effective(120, 100, observed) == 100,
      "Non-unified override selection must keep its 1000 display parameter");
  require(effective(120, 100, observed, 0) == 0, "Owner selection must not enable a disabled main controller");
  only_reference(&owner);

  host_fragment no_window;
  reset_calls();
  observed = no_window.sample(120, 100);
  require(observed.raw_display == 0 && effective(120, 100, observed) == 60,
      "No reference must preserve unknown rather than manufacture a monitor");
  only_reference(nullptr);
}
}  // namespace display_reference_fixture
