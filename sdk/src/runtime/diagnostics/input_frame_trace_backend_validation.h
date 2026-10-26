#pragma once

#include <soui/diagnostics/input_frame_trace.h>
#include "../../../../third_party/include/miniblink/input_ui_api.h"

namespace soui::sdk::diagnostics::input_frame_trace::backend_detail {

inline bool same_identity(const identity& left, const identity& right) noexcept {
  return left.version == right.version && left.process_id == right.process_id &&
      left.run_id_high == right.run_id_high && left.run_id_low == right.run_id_low &&
      left.process_incarnation == right.process_incarnation && left.qpc_frequency == right.qpc_frequency;
}
inline identity session_of(const wkeInputUiTraceSnapshot& raw) noexcept {
  identity value;
  value.process_id = raw.processId;
  value.run_id_high = raw.runIdHigh; value.run_id_low = raw.runIdLow;
  value.process_incarnation = raw.processIncarnation; value.qpc_frequency = raw.qpcFrequency;
  return value;
}
inline bool valid_snapshot(const wkeInputUiTraceSnapshot& raw, const identity& expected,
    mode requested, unsigned observed_slot) noexcept {
  constexpr unsigned known_flags = WKE_INPUT_UI_TRACE_AVAILABLE | WKE_INPUT_UI_TRACE_STOPPED |
      WKE_INPUT_UI_TRACE_QUIESCENT | WKE_INPUT_UI_TRACE_SATURATED | WKE_INPUT_UI_TRACE_OWNER_VIOLATION |
      WKE_INPUT_UI_TRACE_INVALID_EVENT | WKE_INPUT_UI_TRACE_CLOCK_INVALID | WKE_INPUT_UI_TRACE_LIMIT_REACHED;
  if (raw.structSize != sizeof(raw) || raw.abiVersion != WKE_INPUT_UI_ABI_VERSION ||
      raw.mode != static_cast<unsigned>(requested) || (raw.flags & ~known_flags) ||
      !same_identity(session_of(raw), expected) || raw.stageCount != stage_count ||
      raw.counterCount != counter_count || raw.durationMetricCount != duration_metric_count ||
      raw.durationBucketCount != duration_buckets || raw.registeredThreads > maximum_threads ||
      raw.registeredThreads < observed_slot || raw.activeProducers > raw.registeredThreads ||
      raw.queued > raw.registeredThreads * events_per_thread) return false;
  if (requested == mode::scalar && (raw.recorded || raw.queued)) return false;
  if (raw.flags & WKE_INPUT_UI_TRACE_QUIESCENT) {
    if (!(raw.flags & WKE_INPUT_UI_TRACE_STOPPED) || raw.activeProducers || raw.pendingRegistrations ||
        raw.recorded > raw.attempted || raw.queued > raw.recorded) return false;
  }
  // Live producer counters are separate atomic observations, not a transaction.
  // In particular, published precedes recorded; do not reject that valid race.
  return true;
}
inline bool valid_range(std::uint64_t first, std::uint64_t last) noexcept {
  return (!first && !last) || (first && last >= first);
}
struct event_validator final {
  std::array<std::uint32_t, maximum_threads> owners{};
  std::array<std::uint64_t, maximum_threads> clocks{};
  unsigned highest_slot{0};

  bool accept(const event& value) noexcept {
    const auto point = static_cast<std::uint32_t>(value.point);
    if (value.struct_size != sizeof(event) || value.version != abi_version || !point || point >= stage_count ||
        !value.thread_id || !value.qpc || !value.producer_slot || value.producer_slot > maximum_threads ||
        static_cast<std::uint32_t>(value.why) > static_cast<std::uint32_t>(reason::cancelled) ||
        static_cast<std::uint32_t>(value.outcome) > static_cast<std::uint32_t>(terminal::rejected) ||
        (value.flags & ~(coverage_valid | content_marker_same_submission | microtask_causality_established)) ||
        !valid_range(value.input_first, value.input_last) || !valid_range(value.action_first, value.action_last))
      return false;
    if (value.input_first && (!value.view_id || !value.view_generation || !value.document_generation)) return false;
    if (value.point == stage::input_terminal && (!value.input_first || value.outcome == terminal::none)) return false;
    if (value.point == stage::microtasks_complete && !(value.flags & microtask_causality_established)) return false;
    if ((value.flags & coverage_valid) && (!value.frame_id || !value.input_first)) return false;
    if (value.point == stage::frame_coverage && !(value.flags & coverage_valid)) return false;
    const auto index = value.producer_slot - 1;
    if ((owners[index] && owners[index] != value.thread_id) || value.qpc < clocks[index]) return false;
    owners[index] = value.thread_id; clocks[index] = value.qpc;
    if (highest_slot < value.producer_slot) highest_slot = value.producer_slot;
    return true; // Equal QPC ticks are valid; there is no cross-slot clock ordering.
  }
};
} // namespace soui::sdk::diagnostics::input_frame_trace::backend_detail
