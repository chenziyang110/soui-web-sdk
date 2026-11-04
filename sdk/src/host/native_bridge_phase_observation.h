#pragma once

#include "../runtime/diagnostics/input_frame_duration_bucket.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <string_view>

namespace soui::sdk::detail {

enum class native_phase_option { disabled, enabled, invalid };
inline native_phase_option parse_native_phase_option(std::string_view value) noexcept {
  if (value.empty() || value == "0") return native_phase_option::disabled;
  return value == "1" ? native_phase_option::enabled : native_phase_option::invalid;
}

enum class native_bridge_phase : unsigned {
  native_first_parse, resolver_create_js, reply_run_js, count,
};
constexpr std::size_t native_phase_count = 3;
constexpr std::size_t native_phase_bucket_count = 64;
constexpr unsigned native_phase_snapshot_limit = 3;
static_assert(static_cast<unsigned>(native_bridge_phase::count) == native_phase_count);

inline const char* native_phase_name(native_bridge_phase phase) noexcept {
  switch (phase) {
    case native_bridge_phase::native_first_parse: return "native_first_parse";
    case native_bridge_phase::resolver_create_js: return "resolver_create_js";
    case native_bridge_phase::reply_run_js: return "reply_run_js";
    default: return "invalid";
  }
}

enum class native_phase_status { not_started, ready, busy, unavailable };
enum native_phase_fault : std::uint32_t {
  native_phase_no_fault = 0,
  native_phase_counter_saturated = 1U << 0,
  native_phase_clock_invalid = 1U << 1,
  native_phase_owner_violation = 1U << 2,
  native_phase_sequence_exhausted = 1U << 3,
  native_phase_atomic_unavailable = 1U << 4,
  native_phase_incarnation_invalid = 1U << 5,
  native_phase_binding_invalid = 1U << 6,
  native_phase_unbound_scope = 1U << 7,
  native_phase_invalid_phase = 1U << 8,
  native_phase_invalid_state = 1U << 9,
};

struct native_phase_values final {
  std::uint64_t started{}, completed{}, returned{}, unwound{}, in_flight{};
  std::uint64_t nested_starts{}, sum_elapsed_qpc{};
  std::array<std::uint64_t, native_phase_bucket_count> buckets{};
};

// POD copy only. If !coherent or !usable, an exporter must not turn default
// storage into measured zero buckets. It must preserve status/faults instead.
struct native_phase_snapshot final {
  native_phase_status status{native_phase_status::not_started};
  bool usable{}, coherent{};
  std::uint32_t faults{};
  std::uint64_t observer_incarnation{}, qpc_frequency{};
  std::uint64_t capture_begin_qpc{}, capture_end_qpc{}, publication_sequence{};
  unsigned snapshot_attempts{};
  std::array<native_phase_values, native_phase_count> phases{};
};

struct native_phase_test_access; // Defined only by the independent CPU fixture.
struct native_phase_no_snapshot_probe final {
  static void after_first_sequence(unsigned) noexcept {}
  static void after_fields(unsigned) noexcept {}
};

template<class Clock,
         bool LockFree64 = std::atomic<std::uint64_t>::is_always_lock_free>
class basic_native_phase_scope;

// Clock has static noexcept qpc()/thread_id(). No clock instance, dynamic
// callback, allocation, registry or event ring is held by the recorder.
template<class Clock,
         bool LockFree64 = std::atomic<std::uint64_t>::is_always_lock_free>
class basic_native_phase_recorder final {
 public:
  explicit basic_native_phase_recorder(std::uint64_t incarnation) noexcept
      : incarnation_(incarnation) {
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "Native phase fault flags require lock-free uint32 atomics");
    if constexpr (!LockFree64) note(native_phase_atomic_unavailable);
    if (!incarnation) note(native_phase_incarnation_invalid);
  }
  basic_native_phase_recorder(const basic_native_phase_recorder&) = delete;
  basic_native_phase_recorder& operator=(const basic_native_phase_recorder&) = delete;

