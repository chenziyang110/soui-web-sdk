#pragma once
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <memory>
#include <string>
#include "native_input_policy.h"
#include "input_message_metadata.h"
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
  void poll_finish() noexcept; // Monitor latches SenderDone and posts one owner opportunity.
  HWND window() const noexcept { return window_; }
  unsigned owner_thread() const noexcept { return owner_thread_; }
  nlohmann::json input_json();
  bool input_complete();
  bool input_failed();
  class dispatch_guard {
   public:
    dispatch_guard(paint_probe& p,const MSG& m,std::uint64_t q,std::uint64_t extra) noexcept
        :probe_(p){probe_.dispatch(m,q,extra);}
    ~dispatch_guard(){probe_.dispatch_done();}
    dispatch_guard(const dispatch_guard&)=delete;
    dispatch_guard& operator=(const dispatch_guard&)=delete;
   private:paint_probe& probe_;
  };
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
  HANDLE ready_event_{},start_event_{},sender_done_event_{},finish_event_{};
  static constexpr UINT drain_message=WM_APP+0x345;
  struct dispatch_frame {
    MSG message{};std::uint64_t retrieved{},extra{},ordinal{};bool claimed{};
  };
  std::array<dispatch_frame,8> dispatch_stack_{};
  unsigned dispatch_depth_{};
  std::uint64_t retrieval_ordinal_{};
  std::atomic<bool> dispatch_context_fault_{false};
  bool drain_notification_attempted_{},drain_notification_posted_{};
  DWORD receiver_drain_thread_{};
  std::uint64_t receiver_drain_retrieval_ordinal_{},drain_notification_received_qpc_{};
  message_source_probe source_probe_;
  std::uint64_t finish_fence_qpc_{};unsigned finish_fence_calls_{};bool finish_fence_finished_{};
  struct entry_sample {std::uint64_t extra{},qpc{};DWORD sent_flags{};message_source_sample source{};};
  void mouse(HWND,WPARAM,LPARAM,const entry_sample&) noexcept;
  void owner_safe_point();
  void seal_on_owner_locked();
  void dispatch(const MSG&,std::uint64_t,std::uint64_t) noexcept;
  void dispatch_done() noexcept;
};
} // namespace owner_fairness
