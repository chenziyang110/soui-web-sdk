#pragma once
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace owner_fairness {
struct phase_sample {
  std::uint64_t begin{}, end{}, paints{}, timers{}, invalidations{}, owner_events{};
  std::uint64_t last_paint{}, last_timer{}, max_paint_gap{}, max_timer_gap{};
};
struct probe_snapshot {
  std::uint64_t frequency{};
  unsigned stage{};
  bool fault{};
  std::array<phase_sample, 4> phases{}; // pre-idle, load, drain, post-idle
};
class paint_probe final {
 public:
  void create(HWND parent, HINSTANCE instance);
  void invalidate() noexcept; // Background thread: this HWND only, never Host/COM.
  std::uint64_t advance(unsigned next); // Actual authorized native mark callback.
  void owner_event();
  unsigned stage() const noexcept { return stage_.load(); }
  probe_snapshot snapshot();
  void destroy(); // Owner only, after invalidator thread has really stopped.
 private:
  static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
  void note(bool paint);
  std::uint64_t now(); // Called under mutex; QPC/overflow faults invalidate sample.
  void increment(std::uint64_t&);
  HWND window_{};
  HINSTANCE instance_{};
  std::mutex mutex_;
  std::atomic<bool> callback_fault_{false};
  std::atomic<unsigned> stage_{0};
  probe_snapshot value_;
};
} // namespace owner_fairness
