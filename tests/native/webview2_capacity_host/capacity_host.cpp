#include <soui/host/webview2_host.h>
#include <soui/json_bridge.h>
#include <json.hpp>
#include <windows.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using json = nlohmann::json;
using clock_type = std::chrono::steady_clock;
namespace fs = std::filesystem;
constexpr UINT observe_message = WM_APP + 17;
constexpr auto occupancy_window = std::chrono::milliseconds(1000);
constexpr auto host_deadline = std::chrono::seconds(55);

[[noreturn]] void fail(const char* reason) {
  // No failed owner is unwound while its callback/BrowserProcessExited is owed.
  std::cerr << "WebView2 capacity fixture failed: " << reason << '\n' << std::flush;
  ExitProcess(91);
}
void require(bool ok, const char* reason) { if (!ok) fail(reason); }
struct retain_owner_on_exception {
  const int entered = std::uncaught_exceptions();
  ~retain_owner_on_exception() noexcept {
    if (std::uncaught_exceptions() > entered) fail("owner operation threw before validated close");
  }
};
std::string creation_time() {
  FILETIME created{}, exited{}, kernel{}, user{};
  require(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) != FALSE,
          "subject FILETIME missing");
  ULARGE_INTEGER value{}; value.LowPart = created.dwLowDateTime; value.HighPart = created.dwHighDateTime;
  return std::to_string(value.QuadPart);
}
void create_new_json(const fs::path& target, const json& value) {
  const auto bytes = value.dump(2) + "\n";
  require(bytes.size() <= 65536, "receipt unexpectedly large");
  const HANDLE file = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
  require(file != INVALID_HANDLE_VALUE, "fresh receipt CreateNew failed");
  DWORD written{};
  const bool good = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE &&
      written == bytes.size() && FlushFileBuffers(file) != FALSE;
  const bool closed = CloseHandle(file) != FALSE;
  require(good && closed, "receipt raw write/close failed");
}

struct observed_bridge final : soui::sdk::bridge_adapter {
  soui::sdk::json_bridge real{"app://webview2-capacity", soui::sdk::diagnostics::mode::scalar};
  HWND owner{};
  std::atomic<bool> holding{false}, delivery_permitted{false}, report_received{false};
  std::atomic<unsigned> business{0};
  std::mutex gate_mutex, evidence_mutex;
  std::condition_variable gate;
  unsigned work_entries{}, renewals{}, cleanups{};
  std::array<bool, 129> seen{};
  json trace = json::array(), page_report;
  clock_type::time_point first_entered{}, released{};

