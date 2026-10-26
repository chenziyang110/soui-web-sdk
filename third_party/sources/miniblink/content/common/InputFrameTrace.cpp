#include "content/common/InputFrameTrace.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <iomanip>
#include <limits>
#include <locale>
#include <mutex>
#include <ostream>
#include <sstream>
#include <utility>

namespace content { namespace input_frame_trace {
namespace detail {
using atomic_count = std::atomic<std::uint64_t>;

struct thread_storage final {
  explicit thread_storage(std::uint32_t owner, mode requested) : owner_thread(owner) {
    for (auto& value : stages) value.store(0, std::memory_order_relaxed);
    for (auto& value : counters) value.store(0, std::memory_order_relaxed);
    for (auto& metric : durations)
      for (auto& value : metric) value.store(0, std::memory_order_relaxed);
    if (requested == mode::detailed) entries.reset(new event[events_per_thread]);
  }
  const std::uint32_t owner_thread;
  std::atomic<bool> active{true};
  std::unique_ptr<event[]> entries;
  std::uint64_t next_write{0}, last_qpc{0}; // actual owner only.
  atomic_count published{0}, consumed{0}, attempted{0}, recorded{0}, dropped{0};
  std::array<atomic_count, stage_count> stages;
  std::array<atomic_count, counter_count> counters;
  std::array<std::array<atomic_count, duration_buckets>, duration_metric_count> durations;
};

struct state final {
  std::mutex registration_mutex;
  identity session;
  mode recording_mode{mode::scalar};
  bool configured{false}; // frozen before publishing the first thread slot.
  std::atomic<bool> stopped{false}, consumer_busy{false};
  std::atomic<bool> saturated{false}, owner_violation{false};
  std::atomic<bool> invalid_event{false}, clock_invalid{false};
  atomic_count unregistered{0}, registration_rejections{0}, invalid_writes{0}, causal_lost{0};
  std::atomic<std::uint32_t> registered{0};
  std::atomic<std::uint32_t> registrars{0};
  std::array<std::unique_ptr<thread_storage>, maximum_threads> threads;
  std::size_t consumer_cursor{0}; // one drain consumer only.
};
} // namespace detail
namespace {
constexpr auto maximum_count = (std::numeric_limits<std::uint64_t>::max)();

// Only the registered producer writes these values. Readers use atomic loads.
void add_single(detail::atomic_count& target, std::uint64_t amount,
                detail::state& owner) noexcept {
  const auto previous = target.load(std::memory_order_relaxed);
  if (amount > maximum_count - previous) {
    target.store(maximum_count, std::memory_order_relaxed);
    owner.saturated.store(true, std::memory_order_relaxed);
  } else target.store(previous + amount, std::memory_order_relaxed);
}
// Exceptional misuse/registration counters can have multiple callers. No CAS
// retry loop; overflow marks the entire observation unreliable and clamps it.
void add_multiple(detail::atomic_count& target, detail::state& owner) noexcept {
  if (target.fetch_add(1, std::memory_order_relaxed) == maximum_count) {
    owner.saturated.store(true, std::memory_order_relaxed);
    target.store(maximum_count, std::memory_order_relaxed);
  }
}
void sum(std::uint64_t& target, std::uint64_t amount, bool& saturated) noexcept {
  if (amount > maximum_count - target) { target = maximum_count; saturated = true; }
  else target += amount;
}
bool range_valid(std::uint64_t first, std::uint64_t last) noexcept {
  return (!first && !last) || (first && last >= first);
}
bool valid(const event& value) noexcept {
  const auto point = static_cast<std::uint32_t>(value.point);
  if (value.struct_size != sizeof(event) || value.version != abi_version ||
      !point || point >= stage_count || value.producer_slot ||
      static_cast<std::uint32_t>(value.why) > static_cast<std::uint32_t>(reason::cancelled) ||
      static_cast<std::uint32_t>(value.outcome) > static_cast<std::uint32_t>(terminal::rejected) ||
      (value.flags & ~(coverage_valid | content_marker_same_submission | microtask_causality_established)) ||
      !range_valid(value.input_first, value.input_last) ||
      !range_valid(value.action_first, value.action_last)) return false;
  if (value.input_first && (!value.view_id || !value.view_generation || !value.document_generation)) return false;
  if (value.point == stage::input_terminal && (!value.input_first || value.outcome == terminal::none)) return false;
  if (value.point == stage::microtasks_complete && !(value.flags & microtask_causality_established)) return false;
  if ((value.flags & coverage_valid) && (!value.frame_id || !value.input_first)) return false;
  if (value.point == stage::frame_coverage && !(value.flags & coverage_valid)) return false;
  return true;
}
bool actual_owner(const detail::thread_storage& slot, detail::state& state) noexcept {
  if (slot.owner_thread == GetCurrentThreadId()) return true;
  state.owner_violation.store(true, std::memory_order_relaxed);
  add_multiple(state.invalid_writes, state);
  return false;
}
std::string run_id(const identity& session) {
  std::ostringstream output;
  output << std::hex << std::setfill('0') << std::setw(16) << session.run_id_high
         << std::setw(16) << session.run_id_low;
  return output.str();
}
void exact(std::ostream& output, const char* key, std::uint64_t value) {
  output << ",\"" << key << "\":\"" << std::dec << value << '"';
}
void write_event(std::ostream& output, const event& value) {
  output << "{\"event\":\"trace.point\",\"version\":1,\"point\":"
         << static_cast<std::uint32_t>(value.point) << ",\"why\":"
         << static_cast<std::uint32_t>(value.why) << ",\"outcome\":"
         << static_cast<std::uint32_t>(value.outcome) << ",\"flags\":" << value.flags
         << ",\"thread_id\":" << value.thread_id << ",\"producer_slot\":" << value.producer_slot;
  exact(output, "qpc", value.qpc); exact(output, "view_id", value.view_id);
  exact(output, "view_generation", value.view_generation);
  exact(output, "document_generation", value.document_generation);
  exact(output, "geometry_generation", value.geometry_generation);
  exact(output, "input_first", value.input_first); exact(output, "input_last", value.input_last);
  exact(output, "task_id", value.task_id); exact(output, "frame_id", value.frame_id);
  exact(output, "action_first", value.action_first); exact(output, "action_last", value.action_last);
  exact(output, "value", value.value); exact(output, "related_id", value.related_id);
  output << "}\n";
}
} // namespace

std::uint64_t now_qpc() noexcept {
  LARGE_INTEGER value{};
  return QueryPerformanceCounter(&value) && value.QuadPart > 0
      ? static_cast<std::uint64_t>(value.QuadPart) : 0;
}
identity current_identity(std::uint64_t high, std::uint64_t low) noexcept {
  identity result;
  result.run_id_high = high; result.run_id_low = low;
  LARGE_INTEGER frequency{};
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0 ||
      !GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return result;
  result.process_id = GetCurrentProcessId();
  result.process_incarnation = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
  result.qpc_frequency = static_cast<std::uint64_t>(frequency.QuadPart);
  return result;
}

writer::writer(std::shared_ptr<detail::state> storage, std::size_t slot) noexcept
    : storage_(std::move(storage)), slot_(slot) {}
writer::~writer() { release(); }
writer::writer(writer&& other) noexcept : storage_(std::move(other.storage_)), slot_(other.slot_) {
  other.slot_ = maximum_threads;
}
writer& writer::operator=(writer&& other) noexcept {
  if (this != &other) {
    release(); storage_ = std::move(other.storage_); slot_ = other.slot_;
    other.slot_ = maximum_threads;
  }
  return *this;
}
writer::operator bool() const noexcept { return storage_ && slot_ < maximum_threads; }
void writer::release() noexcept {
  if (*this) storage_->threads[slot_]->active.store(false, std::memory_order_release);
  storage_.reset(); slot_ = maximum_threads;
}
record_result writer::record(event value) noexcept {
  if (!*this) return record_result::unregistered;
  auto& state = *storage_; auto& slot = *state.threads[slot_];
  if (!actual_owner(slot, state)) return record_result::wrong_thread;
  add_single(slot.attempted, 1, state);
  if (state.stopped.load(std::memory_order_acquire)) {
    add_single(slot.dropped, 1, state); return record_result::stopped;
  }
  if (!valid(value) || (value.thread_id && value.thread_id != slot.owner_thread)) {
    state.invalid_event.store(true, std::memory_order_relaxed);
    add_single(slot.dropped, 1, state); return record_result::invalid;
  }
  add_single(slot.stages[static_cast<std::size_t>(value.point)], 1, state);
  if (state.recording_mode == mode::scalar) return record_result::scalar_only;
  value.thread_id = slot.owner_thread;
  value.producer_slot = static_cast<std::uint32_t>(slot_+1);
  if (!value.qpc) value.qpc = now_qpc();
  if (!value.qpc || value.qpc < slot.last_qpc) {
    state.clock_invalid.store(true, std::memory_order_relaxed);
    add_single(slot.dropped, 1, state); return record_result::invalid;
  }
  slot.last_qpc = value.qpc;
  const auto consumed = slot.consumed.load(std::memory_order_acquire);
  if (slot.next_write == maximum_count || slot.next_write - consumed >= events_per_thread) {
    if (slot.next_write == maximum_count) state.saturated.store(true, std::memory_order_relaxed);
    add_single(slot.dropped, 1, state); return record_result::full;
  }
  slot.entries[slot.next_write % events_per_thread] = value;
  ++slot.next_write;
  slot.published.store(slot.next_write, std::memory_order_release);
  add_single(slot.recorded, 1, state);
  return record_result::recorded;
}
void writer::add(counter metric, std::uint64_t amount) noexcept {
  if (!*this) return;
  auto& state = *storage_; auto& slot = *state.threads[slot_];
  if (!actual_owner(slot, state) || state.stopped.load(std::memory_order_acquire)) return;
  const auto index = static_cast<std::size_t>(metric);
  if (index >= counter_count) { state.invalid_event.store(true, std::memory_order_relaxed); return; }
  add_single(slot.counters[index], amount, state);
}
void writer::observe_duration(duration_metric metric, std::uint64_t ticks) noexcept {
  if (!*this) return;
  auto& state = *storage_; auto& slot = *state.threads[slot_];
  if (!actual_owner(slot, state) || state.stopped.load(std::memory_order_acquire)) return;
  const auto index = static_cast<std::size_t>(metric);
  if (index >= duration_metric_count) { state.invalid_event.store(true, std::memory_order_relaxed); return; }
  std::size_t bucket = 0;
  while (ticks > 1 && bucket + 1 < duration_buckets) { ticks >>= 1; ++bucket; }
  add_single(slot.durations[index][bucket], 1, state);
}

recorder::recorder() noexcept : fallback_identity_(current_identity(0,0)) {
  try {
    storage_ = std::make_shared<detail::state>();
    storage_->session = fallback_identity_;
  } catch (...) {
    // Diagnostics is unavailable. snapshot().available=false exposes this cold
    // allocation/construction failure; product/DLL startup must continue.
  }
}
recorder::~recorder() { stop(); }
bool recorder::configure(identity session, mode requested) noexcept {
  if (!storage_) return false;
  std::lock_guard<std::mutex> lock(storage_->registration_mutex);
  const auto actual = current_identity(session.run_id_high, session.run_id_low);
  if (storage_->configured || storage_->registered.load(std::memory_order_relaxed) ||
      storage_->stopped.load(std::memory_order_acquire) || session.version != abi_version ||
      !(session.run_id_high || session.run_id_low) || !session.process_id ||
      !session.process_incarnation || !session.qpc_frequency ||
      session.process_id != actual.process_id || session.process_incarnation != actual.process_incarnation ||
      session.qpc_frequency != actual.qpc_frequency ||
      (requested != mode::scalar && requested != mode::detailed)) return false;
  storage_->session = session; storage_->recording_mode = requested; storage_->configured = true;
  return true;
}
writer recorder::attach_current_thread() {
  if (!storage_) { unavailable_writes_.fetch_add(1,std::memory_order_relaxed); return {}; }
  struct registration_guard final {
    explicit registration_guard(std::atomic<std::uint32_t>& value) noexcept : count(value) {
      count.fetch_add(1, std::memory_order_acq_rel);
    }
    ~registration_guard() { count.fetch_sub(1, std::memory_order_release); }
    std::atomic<std::uint32_t>& count;
  } registration(storage_->registrars);
  std::lock_guard<std::mutex> lock(storage_->registration_mutex);
  auto& state = *storage_;
  const auto count = state.registered.load(std::memory_order_relaxed);
  const auto thread = GetCurrentThreadId();
  if (state.stopped.load(std::memory_order_acquire) || count == maximum_threads) {
    add_multiple(state.registration_rejections, state); return {};
  }
  for (std::size_t index = 0; index < count; ++index) {
    const auto& existing = *state.threads[index];
    if (existing.owner_thread == thread && existing.active.load(std::memory_order_acquire)) {
      add_multiple(state.registration_rejections, state); return {};
    }
  }
  try {
    state.threads[count].reset(new detail::thread_storage(thread, state.recording_mode));
  } catch (const std::bad_alloc&) {
    add_multiple(state.registration_rejections, state); return {};
  }
  state.registered.store(count + 1, std::memory_order_release);
  return writer(storage_, count);
}
void recorder::stop() noexcept {
  if (storage_) storage_->stopped.store(true, std::memory_order_release);
  else unavailable_stopped_.store(true,std::memory_order_release);
}
bool recorder::quiescent() const noexcept {
  if (!storage_) return unavailable_stopped_.load(std::memory_order_acquire);
  if (!storage_->stopped.load(std::memory_order_acquire)) return false;
  if (storage_->registrars.load(std::memory_order_acquire)) return false;
  const auto count = storage_->registered.load(std::memory_order_acquire);
  for (std::size_t index = 0; index < count; ++index)
    if (storage_->threads[index]->active.load(std::memory_order_acquire)) return false;
  return true;
}
scalar_snapshot recorder::snapshot() const noexcept {
  if (!storage_) {
    scalar_snapshot result; result.session = fallback_identity_;
    result.stopped = unavailable_stopped_.load(std::memory_order_acquire);
    result.quiescent = result.stopped;
    result.unregistered_writes = unavailable_writes_.load(std::memory_order_relaxed);
    result.causal_lost = unavailable_causal_.load(std::memory_order_relaxed);
    return result;
  }
  std::lock_guard<std::mutex> lock(storage_->registration_mutex);
  scalar_snapshot result;
  result.available = true;
  const auto& state = *storage_;
  result.session = state.session; result.recording_mode = state.recording_mode;
  result.registered_threads = state.registered.load(std::memory_order_acquire);
  result.pending_registrations = state.registrars.load(std::memory_order_acquire);
  result.stopped = state.stopped.load(std::memory_order_acquire);
  result.counter_saturated = state.saturated.load(std::memory_order_relaxed);
  result.owner_violation = state.owner_violation.load(std::memory_order_relaxed);
  result.invalid_event = state.invalid_event.load(std::memory_order_relaxed);
  result.clock_invalid = state.clock_invalid.load(std::memory_order_relaxed);
  result.unregistered_writes = state.unregistered.load(std::memory_order_relaxed);
  result.registration_rejections = state.registration_rejections.load(std::memory_order_relaxed);
  result.causal_lost = state.causal_lost.load(std::memory_order_relaxed);
  result.dropped = state.invalid_writes.load(std::memory_order_relaxed);
  for (std::size_t index = 0; index < result.registered_threads; ++index) {
    const auto& slot = *state.threads[index];
    if (slot.active.load(std::memory_order_acquire)) ++result.active_producers;
    sum(result.attempted, slot.attempted.load(std::memory_order_relaxed), result.counter_saturated);
    sum(result.recorded, slot.recorded.load(std::memory_order_relaxed), result.counter_saturated);
    sum(result.dropped, slot.dropped.load(std::memory_order_relaxed), result.counter_saturated);
    // Consumers may advance between these loads; read consumed first so this
    // approximate live occupancy can never underflow. Final snapshots are exact.
    const auto consumed = slot.consumed.load(std::memory_order_acquire);
    const auto published = slot.published.load(std::memory_order_acquire);
    // Live loads can span both producer and consumer advancement. Clamp this
    // conservative estimate to the physical slot bound; quiescent is exact.
    sum(result.queued, (std::min)(published-consumed,static_cast<std::uint64_t>(events_per_thread)), result.counter_saturated);
    for (std::size_t n = 0; n < stage_count; ++n)
      sum(result.stages[n], slot.stages[n].load(std::memory_order_relaxed), result.counter_saturated);
    for (std::size_t n = 0; n < counter_count; ++n)
      sum(result.counters[n], slot.counters[n].load(std::memory_order_relaxed), result.counter_saturated);
    for (std::size_t n = 0; n < duration_metric_count; ++n)
      for (std::size_t bucket = 0; bucket < duration_buckets; ++bucket)
        sum(result.durations[n][bucket], slot.durations[n][bucket].load(std::memory_order_relaxed), result.counter_saturated);
  }
  result.quiescent = result.stopped && result.active_producers == 0 && result.pending_registrations == 0;
  return result;
}
drain_result recorder::drain(event* output, std::size_t capacity) noexcept {
  drain_result result;
  if (!storage_ || !output || !capacity) return result;
  auto& state = *storage_;
  if (state.consumer_busy.exchange(true, std::memory_order_acquire)) { result.busy = true; return result; }
  const auto count = state.registered.load(std::memory_order_acquire);
  capacity = (std::min)(capacity, events_per_thread);
  std::size_t empty_scans = 0;
  while (result.count < capacity && empty_scans < count) {
    const auto index = state.consumer_cursor++ % count;
    auto& slot = *state.threads[index];
    const auto consumed = slot.consumed.load(std::memory_order_relaxed);
    if (consumed == slot.published.load(std::memory_order_acquire)) { ++empty_scans; continue; }
    empty_scans = 0;
    output[result.count++] = slot.entries[consumed % events_per_thread];
    slot.consumed.store(consumed + 1, std::memory_order_release);
  }
  state.consumer_busy.store(false, std::memory_order_release);
  return result;
}
void recorder::note_unregistered_write() noexcept {
  if (storage_) add_multiple(storage_->unregistered, *storage_);
  else unavailable_writes_.fetch_add(1,std::memory_order_relaxed);
}
void recorder::note_causal_loss(std::uint64_t amount) noexcept {
  if (!amount) return;
  if (!storage_) { unavailable_causal_.fetch_add(amount,std::memory_order_relaxed); return; }
  const auto previous = storage_->causal_lost.fetch_add(amount, std::memory_order_relaxed);
  if (amount > maximum_count - previous) {
    storage_->saturated.store(true, std::memory_order_relaxed);
    storage_->causal_lost.store(maximum_count, std::memory_order_relaxed);
  }
}

namespace {
recorder global_recorder; // storage allocated before application event handling.
thread_local writer* local_writer = nullptr; // trivial TLS; no hot initialization.
constexpr event empty_context{};
thread_local event local_context = empty_context;
thread_local bool context_valid = false;
std::atomic<std::uint64_t> task_sequence{0x8000000000000001ULL};
constexpr std::uint64_t task_sequence_limit = 0xffffffffffffffffULL;
struct local_registration final {
  ~local_registration() { local_writer = nullptr; context_valid = false; }
  writer producer;
};
}
recorder& process_recorder() noexcept { return global_recorder; }
bool register_current_thread() {
  if (local_writer && *local_writer) return true;
  thread_local local_registration registration; // constructed only in this cold API.
  if (registration.producer) {
    auto& state=*registration.producer.storage_;
    state.registrars.fetch_add(1,std::memory_order_acq_rel);
    bool resumed=false;
    {
      std::lock_guard<std::mutex> lock(state.registration_mutex);
      auto& slot=*state.threads[registration.producer.slot_];
      const auto owner=GetCurrentThreadId();bool duplicate=false;
      const auto count=state.registered.load(std::memory_order_acquire);
      for(std::size_t index=0;index<count;++index)
        if(state.threads[index]->owner_thread==owner&&state.threads[index]->active.load(std::memory_order_acquire))duplicate=true;
      if(!state.stopped.load(std::memory_order_acquire)&&slot.owner_thread==owner&&!duplicate) {
        slot.active.store(true,std::memory_order_release);resumed=true;
      } else add_multiple(state.registration_rejections,state);
    }
    state.registrars.fetch_sub(1,std::memory_order_release);
    if(resumed)local_writer=&registration.producer;
    return resumed;
  }
  registration.producer = global_recorder.attach_current_thread();
  if (!registration.producer) return false;
  local_writer = &registration.producer;
  return true;
}
bool current_thread_registered() noexcept {
  if (!local_writer || !*local_writer) return false;
  const auto& slot = *local_writer->storage_->threads[local_writer->slot_];
  return slot.active.load(std::memory_order_acquire);
}
void detach_current_thread() noexcept {
  if (local_writer && *local_writer)
    local_writer->storage_->threads[local_writer->slot_]->active.store(false,std::memory_order_release);
  local_writer = nullptr; context_valid = false;
}
record_result record(event value) noexcept {
  if (!local_writer) { global_recorder.note_unregistered_write(); return record_result::unregistered; }
  return local_writer->record(value);
}
void add(counter metric, std::uint64_t amount) noexcept {
  if (!local_writer) { global_recorder.note_unregistered_write(); return; }
  local_writer->add(metric, amount);
}
void observe_duration(duration_metric metric, std::uint64_t ticks) noexcept {
  if (!local_writer) { global_recorder.note_unregistered_write(); return; }
  local_writer->observe_duration(metric, ticks);
}
bool current_context(event& output) noexcept {
  if (!local_writer || !*local_writer || !context_valid) return false;
  output = local_context; return true;
}
bool set_current_context(const event& value) noexcept {
  if (!local_writer || !*local_writer) { global_recorder.note_unregistered_write(); return false; }
  if (!valid(value)) { note_causal_loss(); return false; }
  local_context = value; context_valid = true; return true;
}
void clear_current_context() noexcept { context_valid = false; local_context = empty_context; }
void note_causal_loss(std::uint64_t amount) noexcept { global_recorder.note_causal_loss(amount); }
std::uint64_t next_task_id() noexcept {
  if (!local_writer || !*local_writer) { global_recorder.note_unregistered_write(); return 0; }
  auto value = task_sequence.load(std::memory_order_relaxed);
  for (unsigned attempt = 0; attempt < 8; ++attempt) {
    if (!value || value == task_sequence_limit) break;
    if (task_sequence.compare_exchange_weak(value,value+1,std::memory_order_relaxed)) return value;
  }
  note_causal_loss(); add(counter::causal_context_dropped); return 0;
}
context_scope::context_scope(const event& value) noexcept {
  previous_valid_ = current_context(previous_); installed_ = set_current_context(value);
}
context_scope::~context_scope() {
  if (!installed_) return;
  clear_current_context();
  if (previous_valid_) (void)set_current_context(previous_);
}

exporter::exporter(std::ostream& output, trace_source& source) noexcept : output_(output), source_(source) {}
bool exporter::begin() {
  if (begun_ || finished_) return false;
  const auto snapshot = source_.snapshot();
  const auto& session = snapshot.session;
  if (!snapshot.available || !session.process_id || !session.process_incarnation || !session.qpc_frequency ||
      !(session.run_id_high || session.run_id_low)) return false;
  output_.imbue(std::locale::classic()); output_.flags(std::ios_base::dec); output_.width(0);
  output_ << std::dec << "{\"event\":\"trace.begin\",\"schema\":\"soui.input-frame-trace/v1\",\"version\":1,\"run_id\":\""
          << run_id(session) << "\",\"process_id\":" << session.process_id;
  exact(output_, "process_incarnation", session.process_incarnation);
  exact(output_, "qpc_frequency", session.qpc_frequency);
  output_ << ",\"mode\":\"" << (snapshot.recording_mode == mode::detailed ? "detailed" : "scalar")
          << "\",\"maximum_threads\":32,\"events_per_thread\":1024}\n";
  begun_ = output_.good(); return begun_;
}
drain_result exporter::drain(std::size_t budget) {
  drain_result result;
  if (!begun_ || finished_ || limit_reached_ || !output_.good()) return result;
  if (exported_ == maximum_export_events) {
    if (source_.snapshot().queued) { limit_reached_ = true; source_.note_causal_loss(); }
    return result;
  }
  budget = (std::min)((std::min)(budget, events_per_thread), maximum_export_events-exported_);
  std::array<event, 256> buffer;
  while (result.count < budget) {
    const auto next = source_.drain(buffer.data(), (std::min)(buffer.size(), budget - result.count));
    if (next.busy) { result.busy = true; break; }
    for (std::size_t n = 0; n < next.count; ++n) write_event(output_, buffer[n]);
    result.count += next.count;
    exported_ += next.count;
    if (!next.count || !output_.good()) break;
  }
  return result;
}
bool exporter::finish() {
  if (finished_) return true;
  if (!begun_ || limit_reached_ || !output_.good()) return false;
  const auto value = source_.snapshot();
  if (!value.available || !value.quiescent || value.queued || value.recorded!=exported_) return false;
  output_ << "{\"event\":\"trace.end\",\"complete\":true,\"registered_threads\":" << value.registered_threads
          << ",\"active_producers\":" << value.active_producers;
  exact(output_, "attempted", value.attempted); exact(output_, "recorded", value.recorded);
  exact(output_, "dropped", value.dropped); exact(output_, "queued", value.queued);
  exact(output_, "unregistered_writes", value.unregistered_writes);
  exact(output_, "registration_rejections", value.registration_rejections);
  exact(output_, "causal_lost", value.causal_lost);
  output_ << ",\"counter_saturated\":" << (value.counter_saturated ? "true" : "false")
          << ",\"owner_violation\":" << (value.owner_violation ? "true" : "false")
          << ",\"invalid_event\":" << (value.invalid_event ? "true" : "false")
          << ",\"clock_invalid\":" << (value.clock_invalid ? "true" : "false") << ",\"stages\":[";
  for (std::size_t n = 0; n < value.stages.size(); ++n) { if (n) output_ << ','; output_ << '"' << value.stages[n] << '"'; }
  output_ << "],\"counters\":[";
  for (std::size_t n = 0; n < value.counters.size(); ++n) { if (n) output_ << ','; output_ << '"' << value.counters[n] << '"'; }
  output_ << "],\"durations\":[";
  for (std::size_t n = 0; n < duration_metric_count; ++n) {
    if (n) output_ << ','; output_ << '[';
    for (std::size_t bucket = 0; bucket < duration_buckets; ++bucket) {
      if (bucket) output_ << ','; output_ << '"' << value.durations[n][bucket] << '"';
    }
    output_ << ']';
  }
  output_ << "]}\n";
  finished_ = output_.good(); return finished_;
}
} } // namespace content::input_frame_trace
