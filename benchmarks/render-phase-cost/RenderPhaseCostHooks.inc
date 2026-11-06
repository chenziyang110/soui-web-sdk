// Included once, before the original global trace recorder is constructed.
// This gives the bank a longer module lifetime than that recorder and its stop.
#include "RenderPhaseCostHooks.h"

namespace content { namespace phase_cost {
namespace {
clock phase_clock;
bank_type phase_bank(phase_clock);
// Low two bits hold the one cold request; bit2 atomically seals trace binding.
std::atomic<model::u32> requested{0}, configuration_fault{0};
thread_local std::size_t mapped_slot = model::producer_limit;
const char* state_name(model::status value) noexcept {
    switch (value) {
    case model::status::disabled: return "disabled";
    case model::status::busy: return "busy";
    case model::status::complete: return "complete";
    default: return "fault";
    }
}
const char* const metric_names[] = {
    "shared_wake_late_us", "task_due_late_us", "raster_qpc",
    "spare_copy_qpc", "ui_mirror_copy_qpc", "native_submit_context_qpc",
    "native_submit_layered_qpc"
};
static_assert(sizeof(metric_names) / sizeof(*metric_names) == model::metric_count,
    "Private metric names must match the fixed diagnostic bank");
}

model::u64 clock::now_qpc() noexcept { return input_frame_trace::now_qpc(); }
model::u32 clock::current_thread_id() noexcept { return GetCurrentThreadId(); }
bank_type& storage() noexcept { return phase_bank; }
std::size_t current_slot() noexcept { return mapped_slot; }
bank_type* active_bank() noexcept {
    if (!phase_bank.enabled() || !phase_bank.recording()) return nullptr;
    if (!input_frame_trace::current_thread_registered()) return nullptr;
    if (mapped_slot == model::producer_limit) {
        phase_bank.note_binding_fault(); return nullptr;
    }
    return &phase_bank;
}

bool request_mode(const char* value) noexcept {
    const model::u32 next = value && value[0] == '1' && !value[1] ? 2U
        : value && value[0] == '0' && !value[1] ? 1U : 0U;
    model::u32 expected = 0;
    if (!next || !requested.compare_exchange_strong(expected, next)) {
        configuration_fault.store(1, std::memory_order_release);
        return false;
    }
    return true;
}
void bind_trace(model::u32 pid, model::u64 creation, model::u64 run_high,
    model::u64 run_low, model::u64 frequency) noexcept {
    const auto previous = requested.fetch_or(4, std::memory_order_acq_rel);
    if (previous & 4) {
        configuration_fault.store(1, std::memory_order_release);
        return;
    }
    model::binding identity;
    identity.process_id = pid; identity.process_creation = creation;
    identity.run_high = run_high; identity.run_low = run_low;
    identity.qpc_frequency = frequency;
    (void)phase_bank.configure((previous & 3) == 2, identity);
}
void attach(std::size_t slot, model::u32 owner) noexcept {
    if (!phase_bank.enabled() || !phase_bank.recording()) return;
    if (mapped_slot != model::producer_limit || !phase_bank.register_producer(slot, owner)) {
        phase_bank.note_binding_fault(); return;
    }
    mapped_slot = slot;
}
void detach() noexcept {
    if (mapped_slot == model::producer_limit) return;
    const auto slot = mapped_slot; mapped_slot = model::producer_limit;
    (void)phase_bank.release_producer(slot);
}
void stop() noexcept { phase_bank.stop(); }

const char* final_snapshot_json() noexcept {
    // Cold diagnostic API only. A running trace returns busy with no histogram.
    // No target/view/WTF object or protocol body is consulted.
    static thread_local std::string output;
    try {
        const auto begin = input_frame_trace::now_qpc();
        auto value = phase_bank.final_snapshot(input_frame_trace::process_recorder().quiescent());
        const auto end = input_frame_trace::now_qpc();
        const bool bad_configuration = configuration_fault.load(std::memory_order_acquire) != 0;
        const bool usable = value.histograms_available && !bad_configuration && begin && end >= begin;
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << "{\"schema\":\"soui.private-render-phase-cost/v1\",\"supported\":true"
            << ",\"status\":\"" << (bad_configuration ? "configuration-fault" : state_name(value.state)) << "\""
            << ",\"histogramsAvailable\":" << (usable ? "true" : "false")
            << ",\"faults\":" << value.faults
            << ",\"configurationFault\":" << (bad_configuration ? "true" : "false")
            << ",\"processId\":" << value.session.process_id
            << ",\"processCreation\":\"" << value.session.process_creation
            << "\",\"runHigh\":\"" << value.session.run_high
            << "\",\"runLow\":\"" << value.session.run_low
            << "\",\"qpcFrequency\":\"" << value.session.qpc_frequency
            << "\",\"captureBeginQpc\":\"" << begin << "\",\"captureEndQpc\":\"" << end << "\""
            << ",\"registeredProducers\":" << value.registered_producers
            << ",\"earlyWakes\":\"" << value.early_wakes << "\",\"missingDue\":\"" << value.missing_due << "\""
            << ",\"scope\":\"whole-trace-lifetime-inclusive-wall; not thread CPU or per-frame causality\""
            << ",\"metrics\":";
        if (!usable) stream << "null";
        else {
            stream << '[';
            for (std::size_t m = 0; m < model::metric_count; ++m) {
                if (m) stream << ',';
                const auto& metric = value.metrics[m];
                stream << "{\"name\":\"" << metric_names[m] << "\",\"unit\":\""
                    << (m < 2 ? "microseconds" : "qpc-ticks") << "\",\"observations\":\""
                    << metric.observations << "\",\"totalUnits\":\"" << metric.total_units << "\",\"buckets\":[";
                for (std::size_t b = 0; b < model::bucket_count; ++b) {
                    if (b) stream << ',';
                    stream << '\"' << metric.buckets[b] << '\"';
                }
                stream << "],\"outcomes\":[";
                for (std::size_t o = 0; o < model::outcome_count; ++o) {
                    if (o) stream << ',';
                    stream << '\"' << metric.outcomes[o] << '\"';
                }
                stream << "]}";
            }
            stream << ']';
        }
        stream << ",\"physicalPresentation\":false,\"threadCpuMeasured\":false,\"performanceGate\":false}";
        output = stream.str();
        if (output.size() > 16384) return nullptr;
        return output.c_str();
    } catch (...) { return nullptr; }
}
}} // namespace content::phase_cost
