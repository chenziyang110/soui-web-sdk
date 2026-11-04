// Actual diagnostic DLL cold ABI probe. No WKE initialization, view, COM,
// window, script, renderer or input; each mode requires a fresh process.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "DiagnosticPhaseClient.h"
#include <miniblink/input_ui_api.h>
#include <cstring>
#include <iostream>
#include <iterator>

#ifndef SOUI_RENDER_PHASE_EXPECTED_SHA256
#error This probe requires the actual architecture-specific diagnostic DLL SHA.
#endif

namespace {
using perf::json;
using perf::require;
namespace fs = perf::fs;
using client = perf::diagnostic_render_phase::client;
using set_type = void (__cdecl*)(void*, const char*, const char*);
using get_type = void* (__cdecl*)(void*, const char*);
static_assert(sizeof(wkeInputUiApi) == (sizeof(void*) == 8 ? 144 : 80), "Input UI table layout changed");
static_assert(sizeof(wkeInputUiTraceConfig) == 48, "Input UI trace config layout changed");

json base_report(const char* mode) {
    return {{"schema", "soui.private-render-phase-cold-abi/v1"}, {"mode", mode},
        {"pointerBits", sizeof(void*) * 8}, {"compiledExpectedSha256", SOUI_RENDER_PHASE_EXPECTED_SHA256},
        {"passed", false}, {"cpuOnly", true}, {"wkeInitialized", false}, {"comInitialized", false},
        {"guiExecuted", false}, {"renderWorkExecuted", false}, {"physicalPresentation", false},
        {"threadCpuMeasured", false}, {"performanceGate", false}};
}
std::uint64_t filetime(const FILETIME& value) {
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}
HMODULE address_module(const void* address) {
    HMODULE module{};
    require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(address), &module) != FALSE, "Cannot identify actual ABI address module");
    return module;
}
bool module_absent(const fs::path& path) {
    return GetModuleHandleW(path.c_str()) == nullptr && GetModuleHandleW(path.filename().c_str()) == nullptr;
}
json snapshot_summary(const wkeInputUiTraceSnapshot& value) {
    return {{"structSize", value.structSize}, {"abiVersion", value.abiVersion}, {"mode", value.mode},
        {"flags", value.flags}, {"registeredThreads", value.registeredThreads},
        {"activeProducers", value.activeProducers}, {"pendingRegistrations", value.pendingRegistrations},
        {"processId", value.processId}, {"processIncarnation", std::to_string(value.processIncarnation)},
        {"runHigh", std::to_string(value.runIdHigh)}, {"runLow", std::to_string(value.runIdLow)},
        {"qpcFrequency", std::to_string(value.qpcFrequency)},
        {"attempted", std::to_string(value.attempted)}, {"recorded", std::to_string(value.recorded)},
        {"dropped", std::to_string(value.dropped)}, {"queued", std::to_string(value.queued)},
        {"unregisteredWrites", std::to_string(value.unregisteredWrites)},
        {"registrationRejections", std::to_string(value.registrationRejections)},
        {"causalLost", std::to_string(value.causalLost)}, {"stageCount", value.stageCount},
        {"counterCount", value.counterCount}, {"durationMetricCount", value.durationMetricCount},
        {"durationBucketCount", value.durationBucketCount}};
}
wkeInputUiTraceSnapshot trace_snapshot(const wkeInputUiApi& api, const wkeInputUiTraceConfig& config) {
    wkeInputUiTraceSnapshot value{};
    value.structSize = sizeof(value); value.abiVersion = WKE_INPUT_UI_ABI_VERSION;
    require(api.traceSnapshot(&value) == WKE_INPUT_UI_OK, "Actual traceSnapshot failed");
    require(value.structSize == sizeof(value) && value.abiVersion == WKE_INPUT_UI_ABI_VERSION && value.mode == 0 &&
        value.processId == config.processId && value.processIncarnation == config.processIncarnation &&
        value.runIdHigh == config.runIdHigh && value.runIdLow == config.runIdLow && value.qpcFrequency == config.qpcFrequency,
        "Actual trace snapshot identity changed");
    require(value.stageCount && value.stageCount <= WKE_INPUT_UI_STAGE_CAPACITY &&
        value.counterCount && value.counterCount <= WKE_INPUT_UI_COUNTER_CAPACITY &&
        value.durationMetricCount && value.durationMetricCount <= WKE_INPUT_UI_DURATION_CAPACITY &&
        value.durationBucketCount == WKE_INPUT_UI_DURATION_BUCKETS, "Actual trace snapshot dimensions invalid");
    require(value.attempted == 0 && value.recorded == 0 && value.dropped == 0 && value.queued == 0 &&
        value.unregisteredWrites == 0 && value.registrationRejections == 0 && value.causalLost == 0,
        "Cold probe unexpectedly produced or lost trace work");
    for (const auto count : value.stages) require(count == 0, "Cold probe produced a trace stage");
    for (const auto count : value.counters) require(count == 0, "Cold probe produced a trace counter");
    for (const auto& metric : value.durations)
        for (const auto count : metric) require(count == 0, "Cold probe produced a trace duration");
    return value;
}
struct trace_cleanup final {
    const wkeInputUiApi* api{};
    bool configured{}, retained{}, stopped{};
    ~trace_cleanup() {
        if (!api) return;
        if (retained) api->releaseTraceProducer();
        if (configured && !stopped) api->traceStop();
    }
};
json read_result(const fs::path& path) {
    require(fs::file_size(path) <= 128 * 1024, "Client report exceeds fixed write bound");
    std::ifstream file(path, std::ios::binary);
    require(bool(file), "Client report did not persist");
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    require(!file.bad(), "Client report read failed");
    return json::parse(text);
}
std::pair<std::string, json> late_snapshot(get_type get) {
    const auto* data = static_cast<const char*>(get(nullptr, "privateRenderPhaseSnapshotV1"));
    require(data != nullptr, "Late configuration getter returned null");
    std::size_t length = 0;
    while (length <= 16384 && data[length]) ++length;
    require(length && length <= 16384, "Late configuration snapshot extent invalid");
    require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, static_cast<int>(length), nullptr, 0) != 0,
        "Late configuration snapshot is not UTF-8");
    const std::string raw(data, length);
    return {raw, json::parse(raw)};
}