  observed_bridge() {
    require(real.register_method("capacity.work", "capacity.work", [&](std::string_view raw) {
      const auto params = json::parse(raw);
      require(params.is_object() && params.size() == 1 && params.contains("index") &&
              params["index"].is_number_integer(), "work params changed");
      const int index = params["index"].get<int>();
      require(index >= 0 && index <= 128, "refused request entered real business");
      require(!seen[static_cast<std::size_t>(index)], "ordinary business duplicated");
      seen[static_cast<std::size_t>(index)] = true;
      business.fetch_add(1, std::memory_order_acq_rel);
      return json{{"index", index}}.dump();
    }), "normal method registration failed");
    require(real.register_method("capacity.report", "capacity.report", [&](std::string_view raw) {
      const auto value = json::parse(raw);
      require(value.is_object() && value.size() == 13 && value.at("pageOrdinaryLimit") == 128 &&
              value.at("clientRefusal") == "E_BUSY" && value.at("clientPendingAtFull") == 128 &&
              value.at("clientPendingAtRecovery") == 0 &&
              value.at("pageRefusal") == "Transport capacity exceeded." &&
              value.at("nativeRefusal") == "E_BUSY" && value.at("ordinaryCompleted") == 128 &&
              value.at("pageControlReplies") == 2 &&
              value.at("ordinaryAtRenewal").is_number_unsigned() && value.at("ordinaryAtRenewal").get<unsigned>() <= 128 &&
              value.at("ordinaryAtCleanup").is_number_unsigned() && value.at("ordinaryAtCleanup").get<unsigned>() <= 128 &&
              value.at("ordinaryRecovered") == true && value.at("cleanupRemoved") == 2 &&
              value.at("renewalError") == "E_RENEW_TOO_EARLY", "page proof failed");
      require(business.load(std::memory_order_acquire) == 129 && work_entries == 129 &&
              renewals == 1 && cleanups == 2 && delivery_permitted.load(std::memory_order_acquire),
              "native independent counters disagree");
      soui::sdk::diagnostics::bridge_runtime metrics{};
      require(real.read_runtime_metrics(metrics) == soui::sdk::diagnostics::status::ready &&
              metrics.task_service.subscriptions == 0 && metrics.task_service.active_handlers == 0 &&
              metrics.permission_store.renewal_too_early_total == 1 &&
              metrics.permission_store.rotated_leases_total == 0,
              "real subscriptions/handler ownership remain");
      { std::lock_guard<std::mutex> lock(evidence_mutex); page_report = value; }
      report_received.store(true, std::memory_order_release);
      notify();
      return std::string{"{\"recorded\":true}"};
    }), "report registration failed");
    require(real.register_task_handler("capacity.done", [](const auto&, const auto&) {
      return soui::runtime::task_outcome{soui::runtime::result_kind::success, "{}", "completed"};
    }), "real task handler registration failed");
    require(real.enable_session_renewal() && real.set_session_capabilities({"bridge.probe",
      "capacity.work", "capacity.report", "task.start", "task.query", "task.subscribe", "bridge.session.renew"}),
      "actual grants registration failed");
  }
  void notify() { require(owner && PostMessageW(owner, observe_message, 0, 0) != FALSE, "owner wake failed"); }
  void record(const std::string& id, const std::string& code) {
    soui::sdk::diagnostics::bridge_runtime metrics{};
    require(real.read_runtime_metrics(metrics) == soui::sdk::diagnostics::status::ready,
            "actual control telemetry unavailable");
    std::lock_guard<std::mutex> lock(evidence_mutex);
    trace.push_back({{"id", id}, {"code", code}, {"business", business.load()},
      {"deliveryPermitted", delivery_permitted.load()},
      {"taskOwnerEpoch", metrics.task_service.observation.owner_epoch},
      {"subscriptions", metrics.task_service.subscriptions},
      {"renewalTooEarlyTotal", metrics.permission_store.renewal_too_early_total},
      {"rotatedLeasesTotal", metrics.permission_store.rotated_leases_total},
      {"sinceFirstWorkUs", std::chrono::duration_cast<std::chrono::microseconds>(clock_type::now() - first_entered).count()}});
  }
  std::string handle_json(std::string_view raw, bool gesture) override {
    const auto packet = json::parse(raw);
    const auto method = packet.at("method").get<std::string>();
    const auto id = packet.at("requestId").get<std::string>();
    if (method == "capacity.work") {
      require(id != "native-excess" && id != "page-excess", "overflow reached the Bridge worker");
      ++work_entries;
      if (work_entries == 1) {
        require(packet.at("params").at("index") == 0 && business.load() == 0, "first actual work not controlled");
        first_entered = clock_type::now(); holding.store(true, std::memory_order_release); notify();
        // Only the fixture's ordinary callback occupies the real worker. This
        // bounded occupancy window is not a page acknowledgement or clock seam.
        std::unique_lock<std::mutex> lock(gate_mutex);
        (void)gate.wait_until(lock, first_entered + occupancy_window, [] { return false; });
        released = clock_type::now();
        holding.store(false, std::memory_order_release);
      }
    }
    const bool cleanup = method == "task.unsubscribe";
    if (id == "renew-primary" || cleanup) {
      require(!holding.load() && !delivery_permitted.load() && work_entries >= 1 && work_entries <= 128 &&
              work_entries == business.load(),
              "control did not execute before deferred ordinary ownership was released");
      if (cleanup && cleanups == 1) {
        // Original worker pushes the first cleanup response before taking this
        // real second cleanup. Both prior control outputs are now committed.
        require(renewals == 1 && cleanups == 1, "real control commit barrier out of order");
        delivery_permitted.store(true, std::memory_order_release); notify();
      }
    }
    auto answer = real.handle_json(raw, gesture);
    if (id == "renew-primary") {
      const auto result = json::parse(answer);
      require(method == "bridge.session.renew" && !result.at("ok").get<bool>() &&
              result.at("error").at("code") == "E_RENEW_TOO_EARLY", "real renewal outcome changed");
      ++renewals; record(id, "E_RENEW_TOO_EARLY");
    }
    if (cleanup) {
      const auto result = json::parse(answer);
      require(method == "task.unsubscribe" && result.at("ok") == true &&
              result.at("result").at("removed") == true, "real unsubscribe did not remove its owner");
      ++cleanups;
      soui::sdk::diagnostics::bridge_runtime metrics{};
      require(real.read_runtime_metrics(metrics) == soui::sdk::diagnostics::status::ready &&
              metrics.task_service.subscriptions == 2 - cleanups, "native subscription count disagrees");
      record(id, "removed");
    }
    return answer;
  }
  void shutdown_session(const std::string& id) override { real.shutdown_session(id); }
  void set_owner_window(void* window) noexcept override { real.set_owner_window(window); }
  void set_event_sink(event_sink sink) override { real.set_event_sink(std::move(sink)); }
};

