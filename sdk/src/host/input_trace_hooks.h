#pragma once
#include <soui/diagnostics/input_frame_trace.h>
#include <soui/host/interaction_protocol.h>
#include <thread>
#include <limits>
namespace soui::sdk::detail {
namespace input_trace = diagnostics::input_frame_trace;
inline thread_local unsigned input_trace_owner_users = 0;
inline void release_trace_thread_user(std::thread::id owner, bool counted) noexcept {
  if (!counted) return;
  if (owner != std::this_thread::get_id()) { input_trace::note_causal_loss(); return; }
  if (input_trace_owner_users && !--input_trace_owner_users) input_trace::detach_current_thread();
}
class trace_thread_registration final {
 public:
  trace_thread_registration() : owner_(std::this_thread::get_id()) {
    if (input_trace_owner_users == (std::numeric_limits<unsigned>::max)()) { input_trace::note_causal_loss(); return; }
    if (!input_trace_owner_users) (void)input_trace::register_current_thread();
    ++input_trace_owner_users;
    counted_ = true;
  }
  ~trace_thread_registration() {
    release_trace_thread_user(owner_, counted_);
  }
  trace_thread_registration(const trace_thread_registration&) = delete;
  trace_thread_registration& operator=(const trace_thread_registration&) = delete;
 private:
  std::thread::id owner_;
  bool counted_{false};
};
// Pins an existing cold registration through owner reentry. Never registers,
// allocates or lazily attaches from the hot operation.
class trace_thread_borrow final {
 public:
  trace_thread_borrow() : owner_(std::this_thread::get_id()) {
    if (!input_trace_owner_users) return;
    if (input_trace_owner_users == (std::numeric_limits<unsigned>::max)()) { input_trace::note_causal_loss(); return; }
    ++input_trace_owner_users; counted_ = true;
  }
  ~trace_thread_borrow() { release_trace_thread_user(owner_, counted_); }
  trace_thread_borrow(const trace_thread_borrow&) = delete;
  trace_thread_borrow& operator=(const trace_thread_borrow&) = delete;
 private:
  std::thread::id owner_; bool counted_{false};
};
inline input_trace::event input_event(const input_packet& packet, input_trace::stage point,
    input_trace::reason why = input_trace::reason::none, input_trace::terminal outcome = input_trace::terminal::none,
    std::uint64_t view_id = 0) noexcept {
  input_trace::event value;
  value.point = point; value.why = why; value.outcome = outcome;
  value.view_id = view_id;
  value.view_generation = packet.generation.view;
  value.document_generation = packet.generation.document;
  value.geometry_generation = packet.generation.geometry;
  value.input_first = value.input_last = packet.sequence;
  value.value = static_cast<std::uint32_t>(packet.kind);
  return value;
}
class trace_duration final {
 public:
  explicit trace_duration(input_trace::duration_metric metric) noexcept : metric_(metric), started_(input_trace::now_qpc()) {}
  ~trace_duration() { const auto end = input_trace::now_qpc(); if (end >= started_ && started_) input_trace::observe_duration(metric_, end - started_); }
 private:
  input_trace::duration_metric metric_;
  std::uint64_t started_;
};
} // namespace soui::sdk::detail