json positive(const fs::path& input, const fs::path& output) {
    require(input.is_absolute() && output.is_absolute(), "Probe DLL/output must be absolute");
    require(!fs::exists(output) && fs::is_directory(output.parent_path()), "Probe output must be a new directory");
    require(fs::create_directory(output), "Cannot create new probe output directory");
    json report = base_report("positive");
    client phase;
    try {
        const auto dll = fs::canonical(input);
        require(module_absent(dll), "Diagnostic DLL must not be preloaded before cold probe");
        phase.initialize(dll, SOUI_RENDER_PHASE_EXPECTED_SHA256);
        report["clientAfterInitialize"] = phase.identity();
        const auto loaded = fs::u8path(report["clientAfterInitialize"].at("loadedPath").get<std::string>());
        const auto module = GetModuleHandleW(loaded.c_str());
        require(module != nullptr, "Pinned actual diagnostic module missing");
        const auto get_api = reinterpret_cast<FN_wkeGetInputUiApi>(GetProcAddress(module, "wkeGetInputUiApi"));
        require(get_api && address_module(reinterpret_cast<const void*>(get_api)) == module,
            "Actual Input UI export not in verified diagnostic module");
        require(get_api(0) == nullptr && get_api(UINT32_MAX) == nullptr, "Wrong Input UI versions were accepted");
        const auto* api = get_api(WKE_INPUT_UI_ABI_VERSION);
        require(api && address_module(api) == module && api->structSize == sizeof(wkeInputUiApi) &&
            api->abiVersion == WKE_INPUT_UI_ABI_VERSION, "Actual Input UI table ABI/module mismatch");
        constexpr auto required_caps = WKE_INPUT_UI_CAP_TRACE | WKE_INPUT_UI_CAP_EXTERNAL_TRACE_PRODUCER |
            WKE_INPUT_UI_CAP_FRACTIONAL_FRAME_POLICY | WKE_INPUT_UI_CAP_FRAME_POLICY_STATE;
        require((api->capabilities & required_caps) == required_caps, "Actual Input UI capabilities missing");
        require(api->traceConfigure && api->traceStop && api->traceSnapshot && api->traceDrain &&
            api->retainTraceProducer && api->releaseTraceProducer && api->queryFramePolicy && api->applyFramePolicy,
            "Actual Input UI capability pointer missing");
        for (const auto* address : {reinterpret_cast<const void*>(api->traceConfigure),
                reinterpret_cast<const void*>(api->traceStop), reinterpret_cast<const void*>(api->traceSnapshot),
                reinterpret_cast<const void*>(api->traceDrain), reinterpret_cast<const void*>(api->retainTraceProducer),
                reinterpret_cast<const void*>(api->releaseTraceProducer), reinterpret_cast<const void*>(api->queryFramePolicy),
                reinterpret_cast<const void*>(api->applyFramePolicy)})
            require(address_module(address) == module, "Actual Input UI function is outside verified module");
        report["abi"] = {{"structSize", api->structSize}, {"abiVersion", api->abiVersion},
            {"capabilities", std::to_string(api->capabilities)}, {"requiredCapabilities", std::to_string(required_caps)},
            {"wrongVersionZeroRejected", true}, {"wrongVersionMaximumRejected", true},
            {"framePolicyPointersPresentButNotInvoked", true}};

        wkeInputUiTraceConfig config{};
        config.structSize = sizeof(config); config.abiVersion = WKE_INPUT_UI_ABI_VERSION; config.mode = 0;
        config.processId = GetCurrentProcessId();
        FILETIME created{}, exited{}, kernel{}, user{};
        require(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) != FALSE,
            "Actual process creation unavailable");
        config.processIncarnation = filetime(created);
        LARGE_INTEGER frequency{};
        require(QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0 && config.processIncarnation,
            "Actual process/QPC identity unavailable");
        config.qpcFrequency = static_cast<unsigned long long>(frequency.QuadPart);
        std::array<unsigned long long, 2> run{};
        require(BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(run.data()), static_cast<ULONG>(sizeof(run)),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0 && (run[0] || run[1]), "New cold probe run identity unavailable");
        config.runIdHigh = run[0]; config.runIdLow = run[1];
        trace_cleanup cleanup; cleanup.api = api;
        require(api->traceConfigure(&config) == WKE_INPUT_UI_OK, "Actual scalar traceConfigure failed");
        cleanup.configured = true;
        phase.after_trace_configured();
        const auto configured_identity = phase.identity();
        require(configured_identity.at("runHigh") == std::to_string(config.runIdHigh) &&
            configured_identity.at("runLow") == std::to_string(config.runIdLow), "Client captured a different trace run");
        report["clientAfterTraceConfigured"] = configured_identity;
        require(api->retainTraceProducer() == WKE_INPUT_UI_OK, "Actual external producer retain failed");
        cleanup.retained = true;
        const auto active = trace_snapshot(*api, config);
        require(active.flags == WKE_INPUT_UI_TRACE_AVAILABLE && active.registeredThreads == 1 &&
            active.activeProducers == 1 && active.pendingRegistrations == 0, "Actual retained trace producer is not active");
        report["activeTrace"] = snapshot_summary(active);
        api->releaseTraceProducer(); cleanup.retained = false;
        api->traceStop(); cleanup.stopped = true;
        const auto stopped = trace_snapshot(*api, config);
        require(stopped.flags == (WKE_INPUT_UI_TRACE_AVAILABLE | WKE_INPUT_UI_TRACE_STOPPED | WKE_INPUT_UI_TRACE_QUIESCENT) &&
            stopped.registeredThreads == 1 && stopped.activeProducers == 0 && stopped.pendingRegistrations == 0,
            "Actual trace did not stop/quiesce after release");
        report["stoppedTrace"] = snapshot_summary(stopped);
        require(phase.save_final(output), "Actual diagnostic final snapshot did not qualify/persist");
        const auto final_path = output / L"render-phase-cost-final.json";
        const auto final = read_result(final_path);
        require(final.at("snapshotUsable") == true && final.at("status") == "complete",
            "Persisted final report qualification differs");
        const auto& snapshot = final.at("snapshot");
        require(snapshot.at("registeredProducers") == 1 && snapshot.at("earlyWakes") == "0" &&
            snapshot.at("missingDue") == "0", "Actual private producer mapping/counts differ");
        const auto& metrics = snapshot.at("metrics");
        require(metrics.is_array() && metrics.size() == 7, "Actual final metric count differs");
        for (const auto& metric : metrics) {
            require(metric.at("observations") == "0" && metric.at("totalUnits") == "0",
                "Cold probe unexpectedly measured a render phase");
            for (const auto& count : metric.at("buckets")) require(count == "0", "Cold render bucket is nonzero");
            for (const auto& count : metric.at("outcomes")) require(count == "0", "Cold render outcome is nonzero");
        }
        const auto final_sha = perf::sha256(final_path);
        report["coherentFinal"] = {{"path", final_path.u8string()}, {"bytes", fs::file_size(final_path)},
            {"sha256", final_sha}, {"snapshot", snapshot}, {"zeroMetricsMean", "No render work was performed"}};

        const auto set = reinterpret_cast<set_type>(GetProcAddress(module, "wkeSetDebugConfig"));
        const auto get = reinterpret_cast<get_type>(GetProcAddress(module, "wkeGetDebugConfig"));
        require(set && get && address_module(reinterpret_cast<const void*>(set)) == module &&
            address_module(reinterpret_cast<const void*>(get)) == module, "Verified cold exports changed");
        set(nullptr, "privateRenderPhaseModeV1", "0");
        const auto late = late_snapshot(get);
        const auto& rejected = late.second;
        require(rejected.at("schema") == "soui.private-render-phase-cost/v1" && rejected.at("supported") == true &&
            rejected.at("status") == "configuration-fault" && rejected.at("configurationFault") == true &&
            rejected.at("histogramsAvailable") == false && rejected.at("metrics").is_null(),
            "Late mode setter was not rejected as configuration fault");
        for (const auto* field : {"processId", "processCreation", "runHigh", "runLow", "qpcFrequency", "registeredProducers"})
            require(rejected.at(field) == snapshot.at(field), "Late setter relabeled prior session/producer history");
        require(perf::sha256(final_path) == final_sha, "Late setter altered retained coherent report");
        report["lateConfiguration"] = {{"raw", late.first}, {"snapshot", rejected}};
        report["passed"] = true;
        perf::write_new(output / L"cold-abi-result.json", report.dump(2) + "\n");
        return report;
    } catch (const std::exception& error) {
        report["passed"] = false;
        report["error"] = error.what(); report["clientAtFailure"] = phase.identity();
        // Preserve the original failure; a failed write is not a passing probe.
        try { perf::write_new(output / L"cold-abi-result.json", report.dump(2) + "\n"); }
        catch (const std::exception& write_error) { report["failureReportWriteError"] = write_error.what(); }
        std::cout << report.dump() << '\n';
        throw;
    }
}