struct application {
  observed_bridge bridge; // Bridge outlives both adapter and its actual worker.
  HWND window{};
  std::unique_ptr<soui::sdk::webview_host> host;
  soui::sdk::webview2_adapter* renderer{};
  std::unique_ptr<soui::sdk::webview2_runtime_exit_watch> watch;
  void pump() {
    if (!host) return;
    // Prevent the fixture's gate from closing while an already permitted owner
    // pump is still delivering. Try-lock also refuses reentrant owner callbacks.
    std::unique_lock<std::mutex> delivery(bridge.gate_mutex, std::try_to_lock);
    if (!delivery.owns_lock()) return;
    if (host->application_ready() && !bridge.holding.load(std::memory_order_acquire) &&
        !bridge.delivery_permitted.load(std::memory_order_acquire)) return;
    host->pump();
  }
};
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* app = reinterpret_cast<application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    app = static_cast<application*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
  }
  if (app && (message == soui::sdk::webview2_event_message || message == observe_message)) {
    app->pump(); return 0;
  }
  if (message == WM_CLOSE) return 0; // Teardown below owns the actual barrier.
  return DefWindowProcW(window, message, wparam, lparam);
}
void dispatch_messages() {
  MSG message{};
  for (unsigned batch = 0; batch < 256 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++batch) {
    require(message.message != WM_QUIT, "unexpected quit before ownership completion");
    TranslateMessage(&message); DispatchMessageW(&message);
  }
}
struct options { fs::path profile, resources, output, runtime; };
options parse(int argc, wchar_t** argv) {
  require(argc == 7 || argc == 9, "expected profile/resources/output and optional runtime");
  require(std::wstring(argv[1]) == L"--profile" && std::wstring(argv[3]) == L"--resources" &&
          std::wstring(argv[5]) == L"--output", "unknown arguments");
  options value{argv[2], argv[4], argv[6], {}};
  if (argc == 9) { require(std::wstring(argv[7]) == L"--runtime", "unknown runtime option"); value.runtime = argv[8]; }
  require(value.profile.is_absolute() && !fs::exists(value.profile) && value.resources.is_absolute() &&
          fs::is_regular_file(value.resources / "index.html") && value.output.is_absolute() &&
          !fs::exists(value.output) && fs::is_directory(value.output.parent_path()), "fresh absolute paths required");
  require(value.runtime.empty() || (value.runtime.is_absolute() && fs::is_directory(value.runtime)), "invalid fixed runtime");
  return value;
}
} // namespace

