#include <soui/diagnostics/miniblink_trace_source.h>
#include "input_frame_trace_backend_validation.h"

#include "../../../../third_party/include/miniblink/input_ui_api.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <limits>
#include <new>
#include <utility>

namespace soui::sdk::diagnostics::input_frame_trace {
namespace {
class module_reference final {
 public:
  explicit module_reference(HMODULE value = nullptr) noexcept : value_(value) {}
  ~module_reference() { if (value_) FreeLibrary(value_); }
  module_reference(const module_reference&) = delete;
  module_reference& operator=(const module_reference&) = delete;
  HMODULE get() const noexcept { return value_; }
  HMODULE release() noexcept { return std::exchange(value_, nullptr); }
 private:
  HMODULE value_;
};
bool compatible(const wkeInputUiApi* value) noexcept {
  return value && value->abiVersion == WKE_INPUT_UI_ABI_VERSION
      && value->structSize >= offsetof(wkeInputUiApi, serviceOwnerOpportunity)
      && (value->capabilities & WKE_INPUT_UI_CAP_TRACE)
      && value->traceConfigure && value->traceStop && value->traceSnapshot && value->traceDrain;
}
void bounded_sum(std::uint64_t& value, std::uint64_t amount, bool& saturated) noexcept {
  if (amount > (std::numeric_limits<std::uint64_t>::max)() - value) {
    value = (std::numeric_limits<std::uint64_t>::max)(); saturated = true;
  } else value += amount;
}
}

struct miniblink_trace_source::implementation final {
  ~implementation() { if (module) FreeLibrary(module); }
  HMODULE module{nullptr};
  const wkeInputUiApi* api{nullptr};
  identity session;
  mode recording_mode{mode::scalar};
  backend_detail::event_validator validator;
  std::atomic<unsigned> observed_slot{0};
  std::atomic_flag consumer = ATOMIC_FLAG_INIT;
  std::atomic<std::uint64_t> local_loss{0};
  std::atomic<bool> unavailable{false}, local_saturated{false};
};

miniblink_trace_source::miniblink_trace_source(std::unique_ptr<implementation> value) noexcept
    : implementation_(std::move(value)) {}
miniblink_trace_source::~miniblink_trace_source() = default;

std::unique_ptr<miniblink_trace_source> miniblink_trace_source::attach(void* module,
    backend_status& result) noexcept {
  result = backend_status::module_unavailable;
  if (!module) return {};
  const auto native = static_cast<HMODULE>(module);
  const auto address = GetProcAddress(native, "wkeGetInputUiApi");
  if (!address) { result = backend_status::api_unavailable; return {}; }
  HMODULE retained = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
          reinterpret_cast<LPCWSTR>(address), &retained)) return {};
  module_reference reference(retained);
  // A forwarded export is not the exact kernel instance supplied by the caller.
  if (retained != native) { result = backend_status::incompatible; return {}; }
  const auto get_api = reinterpret_cast<FN_wkeGetInputUiApi>(address);
  const auto* api = get_api(WKE_INPUT_UI_ABI_VERSION);
  if (!compatible(api)) { result = backend_status::incompatible; return {}; }
  try {
    auto value = std::make_unique<implementation>();
    value->module = reference.release(); value->api = api;
    wkeInputUiTraceSnapshot initial{};
    initial.structSize = sizeof(initial); initial.abiVersion = WKE_INPUT_UI_ABI_VERSION;
    const auto actual = current_identity(0, 0);
    if (api->traceSnapshot(&initial) != WKE_INPUT_UI_OK || !actual.process_id ||
        initial.processId != actual.process_id || initial.processIncarnation != actual.process_incarnation ||
        initial.qpcFrequency != actual.qpc_frequency || initial.mode > 1) {
      result = backend_status::incompatible; return {};
    }
    value->session = backend_detail::session_of(initial);
    value->recording_mode = static_cast<mode>(initial.mode);
    if (!(initial.flags & WKE_INPUT_UI_TRACE_AVAILABLE) ||
        !backend_detail::valid_snapshot(initial, value->session, value->recording_mode, 0)) {
      result = backend_status::incompatible; return {};
    }
    auto source = std::unique_ptr<miniblink_trace_source>(new miniblink_trace_source(std::move(value)));
    result = backend_status::ready; return source;
  } catch (...) { result = backend_status::no_memory; return {}; }
}

