#include <soui/host/soui_miniblink_host.h>
#include <soui/json_bridge.h>
#include "startup_observer.h"
#include <windows.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <thread>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
using json = nlohmann::json;
namespace fs = std::filesystem;

[[noreturn]] void fatal(const char* code) noexcept {
  std::fprintf(stderr, "host-startup-fence-fatal:%s\n", code);
  std::fflush(stderr);
  ExitProcess(91); // Retain unresolved HWND, callback storage and borrowed owners.
}
void require(bool value, const char* code) { if (!value) fatal(code); }

std::string hash_file(HANDLE file) {
  BCRYPT_ALG_HANDLE provider = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD object_bytes = 0, received = 0;
  require(BCryptOpenAlgorithmProvider(&provider, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "hash-provider");
  require(BCryptGetProperty(provider, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_bytes),
      sizeof(object_bytes), &received, 0) >= 0 && received == sizeof(object_bytes), "hash-object");
  std::vector<unsigned char> object(object_bytes), buffer(1024U * 1024U);
  require(BCryptCreateHash(provider, &hash, object.data(), object_bytes, nullptr, 0, 0) >= 0, "hash-create");
  for (;;) {
    DWORD bytes = 0;
    require(ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes, nullptr) != FALSE, "hash-read");
    if (!bytes) break;
    require(BCryptHashData(hash, buffer.data(), bytes, 0) >= 0, "hash-data");
  }
  std::array<unsigned char, 32> digest{};
  require(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0, "hash-finish");
  require(BCryptDestroyHash(hash) >= 0 && BCryptCloseAlgorithmProvider(provider, 0) >= 0, "hash-close");
  constexpr char hex[] = "0123456789abcdef";
  std::string result(64, '0');
  for (std::size_t i = 0; i != digest.size(); ++i) {
    result[i * 2] = hex[digest[i] >> 4];
    result[i * 2 + 1] = hex[digest[i] & 15];
  }
  return result;
}

struct settings final { fs::path kernel, resources, output; std::string kernel_sha, case_name; };
settings parse() {
  int count = 0;
  auto** raw = CommandLineToArgvW(GetCommandLineW(), &count);
  require(raw != nullptr, "arguments");
  std::vector<std::wstring> args;
  try { args.assign(raw, raw + count); }
  catch (...) { LocalFree(raw); throw; }
  LocalFree(raw);
  require(args.size() == 11 && args[9] == L"--case" && args[1] == L"--kernel" && args[3] == L"--kernel-sha" &&
      args[5] == L"--resources" && args[7] == L"--output", "argument-shape");
  settings result{args[2], args[6], args[8], std::string(args[4].begin(), args[4].end()), std::string(args[10].begin(), args[10].end())};
  require(result.case_name == "hidden-start-registers" || result.case_name == "startup-close-sent" || result.case_name == "two-host-shared-engine", "case-id");
  require(result.kernel.is_absolute() && result.resources.is_absolute() && result.output.is_absolute(), "absolute-paths");
  require(fs::is_regular_file(result.kernel) && fs::is_regular_file(result.resources / "index.html") &&
      !fs::exists(result.output) && fs::is_directory(result.output.parent_path()), "input-or-fresh-output");
  require(result.kernel_sha.size() == 64 && result.kernel_sha.find_first_not_of("0123456789abcdef") == std::string::npos,
      "kernel-sha-format");
  wchar_t opt_in[2]{};
  require(GetEnvironmentVariableW(L"SOUI_DIAGNOSTIC_SCRIPT", opt_in, 2) == 1 && opt_in[0] == L'1',
      "diagnostic-opt-in-required-before-start");
  return result;
}

