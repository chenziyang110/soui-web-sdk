#include "../../../sdk/src/runtime/diagnostics/input_frame_trace_backend_validation.h"
#include <iostream>

namespace trace = soui::sdk::diagnostics::input_frame_trace;
namespace detail = trace::backend_detail;
int main() {
  unsigned failures = 0;
  const auto check = [&](bool ok, const char* name) {
    if (!ok) { ++failures; std::cerr << name << '\n'; }
  };
  trace::identity session;
  session.process_id = 9; session.run_id_low = 2;
  session.process_incarnation = 3; session.qpc_frequency = 10000000;
  wkeInputUiTraceSnapshot raw{};
  raw.structSize = sizeof(raw); raw.abiVersion = 1; raw.mode = 1;
  raw.flags = WKE_INPUT_UI_TRACE_AVAILABLE;
  raw.processId = 9; raw.runIdLow = 2; raw.processIncarnation = 3; raw.qpcFrequency = 10000000;
  raw.stageCount = trace::stage_count; raw.counterCount = trace::counter_count;
  raw.durationMetricCount = trace::duration_metric_count; raw.durationBucketCount = trace::duration_buckets;
  raw.registeredThreads = 2; raw.activeProducers = 2;
  const auto valid = [&] { return detail::valid_snapshot(raw, session, trace::mode::detailed, 2); };
  check(valid(), "live snapshot");
  raw.flags |= 0x100; check(!valid(), "unknown flag"); raw.flags &= ~0x100;
  ++raw.runIdLow; check(!valid(), "session changed"); --raw.runIdLow;
  raw.mode = 0; check(!valid(), "mode changed"); raw.mode = 1;
  raw.registeredThreads = 33; check(!valid(), "producer bound"); raw.registeredThreads = 2;
  raw.activeProducers = 3; check(!valid(), "active bound"); raw.activeProducers = 2;
  raw.queued = 2049; check(!valid(), "physical occupancy"); raw.queued = 1;
  check(valid(), "live publication may precede recorded");
  raw.flags |= WKE_INPUT_UI_TRACE_QUIESCENT; check(!valid(), "false quiescence");
  raw.flags |= WKE_INPUT_UI_TRACE_STOPPED; raw.activeProducers = 0;
  raw.recorded = 1; raw.attempted = 1; check(valid(), "exact quiescence");
  raw.pendingRegistrations = 1; check(!valid(), "registrar still live"); raw.pendingRegistrations = 0;
  raw.registeredThreads = 1; check(!valid(), "observed slot vanished");

  detail::event_validator events;
  trace::event event;
  event.qpc = 100; event.thread_id = 7; event.producer_slot = 1;
  check(events.accept(event), "valid event");
  check(events.accept(event), "equal ticks");
  event.qpc = 99; check(!events.accept(event), "slot clock regression");
  event.producer_slot = 2; check(events.accept(event), "cross slot clock independence");
  event.producer_slot = 1; event.qpc = 101; event.thread_id = 8;
  check(!events.accept(event), "slot owner changed");
  event.producer_slot = 3; check(events.accept(event), "reused OS TID in new lifetime slot");
  event.producer_slot = 0; check(!events.accept(event), "unstamped event"); event.producer_slot = 3;
  event.flags = 8; check(!events.accept(event), "unknown event flag"); event.flags = 0;
  event.input_first = 2; event.input_last = 1; check(!events.accept(event), "inverted input range");
  event.input_last = 2; check(!events.accept(event), "missing input identity");
  event.view_id = 1; event.view_generation = 1; event.document_generation = 1;
  event.point = trace::stage::input_terminal; check(!events.accept(event), "missing terminal outcome");
  event.outcome = trace::terminal::cancelled; check(events.accept(event), "explicit cancellation");
  event.point = trace::stage::microtasks_complete; event.outcome = trace::terminal::none;
  check(!events.accept(event), "unproven microtasks");
  event.flags = trace::microtask_causality_established; check(events.accept(event), "proven microtasks");
  event.point = trace::stage::frame_coverage; event.flags = trace::coverage_valid;
  check(!events.accept(event), "missing coverage frame"); event.frame_id = 1;
  check(events.accept(event), "explicit frame coverage");
  return failures ? 1 : 0;
}
