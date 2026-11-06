#include "native_bridge_phase_observation.h"
#include <json.hpp>

#include <atomic>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>
#include <type_traits>

namespace soui::sdk {
#include "native-phase-json.inc"
}

namespace {
std::atomic<std::uint64_t> allocations{0};
}
void* operator new(std::size_t size) {
  allocations.fetch_add(1);
  if (void* value = std::malloc(size ? size : 1)) return value;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace test {
namespace phase = soui::sdk::detail;
struct fake_clock;
using recorder = phase::basic_native_phase_recorder<fake_clock>;
using scope = phase::basic_native_phase_scope<fake_clock>;
struct fake_clock final {
  static inline std::atomic<std::uint64_t> calls{0}, owner_calls{0}, next{100};
  static inline thread_local std::uint32_t owner = 7;
  static inline thread_local std::array<std::uint64_t, 16> values{};
  static inline thread_local std::size_t size{}, index{};
  static inline const recorder* require_even{};
  static inline std::atomic<unsigned> odd_clock_calls{0};
  static std::uint64_t qpc() noexcept;
  static std::uint32_t thread_id() noexcept { owner_calls.fetch_add(1); return owner; }
  static void reset() noexcept {
    calls.store(0); owner_calls.store(0); next.store(100); owner = 7;
    values = {}; size = index = 0; require_even = nullptr; odd_clock_calls.store(0);
  }
  static void script(std::initializer_list<std::uint64_t> ticks) {
    if (ticks.size() > values.size()) throw std::runtime_error("Clock script capacity exceeded");
    size = ticks.size(); index = 0; std::size_t n = 0;
    for (const auto tick : ticks) values[n++] = tick;
  }
};
}

namespace soui::sdk::detail {
// This definition exists in the fixture only; no macro changes the production
// class definition. It seeds boundary states and invokes the real snapshot
// algorithm, rather than reimplementing the recorder.
struct native_phase_test_access final {
  template<class C, bool L>
  static std::uint64_t sequence(const basic_native_phase_recorder<C,L>& value) noexcept {
    return value.sequence_.load();
  }
  template<class C, bool L>
  static void sequence(basic_native_phase_recorder<C,L>& value, std::uint64_t n) noexcept {
    value.sequence_.store(n);
  }
  template<class C, bool L>
  static void started(basic_native_phase_recorder<C,L>& value, std::uint64_t n) noexcept {
    value.phases_[0].started.store(n);
  }
  template<class C, bool L>
  static void completed(basic_native_phase_recorder<C,L>& value, std::uint64_t n) noexcept {
    value.phases_[0].completed.store(n);
  }
  template<class C, bool L>
  static void returned(basic_native_phase_recorder<C,L>& value, std::uint64_t n) noexcept {
    value.phases_[0].returned.store(n);
  }
  template<class C, bool L>
  static void sum(basic_native_phase_recorder<C,L>& value, std::uint64_t n) noexcept {
    value.phases_[0].sum_elapsed_qpc.store(n);
  }
  template<class C, bool L>
  static void bucket(basic_native_phase_recorder<C,L>& value, unsigned at, std::uint64_t n) noexcept {
    value.phases_[0].buckets[at].store(n);
  }
  template<class C, bool L>
  static void nested(basic_native_phase_recorder<C,L>& value, std::uint64_t n) noexcept {
    value.phases_[0].nested_starts.store(n);
  }
  template<class Probe, class C, bool L>
  static native_phase_snapshot snapshot(const basic_native_phase_recorder<C,L>& value) noexcept {
    return value.template snapshot_with_probe<Probe>();
  }
};
}

namespace test {
using access = phase::native_phase_test_access;
constexpr auto first = phase::native_bridge_phase::native_first_parse;
constexpr auto resolver = phase::native_bridge_phase::resolver_create_js;
constexpr auto reply = phase::native_bridge_phase::reply_run_js;
constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();

std::uint64_t fake_clock::qpc() noexcept {
  calls.fetch_add(1);
  if (require_even && (access::sequence(*require_even) & 1U)) odd_clock_calls.fetch_add(1);
  return index < size ? values[index++] : next.fetch_add(1);
}
void need(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::shared_ptr<recorder> bound(std::uint64_t incarnation = 1) {
  auto result = std::make_shared<recorder>(incarnation);
  need(result->bind_on_current_thread(1000), "Initial binding failed");
  return result;
}
std::uint64_t bucket_sum(const phase::native_phase_values& value) {
  std::uint64_t result = 0; for (const auto bucket : value.buckets) result += bucket; return result;
}
void configuration() {
  need(phase::parse_native_phase_option({}) == phase::native_phase_option::disabled, "Unset config enabled");
  need(phase::parse_native_phase_option("") == phase::native_phase_option::disabled, "Empty config enabled");
  need(phase::parse_native_phase_option("0") == phase::native_phase_option::disabled, "Zero config enabled");
  need(phase::parse_native_phase_option("1") == phase::native_phase_option::enabled, "One config disabled");
  for (const auto* value : {"01", "true", "scalar", " 1", "1 ", "2"})
    need(phase::parse_native_phase_option(value) == phase::native_phase_option::invalid, "Invalid config accepted");
  need(phase::parse_native_phase_option(std::string_view("\0",1)) == phase::native_phase_option::invalid, "Embedded nul accepted");
}
void null_scope() {
  fake_clock::reset();
  const auto before = allocations.load();
  { scope disabled({}, first); }
  need(allocations.load() == before, "Disabled scope allocated");
  need(fake_clock::calls.load() == 0 && fake_clock::owner_calls.load() == 0, "Disabled scope read clock/owner");
}
void binding() {
  fake_clock::reset();
  auto value = std::make_shared<recorder>(1);
  const auto pending = value->snapshot();
  need(pending.status == phase::native_phase_status::not_started && !pending.usable, "Unbound source was available");
  need(fake_clock::calls.load() == 0, "Unbound snapshot read QPC");
  need(value->bind_on_current_thread(1000), "Cold bind rejected");
  need(fake_clock::calls.load() == 0, "Binding read QPC");
  const auto ready = value->snapshot();
  need(ready.usable && ready.coherent && ready.qpc_frequency == 1000 && ready.observer_incarnation == 1, "Bound snapshot identity invalid");
  need(ready.snapshot_attempts == 1 && ready.publication_sequence == 0, "Cold snapshot changed publication");
  need(!value->bind_on_current_thread(1000), "Repeated binding accepted");
  const auto repeated = value->snapshot();
  need(!repeated.usable && (repeated.faults & phase::native_phase_binding_invalid), "Repeated binding fault lost");
}
void unbound_scope() {
  fake_clock::reset(); auto value = std::make_shared<recorder>(1);
  { scope early(value, first); }
  need(fake_clock::calls.load() == 0, "Unbound scope read QPC");
  need(!value->bind_on_current_thread(1000), "Binding laundered lost pre-bind scope");
  need(value->snapshot().faults & phase::native_phase_unbound_scope, "Unbound fault absent");
}
void unsupported_atomic() {
  fake_clock::reset();
  using unavailable_recorder = phase::basic_native_phase_recorder<fake_clock, false>;
  using unavailable_scope = phase::basic_native_phase_scope<fake_clock, false>;
  auto value = std::make_shared<unavailable_recorder>(1);
  need(!value->bind_on_current_thread(1000), "Unsupported atomic bound");
  { unavailable_scope disabled(value, first); }
  const auto result = value->snapshot();
  need(!result.usable && result.status == phase::native_phase_status::unavailable &&
       (result.faults & phase::native_phase_atomic_unavailable), "Unsupported atomic became zero measurement");
  need(fake_clock::calls.load() == 0 && fake_clock::owner_calls.load() == 0, "Unsupported source touched clock");
}
void three_phases() {
  fake_clock::reset(); auto value = bound(); fake_clock::require_even = value.get();
  fake_clock::script({100,105,110,119,120,137,200,201});
  { scope timed(value, first); } { scope timed(value, resolver); } { scope timed(value, reply); }
  const auto result = value->snapshot();
  need(result.usable && result.publication_sequence == 12, "Three phases not fully published");
  for (std::size_t n = 0; n < 3; ++n) {
    const auto& item = result.phases[n];
    need(item.started == 1 && item.completed == 1 && item.returned == 1 && item.unwound == 0 && item.in_flight == 0 && bucket_sum(item) == 1, "Phase counts mixed");
  }
  need(result.phases[0].buckets[2] == 1 && result.phases[0].sum_elapsed_qpc == 5, "Parse duration wrong");
  need(result.phases[1].buckets[3] == 1 && result.phases[1].sum_elapsed_qpc == 9, "Resolver duration wrong");
  need(result.phases[2].buckets[4] == 1 && result.phases[2].sum_elapsed_qpc == 17, "Reply duration wrong");
  need(fake_clock::odd_clock_calls.load() == 0, "QPC occurred during odd publication");
  fake_clock::require_even = nullptr;
}
void returns_and_unwinding() {
  fake_clock::reset(); auto value = bound(); unsigned calls = 0;
  auto call = [&] { scope timed(value, first); ++calls; return 17; };
  need(call() == 17 && calls == 1, "Instrumentation changed target call/return");
  bool caught = false;
  try { scope timed(value, first); ++calls; throw 23; } catch (int result) { caught = result == 23; }
  const auto result = value->snapshot();
  need(caught && calls == 2, "Instrumentation changed original exception");
  need(result.usable && result.phases[0].returned == 1 && result.phases[0].unwound == 1 && result.phases[0].completed == 2, "Unwind counted as success");
}
void normal_scope_during_existing_unwind() {
  fake_clock::reset(); auto value = bound();
  struct cleanup final {
    std::shared_ptr<recorder> metrics;
    ~cleanup() { scope nested(metrics, resolver); }
  };
  try { cleanup on_exit{value}; scope outer(value,first); throw 11; } catch (int) {}
  const auto result = value->snapshot();
  need(result.usable && result.phases[0].unwound == 1 && result.phases[1].returned == 1 && result.phases[1].unwound == 0,
       "Existing exception made a normally returned cleanup scope look unwound");
}
void cross_phase_reentry() {
  fake_clock::reset(); auto value = bound(); fake_clock::require_even = value.get();
  fake_clock::script({100,110,120,150,200,201});
  { scope outer(value, first); { scope inner(value, reply); } }
  const auto result = value->snapshot();
  need(result.usable && result.phases[0].sum_elapsed_qpc == 50 && result.phases[2].sum_elapsed_qpc == 10, "Reentry is not inclusive");
  need(result.phases[0].nested_starts == 0 && result.phases[2].nested_starts == 1, "Nested attribution lost");
  need(fake_clock::odd_clock_calls.load() == 0, "Nested QPC saw held publication");
  fake_clock::require_even = nullptr;
}
void same_phase_reentry_live_read() {
  fake_clock::reset(); auto value = bound(); fake_clock::script({100,110,120,125,126,140,200,201});
  {
    scope outer(value, first); { scope inner(value, first); }
    const auto live = value->snapshot();
    need(live.usable && live.coherent && live.phases[0].in_flight == 1 && live.phases[0].completed == 1, "Reader waited for outer JS or hid live scope");
  }
  const auto result = value->snapshot();
  need(result.usable && result.phases[0].nested_starts == 1 && result.phases[0].completed == 2 && result.phases[0].sum_elapsed_qpc == 50, "Same-phase inclusive counts wrong");
}
void retired_block_lifetime() {
  fake_clock::reset(); auto old = bound(1); std::weak_ptr<recorder> weak = old;
  std::shared_ptr<recorder> retained;
  auto next = bound(2);
  {
    scope pending(old, first); old.reset();
    need(!weak.expired(), "Scope did not retain old metrics");
    retained = weak.lock();
  }
  need(retained->snapshot().phases[0].completed == 1, "Retired metrics missed completion");
  need(next->snapshot().phases[0].started == 0, "Old completion entered new mount");
  retained.reset(); need(weak.expired(), "Metrics retained after final scope/reference");
}
void wrong_owner_begin() {
  fake_clock::reset(); auto value = bound(); fake_clock::owner = 9;
  { scope wrong(value, first); }
  need(fake_clock::calls.load() == 0, "Wrong owner sampled duration");
  fake_clock::owner = 7; const auto result = value->snapshot();
  need(!result.usable && (result.faults & phase::native_phase_owner_violation) && result.phases[0].started == 0 && result.publication_sequence == 0, "Wrong owner became second writer");
}
void wrong_owner_finish() {
  fake_clock::reset(); auto value = bound();
  { scope entered(value, first); fake_clock::owner = 9; }
  need(fake_clock::calls.load() == 1, "Wrong owner destructor sampled QPC");
  fake_clock::owner = 7; const auto result = value->snapshot();
  need(!result.usable && result.phases[0].in_flight == 1 && result.phases[0].completed == 0, "Wrong owner fabricated retirement");
}
void invalid_phase() {
  fake_clock::reset(); auto value = bound();
  { scope wrong(value, static_cast<phase::native_bridge_phase>(99)); }
  need(fake_clock::calls.load() == 0, "Invalid phase sampled clock");
  const auto result = value->snapshot();
  need(!result.usable && (result.faults & phase::native_phase_invalid_phase) && result.publication_sequence == 0, "Invalid phase changed counts");
}
void invalid_cold_identity() {
  fake_clock::reset(); auto incarnation = std::make_shared<recorder>(0);
  need(!incarnation->bind_on_current_thread(1000) && (incarnation->snapshot().faults & phase::native_phase_incarnation_invalid), "Zero incarnation accepted");
  auto frequency = std::make_shared<recorder>(1);
  need(!frequency->bind_on_current_thread(0) && (frequency->snapshot().faults & phase::native_phase_clock_invalid), "Zero frequency accepted");
  fake_clock::owner = 0; auto owner = std::make_shared<recorder>(2);
  need(!owner->bind_on_current_thread(1000) && (owner->snapshot().faults & phase::native_phase_owner_violation), "Zero owner accepted");
  need(fake_clock::calls.load() == 0, "Invalid binding sampled QPC");
}
void invalid_duration_clock() {
  for (const auto pair : {std::array<std::uint64_t,2>{0,20}, {20,0}, {20,19}}) {
    fake_clock::reset(); auto value = bound(); fake_clock::script({pair[0],pair[1],30,31});
    { scope timed(value, first); }
    const auto result = value->snapshot();
    need(!result.usable && (result.faults & phase::native_phase_clock_invalid), "Invalid duration clock accepted");
    need(result.phases[0].started == 1 && result.phases[0].completed == 1 && result.phases[0].in_flight == 0 && bucket_sum(result.phases[0]) == 0, "Clock failure fabricated duration or lost retirement");
  }
}
void duration_bucket_boundaries() {
  const struct { std::uint64_t ticks; unsigned bucket; } cases[] = {
    {0,0},{1,0},{2,1},{3,1},{4,2},
    {std::uint64_t{1}<<31,31},{std::uint64_t{1}<<32,32},{std::uint64_t{1}<<63,63},
  };
  for (const auto& sample : cases) {
    const auto ticks = sample.ticks;
    fake_clock::reset(); auto value = bound(); fake_clock::script({1,1+ticks,maximum-1,maximum});
    { scope timed(value, first); }
    const auto result = value->snapshot();
    need(result.usable && result.phases[0].buckets[sample.bucket] == 1 && bucket_sum(result.phases[0]) == 1 && result.phases[0].sum_elapsed_qpc == ticks, "Duration boundary mapped incorrectly");
  }
}
void begin_counter_exhaustion() {
  fake_clock::reset(); auto value = bound(); access::started(*value,maximum);
  { scope rejected(value, first); }
  need(fake_clock::calls.load() == 0, "Counter-exhausted scope sampled clock");
  const auto result = value->snapshot();
  need(!result.usable && (result.faults & phase::native_phase_counter_saturated) && result.phases[0].started == maximum && result.phases[0].in_flight == 0, "Begin counter wrapped or admitted scope");
}
void finish_counter_exhaustion() {
  fake_clock::reset(); auto value = bound();
  access::completed(*value,maximum); access::returned(*value,maximum); access::sum(*value,maximum-1); access::bucket(*value,2,maximum);
  fake_clock::script({10,14,20,21}); { scope timed(value, first); }
  const auto result = value->snapshot();
  need(!result.usable && (result.faults & phase::native_phase_counter_saturated), "Finish overflow usable");
  const auto& item = result.phases[0];
  need(item.completed == maximum && item.returned == maximum && item.sum_elapsed_qpc == maximum && item.buckets[2] == maximum && item.in_flight == 0, "Finish overflow wrapped or lost owned retirement");
}
void nested_counter_exhaustion() {
  fake_clock::reset(); auto value = bound(); access::nested(*value,maximum);
  { scope outer(value, first); { scope rejected(value, first); } }
  const auto result = value->snapshot();
  need(!result.usable && result.phases[0].started == 1 && result.phases[0].completed == 1 && result.phases[0].in_flight == 0, "Rejected nested scope retired outer count");
}
void sequence_exhaustion() {
  fake_clock::reset(); auto value = bound(); access::sequence(*value,maximum-1);
  { scope rejected(value,first); }
  need(fake_clock::calls.load() == 0, "Exhausted publication sampled clock");
  const auto result = value->snapshot();
  need(!result.usable && (result.faults & phase::native_phase_sequence_exhausted) && access::sequence(*value) == maximum-1 && result.phases[0].started == 0, "Sequence wrapped/reused");
}
void odd_snapshot_busy() {
  fake_clock::reset(); auto value = bound(); access::sequence(*value,1);
  const auto result = value->snapshot();
  need(!result.usable && !result.coherent && result.status == phase::native_phase_status::busy && result.snapshot_attempts == 3, "Odd publication did not stop at three attempts");
  need(access::sequence(*value) == 1 && fake_clock::calls.load() == 2, "Reader changed publication or repeated QPC per retry");
}
struct conflict_probe final {
  static inline std::shared_ptr<recorder> target;
  static inline unsigned remaining{}, calls{};
  static void after_first_sequence(unsigned) noexcept {
    ++calls;
    if (remaining) { --remaining; scope change(target, first); }
  }
  static void after_fields(unsigned) noexcept {}
};
void bounded_snapshot_conflicts() {
  for (const unsigned conflicts : {1U,3U}) {
    fake_clock::reset(); auto value = bound(); conflict_probe::target=value;conflict_probe::remaining=conflicts;conflict_probe::calls=0;
    const auto result = access::snapshot<conflict_probe>(*value);
    need(result.snapshot_attempts == (conflicts == 1 ? 2U : 3U) && conflict_probe::calls == result.snapshot_attempts, "Snapshot retry cap changed");
    need(result.coherent == (conflicts == 1) && result.usable == (conflicts == 1), "Conflicted snapshot accepted");
    if (conflicts == 1) need(result.phases[0].completed == 1, "Stable retry lost committed sample");
    else {
      need(result.status == phase::native_phase_status::busy && result.phases[0].started == 0, "Busy snapshot exposed partial buckets");
      const auto later = value->snapshot();
      need(later.usable && later.phases[0].completed == 3, "Busy reader discarded committed producer work");
    }
    conflict_probe::target.reset();
  }
}
struct reader_fault_probe final {
  static inline std::shared_ptr<recorder> target;
  static void after_first_sequence(unsigned) noexcept {}
  static void after_fields(unsigned) noexcept {
    const auto previous = fake_clock::owner; fake_clock::owner = 9;
    { scope rejected(target,first); }
    fake_clock::owner = previous;
  }
};
void fault_during_snapshot() {
  fake_clock::reset(); auto value = bound(); reader_fault_probe::target = value;
  const auto result = access::snapshot<reader_fault_probe>(*value);
  need(result.coherent && !result.usable && (result.faults & phase::native_phase_owner_violation) && result.publication_sequence == 0, "Concurrent fault was lost or mutated counters");
  reader_fault_probe::target.reset();
}
void reader_clock_fault_sticky() {
  for (const auto pair : {std::array<std::uint64_t,2>{0,200}, {200,0}, {200,199}}) {
    fake_clock::reset(); auto value = bound(); { scope timed(value, first); }
    const auto sequence = access::sequence(*value);fake_clock::script({pair[0],pair[1]});
    const auto failed = value->snapshot();
    need(!failed.usable && (failed.faults & phase::native_phase_clock_invalid) && access::sequence(*value) == sequence, "Reader fault changed writer sequence");
    fake_clock::script({300,301}); const auto later = value->snapshot();
    need(!later.usable && (later.faults & phase::native_phase_clock_invalid) && later.phases[0].completed == 1, "Later valid clock washed away reader fault");
  }
}
void concurrent_reader_clock_fault() {
  fake_clock::reset(); auto value = bound();
  {
    scope outer(value, first); const auto sequence = access::sequence(*value);
    phase::native_phase_snapshot observed;
    std::thread reader([&] { fake_clock::owner=9; fake_clock::script({0,200}); observed=value->snapshot(); });
    reader.join();
    need(!observed.usable && observed.coherent && observed.phases[0].in_flight == 1 && access::sequence(*value) == sequence, "UI reader fault touched Engine publication");
  }
  const auto result=value->snapshot();
  need(!result.usable && result.phases[0].completed == 1 && result.phases[0].in_flight == 0, "Reader fault prevented owned scope retirement");
}

void serializer_worst_u64() {
  phase::native_phase_snapshot value;
  value.usable=value.coherent=true;value.status=phase::native_phase_status::ready;
  value.observer_incarnation=value.qpc_frequency=value.capture_begin_qpc=value.capture_end_qpc=value.publication_sequence=maximum;
  value.snapshot_attempts=3;
  for(auto& item:value.phases){
    item.started=item.completed=item.returned=item.unwound=item.in_flight=item.nested_starts=item.sum_elapsed_qpc=maximum;
    item.buckets.fill(maximum);
  }
  const auto encoded=soui::sdk::native_bridge_phase_json(value,true,false,"ready");
  need(encoded.size()<=8192,"Production serializer exceeds bounded extension");
  const auto parsed=nlohmann::json::parse(encoded);
  need(parsed["schema"]=="soui.miniblink-native-phases/v1" && parsed["phases"].size()==3,"Production schema/phase count changed");
  const auto exact=std::to_string(maximum);
  for(const auto* field:{"observerIncarnation","qpcFrequency","captureBeginQpc","captureEndQpc","publicationSequence"})
    need(parsed[field].is_string()&&parsed[field].get<std::string>()==exact,"uint64 metadata lost precision");
  for(std::size_t n=0;n<phase::native_phase_count;++n){
    const auto& item=parsed["phases"][n];
    need(item["phase"]==phase::native_phase_name(static_cast<phase::native_bridge_phase>(n)),"Fixed phase identity changed");
    for(const auto* field:{"started","completed","returned","unwound","inFlight","nestedStarts","sumElapsedQpc"})
      need(item[field].is_string()&&item[field].get<std::string>()==exact,"uint64 counter lost precision");
    need(item["buckets"].is_array()&&item["buckets"].size()==64,"Histogram ABI shape changed");
    for(const auto& bucket:item["buckets"])need(bucket.is_string()&&bucket.get<std::string>()==exact,"uint64 bucket lost precision");
  }
  need(parsed["inclusiveWallTime"]==true&&parsed["wholeHostAtomic"]==false&&parsed["quiescent"]==false,"Serializer exaggerated observation scope");
}
void serializer_available_zero() {
  fake_clock::reset();auto value=bound();
  const auto parsed=nlohmann::json::parse(soui::sdk::native_bridge_phase_json(value->snapshot(),true,false,"ready"));
  need(parsed["usable"]==true&&parsed["publicationSequence"]=="0","Valid zero sequence treated as missing");
  for(const auto& item:parsed["phases"]){
    need(item["started"]=="0"&&item["completed"]=="0","Valid empty source lost zero counts");
    for(const auto& bucket:item["buckets"])need(bucket=="0","Valid empty bucket not exact string zero");
  }
}
void serializer_unavailable_sources() {
  fake_clock::reset();auto value=bound();fake_clock::script({0,200});
  auto parsed=nlohmann::json::parse(soui::sdk::native_bridge_phase_json(value->snapshot(),true,true,"unavailable"));
  need(parsed["phases"].is_null()&&parsed["usable"]==false&&parsed["clockInvalid"]==true&&parsed["closing"]==true,
       "Real clock fault serialized as zero measurements");
  need(parsed["captureBeginQpc"].is_null()&&parsed["captureEndQpc"]=="200","Missing QPC disguised as precise zero");
  phase::native_phase_snapshot absent;
  absent.faults=phase::native_phase_counter_saturated|phase::native_phase_owner_violation|phase::native_phase_sequence_exhausted;
  parsed=nlohmann::json::parse(soui::sdk::native_bridge_phase_json(absent,false,false,"unavailable"));
  need(parsed["phases"].is_null()&&parsed["observerIncarnation"].is_null()&&parsed["publicationSequence"].is_null()&&parsed["qpcFrequency"].is_null(),
       "Unavailable storage emitted measured zero identities/buckets");
  need(parsed["counterSaturated"]==true&&parsed["ownerViolation"]==true&&parsed["sequenceExhausted"]==true,"Sticky source flags omitted");
}
void serializer_length_rejection() {
  phase::native_phase_snapshot value;const std::string oversized(8192,'x');bool rejected=false;
  try{(void)soui::sdk::native_bridge_phase_json(value,false,false,oversized.c_str());}
  catch(const std::length_error&){rejected=true;}
  need(rejected,"Production serializer no longer enforces 8 KiB limit");
}
void serializer_legacy_script_suffix() {
  fake_clock::reset();auto value=bound();
  const auto extension=soui::sdk::native_bridge_phase_json(value->snapshot(),true,false,"ready");
  const std::string old_script="{\"marker\":true}";
  const auto diagnostics=std::string("document=ready,bridge=ready,native_phases=")+extension+",script="+old_script;
  const auto marker=diagnostics.find(",script=");
  need(marker!=std::string::npos&&diagnostics.find(",script=",marker+1)==std::string::npos,"Extension injected a legacy delimiter");
  const auto script=nlohmann::json::parse(diagnostics.substr(marker+8));
  need(script["marker"]==true,"Original parse-to-end script consumer broke");
}

static_assert(!std::is_copy_constructible<scope>::value && !std::is_move_constructible<scope>::value);
static_assert(std::is_nothrow_destructible<scope>::value);
static_assert(std::is_trivially_copyable<phase::native_phase_snapshot>::value);
}

int main() {
  struct entry { const char* name; void (*run)(); };
  const entry cases[] = {
    {"configuration",test::configuration},{"null-scope",test::null_scope},
    {"binding",test::binding},{"unbound-scope",test::unbound_scope},
    {"unsupported-atomic",test::unsupported_atomic},{"three-phases",test::three_phases},
    {"return-and-unwind",test::returns_and_unwinding},{"normal-during-unwind",test::normal_scope_during_existing_unwind},
    {"cross-phase-reentry",test::cross_phase_reentry},
    {"same-phase-live-read",test::same_phase_reentry_live_read},{"retired-metrics",test::retired_block_lifetime},
    {"wrong-owner-begin",test::wrong_owner_begin},{"wrong-owner-finish",test::wrong_owner_finish},
    {"invalid-phase",test::invalid_phase},{"invalid-cold-identity",test::invalid_cold_identity},
    {"invalid-duration-clock",test::invalid_duration_clock},{"bucket-boundaries",test::duration_bucket_boundaries},
    {"begin-counter-overflow",test::begin_counter_exhaustion},{"finish-counter-overflow",test::finish_counter_exhaustion},
    {"nested-counter-overflow",test::nested_counter_exhaustion},{"sequence-exhaustion",test::sequence_exhaustion},
    {"odd-snapshot-busy",test::odd_snapshot_busy},{"bounded-snapshot-conflicts",test::bounded_snapshot_conflicts},
    {"fault-during-snapshot",test::fault_during_snapshot},{"reader-clock-sticky",test::reader_clock_fault_sticky},
    {"concurrent-reader-clock",test::concurrent_reader_clock_fault},
    {"serializer-worst-u64",test::serializer_worst_u64},{"serializer-available-zero",test::serializer_available_zero},
    {"serializer-unavailable",test::serializer_unavailable_sources},{"serializer-length-rejection",test::serializer_length_rejection},
    {"serializer-legacy-suffix",test::serializer_legacy_script_suffix},
  };
  unsigned passed=0;
  for (const auto& item : cases) {
    try { item.run(); ++passed; }
    catch (const std::exception& error) { std::cerr << item.name << ": " << error.what() << '\n'; return 1; }
    catch (...) { std::cerr << item.name << ": unexpected exception\n"; return 1; }
  }
  std::cout << "{\"passed\":" << passed << ",\"scope\":\"actual-private-helper-cpu-contracts\","
            << "\"recorderBytes\":" << sizeof(test::recorder) << ",\"snapshotBytes\":" << sizeof(soui::sdk::detail::native_phase_snapshot)
            << ",\"guiExecuted\":false,\"performanceVerified\":false}\n";
  return 0;
}