bool write_new(const fs::path& output, const std::string& bytes) {
  const auto file = CreateFileW(output.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool ok = bytes.size() <= MAXDWORD && WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
      written == bytes.size();
  const bool closed = CloseHandle(file) != FALSE;
  return ok && closed;
}

struct application final {
  std::atomic<unsigned> pulses{0}; // Declared before Bridge: handler context outlives its real task-service join.
  soui::sdk::json_bridge bridge{"app://startup-fence"}; // Retained until after actual Host destruction.
  std::unique_ptr<soui::sdk::webview_host> host; // Released before Bridge's real task-service join.
  soui::sdk::dynamic_miniblink_adapter* renderer{}; // Borrowed only while Host retains this exact adapter.
  HWND window{};
  bool closing{false}, owner_destroy_allowed{false}, observed{false};
  unsigned posted_events_before_observation{0}, close_retries{0};
};

LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
  auto* app = reinterpret_cast<application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    app = static_cast<application*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    SetLastError(0);
    const auto previous = SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    require(previous != 0 || GetLastError() == 0, "owner-state-store");
    app->window = window;
  }
  if (app) {
    try {
      if (message == soui::sdk::bridge_event_message) {
        if (!app->observed) ++app->posted_events_before_observation;
        require(app->host != nullptr, "event-without-host-owner");
        app->host->deliver_posted_event(reinterpret_cast<void*>(lparam));
        return 0;
      }
      // This fixed hidden-window test has no user resize/focus/action graph.
      // WM_CLOSE must never discard an owner while destroy() is unresolved.
      if (message == WM_CLOSE) return 0;
      if (message == WM_DESTROY) require(app->owner_destroy_allowed, "unexpected-owner-destroy");
      if (message == WM_NCDESTROY) SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    } catch (...) { fatal("owner-callback-exception"); }
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

json fixed_native(const std::string& diagnostics) {
  const auto marker = diagnostics.find(",script=");
  if (marker == std::string::npos) return nullptr;
  const auto observation = json::parse(diagnostics.substr(marker + 8), nullptr, false);
  if (!observation.is_object() || !observation.contains("vendorDrain") || !observation["vendorDrain"].is_object()) return nullptr;
  const auto& drain = observation["vendorDrain"];
  const std::array<const char*, 4> bool_names{"inspected", "valid", "vendorFailed", "nonAtomic"};
  for (auto name : bool_names) if (!drain.contains(name) || !drain[name].is_boolean()) return nullptr;
  const std::array<const char*, 8> number_names{
      "status", "closing", "drained", "queued", "running", "nativeScopes", "engineCallbacks", "oleRegistered"};
  for (auto name : number_names) if (!drain.contains(name) || !drain[name].is_number_unsigned()) return nullptr;
  const auto status = drain["status"].get<unsigned>();
  if (drain["inspected"] != true || drain["valid"] != true || drain["nonAtomic"] != true ||
      (status != 0 && status != 9) || drain["vendorFailed"] != (status == 9) ||
      drain["closing"].get<unsigned>() > 1 || drain["drained"].get<unsigned>() > 1 ||
      drain["oleRegistered"].get<unsigned>() > 1) return nullptr;
  json result = json::object();
  for (auto name : bool_names) result[name] = drain[name];
  for (auto name : number_names) result[name] = drain[name];
  const auto found = observation.find("registrationCompletion");
  if (found == observation.end() || !found->is_object()) return nullptr;
  const auto& completion = *found;
  for (auto name : {"observed", "shapeValid", "cancelled", "endpointMatches", "identityMatches", "nonAtomic"})
    if (!completion.contains(name) || !completion[name].is_boolean()) return nullptr;
  if (completion["nonAtomic"] != true || !completion.contains("kind") || !completion["kind"].is_number_unsigned() ||
      !completion.contains("identity") || !completion["identity"].is_object()) return nullptr;
  for (auto name : {"taskSequence", "scopeSequence"})
    if (!completion.contains(name) || !completion[name].is_string() || completion[name].get_ref<const std::string&>().empty() ||
        completion[name].get_ref<const std::string&>().size() > 20 ||
        completion[name].get_ref<const std::string&>().find_first_not_of("0123456789") != std::string::npos) return nullptr;
  for (auto name : {"view", "document", "geometry"})
    if (!completion["identity"].contains(name) || !completion["identity"][name].is_string() ||
        completion["identity"][name].get_ref<const std::string&>().empty() ||
        completion["identity"][name].get_ref<const std::string&>().size() > 20 ||
        completion["identity"][name].get_ref<const std::string&>().find_first_not_of("0123456789") != std::string::npos) return nullptr;
  json first = json::object();
  for (auto name : {"observed", "shapeValid", "cancelled", "endpointMatches", "identityMatches", "nonAtomic",
                   "kind", "taskSequence", "scopeSequence", "identity"}) first[name] = completion[name];
  json output{{"vendorDrain", result}, {"registrationCompletion", first}, {"nonAtomic", true}};
  if (observation.contains("contextReceipt") && observation["contextReceipt"].is_string())
    output["contextReceipt"] = observation["contextReceipt"];
  if (observation.contains("contextAccepted") && observation["contextAccepted"].is_boolean())
    output["contextAccepted"] = observation["contextAccepted"];
  if (observation.contains("broker") && observation["broker"].is_object()) {
    json broker = json::object();
    for (auto name : {"view", "document", "geometry"}) {
      if (!observation["broker"].contains(name) || !observation["broker"][name].is_string()) return nullptr;
      broker[name] = observation["broker"][name];
    }
    output["broker"] = std::move(broker);
  }
  for (auto name : {"documentReadyFlag", "applicationReadyFlag"}) {
    if (!observation.contains(name) || !observation[name].is_boolean()) return nullptr;
    output[name] = observation[name];
  }
  for (auto name : {"documentReadyReceipt", "applicationReadyReceipt"}) {
    if (!observation.contains(name) || !observation[name].is_string()) return nullptr;
    output[name] = observation[name];
  }
  if (observation.contains("contextReceiptAfter") && observation["contextReceiptAfter"].is_string())
    output["contextReceiptAfter"] = observation["contextReceiptAfter"];
  output["page"] = nullptr; // No JS observation is invented when the real exec state is unavailable.
  if (observation.contains("page")) {
    const auto& page = observation["page"];
    if (!page.is_object()) return nullptr;
    json fixed_page = json::object();
    for (auto name : {"readyState", "nativePort", "souiNativePort"}) {
      if (!page.contains(name) || !page[name].is_string()) return nullptr;
      const auto value = page[name].get<std::string>();
      const bool allowed = std::string_view(name) == "readyState"
          ? value == "loading" || value == "interactive" || value == "complete"
          : value == "undefined" || value == "function" || value == "object" || value == "string" ||
            value == "number" || value == "boolean" || value == "symbol" || value == "bigint";
      if (!allowed) return nullptr;
      fixed_page[name] = value;
    }
    output["page"] = std::move(fixed_page);
  }
  return output; // Fixed native fields only: no raw JS, token, handle, thread or pointer.
}

void close_retained(application& app) {
  app.closing = true;
  const auto started = GetTickCount64();
  for (;;) {
    const auto result = app.host->destroy();
    if (result.ok) break;
    ++app.close_retries;
    require(GetTickCount64() - started < 10000, "destroy-deadline-retained-owners");
    MSG message{};
    for (unsigned i = 0; i != 64 && PeekMessageW(&message, app.window, 0, 0, PM_REMOVE); ++i) {
      TranslateMessage(&message);
      DispatchMessageW(&message); // Only this actual owner and children, after the startup measurement.
    }
    require(MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT) != WAIT_FAILED, "cleanup-message-wait");
  }
  app.host.reset(); // True destroy proof precedes releasing the adapter and borrowed Bridge.
}

