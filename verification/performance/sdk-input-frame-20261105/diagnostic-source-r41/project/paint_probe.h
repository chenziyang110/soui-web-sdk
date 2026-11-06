#pragma once
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <memory>
#include <string>
#include "native_input_policy.h"
#include <json.hpp>

namespace owner_fairness {
struct phase_sample {
  std::uint64_t begin{}, end{}, paints{}, timers{}, invalidations{}, owner_events{};
  std::uint64_t last_paint{}, last_timer{}, max_paint_gap{}, max_timer_gap{};
  progress echoes, pumps;
  std::uint64_t max_pump_duration{};
  std::uint64_t cross_phase_pump_returns{};
};
struct probe_snapshot {
  std::uint64_t frequency{};
  unsigned stage{};
  bool fault{};
  std::array<phase_sample, 4> phases{}; // pre-idle, load, drain, post-idle
};
class paint_probe final {
 public:
  struct pump_stamp { std::uint64_t qpc{};unsigned stage{}; };
  void create(HWND parent, HINSTANCE instance, const std::string& run, const std::string& birth);
  void invalidate() noexcept; // Background thread: this HWND only, never Host/COM.
  std::uint64_t advance(unsigned next); // Actual authorized native mark callback.
  void owner_event();
  pump_stamp pump_begin();
  void pump_end(pump_stamp);
  void echo();
  bool input_ready() const noexcept;
  bool input_finished() const noexcept;
  void cancel_input();
  std::uint64_t load_active();
  nlohmann::json input_finish_fence(); // One control observation, no false-event close.
  void poll_finish() noexcept;
  HWND window() const noexcept { return window_; }
  unsigned owner_thread() const noexcept { return owner_thread_; }
  nlohmann::json input_json();
  bool input_complete();
  bool input_failed();
  void dispatch(const MSG&, std::uint64_t) noexcept;
  void dispatch_done() noexcept;
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
  std::unique_ptr<input_policy> inputs_=std::make_unique<input_policy>();
  DWORD owner_thread_{};
  std::uint32_t nonce_{},marker_frame_{1};
  HANDLE ready_event_{},start_event_{},finish_event_{};
  MSG dispatch_{};std::uint64_t retrieved_{};bool dispatch_claimed_{};
  std::uint64_t finish_fence_qpc_{};unsigned finish_fence_calls_{};bool finish_fence_finished_{};
  void mouse(HWND,WPARAM,LPARAM) noexcept;
};
} // namespace owner_fairness
