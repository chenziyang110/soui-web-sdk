#pragma once

// Dedicated diagnostic consumer only. Never included by the default SDK.
// Its build supplies SOUI_RENDER_PHASE_EXPECTED_SHA256 as a string literal
// from the completed diagnostic DLL build receipt, separately per architecture.
#include "support.h"
#include <algorithm>
#include <initializer_list>
#include <limits>
#include <set>
#include <string_view>

namespace perf { namespace diagnostic_render_phase {

class client final {
public:
    client() = default;
    client(const client&) = delete;
    client& operator=(const client&) = delete;
    client(client&&) = delete;
    client& operator=(client&&) = delete;
    // No FreeLibrary: the verified module is explicitly pinned until exit.
    // The read-only file lease also excludes replacement while this client lives.
    ~client() = default;

    void initialize(fs::path dll, const char* compiled_expected_sha256) {
        require(!initialize_attempted_, "Render phase initialization is single-use");
        initialize_attempted_ = true;
        try {
            expected_sha_ = sha_text(compiled_sha());
            require(sha_text(compiled_expected_sha256) == expected_sha_,
                "Render phase expected SHA is not the compiled authorization");
            require(dll.is_absolute(), "Render phase DLL requires an absolute path");
            requested_path_ = fs::canonical(dll);
            require(fs::is_regular_file(requested_path_), "Render phase DLL is not a file");
            file_lease_.value = CreateFileW(requested_path_.c_str(), GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            require(file_lease_.value != INVALID_HANDLE_VALUE, "Render phase DLL lease failed");
            disk_sha_ = sha256(requested_path_);
            require(disk_sha_ == expected_sha_, "Render phase DLL does not match compiled SHA");

            module_ = LoadLibraryExW(requested_path_.c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            require(module_ != nullptr, "Verified render phase DLL load failed");
            loaded_path_ = module_path(module_);
            require(fs::equivalent(requested_path_, loaded_path_), "Loaded render phase module path differs");
            loaded_sha_ = sha256(loaded_path_);
            require(loaded_sha_ == expected_sha_, "Loaded render phase DLL file differs");
            set_ = reinterpret_cast<set_type>(GetProcAddress(module_, "wkeSetDebugConfig"));
            get_ = reinterpret_cast<get_type>(GetProcAddress(module_, "wkeGetDebugConfig"));
            require(set_ && get_, "Verified render phase exports are missing");
            require(address_module(reinterpret_cast<const void*>(set_)) == module_ &&
                address_module(reinterpret_cast<const void*>(get_)) == module_,
                "Render phase export address belongs to another module");
            HMODULE retained{};
            require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(set_), &retained) && retained == module_,
                "Render phase module pin failed");
            pinned_ = true;
            own_pid_ = GetCurrentProcessId();
            FILETIME created{}, exited{}, kernel{}, user{};
            require(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) != FALSE,
                "Render phase process incarnation unavailable");
            own_creation_ = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
            LARGE_INTEGER frequency{};
            require(QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0 && own_creation_,
                "Render phase QPC/process identity unavailable");
            own_frequency_ = static_cast<std::uint64_t>(frequency.QuadPart);
            // The new key is never sent to an unverified or old DLL. No view,
            // script, timer or trace configure has been touched by this client.
            set_(nullptr, "privateRenderPhaseModeV1", "1");
            initialized_ = true;
            state_ = "cold-requested";
        } catch (const std::exception& error) {
            state_ = "initialization-failed";
            error_ = error.what();
            throw;
        }
    }

    // Call exactly once immediately after the existing scalar trace configure.
    void after_trace_configured() {
        require(initialized_ && !after_attempted_, "Render phase configuration check is out of order");
        after_attempted_ = true;
        after_ = read_snapshot();
        try {
            require(after_.error.empty(), "Render phase initial snapshot is unavailable or malformed");
            validate_base(after_.value);
            const auto& value = after_.value;
            require(value.at("status") == "busy" && value.at("histogramsAvailable") == false &&
                value.at("metrics").is_null() && number(value.at("faults"), 31) == 0 &&
                value.at("configurationFault") == false, "Render phase trace did not enter clean busy state");
            validate_process(value);
            run_high_ = u64(value.at("runHigh"));
            run_low_ = u64(value.at("runLow"));
            require(run_high_ || run_low_, "Render phase run identity is zero");
            after_capture_end_ = u64(value.at("captureEndQpc"));
            configured_ = true;
            state_ = "trace-configured";
        } catch (const std::exception& error) {
            state_ = "configuration-check-failed";
            error_ = after_.error.empty() ? error.what() : after_.error;
            throw;
        }
    }