const char* architecture() noexcept {
#ifdef _WIN64
  return "x64";
#else
  return "x86";
#endif
}

void pump_owner(application& app) {
  MSG message{};
  for (unsigned i = 0; i != 64 && PeekMessageW(&message, app.window, 0, 0, PM_REMOVE); ++i) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  app.host->pump(); // Real public Host work; never used before immediate startup observation.
}

bool registered(const json& observation) {
  if (!observation.is_object()) return false;
  const auto& drain = observation["vendorDrain"];
  const auto& first = observation["registrationCompletion"];
  return drain["status"] == 0 && drain["closing"] == 0 && drain["oleRegistered"] == 1 &&
      first["observed"] == true && first["shapeValid"] == true && first["cancelled"] == false &&
      first["endpointMatches"] == true && first["kind"] == 3 && first["taskSequence"] != "0" && first["scopeSequence"] == "0";
  // Current document identity may already advance during real first navigation. The saved first receipt is not rewritten.
}

namespace observer = soui::sdk::detail::startup_fence_test;
json fixed_observer(const observer::snapshot& sample) {
  std::vector<std::string> times;
  const auto retained = std::min<std::uint64_t>(sample.engine_request_count, observer::event_capacity);
  const auto first = sample.engine_request_count > observer::event_capacity
      ? sample.engine_request_count % observer::event_capacity : 0;
  for (std::uint64_t i = 0; i < retained; ++i)
    times.push_back(std::to_string(sample.engine_request_qpc[(first + i) % observer::event_capacity]));
  return json{{"found", sample.found}, {"capacityExceeded", sample.capacity_exceeded}, {"clockValid", sample.clock_valid},
      {"fenceConfirmed", sample.fence_confirmed}, {"beginQpc", std::to_string(sample.fence_begin_qpc)},
      {"confirmedQpc", std::to_string(sample.fence_confirmed_qpc)}, {"engineRequestCount", std::to_string(sample.engine_request_count)},
      {"retainedEventQpc", times}, {"earlierEventsDiscarded", sample.engine_request_count > observer::event_capacity},
      {"preNavigationDrain", {{"status", sample.status}, {"closing", sample.closing}, {"oleRegistered", sample.ole_registered},
          {"queued", sample.queued}, {"running", sample.running}, {"nativeScopes", sample.scopes}, {"engineCallbacks", sample.callbacks}}}};
}
bool actual_fence(const observer::snapshot& sample) noexcept {
  return sample.found && !sample.capacity_exceeded && sample.clock_valid && sample.fence_confirmed &&
      sample.fence_begin_qpc > 0 && sample.fence_confirmed_qpc > sample.fence_begin_qpc &&
      sample.status == 0 && !sample.closing && sample.ole_registered == 1 &&
      !sample.queued && !sample.running && !sample.scopes && !sample.callbacks;
}
void register_pulse(application& app) {
  require(app.bridge.register_method("startup.pulse", "startup.pulse", [context = &app](std::string_view params) {
    const auto value = json::parse(params, nullptr, false);
    if (!value.is_object() || value.size() != 1 || !value.contains("sequence") ||
        !value["sequence"].is_number_unsigned() || value["sequence"].get<unsigned>() == 0)
      return std::string{R"({"error":"E_INVALID_PARAMS","message":"Expected a positive pulse sequence."})"};
    context->pulses.fetch_add(1, std::memory_order_release);
    return std::string{R"({"observed":true})"};
  }), "real-registered-pulse");
}
void install_host(application& app) {
  auto renderer = std::make_unique<soui::sdk::dynamic_miniblink_adapter>();
  app.renderer = renderer.get();
  app.host = std::make_unique<soui::sdk::webview_host>(std::move(renderer));
}
bool wait_for_page(application& app, unsigned& observed_pulses) {
  const auto begun = GetTickCount64();
  while (app.pulses.load(std::memory_order_acquire) < 2 && GetTickCount64() - begun < 10000) {
    pump_owner(app);
    require(MsgWaitForMultipleObjects(0, nullptr, FALSE, 1, QS_ALLINPUT) != WAIT_FAILED, "page-progress-wait");
  }
  observed_pulses = app.pulses.load(std::memory_order_acquire);
  return observed_pulses >= 2;
}
void withdraw_owner(application& app) {
  app.owner_destroy_allowed = true;
  const auto owner = app.window;
  require(DestroyWindow(owner) != FALSE && !IsWindow(owner), "owner-withdrawal");
  app.window = nullptr;
}