std::unique_ptr<miniblink_trace_source> miniblink_trace_source::load(const wchar_t* path,
    const identity& session, mode requested, backend_status& result) noexcept {
  result = backend_status::module_unavailable;
  try {
    if (!path || !*path || !std::filesystem::path(path).is_absolute()) return {};
    // Per-call search policy; no process-wide DLL search settings are changed.
    // These flags are probed at runtime to retain unpatched Win7 compatibility.
    constexpr DWORD local_application_system_search = 0x00000100 | 0x00000200 | 0x00000800;
    auto loaded = LoadLibraryExW(path, nullptr, local_application_system_search);
    if (!loaded && GetLastError() == ERROR_INVALID_PARAMETER)
      loaded = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    module_reference initial(loaded);
    if (!initial.get()) return {};
    auto source = attach(initial.get(), result);
    if (!source) return {};
    if (session.version != abi_version || (requested != mode::scalar && requested != mode::detailed)) {
      result = backend_status::incompatible; return {};
    }
    const wkeInputUiTraceConfig config = {sizeof(wkeInputUiTraceConfig), WKE_INPUT_UI_ABI_VERSION,
        static_cast<unsigned>(requested), session.process_id, session.run_id_high, session.run_id_low,
        session.process_incarnation, session.qpc_frequency};
    if (source->implementation_->api->traceConfigure(&config) != WKE_INPUT_UI_OK) {
      result = backend_status::configure_rejected; return {};
    }
    // load has not exposed the source: freeze the newly configured identity now.
    source->implementation_->session = session;
    source->implementation_->recording_mode = requested;
    if (!source->snapshot().available) { result = backend_status::configure_rejected; return {}; }
    result = backend_status::ready; return source;
  } catch (const std::bad_alloc&) { result = backend_status::no_memory; return {}; }
  catch (...) { result = backend_status::module_unavailable; return {}; }
}

bool miniblink_trace_source::stop() noexcept {
  if (!implementation_ || implementation_->unavailable.load(std::memory_order_acquire)) return false;
  implementation_->api->traceStop(); return true;
}

