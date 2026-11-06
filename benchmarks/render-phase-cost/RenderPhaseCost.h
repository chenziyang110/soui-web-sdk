#pragma once

// Benchmark-private overlay support. No trace-v1 types, Windows calls or heap.
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace soui { namespace bench { namespace render_phase {

using u32 = std::uint32_t;
using u64 = std::uint64_t;
constexpr std::size_t producer_limit = 32;
constexpr std::size_t bucket_count = 64;
enum class metric : u32 {
    shared_wake_late_us, task_due_late_us, raster_qpc, spare_copy_qpc,
    ui_mirror_copy_qpc, native_submit_context_qpc, native_submit_layered_qpc, count
};
constexpr std::size_t metric_count = static_cast<std::size_t>(metric::count);
enum class outcome : u32 { returned, cancelled, api_failed, abandoned, count };
constexpr std::size_t outcome_count = static_cast<std::size_t>(outcome::count);
enum class status : u32 { disabled, busy, complete, fault };
enum fault : u32 {
    binding_fault = 1, atomic_not_lock_free = 2, clock_fault = 4,
    saturated = 8, invalid_due = 16
};
struct binding {
    u32 process_id = 0;
    u64 process_creation = 0, run_high = 0, run_low = 0, qpc_frequency = 0;
};
struct metric_snapshot {
    u64 observations = 0, total_units = 0;
    std::array<u64, bucket_count> buckets{};
    std::array<u64, outcome_count> outcomes{};
};
struct snapshot {
    status state = status::disabled;
    binding session;
    u32 faults = 0, registered_producers = 0;
    bool histograms_available = false;
    u64 early_wakes = 0, missing_due = 0;
    std::array<metric_snapshot, metric_count> metrics{};
};

// Clock: u64 now_qpc() noexcept; u32 current_thread_id() noexcept.
// configure/register are serialized by the existing trace cold-registration
// lock. The caller supplies its real trace slot; no new registry is created.
// Bank/Clock/module outlive every scope, including stop/detach completion.
template<class Clock> class bank final {
    struct metric_state {
        std::atomic<u64> observations{0}, total_units{0};
        std::array<std::atomic<u64>, bucket_count> buckets;
        std::array<std::atomic<u64>, outcome_count> outcomes;
        metric_state() noexcept {
            for (auto& v : buckets) v.store(0, std::memory_order_relaxed);
            for (auto& v : outcomes) v.store(0, std::memory_order_relaxed);
        }
    };
    struct slot_state {
        std::atomic<u32> owner{0}, active{0}, inflight{0};
        std::atomic<u64> early_wakes{0}, missing_due{0};
        u64 last_qpc = 0; // Real owner only; not part of a live snapshot.
        double last_monotonic = 0;
        std::array<metric_state, metric_count> metrics;
    };
public:
    class scope final {
    public:
        scope(bank& owner, std::size_t slot, metric kind) noexcept
            : slot_(slot), kind_(kind) { owner.open(*this); }
        ~scope() { finish(outcome::abandoned); }
        scope(const scope&) = delete;
        scope& operator=(const scope&) = delete;
        scope(scope&&) = delete;
        scope& operator=(scope&&) = delete;
        explicit operator bool() const noexcept { return bank_ != nullptr; }
        bool finish(outcome result = outcome::returned) noexcept {
            bank* owner = bank_;
            if (!owner) return false;
            bank_ = nullptr; // Exactly one finish, including the destructor.
            return owner->close(*this, result);
        }
    private:
        friend class bank;
        bank* bank_ = nullptr;
        std::size_t slot_;
        metric kind_;
        u32 owner_ = 0;
        u64 begin_ = 0;
    };

    explicit bank(Clock& clock) noexcept : clock_(clock) {}
    bank(const bank&) = delete;
    bank& operator=(const bank&) = delete;

    bool configure(bool enabled, binding value) noexcept {
        if (configured_.load(std::memory_order_acquire)) return false;
        if (enabled && (!value.process_id || !value.process_creation ||
            !(value.run_high || value.run_low) || !value.qpc_frequency)) {
            fail(binding_fault);
        } else if (enabled && !lock_free()) {
            fail(atomic_not_lock_free);
        } else {
            session_ = value;
            enabled_.store(enabled ? 1U : 0U, std::memory_order_release);
            recording_.store(enabled ? 1U : 0U, std::memory_order_release);
        }
        configured_.store(1, std::memory_order_release);
        return faults_.load(std::memory_order_acquire) == 0;
    }
    bool enabled() const noexcept { return enabled_.load(std::memory_order_acquire) != 0; }
    bool recording() const noexcept { return recording_.load(std::memory_order_acquire) != 0; }
    // For the trace hook's "registered there, missing private mapping" case.
    // Ordinary unregistered calls still skip silently and read no clock.
    void note_binding_fault() noexcept { fail(binding_fault); }
    bool register_producer(std::size_t index, u32 owner) noexcept {
        if (!enabled() || !recording()) return false;
        if (index >= producer_limit || !owner || clock_.current_thread_id() != owner) {
            fail(binding_fault); return false;
        }
        auto& slot = slots_[index];
        const u32 previous = slot.owner.load(std::memory_order_acquire);
        if ((previous && previous != owner) || slot.active.load(std::memory_order_acquire) ||
            slot.inflight.load(std::memory_order_acquire)) {
            fail(binding_fault); return false;
        }
        for (std::size_t i = 0; i < producer_limit; ++i) {
            if (i != index && slots_[i].active.load(std::memory_order_acquire) &&
                slots_[i].owner.load(std::memory_order_acquire) == owner) {
                fail(binding_fault); return false;
            }
        }
        slot.owner.store(owner, std::memory_order_release);
        slot.active.store(1, std::memory_order_release);
        return true;
    }
    bool release_producer(std::size_t index) noexcept {
        if (!enabled()) return false;
        if (index >= producer_limit) { fail(binding_fault); return false; }
        auto& slot = slots_[index];
        if (!slot.active.load(std::memory_order_acquire) ||
            slot.owner.load(std::memory_order_acquire) != clock_.current_thread_id()) {
            fail(binding_fault); return false;
        }
        slot.active.store(0, std::memory_order_release);
        return true;
    }
    void stop() noexcept { recording_.store(0, std::memory_order_release); }

    // Only these metrics use WTF seconds. Now is called AFTER admission; do
    // not obtain dispatch time before this call. Units are ceil microseconds,
    // never QPC ticks. Early/missing due are separate, not zero observations.
    template<class Now> bool observe_lateness(std::size_t index, metric kind,
        double due, Now now) noexcept {
        slot_state* slot = acquire(index);
        if (!slot) return false;
        activity leave(*slot);
        if (static_cast<u32>(kind) > static_cast<u32>(metric::task_due_late_us)) {
            fail(binding_fault); return false;
        }
        if (due == 0) { add(slot->missing_due, 1); return false; }
        if (!std::isfinite(due) || due < 0) { fail(invalid_due); return false; }
        const double dispatch = now();
        if (!std::isfinite(dispatch) || dispatch <= 0 || dispatch < slot->last_monotonic) {
            fail(clock_fault); return false;
        }
        slot->last_monotonic = dispatch;
        if (dispatch < due) { add(slot->early_wakes, 1); return false; }
        const double units = std::ceil((dispatch - due) * 1000000.0);
        // 2^64 is exact as double; casting it (or rounded UINT64_MAX) is UB.
        if (!std::isfinite(units) || units >= 18446744073709551616.0) {
            fail(saturated); return false;
        }
        record(*slot, kind, static_cast<u64>(units), outcome::returned);
        return faults_.load(std::memory_order_acquire) == 0;
    }

    // trace_quiescent comes from the actual stopped trace, including pending
    // registrars. A private bank alone cannot prove that outside lifecycle.
    snapshot final_snapshot(bool trace_quiescent) noexcept {
        snapshot value;
        if (!configured_.load(std::memory_order_acquire)) return value;
        value.session = session_;
        value.faults = faults_.load(std::memory_order_acquire);
        if (!enabled()) { value.state = value.faults ? status::fault : status::disabled; return value; }
        value.state = status::busy;
        if (recording() || !trace_quiescent) return value;
        for (const auto& slot : slots_) {
            if (slot.active.load(std::memory_order_acquire) || slot.inflight.load(std::memory_order_acquire)) return value;
        }
        if (value.faults) { value.state = status::fault; return value; }
        for (const auto& slot : slots_) {
            if (slot.owner.load(std::memory_order_acquire)) ++value.registered_producers;
            sum(value.early_wakes, slot.early_wakes.load(std::memory_order_acquire));
            sum(value.missing_due, slot.missing_due.load(std::memory_order_acquire));
            for (std::size_t m = 0; m < metric_count; ++m) {
                auto& output = value.metrics[m]; const auto& input = slot.metrics[m];
                sum(output.observations, input.observations.load(std::memory_order_acquire));
                sum(output.total_units, input.total_units.load(std::memory_order_acquire));
                for (std::size_t b = 0; b < bucket_count; ++b) sum(output.buckets[b], input.buckets[b].load(std::memory_order_acquire));
                for (std::size_t o = 0; o < outcome_count; ++o) sum(output.outcomes[o], input.outcomes[o].load(std::memory_order_acquire));
            }
        }
        value.faults = faults_.load(std::memory_order_acquire);
        if (value.faults) {
            value.state = status::fault;
            value.metrics = {}; // Fault is unavailable, never a usable partial histogram.
            return value;
        }
        value.state = status::complete; value.histograms_available = true;
        return value;
    }
private:
    struct activity {
        explicit activity(slot_state& slot) noexcept : slot_(slot) {}
        ~activity() { slot_.inflight.fetch_sub(1, std::memory_order_release); }
        slot_state& slot_;
    };
    bool lock_free() const noexcept {
        return enabled_.is_lock_free() && slots_[0].owner.is_lock_free() &&
            slots_[0].metrics[0].total_units.is_lock_free();
    }
    void fail(u32 bit) noexcept { faults_.fetch_or(bit, std::memory_order_relaxed); }
    slot_state* acquire(std::size_t index) noexcept {
        if (!enabled() || !recording() || faults_.load(std::memory_order_acquire)) return nullptr;
        if (index >= producer_limit) { fail(binding_fault); return nullptr; }
        auto& slot = slots_[index];
        if (!slot.active.load(std::memory_order_acquire)) return nullptr;
        if (slot.owner.load(std::memory_order_acquire) != clock_.current_thread_id()) {
            fail(binding_fault); return nullptr;
        }
        if (slot.inflight.load(std::memory_order_relaxed) == (std::numeric_limits<u32>::max)()) {
            fail(saturated); return nullptr;
        }
        slot.inflight.fetch_add(1, std::memory_order_acq_rel);
        if (!recording() || !slot.active.load(std::memory_order_acquire)) {
            slot.inflight.fetch_sub(1, std::memory_order_release); return nullptr;
        }
        return &slot;
    }
    u64 qpc(slot_state& slot) noexcept {
        const u64 value = clock_.now_qpc();
        if (!value || value < slot.last_qpc) { fail(clock_fault); return 0; }
        slot.last_qpc = value; return value;
    }
    void open(scope& span) noexcept {
        slot_state* slot = acquire(span.slot_);
        if (!slot) return;
        if (static_cast<u32>(span.kind_) < static_cast<u32>(metric::raster_qpc) ||
            static_cast<u32>(span.kind_) >= static_cast<u32>(metric::count)) {
            fail(binding_fault); slot->inflight.fetch_sub(1, std::memory_order_release); return;
        }
        span.begin_ = qpc(*slot);
        if (!span.begin_) { slot->inflight.fetch_sub(1, std::memory_order_release); return; }
        span.owner_ = slot->owner.load(std::memory_order_acquire); span.bank_ = this;
    }
    bool close(scope& span, outcome result) noexcept {
        auto& slot = slots_[span.slot_]; activity leave(slot);
        if (clock_.current_thread_id() != span.owner_ ||
            slot.owner.load(std::memory_order_acquire) != span.owner_ ||
            static_cast<u32>(result) >= static_cast<u32>(outcome::count)) {
            fail(binding_fault); return false;
        }
        const u64 end = qpc(slot); // An admitted scope may finish after stop/detach.
        if (!end) return false;
        record(slot, span.kind_, end - span.begin_, result);
        return faults_.load(std::memory_order_acquire) == 0;
    }
    void add(std::atomic<u64>& target, u64 amount) noexcept {
        const u64 old = target.load(std::memory_order_relaxed);
        if (amount > (std::numeric_limits<u64>::max)() - old) {
            target.store((std::numeric_limits<u64>::max)(), std::memory_order_relaxed); fail(saturated);
        } else target.store(old + amount, std::memory_order_relaxed);
    }
    void sum(u64& target, u64 amount) noexcept {
        if (amount > (std::numeric_limits<u64>::max)() - target) fail(saturated);
        else target += amount;
    }
    void record(slot_state& slot, metric kind, u64 units, outcome result) noexcept {
        auto& output = slot.metrics[static_cast<std::size_t>(kind)];
        std::size_t bucket = 0;
        for (u64 remaining = units; remaining > 1; remaining >>= 1) ++bucket;
        add(output.observations, 1); add(output.total_units, units);
        add(output.buckets[bucket], 1); add(output.outcomes[static_cast<std::size_t>(result)], 1);
    }
    Clock& clock_;
    binding session_;
    std::atomic<u32> configured_{0}, enabled_{0}, recording_{0}, faults_{0};
    std::array<slot_state, producer_limit> slots_;
};

}}} // namespace soui::bench::render_phase