int run(const settings& options, HINSTANCE instance) {
  auto app = std::make_unique<application>();
  std::unique_ptr<application> second;
  try { // Catch while every true owner exists; unresolved resources never unwind through application.
    const auto held_kernel = CreateFileW(options.kernel.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    require(held_kernel != INVALID_HANDLE_VALUE, "held-kernel");
    const auto actual_sha = hash_file(held_kernel);
    require(actual_sha == options.kernel_sha, "actual-kernel-identity");
    FILETIME creation{}, exit{}, kernel{}, user{};
    require(GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user) != FALSE, "actual-process-identity");
    const auto creation_time = (std::uint64_t(creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
    LARGE_INTEGER frequency{};
    require(QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0, "actual-qpc-frequency");
    const std::wstring class_name = L"SouiHostStartupFence_" + std::to_wstring(GetCurrentProcessId());
    WNDCLASSW type{};
    type.lpfnWndProc = &procedure; type.hInstance = instance; type.lpszClassName = class_name.c_str();
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    require(type.hCursor != nullptr && RegisterClassW(&type) != 0, "owner-class");
    const auto create_owner = [&](application& state) {
      const auto owner = CreateWindowExW(0, class_name.c_str(), L"SOUI controlled startup regression",
          WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 640, 480, nullptr, nullptr, instance, &state);
      require(owner != nullptr && !IsWindowVisible(owner), "hidden-owner");
      register_pulse(state); install_host(state);
      return owner;
    };
    const auto owner = create_owner(*app);
    soui::sdk::host_config config;
    config.architecture = architecture(); config.miniblink_binary = options.kernel; config.resource_root = options.resources;
    config.owner_window = owner; config.bridge_adapter = &app->bridge;
    std::atomic<bool> close_sent{false}, close_returned{false};
    std::thread closer;
    if (options.case_name == "startup-close-sent") {
      const auto* borrowed_app = app.get();
      closer = std::thread([owner, borrowed_app, &close_sent, &close_returned] {
        try {
          const auto begun = GetTickCount64();
          HWND child = nullptr;
          for (;;) {
            child = GetWindow(owner, GW_CHILD);
            DWORD process = 0;
            if (child && GetParent(child) == owner &&
                GetWindowThreadProcessId(child, &process) != GetCurrentThreadId() &&
                process == GetCurrentProcessId() &&
                GetWindowLongPtrW(child, GWLP_USERDATA) == reinterpret_cast<LONG_PTR>(borrowed_app->renderer)) break;
            require(GetTickCount64() - begun < 10000, "close-sender-child-deadline");
            Sleep(1); // Actual child publication can precede NCCREATE userdata installation.
          }
          close_sent.store(true, std::memory_order_release);
          (void)SendMessageW(child, WM_CLOSE, 0, 0);
          close_returned.store(true, std::memory_order_release);
        } catch (...) { fatal("close-sender-exception-retained-owners"); }
      });
    }
    const auto mounted = app->host->start(config); // No caller owner pump around this genuine public start.
    if (closer.joinable()) closer.join(); // No detach or timeout-based release of sender context.
    const auto immediate = mounted.ok ? fixed_native(app->host->application_diagnostics()) : json(nullptr);
    const auto first_fence = observer::read(app->renderer);
    app->observed = true;
    const bool no_caller_pump = app->posted_events_before_observation == 0 && !IsWindowVisible(owner);
    const bool had_session = app->host->session() != nullptr;
    unsigned page_pulse_calls = 0;
    bool action_seen = mounted.ok && options.case_name != "startup-close-sent" && wait_for_page(*app, page_pulse_calls);
    const auto after_page_wait = mounted.ok && options.case_name != "startup-close-sent"
        ? fixed_native(app->host->application_diagnostics()) : json(nullptr);
    const auto after_page_wait_engine = mounted.ok ? observer::read(app->renderer) : observer::snapshot{};
    bool second_started = false, second_session = false, overlap = false, same_first_document = false;
    soui::sdk::host_result second_start{false, "", ""};
    json second_immediate = nullptr, first_before_second = nullptr, first_after_second = nullptr;
    observer::snapshot second_fence{}, first_engine{};
    std::vector<std::string> overlapping_events;
    if (options.case_name == "two-host-shared-engine" && mounted.ok && action_seen) {
      first_before_second = fixed_native(app->host->application_diagnostics());
      second = std::make_unique<application>();
      const auto second_owner = create_owner(*second);
      auto second_config = config;
      second_config.owner_window = second_owner; second_config.bridge_adapter = &second->bridge;
      second_start = second->host->start(second_config); // A's real shared engine is not held or parked.
      second_started = second_start.ok;
      second_session = second->host->session() != nullptr;
      second_immediate = second_started ? fixed_native(second->host->application_diagnostics()) : json(nullptr);
      second_fence = observer::read(second->renderer);
      first_engine = observer::read(app->renderer);
      second->observed = true;
      first_after_second = fixed_native(app->host->application_diagnostics());
      for (const auto time : first_engine.engine_request_qpc)
        if (time > second_fence.fence_begin_qpc && time < second_fence.fence_confirmed_qpc)
          overlapping_events.push_back(std::to_string(time));
      overlap = first_engine.found && first_engine.clock_valid && !first_engine.capacity_exceeded &&
          actual_fence(second_fence) && !overlapping_events.empty();
      same_first_document = first_before_second.is_object() && first_after_second.is_object() &&
          registered(first_before_second) && registered(first_after_second) &&
          first_before_second.value("contextAccepted", false) && first_after_second.value("contextAccepted", false) &&
          first_before_second["contextReceipt"] == first_after_second["contextReceipt"] &&
          first_before_second["broker"] == first_after_second["broker"];
    }
    const bool cancelled = options.case_name == "startup-close-sent" && !mounted.ok &&
        mounted.code == "E_CURSOR_STARTUP_CANCELLED" && !had_session &&
        close_sent.load(std::memory_order_acquire) && close_returned.load(std::memory_order_acquire);
    const bool first_ok = no_caller_pump && mounted.ok && had_session && actual_fence(first_fence) &&
        registered(immediate) && action_seen;
    bool desired = options.case_name == "startup-close-sent" ? no_caller_pump && cancelled : first_ok;
    if (options.case_name == "two-host-shared-engine")
      desired = first_ok && second_started && second_session && registered(second_immediate) &&
          actual_fence(second_fence) && overlap && same_first_document &&
          second->posted_events_before_observation == 0;
    json owners = json::array();
    if (second) {
      close_retained(*second); withdraw_owner(*second);
      owners.push_back({{"role", "B"}, {"hostDestroyReturned", true}, {"ownerWithdrawn", true},
          {"closeRetries", second->close_retries},
          {"pulseCallsBeforeBridgeJoin", second->pulses.load(std::memory_order_acquire)}});
      second.reset(); // B's actual task-service join occurs while its handler context still exists.
    }
    close_retained(*app); withdraw_owner(*app);
    owners.push_back({{"role", "A"}, {"hostDestroyReturned", true}, {"ownerWithdrawn", true},
        {"closeRetries", app->close_retries},
        {"pulseCallsBeforeBridgeJoin", app->pulses.load(std::memory_order_acquire)}});
    const auto early_events = app->posted_events_before_observation;
    app.reset(); // Actual Bridge joins before its earlier-declared atomic handler context is destroyed.
    require(UnregisterClassW(class_name.c_str(), instance) != FALSE, "owner-class-withdrawal");
    require(soui::sdk::shutdown_miniblink_runtime().ok, "actual-runtime-shutdown");
    require(CloseHandle(held_kernel) != FALSE, "held-kernel-close");
    const json report{
        {"schema", "soui.public-host-startup-fence/1"}, {"case", options.case_name}, {"architecture", architecture()},
        {"pid", GetCurrentProcessId()}, {"creationFileTime", std::to_string(creation_time)}, {"dllSha256", actual_sha},
        {"qpcFrequency", std::to_string(frequency.QuadPart)},
        {"startReturned", mounted.ok}, {"startCode", mounted.code}, {"publishedSession", had_session},
        {"callerPumpsBeforeObservation", 0}, {"postedEventsBeforeObservation", early_events},
        {"hiddenOwnerAtObservation", no_caller_pump}, {"immediate", immediate}, {"firstFence", fixed_observer(first_fence)},
        {"realPageActionObserved", action_seen}, {"pagePulseCallsAtWaitEnd", page_pulse_calls},
        {"afterPageWait", after_page_wait}, {"afterPageWaitEngine", fixed_observer(after_page_wait_engine)},
        {"actualCloseSent", close_sent.load(std::memory_order_acquire)},
        {"actualCloseReturned", close_returned.load(std::memory_order_acquire)}, {"cancellationObserved", cancelled},
        {"secondStarted", second_started}, {"secondStartCode", second_start.code}, {"secondPublishedSession", second_session},
        {"secondImmediate", second_immediate}, {"secondFence", fixed_observer(second_fence)},
        {"firstEngine", fixed_observer(first_engine)}, {"overlappingEngineEventQpc", overlapping_events},
        {"actualEngineOverlap", overlap}, {"sameFirstDocument", same_first_document},
        {"firstBeforeSecond", first_before_second}, {"firstAfterSecond", first_after_second},
        {"owners", owners}, {"bridgeDestroyedAfterHost", true}, {"runtimeShutdownReturned", true},
        {"finiteCaseAccepted", desired}, {"moduleUnloadQualified", false}, {"fullSdkQualified", false},
        {"win7Qualified", false}, {"qualificationPassed", false}, {"releaseReady", false}};
    require(write_new(options.output, report.dump(2) + '\n'), "fresh-report-write");
    return desired ? 0 : 2; // Absence of a retained overlap event fails the observation, never proves no progress.
  } catch (...) { fatal("native-owner-scope-exception"); }
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
  std::set_terminate([] { fatal("cpp-terminate-retained-owners"); });
  try {
    const auto options = parse();
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    require(SUCCEEDED(com), "owner-com-initialize");
    const int result = run(options, instance);
    CoUninitialize();
    return result;
  } catch (...) { fatal("preflight-exception"); }
}