    // Cached identity only: this accessor never invokes the DLL or reads QPC.
    json identity() const {
        return {{"schema", "soui.private-render-phase-client/v1"}, {"state", state_},
            {"compiledExpectedSha256", expected_sha_}, {"requestedPath", path_json(requested_path_)},
            {"requestedFileSha256", text_json(disk_sha_)}, {"loadedPath", path_json(loaded_path_)},
            {"loadedFileSha256", text_json(loaded_sha_)}, {"modulePinnedUntilProcessExit", pinned_},
            {"exportsSameVerifiedModule", pinned_}, {"initialized", initialized_},
            {"traceConfigurationVerified", configured_}, {"processId", own_pid_},
            {"processCreation", own_creation_ ? json(std::to_string(own_creation_)) : json(nullptr)},
            {"qpcFrequency", own_frequency_ ? json(std::to_string(own_frequency_)) : json(nullptr)},
            {"runHigh", configured_ ? json(std::to_string(run_high_)) : json(nullptr)},
            {"runLow", configured_ ? json(std::to_string(run_low_)) : json(nullptr)},
            {"initialSnapshotRaw", after_.raw}, {"initialSnapshot", after_.value},
            {"initialInvalidUtf8Hex", after_.invalid_utf8_hex},
            {"error", text_json(error_)}, {"physicalPresentation", false},
            {"threadCpuMeasured", false}, {"performanceGate", false}};
    }