  // Cold, before publishing native bindings. Binding cannot be reset/migrated.
  // Even competing/repeated bind attempts are bounded and fail closed.
  bool bind_on_current_thread(std::uint64_t frequency) noexcept {
    if constexpr (!LockFree64) return false;
    if (faults_.load() != native_phase_no_fault) return false;
    std::uint32_t expected = 0;
    if (!binding_.compare_exchange_strong(expected, 1)) {
      note(native_phase_binding_invalid);
      return false;
    }
    const auto owner = Clock::thread_id();
    if (!frequency || !owner) {
      note(!frequency ? native_phase_clock_invalid : native_phase_owner_violation);
      binding_.store(3);
      return false;
    }
    owner_thread_.store(owner);
    frequency_.store(frequency);
    binding_.store(2);
    return faults_.load() == native_phase_no_fault;
  }

  native_phase_snapshot snapshot() const noexcept {
    return snapshot_with_probe<native_phase_no_snapshot_probe>();
  }

 private:
  friend class basic_native_phase_scope<Clock, LockFree64>;
  friend struct native_phase_test_access;
  using atomic_count = std::atomic<std::uint64_t>;
  static constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();

  struct phase_storage final {
    atomic_count started{0}, completed{0}, returned{0}, unwound{0}, in_flight{0};
    atomic_count nested_starts{0}, sum_elapsed_qpc{0};
    std::array<atomic_count, native_phase_bucket_count> buckets;
    phase_storage() noexcept {
      for (auto& bucket : buckets) bucket.store(0);
    }
  };

  void note(std::uint32_t fault) const noexcept { faults_.fetch_or(fault); }

  bool actual_owner() const noexcept {
    if (Clock::thread_id() == owner_thread_.load()) return true;
    note(native_phase_owner_violation);
    return false;
  }

  // Single Engine writer. No QPC, callbacks, allocations or locks may occur
  // between these stores. Snapshot fields themselves are atomic (not a
  // non-atomic seqlock). Initial memory order is deliberately seq_cst.
  bool begin_commit(std::uint64_t& sequence) noexcept {
    sequence = sequence_.load();
    if (sequence & 1U) { note(native_phase_invalid_state); return false; }
    if (sequence > maximum - 2U) {
      note(native_phase_sequence_exhausted);
      return false;
    }
    sequence_.store(sequence + 1U);
    return true;
  }
  void end_commit(std::uint64_t sequence) noexcept { sequence_.store(sequence + 2U); }

  void add(atomic_count& value, std::uint64_t amount = 1) noexcept {
    const auto previous = value.load();
    if (amount > maximum - previous) {
      value.store(maximum);
      note(native_phase_counter_saturated);
    } else value.store(previous + amount);
  }

  bool begin(native_bridge_phase phase) noexcept {
    if constexpr (!LockFree64) return false;
    const auto index = static_cast<std::size_t>(phase);
    if (index >= native_phase_count) { note(native_phase_invalid_phase); return false; }
    if (binding_.load() != 2) { note(native_phase_unbound_scope); return false; }
    if (!actual_owner() || faults_.load() != native_phase_no_fault) return false;
    std::uint64_t sequence{};
    if (!begin_commit(sequence)) return false;
    auto& value = phases_[index];
    const auto total = total_in_flight_.load();
    // All begin increments must be representable before admitting this scope.
    // A failed begin never later decrements an in-flight count it did not own.
    if (value.started.load() == maximum || value.in_flight.load() == maximum ||
        total == maximum || (total && value.nested_starts.load() == maximum)) {
      note(native_phase_counter_saturated);
      end_commit(sequence);
      return false;
    }
    add(value.started);
    add(value.in_flight);
    if (total) add(value.nested_starts);
    total_in_flight_.store(total + 1U);
    end_commit(sequence);
    return true;
  }

  void finish(native_bridge_phase phase, std::uint64_t started, bool unwound) noexcept {
    // Only an actually entered scope calls this, retaining this metrics block.
    if (!actual_owner()) return;
    const auto ended = Clock::qpc(); // Before the end publication, never while odd.
    const bool clock_valid = started && ended && ended >= started;
    if (!clock_valid) note(native_phase_clock_invalid);
    std::uint64_t sequence{};
    if (!begin_commit(sequence)) return;
    auto& value = phases_[static_cast<std::size_t>(phase)];
    const auto local = value.in_flight.load(), total = total_in_flight_.load();
    if (!local || !total) {
      note(native_phase_invalid_state);
      end_commit(sequence);
      return;
    }
    value.in_flight.store(local - 1U);
    total_in_flight_.store(total - 1U);
    add(value.completed);
    add(unwound ? value.unwound : value.returned);
    if (clock_valid) {
      const auto ticks = ended - started;
      const auto bucket = diagnostics::input_frame_trace::detail::duration_bucket_index(ticks);
      add(value.buckets[bucket]);
      add(value.sum_elapsed_qpc, ticks);
    }
    end_commit(sequence);
  }

