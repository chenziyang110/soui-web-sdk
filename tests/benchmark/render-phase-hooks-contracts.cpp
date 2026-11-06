// CPU-only contracts for the actual private overlay hooks, not a second hook
// implementation. The original trace is represented only by its four inputs:
// clock, registration, recorder quiescence and explicit lifecycle calls below.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include "json/json.hpp"

namespace content { namespace input_frame_trace {
namespace contract_state {
std::uint64_t clock_reads = 0;
std::uint64_t tick = 100;
bool registered = false;
bool quiet = false;
}
std::uint64_t now_qpc() noexcept {
    ++contract_state::clock_reads;
    contract_state::tick += 10;
    return contract_state::tick;
}
bool current_thread_registered() noexcept { return contract_state::registered; }
struct recorder_stub final {
    bool quiescent() const noexcept { return contract_state::quiet; }
};
recorder_stub& process_recorder() noexcept {
    static recorder_stub value;
    return value;
}
}}

// This is the same .inc used by the generated InputFrameTrace.cpp overlay.
// The include directory chooses the explicitly frozen overlay or repo copy.
#include "RenderPhaseCostHooks.inc"

namespace {
using json = nlohmann::json;
namespace hooks = content::phase_cost;
namespace model = hooks::model;
namespace trace = content::input_frame_trace::contract_state;
const model::u64 creation = 9007199254740993ULL;
const model::u64 run_high = 9007199254740995ULL;
const model::u64 run_low = 18446744073709551614ULL;
unsigned passed = 0;
const char* current_case = "not-started";
json coherent_wire = nullptr;
json rejected_wire = nullptr;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Body> void test(const char* name, Body body) {
    current_case = name;
    body();
    ++passed;
}
void bind_enabled() {
    require(hooks::request_mode("1"), "first cold enable rejected");
    hooks::bind_trace(GetCurrentProcessId(), creation, run_high, run_low, 10000000);
    require(hooks::storage().enabled() && hooks::storage().recording(), "bank not enabled");
}
json cold_json() {
    const char* text = hooks::final_snapshot_json();
    require(text != nullptr, "cold snapshot missing");
    require(std::char_traits<char>::length(text) <= 16384, "cold snapshot exceeds bound");
    return json::parse(text);
}
const model::metric_snapshot& metric(const model::snapshot& value, model::metric kind) {
    return value.metrics[static_cast<std::size_t>(kind)];
}
void ordinary_mode() {
    static_assert(!std::is_copy_constructible<hooks::scope>::value, "scope cannot be copied");
    static_assert(!std::is_move_constructible<hooks::scope>::value, "scope cannot be moved");
    unsigned monotonic_reads = 0;
    test("default-off-skips-clock", [&] {
        require(!hooks::storage().enabled(), "cold bank unexpectedly enabled");
        hooks::scope value(model::metric::raster_qpc);
        require(!value.finish(), "disabled scope was accepted");
        hooks::observe_lateness(model::metric::task_due_late_us, 1.0,
            [&]() noexcept { ++monotonic_reads; return 1.25; });
        require(trace::clock_reads == 0 && monotonic_reads == 0, "disabled hook read clock");
        require(hooks::storage().final_snapshot(true).faults == 0, "disabled hook set fault");
    });
    test("enabled-unregistered-skips-sentinel-without-fault", [&] {
        bind_enabled();
        require(hooks::current_slot() == model::producer_limit, "expected unmapped TLS sentinel");
        hooks::scope value(model::metric::raster_qpc);
        require(!value.finish(), "unregistered scope was accepted");
        hooks::observe_lateness(model::metric::task_due_late_us, 1.0,
            [&]() noexcept { ++monotonic_reads; return 1.25; });
        require(trace::clock_reads == 0 && monotonic_reads == 0, "unregistered hook read clock");
        const auto view = hooks::storage().final_snapshot(false);
        require(view.faults == 0 && view.state == model::status::busy,
            "ordinary unregistered sentinel faulted whole bank");
    });
    test("registered-mapping-and-once-only-finish", [&] {
        trace::registered = true;
        hooks::attach(5, GetCurrentThreadId());
        require(hooks::current_slot() == 5, "trace slot mapping not published");
        {
            hooks::scope value(model::metric::raster_qpc);
            require(trace::clock_reads == 1, "admitted scope start clock missing");
            require(value.finish(), "registered scope failed");
            require(!value.finish(), "scope finished twice");
        }
        require(trace::clock_reads == 2, "finish or destructor read extra clock");
        hooks::observe_lateness(model::metric::task_due_late_us, 1.0,
            [&]() noexcept { ++monotonic_reads; return 1.25; });
        require(monotonic_reads == 1 && trace::clock_reads == 2,
            "lateness must use its admitted monotonic clock, not QPC");
    });
    test("stop-rejects-new-and-retains-admitted-scope", [&] {
        hooks::scope admitted(model::metric::native_submit_context_qpc);
        require(trace::clock_reads == 3, "pending scope did not start");
        hooks::stop();
        {
            hooks::scope rejected(model::metric::raster_qpc);
            require(!rejected.finish(), "scope admitted after stop");
            hooks::observe_lateness(model::metric::task_due_late_us, 1.0,
                [&]() noexcept { ++monotonic_reads; return 1.25; });
        }
        require(trace::clock_reads == 3 && monotonic_reads == 1, "stopped hook read clock");
        hooks::detach();
        trace::registered = false;
        trace::quiet = true;
        const auto pending = hooks::storage().final_snapshot(true);
        require(pending.state == model::status::busy && !pending.histograms_available,
            "live admitted scope was mistaken for a final snapshot");
        require(admitted.finish(), "admitted scope lost after stop/detach");
        require(!admitted.finish() && trace::clock_reads == 4, "admitted scope completed twice");
        const auto final = hooks::storage().final_snapshot(true);
        require(final.state == model::status::complete && final.histograms_available && final.faults == 0,
            "quiescent final snapshot unavailable");
        require(final.registered_producers == 1, "producer history changed");
        const auto& raster = metric(final, model::metric::raster_qpc);
        const auto& submit = metric(final, model::metric::native_submit_context_qpc);
        const auto& late = metric(final, model::metric::task_due_late_us);
        require(raster.observations == 1 && raster.total_units == 10 && raster.outcomes[0] == 1,
            "raster scope count or QPC units changed");
        require(submit.observations == 1 && submit.total_units == 10 && submit.outcomes[0] == 1,
            "stopped in-flight submission was not retained");
        require(late.observations == 1 && late.total_units == 250000 && late.buckets[17] == 1,
            "monotonic lateness did not preserve microsecond units");
    });
    test("cold-json-preserves-fixed-shape-and-u64-strings", [&] {
        const auto value = cold_json();
        require(value.at("schema") == "soui.private-render-phase-cost/v1" &&
            value.at("status") == "complete" && value.at("histogramsAvailable") == true,
            "unexpected final wire status");
        require(value.at("processCreation") == "9007199254740993" &&
            value.at("runHigh") == "9007199254740995" &&
            value.at("runLow") == "18446744073709551614" && value.at("qpcFrequency") == "10000000",
            "u64 wire precision lost");
        const auto& metrics = value.at("metrics");
        require(metrics.size() == 7, "metric count changed");
        for (std::size_t m = 0; m < metrics.size(); ++m) {
            const auto& entry = metrics.at(m);
            require(entry.at("unit") == (m < 2 ? "microseconds" : "qpc-ticks"), "unit mismatch");
            require(entry.at("observations").is_string() && entry.at("totalUnits").is_string(),
                "metric totals are not u64 strings");
            require(entry.at("buckets").size() == 64 && entry.at("outcomes").size() == 4,
                "bounded metric arrays changed");
            for (const auto& count : entry.at("buckets")) require(count.is_string(), "bucket lost precision");
            for (const auto& count : entry.at("outcomes")) require(count.is_string(), "outcome lost precision");
        }
        require(value.at("physicalPresentation") == false && value.at("threadCpuMeasured") == false &&
            value.at("performanceGate") == false, "wire overstates qualification");
        coherent_wire = value; // Preserve the usable snapshot before the next test poisons configuration.
    });
    test("late-config-fails-without-resetting-evidence", [&] {
        require(!hooks::request_mode("0"), "late duplicate request accepted");
        hooks::bind_trace(1, 2, 3, 4, 5);
        const auto value = cold_json();
        rejected_wire = value;
        require(value.at("status") == "configuration-fault" && value.at("configurationFault") == true &&
            value.at("histogramsAvailable") == false && value.at("metrics").is_null(),
            "late config did not invalidate wire qualification");
        const auto original = hooks::storage().final_snapshot(true);
        require(original.session.process_id == GetCurrentProcessId() && original.session.process_creation == creation &&
            original.session.run_high == run_high && original.session.run_low == run_low &&
            original.session.qpc_frequency == 10000000, "late config relabeled session");
        require(metric(original, model::metric::raster_qpc).observations == 1 &&
            metric(original, model::metric::native_submit_context_qpc).observations == 1,
            "late config reset retained evidence");
    });
}
void missing_mapping_mode() {
    test("registered-trace-without-private-mapping-is-fault-not-clock", [] {
        bind_enabled();
        trace::registered = true;
        require(hooks::current_slot() == model::producer_limit, "fault fixture accidentally mapped");
        unsigned monotonic_reads = 0;
        {
            hooks::scope value(model::metric::raster_qpc);
            require(!value.finish(), "missing mapping accepted scope");
            hooks::observe_lateness(model::metric::task_due_late_us, 1.0,
                [&]() noexcept { ++monotonic_reads; return 1.25; });
        }
        require(trace::clock_reads == 0 && monotonic_reads == 0, "missing mapping read clock");
        hooks::stop();
        trace::registered = false;
        trace::quiet = true;
        const auto value = hooks::storage().final_snapshot(true);
        require(value.state == model::status::fault && value.faults == model::binding_fault &&
            !value.histograms_available, "missing registered mapping did not fail closed");
        const auto wire = cold_json();
        rejected_wire = wire;
        require(wire.at("status") == "fault" && wire.at("faults") == model::binding_fault &&
            wire.at("histogramsAvailable") == false && wire.at("metrics").is_null(),
            "missing mapping became usable JSON");
    });
}
}

int main(int argc, char** argv) {
    const bool missing = argc == 2 && std::string(argv[1]) == "--missing-mapping";
    if (argc != 1 && !missing) { std::cerr << "Expected no arguments or --missing-mapping\n"; return 2; }
    try {
        if (missing) missing_mapping_mode(); else ordinary_mode();
        std::cout << json({{"schema", "soui.private-render-phase-hooks-contracts/v1"},
            {"mode", missing ? "missing-mapping" : "ordinary"}, {"passed", passed},
            {"coherentSnapshot", coherent_wire}, {"rejectedSnapshot", rejected_wire},
            {"guiExecuted", false}, {"performanceGate", false}}).dump() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Contract failed: " << current_case << ": " << error.what() << '\n';
        return 1;
    }
}