json reject_old(const fs::path& input) {
    require(input.is_absolute(), "Old DLL path must be absolute");
    const auto dll = fs::canonical(input);
    require(fs::is_regular_file(dll), "Old DLL input is not a regular file");
    require(module_absent(dll), "Old DLL was already loaded before negative probe");
    const auto original_sha = perf::sha256(dll);
    require(original_sha != SOUI_RENDER_PHASE_EXPECTED_SHA256, "Negative input is the authorized diagnostic DLL");
    client phase;
    std::string rejection;
    try { phase.initialize(dll, SOUI_RENDER_PHASE_EXPECTED_SHA256); }
    catch (const std::exception& error) { rejection = error.what(); }
    require(rejection == "Render phase DLL does not match compiled SHA", "Old DLL was not rejected by the precise pre-load SHA gate");
    const auto identity = phase.identity();
    require(identity.at("state") == "initialization-failed" && identity.at("initialized") == false &&
        identity.at("modulePinnedUntilProcessExit") == false && identity.at("loadedPath").is_null() &&
        identity.at("loadedFileSha256").is_null(), "Negative initialization reached module load/exports");
    require(identity.at("requestedFileSha256") == original_sha && module_absent(dll), "Old DLL identity changed or module was loaded");
    auto report = base_report("reject-old");
    report["path"] = dll.u8string(); report["oldFileSha256"] = original_sha;
    report["client"] = identity; report["rejection"] = rejection;
    report["moduleAbsentBefore"] = true; report["moduleAbsentAfter"] = true;
    report["rejectedBeforeLoadAndColdKeys"] = true; report["passed"] = true;
    return report;
}
}

int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    try {
        json result;
        if (argc == 5 && std::wstring(argv[1]) == L"--dll" && std::wstring(argv[3]) == L"--output")
            result = positive(fs::path(argv[2]), fs::path(argv[4]));
        else if (argc == 3 && std::wstring(argv[1]) == L"--reject-old")
            result = reject_old(fs::path(argv[2]));
        else {
            std::cerr << "Expected --dll ABS --output ABS_NEW_DIRECTORY or --reject-old ABS\n";
            return 2;
        }
        std::cout << result.dump() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Cold ABI probe failed: " << error.what() << '\n';
        return 1;
    }
}
