#include <soui/diagnostics/input_frame_trace.h>
#include "content/common/InputFrameTrace.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <new>

// Isolated fixture-only failure injection; production has no test allocator.
std::atomic<bool> fail_next_trace_allocation{false};
void* operator new(std::size_t bytes) {
  if (fail_next_trace_allocation.exchange(false)) throw std::bad_alloc();
  if (void* value = std::malloc(bytes ? bytes : 1)) return value;
  throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }

namespace sdk_trace = soui::sdk::diagnostics::input_frame_trace;
namespace vendor_trace = content::input_frame_trace;

static_assert(sizeof(sdk_trace::event) == sizeof(vendor_trace::event), "Mirrored event sizes differ");
static_assert(sizeof(sdk_trace::identity) == sizeof(vendor_trace::identity), "Mirrored identities differ");
static_assert(offsetof(sdk_trace::event, input_first) == offsetof(vendor_trace::event, input_first), "Mirrored event offsets differ");
static_assert(static_cast<unsigned>(sdk_trace::stage::scene_activated) ==
              static_cast<unsigned>(vendor_trace::stage::scene_activated), "Mirrored stage codes differ");
static_assert(sdk_trace::decode_injection_tag(0x53400001ULL) == 1 &&
              sdk_trace::decode_injection_tag(0x153400001ULL) == 0 &&
              sdk_trace::decode_injection_tag(0x53400000ULL) == 0,
              "Diagnostic tags must reject upper bits and zero sequence");

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

template<class Recorder, class Event, class Identity, class Mode, class Exporter>
void ring_and_lifecycle(Identity identity, Mode detailed) {
  Recorder source;
  auto wrong_clock = identity; ++wrong_clock.qpc_frequency;
  require(!source.configure(wrong_clock, detailed), "foreign QPC frequency accepted");
  require(source.configure(identity, detailed), "actual identity rejected");
  require(!source.configure(identity, detailed), "session was reconfigured");
  std::array<Event, 1024> output;
  require(source.drain(output.data(), output.size()).count == 0, "empty drain failed");
  auto producer = source.attach_current_thread();
  require(static_cast<bool>(producer), "cold registration failed");
  require(!source.attach_current_thread(), "duplicate active owner registered");
  Event value;
  using Stage = decltype(value.point);
  value.point = Stage::task_posted;
  value.task_id = 7;
  for (std::uint64_t n = 1; n <= 1024; ++n) {
    value.qpc = n; // Authored fixed ticks test ordering, never performance.
    require(static_cast<unsigned>(producer.record(value)) == 0, "ring rejected capacity item");
  }
  value.qpc = 1025;
  require(static_cast<unsigned>(producer.record(value)) == 3, "full ring did not refuse");
  require(source.snapshot().dropped == 1, "overflow was not reported");
  require(source.drain(output.data(), 1).count == 1 && output[0].qpc == 1, "FIFO drain differs");
  value.qpc = 1026;
  require(static_cast<unsigned>(producer.record(value)) == 0, "drain did not restore bounded capacity");
  std::ostringstream serialized;
  Exporter exporter(serialized, source);
  require(exporter.begin(), "export header refused actual identity");
  require(!exporter.finish(), "active producer exported complete");
  source.stop();
  require(!source.quiescent(), "stop falsely implies producer quiescence");
  value.qpc = 1027;
  require(static_cast<unsigned>(producer.record(value)) == 2, "stop accepted a write");
  require(!exporter.finish(), "stopped live producer exported complete");
  producer = decltype(producer){};
  require(source.quiescent(), "released producer remained active");
  require(!exporter.finish(), "queued entries exported complete");
  for (unsigned batch = 0; batch < 5; ++batch) (void)exporter.drain(256);
  require(exporter.finish(), "quiescent empty recorder could not finish");
  require(serialized.str().find("\"qpc\":\"2\"") != std::string::npos, "tick was not an exact string");
  require(serialized.str().find("\"dropped\":\"2\"") != std::string::npos, "loss was concealed");
}

template<class Recorder, class Event>
void storage_is_pinned() {
  decltype(std::declval<Recorder&>().attach_current_thread()) producer;
  {
    Recorder source;
    producer = source.attach_current_thread();
  }
  Event value;
  require(static_cast<unsigned>(producer.record(value)) == 2, "recorder destruction freed live writer storage");
}