scalar_snapshot miniblink_trace_source::snapshot() const noexcept {
  scalar_snapshot value;
  if (!implementation_) return value;
  value.session = implementation_->session;
  value.recording_mode = implementation_->recording_mode;
  if (implementation_->unavailable.load(std::memory_order_acquire)) return value;
  const auto observed = implementation_->observed_slot.load(std::memory_order_acquire);
  wkeInputUiTraceSnapshot raw{};
  raw.structSize = sizeof(raw); raw.abiVersion = WKE_INPUT_UI_ABI_VERSION;
  const auto result = implementation_->api->traceSnapshot(&raw);
  if (result != WKE_INPUT_UI_OK || !(raw.flags & WKE_INPUT_UI_TRACE_AVAILABLE) ||
      !backend_detail::valid_snapshot(raw, implementation_->session, implementation_->recording_mode, observed)) {
    implementation_->unavailable.store(true, std::memory_order_release); return value;
  }
  value.available = result == WKE_INPUT_UI_OK && (raw.flags & WKE_INPUT_UI_TRACE_AVAILABLE)
      && !implementation_->unavailable.load(std::memory_order_acquire);
  value.session.process_id = raw.processId;
  value.session.run_id_high = raw.runIdHigh; value.session.run_id_low = raw.runIdLow;
  value.session.process_incarnation = raw.processIncarnation;
  value.session.qpc_frequency = raw.qpcFrequency;
  value.recording_mode = static_cast<mode>(raw.mode);
  value.registered_threads = raw.registeredThreads; value.active_producers = raw.activeProducers;
  value.pending_registrations = raw.pendingRegistrations;
  value.attempted = raw.attempted; value.recorded = raw.recorded;
  value.dropped = raw.dropped; value.queued = raw.queued;
  value.unregistered_writes = raw.unregisteredWrites;
  value.registration_rejections = raw.registrationRejections; value.causal_lost = raw.causalLost;
  value.stopped = (raw.flags & WKE_INPUT_UI_TRACE_STOPPED) != 0;
  value.quiescent = (raw.flags & WKE_INPUT_UI_TRACE_QUIESCENT) != 0;
  value.counter_saturated = (raw.flags & WKE_INPUT_UI_TRACE_SATURATED) != 0
      || implementation_->local_saturated.load(std::memory_order_relaxed);
  value.owner_violation = (raw.flags & WKE_INPUT_UI_TRACE_OWNER_VIOLATION) != 0;
  value.invalid_event = (raw.flags & WKE_INPUT_UI_TRACE_INVALID_EVENT) != 0;
  value.clock_invalid = (raw.flags & WKE_INPUT_UI_TRACE_CLOCK_INVALID) != 0;
  bounded_sum(value.causal_lost, implementation_->local_loss.load(std::memory_order_relaxed), value.counter_saturated);
  if (raw.flags & WKE_INPUT_UI_TRACE_LIMIT_REACHED)
    bounded_sum(value.causal_lost, 1, value.counter_saturated);
  for (std::size_t n = 0; n < stage_count; ++n) value.stages[n] = raw.stages[n];
  for (std::size_t n = 0; n < counter_count; ++n) value.counters[n] = raw.counters[n];
  for (std::size_t n = 0; n < duration_metric_count; ++n)
    for (std::size_t bucket = 0; bucket < duration_buckets; ++bucket)
      value.durations[n][bucket] = raw.durations[n][bucket];
  return value;
}

drain_result miniblink_trace_source::drain(event* output, std::size_t capacity) noexcept {
  drain_result value;
  if (!implementation_ || !output || !capacity || implementation_->unavailable.load(std::memory_order_acquire))
    return value;
  if (implementation_->consumer.test_and_set(std::memory_order_acquire)) { value.busy = true; return value; }
  struct release_consumer final {
    std::atomic_flag& flag;
    ~release_consumer() { flag.clear(std::memory_order_release); }
  } release{implementation_->consumer};
  unsigned count = 0;
  const auto bounded = static_cast<unsigned>((std::min)(capacity,
      static_cast<std::size_t>(WKE_INPUT_UI_MAX_DRAIN_EVENTS)));
  const auto result = implementation_->api->traceDrain(output, sizeof(event), bounded, &count);
  if (result == WKE_INPUT_UI_BUSY && !count) { value.busy = true; return value; }
  if (result == WKE_INPUT_UI_EXPORT_LIMIT && !count) { note_causal_loss(); return value; }
  if (result != WKE_INPUT_UI_OK || count > bounded) {
    implementation_->unavailable.store(true, std::memory_order_release);
    note_causal_loss(); return value;
  }
  for (unsigned n = 0; n < count; ++n) {
    if (!implementation_->validator.accept(output[n])) {
      implementation_->unavailable.store(true, std::memory_order_release);
      note_causal_loss(); return value; // Never export a valid prefix of a corrupt batch.
    }
  }
  implementation_->observed_slot.store(implementation_->validator.highest_slot, std::memory_order_release);
  value.count = count; return value;
}

void miniblink_trace_source::note_causal_loss(std::uint64_t amount) noexcept {
  if (!implementation_ || !amount) return;
  const auto previous = implementation_->local_loss.fetch_add(amount, std::memory_order_relaxed);
  if (amount > (std::numeric_limits<std::uint64_t>::max)() - previous) {
    implementation_->local_saturated.store(true, std::memory_order_relaxed);
    implementation_->local_loss.store((std::numeric_limits<std::uint64_t>::max)(), std::memory_order_relaxed);
  }
}

} // namespace soui::sdk::diagnostics::input_frame_trace