int run(int argc, wchar_t** argv) {
  const auto cfg = parse(argc, argv);
  const auto subject_ft = creation_time();
  const auto started = clock_type::now(), due = started + host_deadline;
  require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "actual UI STA failed");
  application app;
  retain_owner_on_exception retain; // Declared after app: fail-stop before owner unwind.
  const auto instance = GetModuleHandleW(nullptr);
  WNDCLASSW type{}; type.lpfnWndProc = procedure; type.hInstance = instance;
  type.lpszClassName = L"SouiWebView2CapacityHost";
  require(RegisterClassW(&type) != 0, "window class registration failed");
  app.window = CreateWindowExW(0, type.lpszClassName, L"Real WebView2 capacity", WS_OVERLAPPEDWINDOW,
      0, 0, 480, 320, nullptr, nullptr, instance, &app);
  require(app.window != nullptr, "actual HWND missing"); app.bridge.owner = app.window;
  auto adapter = std::make_unique<soui::sdk::webview2_adapter>(app.window, cfg.profile, std::string{}, cfg.runtime);
  app.renderer = adapter.get();
  require(app.renderer->configure_profile_access(soui::sdk::webview2_profile_access::exclusive).ok,
          "actual exclusive profile not configured");
  app.host = std::make_unique<soui::sdk::webview_host>(std::move(adapter));
  bool prepared = false;
  app.renderer->prepare_async([&](auto result) {
    require(result.ok && app.renderer->acquire_runtime_exit_watch(app.watch).ok, "actual prepare/runtime watch failed");
    soui::sdk::host_config config;
#ifdef _WIN64
    config.architecture = "x64";
#else
    config.architecture = "x86";
#endif
    config.owner_window = app.window; config.resource_root = cfg.resources; config.bridge_adapter = &app.bridge;
    require(app.host->start(config).ok, "actual WebView2 mount failed"); prepared = true;
  });
  while (!app.bridge.report_received.load(std::memory_order_acquire)) {
    require(clock_type::now() < due, "actual host/behavior deadline expired");
    dispatch_messages(); app.pump();
    const auto status = app.renderer->status();
    require(status.ok || status.code == "E_PENDING", "actual adapter failed");
    MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
  }
  require(prepared && app.watch && app.bridge.business.load() == 129, "real behavior incomplete");
  const auto runtime = app.renderer->runtime_version(), session_digest = app.renderer->session_digest();
  auto browser = app.watch->observe();
  require(browser.browser_process_id != 0 && browser.creation_file_time != 0 && browser.registered,
          "real runtime process identity absent");
  bool closed = false;
  while (!closed) {
    require(clock_type::now() < due, "native close deadline expired");
    const auto result = app.host->destroy();
    require(result.ok || result.code == "E_BUSY", "actual host close failed"); closed = result.ok;
    if (!closed) { dispatch_messages(); MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE); }
  }
  require(app.host->destroy().ok, "close not idempotent");
  for (;;) {
    require(clock_type::now() < due, "actual runtime exit watch deadline expired");
    const auto result = app.watch->finish();
    require(result.ok || result.code == "E_BUSY", "runtime exit barrier failed");
    if (result.ok) break;
    dispatch_messages(); MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
  }
  browser = app.watch->observe();
  require(browser.phase == soui::sdk::webview2_runtime_exit_phase::finished && browser.event_observed &&
          browser.process_signaled && browser.active_callbacks == 0 &&
          browser.exit_kind == soui::sdk::webview2_browser_exit_kind::normal, "actual runtime did not close normally");
  app.watch.reset(); app.host.reset(); app.renderer = nullptr;
  require(DestroyWindow(app.window) != FALSE && UnregisterClassW(type.lpszClassName, instance) != FALSE,
          "HWND/class cleanup failed");
  CoUninitialize();
  json trace, page;
  { std::lock_guard<std::mutex> lock(app.bridge.evidence_mutex); trace = app.bridge.trace; page = app.bridge.page_report; }
  create_new_json(cfg.output, {{"schema", "soui.real-webview2-capacity/1"},
    {"subjectPid", GetCurrentProcessId()}, {"subjectCreationFileTime", subject_ft},
    {"runtimeVersion", runtime}, {"sessionDigest", session_digest},
    {"browserPid", browser.browser_process_id}, {"browserCreationFileTime", std::to_string(browser.creation_file_time)},
    {"browserImage", browser.image_path.generic_string()}, {"browserNormalExit", true},
    {"ordinaryBusinessCalls", app.bridge.business.load()}, {"controlTrace", trace}, {"page", page},
    {"requestedOccupancyWindowMs", 1000},
    {"actualOccupancyUs", std::chrono::duration_cast<std::chrono::microseconds>(app.bridge.released - app.bridge.first_entered).count()},
    {"ordinaryLeaseCountDirectSnapshot", nullptr}, {"nativeQueueLengthDirectSnapshot", nullptr},
    {"nativeFullEvidence", "fresh-ID E_BUSY with 128 real preload requests pending and first real callback held"},
    {"renewalRotationObserved", false}, {"longSessionObserved", false},
    {"workerNativeJoinObserved", false}, {"resourceQualification", false}, {"releaseQualification", false}});
  std::cout << "Real WebView2 capacity: preload/native128 refusal; early-renewal reserve; 2 cleanup; 128+1 ordinary; natural runtime close\n";
  return 0;
}
int wmain(int argc, wchar_t** argv) {
  try { return run(argc, argv); }
  catch (const std::exception& exception) { fail(exception.what()); }
  catch (...) { fail("unexpected fixture exception"); }
}