template<class Recorder, class Event>
void owner_and_invalid_fields() {
  Recorder source;
  auto producer = source.attach_current_thread();
  Event value;
  unsigned result = 0;
  std::thread other([&] { result = static_cast<unsigned>(producer.record(value)); });
  other.join();
  require(result == 5 && source.snapshot().owner_violation, "foreign producer thread accepted");
  using Stage = decltype(value.point);
  value.point = Stage::microtasks_complete;
  require(static_cast<unsigned>(producer.record(value)) == 4, "unproven microtask association accepted");
  value.point = Stage::host_receive;
  value.input_first = 1; value.input_last = 2;
  require(static_cast<unsigned>(producer.record(value)) == 4, "unidentified input association accepted");
}

template<class Recorder, class Event>
void scalar_has_no_event_queue() {
  Recorder source;
  auto producer = source.attach_current_thread();
  Event value;
  for (unsigned n = 0; n < 2048; ++n)
    require(static_cast<unsigned>(producer.record(value)) == 1, "scalar allocated/filled an event ring");
  const auto snapshot = source.snapshot();
  require(snapshot.available && snapshot.session.process_id && snapshot.session.process_incarnation &&
          snapshot.session.qpc_frequency && !snapshot.session.run_id_high && !snapshot.session.run_id_low,
          "default scalar snapshot lacks actual identity/frequency");
  require(snapshot.attempted == 2048 && snapshot.recorded == 0 && snapshot.queued == 0 &&
          snapshot.dropped == 0 && snapshot.stages[1] == 2048, "scalar accounting differs");
}
template<class Recorder, class Event, class Identity, class Mode, class Exporter>
void cold_allocation_failure(Identity identity, Mode detailed) {
  fail_next_trace_allocation.store(true);
  Recorder source;
  require(!source.snapshot().available, "cold allocation failure was hidden");
  require(!source.configure(identity,detailed) && !source.attach_current_thread(), "unavailable recorder accepted producer");
  source.note_unregistered_write(); source.note_causal_loss(); source.stop();
  require(source.quiescent() && source.snapshot().causal_lost == 1, "unavailable recorder lifecycle fault");
  Event output;
  require(source.drain(&output,1).count == 0, "unavailable recorder drained storage");
  std::ostringstream serialized; Exporter exporter(serialized,source);
  require(!exporter.begin() && !exporter.finish(), "unavailable diagnostics fabricated complete trace");
}
}

int main() {
  try {
    ring_and_lifecycle<sdk_trace::recorder, sdk_trace::event, sdk_trace::identity, sdk_trace::mode, sdk_trace::exporter>(sdk_trace::current_identity(1, 2), sdk_trace::mode::detailed);
    ring_and_lifecycle<vendor_trace::recorder, vendor_trace::event, vendor_trace::identity, vendor_trace::mode, vendor_trace::exporter>(vendor_trace::current_identity(1, 2), vendor_trace::mode::detailed);
    storage_is_pinned<sdk_trace::recorder, sdk_trace::event>();
    storage_is_pinned<vendor_trace::recorder, vendor_trace::event>();
    owner_and_invalid_fields<sdk_trace::recorder, sdk_trace::event>();
    owner_and_invalid_fields<vendor_trace::recorder, vendor_trace::event>();
    scalar_has_no_event_queue<sdk_trace::recorder, sdk_trace::event>();
    scalar_has_no_event_queue<vendor_trace::recorder, vendor_trace::event>();
    cold_allocation_failure<sdk_trace::recorder,sdk_trace::event,sdk_trace::identity,sdk_trace::mode,sdk_trace::exporter>(sdk_trace::current_identity(1,2),sdk_trace::mode::detailed);
    cold_allocation_failure<vendor_trace::recorder,vendor_trace::event,vendor_trace::identity,vendor_trace::mode,vendor_trace::exporter>(vendor_trace::current_identity(1,2),vendor_trace::mode::detailed);
    require(static_cast<unsigned>(sdk_trace::record(sdk_trace::event{})) == 6, "SDK hot path lazily registered");
    require(static_cast<unsigned>(vendor_trace::record(vendor_trace::event{})) == 6, "vendor hot path lazily registered");
    require(sdk_trace::process_recorder().snapshot().unregistered_writes == 1, "SDK unregistered write hidden");
    require(vendor_trace::process_recorder().snapshot().unregistered_writes == 1, "vendor unregistered write hidden");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
