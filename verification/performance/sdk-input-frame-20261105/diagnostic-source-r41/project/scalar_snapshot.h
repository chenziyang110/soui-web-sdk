#pragma once
#include <soui/diagnostics/input_frame_trace.h>
#include <json.hpp>
namespace owner_fairness {
namespace trace=soui::sdk::diagnostics::input_frame_trace;
inline nlohmann::json scalar_json(const trace::scalar_snapshot& s){
  static_assert(trace::duration_metric_count==15&&trace::counter_count==16,"Review actual public metric layout");
  nlohmann::json h=nlohmann::json::array(),st=nlohmann::json::array();
  for(auto v:s.stages)st.push_back(std::to_string(v));
  for(const auto& bucket:s.durations){nlohmann::json a=nlohmann::json::array();for(auto v:bucket)a.push_back(std::to_string(v));h.push_back(a);}
  return {{"available",s.available},{"mode",s.recording_mode==trace::mode::scalar?"scalar":"detailed"},
    {"runHigh",std::to_string(s.session.run_id_high)},{"runLow",std::to_string(s.session.run_id_low)},
    {"pid",s.session.process_id},{"birth",std::to_string(s.session.process_incarnation)},
    {"qpf",std::to_string(s.session.qpc_frequency)},{"attempted",std::to_string(s.attempted)},
    {"dropped",std::to_string(s.dropped)},{"queued",std::to_string(s.queued)},
    {"unregistered",std::to_string(s.unregistered_writes)},{"registrationRejections",std::to_string(s.registration_rejections)},
    {"causalLost",std::to_string(s.causal_lost)},{"saturated",s.counter_saturated},{"invalid",s.invalid_event},
    {"ownerViolation",s.owner_violation},{"clockInvalid",s.clock_invalid},{"stopped",s.stopped},
    {"quiescent",s.quiescent},{"activeProducers",s.active_producers},{"pendingRegistrations",s.pending_registrations},
    {"causalCounterFault",s.counters[static_cast<std::size_t>(trace::counter::causal_ranges_dropped)]||s.counters[static_cast<std::size_t>(trace::counter::causal_context_dropped)]},
    {"stages",st},{"durations",h},
    {"durationNames",{"lock_wait","lock_hold","client_callback","input_queue","task_queue","layout","record","resource_wait","composite","engine_native","engine_provider","engine_input","engine_fifo","webview2_owner","serialization"}},
    {"liveReadAtomic",s.quiescent}};
}
inline bool scalar_valid(const trace::scalar_snapshot& s){
  return s.available&&s.recording_mode==trace::mode::scalar&&!s.dropped&&!s.queued&&!s.unregistered_writes&&
    !s.registration_rejections&&!s.causal_lost&&!s.counter_saturated&&!s.invalid_event&&!s.owner_violation&&!s.clock_invalid&&
    !s.counters[static_cast<std::size_t>(trace::counter::causal_ranges_dropped)]&&!s.counters[static_cast<std::size_t>(trace::counter::causal_context_dropped)];
}
}