    // Caller first completes scalar_capture::finish and all original shutdown
    // guards. This reads once, without waiting/polling, and retains busy/fault.
    bool save_final(fs::path output) {
        if (save_attempted_) return false;
        save_attempted_ = true;
        snapshot_read final;
        bool usable = false;
        std::string reason;
        try {
            if (!initialized_ || !configured_)
                throw std::runtime_error(error_.empty() ? "Render phase initial trace qualification missing" : error_);
            final = read_snapshot();
            require(final.error.empty(), "Render phase final snapshot is unavailable or malformed");
            validate_base(final.value);
            validate_process(final.value);
            require(u64(final.value.at("runHigh")) == run_high_ &&
                u64(final.value.at("runLow")) == run_low_, "Render phase final run changed");
            require(u64(final.value.at("captureBeginQpc")) >= after_capture_end_,
                "Render phase final clock precedes configuration check");
            require(final.value.at("status") == "complete" && final.value.at("histogramsAvailable") == true &&
                number(final.value.at("faults"), 31) == 0 && final.value.at("configurationFault") == false,
                "Render phase final state is not complete and usable");
            validate_metrics(final.value.at("metrics"));
            usable = true;
        } catch (const std::exception& error) {
            reason = final.error.empty() ? error.what() : final.error;
        }
        state_ = usable ? "complete" : "final-unavailable";
        if (!usable) error_ = reason;
        try {
            require(output.is_absolute() && fs::is_directory(output), "Render phase output directory unavailable");
            const json record = {{"schema", "soui.private-render-phase-final/v1"},
                {"status", state_}, {"error", text_json(reason)}, {"identity", identity()},
                {"raw", final.raw}, {"invalidUtf8Hex", final.invalid_utf8_hex}, {"snapshot", final.value},
                {"snapshotUsable", usable}, {"scope", "whole trace lifetime, inclusive wall durations"},
                {"stageCostsAdditive", false}, {"physicalPresentation", false},
                {"threadCpuMeasured", false}, {"performanceGate", false}};
            write_new(output / L"render-phase-cost-final.json", record.dump(2) + "\n");
        } catch (const std::exception& error) {
            state_ = "final-write-failed";
            error_ = error.what();
            return false;
        }
        return usable;
    }

private:
    using set_type = void (__cdecl*)(void*, const char*, const char*);
    using get_type = void* (__cdecl*)(void*, const char*);
    static constexpr std::size_t snapshot_limit = 16384;
    struct snapshot_read {
        json raw = nullptr, value = nullptr, invalid_utf8_hex = nullptr;
        std::string error;
    };
    static const char* compiled_sha() noexcept {
#ifdef SOUI_RENDER_PHASE_EXPECTED_SHA256
        return SOUI_RENDER_PHASE_EXPECTED_SHA256;
#else
        return ""; // Missing build authorization fails before LoadLibrary.
#endif
    }
    static std::string sha_text(const char* value) {
        require(value != nullptr, "Missing compiled render phase SHA");
        std::string result;
        for (std::size_t i = 0; i < 64; ++i) {
            const char c = value[i];
            require((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'),
                "Render phase SHA must be 64 hexadecimal characters");
            result.push_back(c >= 'A' && c <= 'F' ? static_cast<char>(c + ('a' - 'A')) : c);
        }
        require(value[64] == '\0', "Render phase SHA is too long");
        return result;
    }
    static fs::path module_path(HMODULE module) {
        std::array<wchar_t, 32768> buffer{};
        const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        require(length && length < buffer.size(), "Render phase module path unavailable or truncated");
        return fs::canonical(fs::path(buffer.data(), buffer.data() + length));
    }
    static HMODULE address_module(const void* address) {
        HMODULE module{};
        require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address), &module) != FALSE, "Render phase address module unavailable");
        return module;
    }
    static json text_json(const std::string& text) { return text.empty() ? json(nullptr) : json(text); }
    static json path_json(const fs::path& path) { return path.empty() ? json(nullptr) : json(path.u8string()); }
    static void keys(const json& value, std::initializer_list<const char*> names) {
        require(value.is_object() && value.size() == names.size(), "Render phase object keys differ");
        for (const auto* name : names) require(value.contains(name), "Render phase key missing");
    }
    static std::uint64_t number(const json& value, std::uint64_t limit) {
        require(value.is_number_unsigned(), "Render phase integer type invalid");
        const auto result = value.get<std::uint64_t>();
        require(result <= limit, "Render phase integer out of range");
        return result;
    }
    static std::uint64_t u64(const json& value) {
        require(value.is_string(), "Render phase u64 is not a decimal string");
        const auto& text = value.get_ref<const std::string&>();
        require(!text.empty() && text.size() <= 20 && (text.size() == 1 || text[0] != '0'),
            "Render phase u64 form invalid");
        std::uint64_t result = 0;
        for (const char c : text) {
            require(c >= '0' && c <= '9', "Render phase u64 digit invalid");
            const auto digit = static_cast<unsigned>(c - '0');
            require(result <= ((std::numeric_limits<std::uint64_t>::max)() - digit) / 10,
                "Render phase u64 overflow");
            result = result * 10 + digit;
        }
        return result;
    }
    static std::uint64_t add(std::uint64_t a, std::uint64_t b) {
        require(b <= (std::numeric_limits<std::uint64_t>::max)() - a, "Render phase sum overflow");
        return a + b;
    }
    static void validate_base(const json& value) {
        keys(value, {"schema", "supported", "status", "histogramsAvailable", "faults", "configurationFault",
            "processId", "processCreation", "runHigh", "runLow", "qpcFrequency", "captureBeginQpc",
            "captureEndQpc", "registeredProducers", "earlyWakes", "missingDue", "scope", "metrics",
            "physicalPresentation", "threadCpuMeasured", "performanceGate"});
        require(value.at("schema") == "soui.private-render-phase-cost/v1" && value.at("supported") == true,
            "Render phase schema unsupported");
        require(value.at("status").is_string(), "Render phase status type invalid");
        const auto status = value.at("status").get<std::string>();
        require(status == "disabled" || status == "busy" || status == "complete" || status == "fault" ||
            status == "configuration-fault", "Render phase status unknown");
        require(value.at("histogramsAvailable").is_boolean() && value.at("configurationFault").is_boolean(),
            "Render phase boolean type invalid");
        require(value.at("physicalPresentation") == false && value.at("threadCpuMeasured") == false &&
            value.at("performanceGate") == false, "Render phase qualification flags invalid");
        require(value.at("scope") == "whole-trace-lifetime-inclusive-wall; not thread CPU or per-frame causality",
            "Render phase scope changed");
        (void)number(value.at("processId"), UINT32_MAX);
        (void)number(value.at("faults"), 31);
        (void)number(value.at("registeredProducers"), 32);
        for (const auto* name : {"processCreation", "runHigh", "runLow", "qpcFrequency", "earlyWakes", "missingDue"})
            (void)u64(value.at(name));
        const auto begin = u64(value.at("captureBeginQpc"));
        require(begin && u64(value.at("captureEndQpc")) >= begin, "Render phase capture clock invalid");
        if (value.at("histogramsAvailable") == false)
            require(value.at("metrics").is_null(), "Unavailable render phase metrics must be null");
    }
    void validate_process(const json& value) const {
        require(own_pid_ && number(value.at("processId"), UINT32_MAX) == own_pid_ &&
            u64(value.at("processCreation")) == own_creation_ && u64(value.at("qpcFrequency")) == own_frequency_,
            "Render phase process/QPC identity differs");
    }
    static void validate_metrics(const json& metrics) {
        constexpr const char* names[] = {"shared_wake_late_us", "task_due_late_us", "raster_qpc", "spare_copy_qpc",
            "ui_mirror_copy_qpc", "native_submit_context_qpc", "native_submit_layered_qpc"};
        require(metrics.is_array() && metrics.size() == 7, "Render phase metric count differs");
        const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        for (std::size_t m = 0; m < 7; ++m) {
            const auto& metric = metrics.at(m);
            keys(metric, {"name", "unit", "observations", "totalUnits", "buckets", "outcomes"});
            require(metric.at("name") == names[m] && metric.at("unit") == (m < 2 ? "microseconds" : "qpc-ticks"),
                "Render phase metric name/unit differs");
            const auto observations = u64(metric.at("observations"));
            const auto total = u64(metric.at("totalUnits"));
            const auto& buckets = metric.at("buckets");
            const auto& outcomes = metric.at("outcomes");
            require(buckets.is_array() && buckets.size() == 64 && outcomes.is_array() && outcomes.size() == 4,
                "Render phase bounded arrays differ");
            std::uint64_t count = 0, lower = 0, upper = 0, outcome_count = 0;
            for (std::size_t b = 0; b < 64; ++b) {
                const auto n = u64(buckets.at(b));
                count = add(count, n);
                const auto lo = b == 0 ? 0 : (std::uint64_t(1) << b);
                const auto hi = b == 63 ? maximum : (std::uint64_t(1) << (b + 1)) - 1;
                require(!lo || n <= (maximum - lower) / lo, "Render phase bucket lower bound overflow");
                lower += lo * n;
                upper = n > (maximum - upper) / hi ? maximum : upper + hi * n;
            }
            for (const auto& value : outcomes) outcome_count = add(outcome_count, u64(value));
            require(count == observations && outcome_count == observations && total >= lower && total <= upper,
                "Render phase histogram/outcome/total conservation failed");
        }
    }
    snapshot_read read_snapshot() const {
        snapshot_read result;
        try {
            require(pinned_ && get_, "Render phase verified getter unavailable");
            const auto* data = static_cast<const char*>(get_(nullptr, "privateRenderPhaseSnapshotV1"));
            require(data != nullptr, "Render phase getter returned null");
            std::size_t length = 0;
            while (length <= snapshot_limit && data[length]) ++length;
            require(length && length <= snapshot_limit, "Render phase snapshot is empty or exceeds 16 KiB");
            if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, static_cast<int>(length), nullptr, 0)) {
                constexpr char hex[] = "0123456789abcdef";
                std::string bytes; bytes.reserve(length * 2);
                for (std::size_t i = 0; i < length; ++i) {
                    const auto b = static_cast<unsigned char>(data[i]); bytes += hex[b >> 4]; bytes += hex[b & 15];
                }
                result.invalid_utf8_hex = std::move(bytes);
                throw std::runtime_error("Render phase snapshot is not UTF-8");
            }
            const std::string raw(data, length);
            result.raw = raw;
            std::map<int, std::set<std::string>> object_keys;
            result.value = json::parse(raw, [&](int depth, json::parse_event_t event, json& value) {
                require(depth <= 6, "Render phase JSON nesting exceeds bound");
                if (event == json::parse_event_t::object_start) object_keys[depth + 1].clear();
                if (event == json::parse_event_t::key)
                    require(object_keys[depth].insert(value.get<std::string>()).second, "Duplicate render phase JSON key");
                if (event == json::parse_event_t::object_end) object_keys.erase(depth + 1);
                return true;
            });
        } catch (const std::exception& error) {
            result.error = error.what();
        }
        return result;
    }

    HMODULE module_{};
    set_type set_{};
    get_type get_{};
    handle file_lease_;
    fs::path requested_path_, loaded_path_;
    std::string expected_sha_, disk_sha_, loaded_sha_, state_ = "not-initialized", error_;
    std::uint32_t own_pid_{};
    std::uint64_t own_creation_{}, own_frequency_{}, run_high_{}, run_low_{}, after_capture_end_{};
    snapshot_read after_;
    bool initialize_attempted_{}, initialized_{}, pinned_{}, after_attempted_{}, configured_{}, save_attempted_{};
};

}} // namespace perf::diagnostic_render_phase
