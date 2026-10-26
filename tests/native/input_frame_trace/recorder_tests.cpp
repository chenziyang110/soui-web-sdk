#include <soui/diagnostics/input_frame_trace.h>
#include "content/common/InputFrameTrace.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <streambuf>
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
static_assert(sdk_trace::stage_count == 31 && vendor_trace::stage_count == 31 &&
              static_cast<unsigned>(sdk_trace::stage::interaction_diagnostic) == 29 &&
              static_cast<unsigned>(vendor_trace::stage::interaction_diagnostic) == 29 &&
              static_cast<unsigned>(sdk_trace::stage::gpu_present_diagnostic) == 30 &&
              static_cast<unsigned>(vendor_trace::stage::gpu_present_diagnostic) == 30,
              "Diagnostic stage contract differs");
static_assert(static_cast<std::uint64_t>(sdk_trace::interaction_diagnostic_code::barrier_sync_forward_accepted) ==
              static_cast<std::uint64_t>(vendor_trace::interaction_diagnostic_code::barrier_sync_forward_accepted) &&
              static_cast<std::uint64_t>(sdk_trace::gpu_present_diagnostic_code::swap_succeeded) ==
              static_cast<std::uint64_t>(vendor_trace::gpu_present_diagnostic_code::swap_succeeded),
              "Mirrored diagnostic code domains differ");
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
  std::ostringstream serialized;
  Exporter exporter(serialized, source);
  require(exporter.begin(), "export header refused actual identity");
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
  require(exporter.drain(1).count == 1 && serialized.str().find("\"qpc\":\"1\"")!=std::string::npos, "FIFO drain differs");
  value.qpc = 1026;
  require(static_cast<unsigned>(producer.record(value)) == 0, "drain did not restore bounded capacity");
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
void noncausal_diagnostic_fields() {
  Recorder source;
  auto producer = source.attach_current_thread();
  Event value;
  using Stage = decltype(value.point);
  for (const Stage point : {Stage::interaction_diagnostic, Stage::gpu_present_diagnostic}) {
    value = Event{}; value.point = point;
    const std::uint64_t maximum = point == Stage::interaction_diagnostic ? 20 : 4;
    for (std::uint64_t code = 1; code <= maximum; ++code) {
      value.value = code;
      require(static_cast<unsigned>(producer.record(value)) == 1, "fixed diagnostic code rejected");
    }
    value.value = maximum + 1;
    require(static_cast<unsigned>(producer.record(value)) == 4, "unknown diagnostic code accepted");
    value.value = 0;
    require(static_cast<unsigned>(producer.record(value)) == 4, "zero diagnostic code accepted");
    value.value = 1;
    std::uint64_t Event::* relations[] = {&Event::view_id, &Event::view_generation,
      &Event::document_generation, &Event::geometry_generation, &Event::input_first,
      &Event::input_last, &Event::task_id, &Event::frame_id, &Event::action_first,
      &Event::action_last, &Event::related_id};
    for (auto relation : relations) {
      value.*relation = 1;
      require(static_cast<unsigned>(producer.record(value)) == 4, "diagnostic fabricated relation accepted");
      value.*relation = 0;
    }
    value.flags = 4;
    require(static_cast<unsigned>(producer.record(value)) == 4, "diagnostic causal flag accepted");
    value.flags = 0; value.why = static_cast<decltype(value.why)>(1);
    require(static_cast<unsigned>(producer.record(value)) == 4, "diagnostic reason namespace mixed");
    value.why = static_cast<decltype(value.why)>(0); value.outcome = static_cast<decltype(value.outcome)>(1);
    require(static_cast<unsigned>(producer.record(value)) == 4, "diagnostic terminal accepted");
  }
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
void context_has_no_hot_allocation() {
  const auto sdk_before=sdk_trace::process_recorder().snapshot();
  const auto vendor_before=vendor_trace::process_recorder().snapshot();
  fail_next_trace_allocation.store(true);
  const bool unregistered=!sdk_trace::current_thread_registered()&&!vendor_trace::current_thread_registered();
  const bool query_did_not_allocate=fail_next_trace_allocation.exchange(false);
  require(unregistered&&query_did_not_allocate,"registration query lazily attached or allocated");
  require(sdk_trace::process_recorder().snapshot().registered_threads==sdk_before.registered_threads&&
    sdk_trace::process_recorder().snapshot().unregistered_writes==sdk_before.unregistered_writes&&
    vendor_trace::process_recorder().snapshot().registered_threads==vendor_before.registered_threads&&
    vendor_trace::process_recorder().snapshot().unregistered_writes==vendor_before.unregistered_writes,
    "registration query changed recorder counters");
  require(sdk_trace::register_current_thread(),"context owner registration failed");
  require(vendor_trace::register_current_thread(),"vendor context owner registration failed");
  require(sdk_trace::current_thread_registered()&&vendor_trace::current_thread_registered(),"active TLS registration query failed");
  sdk_trace::event outer; outer.task_id=sdk_trace::next_task_id();
  const auto vendor_id=vendor_trace::next_task_id();
  require(outer.task_id>0 && outer.task_id<0x8000000000000000ULL && vendor_id>0x8000000000000000ULL,
    "module task identity partition differs");
  require(sdk_trace::set_current_context(outer),"valid trace context rejected");
  sdk_trace::event child=outer; child.related_id=outer.task_id; child.task_id=sdk_trace::next_task_id();
  fail_next_trace_allocation.store(true);
  {
    const bool registered=sdk_trace::current_thread_registered()&&vendor_trace::current_thread_registered();
    sdk_trace::context_scope scope(child);
    sdk_trace::event inherited;
    const bool found=sdk_trace::current_context(inherited);
    (void)sdk_trace::record(child);
    sdk_trace::add(sdk_trace::counter::copy_bytes,4);
    sdk_trace::observe_duration(sdk_trace::duration_metric::engine_fifo,3);
    if(!registered || !found || inherited.task_id!=child.task_id)fail_next_trace_allocation.store(false);
  }
  const bool no_allocation=fail_next_trace_allocation.exchange(false);
  sdk_trace::event restored;
  require(no_allocation && sdk_trace::current_context(restored) && restored.task_id==outer.task_id,
    "context propagation allocated or failed to restore");
  sdk_trace::clear_current_context();
  require(!sdk_trace::current_context(restored),"context clear retained causal relation");
  sdk_trace::note_causal_loss();
  require(sdk_trace::process_recorder().snapshot().causal_lost==1,"explicit causal loss was hidden");
  sdk_trace::detach_current_thread();vendor_trace::detach_current_thread();
  require(!sdk_trace::current_thread_registered()&&!vendor_trace::current_thread_registered(),"paused TLS registration remained active");
  for(unsigned mount=0;mount<100;++mount) {
    require(sdk_trace::register_current_thread()&&vendor_trace::register_current_thread(),"same TLS owner could not resume");
    require(sdk_trace::current_thread_registered()&&vendor_trace::current_thread_registered(),"resumed TLS registration query failed");
    sdk_trace::detach_current_thread();vendor_trace::detach_current_thread();
    require(!sdk_trace::current_thread_registered()&&!vendor_trace::current_thread_registered(),"resumed TLS registration did not pause");
  }
  require(sdk_trace::process_recorder().snapshot().registered_threads==1 &&
    vendor_trace::process_recorder().snapshot().registered_threads==1 &&
    sdk_trace::process_recorder().snapshot().active_producers==0,
    "same-thread mount cycles consumed slots or retained an active producer");
}
struct registration_exit_probe final {
  std::atomic<bool>* observed;
  ~registration_exit_probe() {
    observed->store(!sdk_trace::current_thread_registered()&&!vendor_trace::current_thread_registered());
  }
};
void registration_query_tracks_thread_exit() {
  std::atomic<bool> initially_unregistered{false}, attached{false}, query_after_tls_release{false};
  std::thread owner([&] {
    // Construct first: this probe is destroyed after both cold registrations.
    thread_local registration_exit_probe probe{&query_after_tls_release};
    (void)probe;
    initially_unregistered.store(!sdk_trace::current_thread_registered()&&!vendor_trace::current_thread_registered());
    const bool registered=sdk_trace::register_current_thread()&&vendor_trace::register_current_thread();
    attached.store(registered&&sdk_trace::current_thread_registered()&&vendor_trace::current_thread_registered());
  });
  owner.join();
  require(initially_unregistered.load()&&attached.load()&&query_after_tls_release.load(),"registration query did not track actual TLS exit");
  require(!sdk_trace::current_thread_registered()&&!vendor_trace::current_thread_registered()&&
    sdk_trace::process_recorder().snapshot().active_producers==0&&
    vendor_trace::process_recorder().snapshot().active_producers==0,
    "exited producer stayed active or attached its observer");
}
class counting_buffer final : public std::streambuf {
 public:
  std::uint64_t bytes{0};
 protected:
  std::streamsize xsputn(const char*,std::streamsize count) override {
    bytes+=static_cast<std::uint64_t>(count);return count;
  }
  int_type overflow(int_type value) override {
    if(!traits_type::eq_int_type(value,traits_type::eof()))++bytes;
    return traits_type::not_eof(value);
  }
};
void export_limit_cannot_claim_complete() {
  sdk_trace::recorder source;
  require(source.configure(sdk_trace::current_identity(1,3),sdk_trace::mode::detailed),"limit session configure failed");
  auto producer=source.attach_current_thread();
  counting_buffer buffer;std::ostream output(&buffer);sdk_trace::exporter exporter(output,source);
  require(exporter.begin(),"limit header failed");
  sdk_trace::event value;value.point=sdk_trace::stage::task_begin;value.task_id=1;
  for(std::uint64_t index=1;index<=sdk_trace::maximum_export_events+1;++index) {
    value.qpc=index;require(producer.record(value)==sdk_trace::record_result::recorded,"limit fixture overflowed producer");
    if(index%256==0)(void)exporter.drain(256);
  }
  (void)exporter.drain(256);const auto capped_bytes=buffer.bytes;
  (void)exporter.drain(256);
  source.stop();producer=sdk_trace::writer{};
  require(exporter.limit_reached() && !exporter.finish() && buffer.bytes==capped_bytes &&
    buffer.bytes<64ULL*1024*1024 && source.snapshot().causal_lost==1,
    "export cap wrote indefinitely or fabricated complete end");
}
void source_failure_after_begin_refuses_end() {
  class failing_source final : public sdk_trace::trace_source {
   public:
    bool available{true};
    sdk_trace::scalar_snapshot snapshot() const noexcept override {
      sdk_trace::scalar_snapshot result;result.available=available;
      result.session=sdk_trace::current_identity(2,3);result.stopped=true;result.quiescent=true;return result;
    }
    sdk_trace::drain_result drain(sdk_trace::event*,std::size_t) noexcept override{return {};}
    void note_causal_loss(std::uint64_t) noexcept override{}
  } source;
  std::ostringstream output;sdk_trace::exporter exporter(output,source);
  require(exporter.begin(),"source failure fixture header rejected");source.available=false;
  require(!exporter.finish(),"unavailable backend fabricated complete end after begin");
}
void split_consumer_cannot_complete() {
  sdk_trace::recorder source;require(source.configure(sdk_trace::current_identity(3,4),sdk_trace::mode::detailed),"split source configure failed");
  auto producer=source.attach_current_thread();sdk_trace::event value;value.qpc=1;
  require(producer.record(value)==sdk_trace::record_result::recorded,"split producer failed");
  require(source.drain(&value,1).count==1,"other consumer did not drain");
  source.stop();producer=sdk_trace::writer{};
  std::ostringstream output;sdk_trace::exporter exporter(output,source);
  require(exporter.begin()&&!exporter.finish(),"split/missing records fabricated complete end");
}
void concurrent_live_queue_bound() {
  sdk_trace::recorder source;require(source.configure(sdk_trace::current_identity(4,5),sdk_trace::mode::detailed),"live bound configure failed");
  std::thread producer([&]{auto writer=source.attach_current_thread();sdk_trace::event value;
    for(std::uint64_t n=1;n<=10000;++n){value.qpc=n;(void)writer.record(value);}});
  std::thread consumer([&]{std::array<sdk_trace::event,64> values;
    for(unsigned n=0;n<10000;++n)(void)source.drain(values.data(),values.size());});
  bool bounded=true;
  for(unsigned n=0;n<10000;++n){const auto snapshot=source.snapshot();
    if(snapshot.queued>static_cast<std::uint64_t>(snapshot.registered_threads)*sdk_trace::events_per_thread)bounded=false;}
  producer.join();consumer.join();source.stop();require(bounded,"live occupancy exceeded physical ring bound");
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
    noncausal_diagnostic_fields<sdk_trace::recorder, sdk_trace::event>();
    noncausal_diagnostic_fields<vendor_trace::recorder, vendor_trace::event>();
    scalar_has_no_event_queue<sdk_trace::recorder, sdk_trace::event>();
    scalar_has_no_event_queue<vendor_trace::recorder, vendor_trace::event>();
    cold_allocation_failure<sdk_trace::recorder,sdk_trace::event,sdk_trace::identity,sdk_trace::mode,sdk_trace::exporter>(sdk_trace::current_identity(1,2),sdk_trace::mode::detailed);
    cold_allocation_failure<vendor_trace::recorder,vendor_trace::event,vendor_trace::identity,vendor_trace::mode,vendor_trace::exporter>(vendor_trace::current_identity(1,2),vendor_trace::mode::detailed);
    require(static_cast<unsigned>(sdk_trace::record(sdk_trace::event{})) == 6, "SDK hot path lazily registered");
    require(static_cast<unsigned>(vendor_trace::record(vendor_trace::event{})) == 6, "vendor hot path lazily registered");
    require(sdk_trace::process_recorder().snapshot().unregistered_writes == 1, "SDK unregistered write hidden");
    require(vendor_trace::process_recorder().snapshot().unregistered_writes == 1, "vendor unregistered write hidden");
    context_has_no_hot_allocation();
    registration_query_tracks_thread_exit();
    export_limit_cannot_claim_complete();
    source_failure_after_begin_refuses_end();
    split_consumer_cannot_complete();
    concurrent_live_queue_bound();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