  template<class Probe>
  native_phase_snapshot snapshot_with_probe() const noexcept {
    native_phase_snapshot result;
    result.observer_incarnation = incarnation_;
    result.faults = faults_.load();
    if constexpr (!LockFree64) {
      result.status = native_phase_status::unavailable;
      return result;
    }
    if (binding_.load() != 2) {
      result.faults |= faults_.load();
      result.status = result.faults ? native_phase_status::unavailable : native_phase_status::not_started;
      return result;
    }
    result.qpc_frequency = frequency_.load();
    result.capture_begin_qpc = Clock::qpc();
    if (!result.capture_begin_qpc) note(native_phase_clock_invalid);
    for (unsigned attempt = 1; attempt <= native_phase_snapshot_limit; ++attempt) {
      result.snapshot_attempts = attempt;
      result.faults |= faults_.load();
      const auto before = sequence_.load();
      if (before & 1U) continue;
      Probe::after_first_sequence(attempt); // Cold, static no-op in production.
      for (std::size_t n = 0; n < native_phase_count; ++n) {
        const auto& source = phases_[n];
        auto& target = result.phases[n];
        target.started = source.started.load();
        target.completed = source.completed.load();
        target.returned = source.returned.load();
        target.unwound = source.unwound.load();
        target.in_flight = source.in_flight.load();
        target.nested_starts = source.nested_starts.load();
        target.sum_elapsed_qpc = source.sum_elapsed_qpc.load();
        for (std::size_t bucket = 0; bucket < native_phase_bucket_count; ++bucket)
          target.buckets[bucket] = source.buckets[bucket].load();
      }
      Probe::after_fields(attempt);
      const auto after = sequence_.load();
      result.faults |= faults_.load();
      if (before == after && !(after & 1U)) {
        result.coherent = true;
        result.publication_sequence = after;
        break;
      }
    }
    result.capture_end_qpc = Clock::qpc();
    if (!result.capture_end_qpc || !result.capture_begin_qpc ||
        result.capture_end_qpc < result.capture_begin_qpc) note(native_phase_clock_invalid);
    result.faults |= faults_.load();
    if (!result.coherent) result.phases = {};
    result.usable = result.coherent && result.faults == native_phase_no_fault;
    result.status = result.faults ? native_phase_status::unavailable :
        (result.coherent ? native_phase_status::ready : native_phase_status::busy);
    return result;
  }

  const std::uint64_t incarnation_;
  mutable std::atomic<std::uint32_t> faults_{native_phase_no_fault};
  std::atomic<std::uint32_t> binding_{0}, owner_thread_{0};
  atomic_count frequency_{0}, sequence_{0}, total_in_flight_{0};
  std::array<phase_storage, native_phase_count> phases_;
};

template<class Clock, bool LockFree64>
class basic_native_phase_scope final {
 public:
  using recorder = basic_native_phase_recorder<Clock, LockFree64>;
  basic_native_phase_scope(const std::shared_ptr<recorder>& metrics,
                           native_bridge_phase phase) noexcept {
    if (!metrics) return; // No allocation, clock, owner lookup or nonempty refcount.
    metrics_ = metrics;
    phase_ = phase;
    entered_ = metrics_->begin(phase);
    if (!entered_) return;
    exceptions_ = std::uncaught_exceptions();
    started_ = Clock::qpc(); // After the begin publication, never while odd.
    if (!started_) metrics_->note(native_phase_clock_invalid);
  }
  ~basic_native_phase_scope() noexcept {
    if (entered_) metrics_->finish(phase_, started_, std::uncaught_exceptions() > exceptions_);
  }
  basic_native_phase_scope(const basic_native_phase_scope&) = delete;
  basic_native_phase_scope& operator=(const basic_native_phase_scope&) = delete;
  basic_native_phase_scope(basic_native_phase_scope&&) = delete;
  basic_native_phase_scope& operator=(basic_native_phase_scope&&) = delete;

 private:
  std::shared_ptr<recorder> metrics_;
  native_bridge_phase phase_{native_bridge_phase::native_first_parse};
  std::uint64_t started_{};
  int exceptions_{};
  bool entered_{};
};

} // namespace soui::sdk::detail
