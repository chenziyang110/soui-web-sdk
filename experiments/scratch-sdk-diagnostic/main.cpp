#include <soui/host/webview2_host.h>
#include <soui/json_bridge.h>
#include <soui/version.h>
#include "../../sdk/renderers/webview2/stage0_devtools.h"
#include "../scratch-webview2-baseline/diagnostic-capture-policy.h"
#include "../scratch-webview2-baseline/manual-profile-start.h"
#include "diagnostic_recipe.h"
#include <windows.h>
#include <shellapi.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

namespace {
using json = nlohmann::json;
using access = soui::sdk::detail::stage0_webview2_access;
using action = soui::sdk::detail::profile_page_action;
using subscription = soui::sdk::detail::stage0_profile_subscription;
using clock_type = std::chrono::steady_clock;
constexpr UINT wake_message = WM_APP + 61;

void save(const std::filesystem::path& path, const json& value) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream || !(stream << value.dump(2) << '\n')) throw std::runtime_error("Cannot save diagnostic evidence");
}
bool contains_path(const std::filesystem::path& parent, const std::filesystem::path& child) {
  auto left=parent.begin(), right=child.begin();
  for (;left!=parent.end();++left,++right) {
    if (right==child.end() || CompareStringOrdinal(left->c_str(),-1,right->c_str(),-1,TRUE)!=CSTR_EQUAL) return false;
  }
  return true;
}
struct application final {
  HWND window{};
  soui::scratch_diagnostic::manual_profile_start manual;
  std::filesystem::path output, resources, profile;
  std::string prepared_runtime_version;
  soui::sdk::json_bridge bridge{"app://scratch-studio"};
  soui::sdk::webview2_adapter* renderer{};
  std::unique_ptr<soui::sdk::webview_host> host;
  std::unique_ptr<soui::sdk::webview2_runtime_exit_watch> watch;
  std::vector<std::shared_ptr<subscription>> subscriptions;
  soui::sdk::detail::stage0_webview2_grant grant;
  soui::scratch_diagnostic::bounded_trace_buffer trace;
  json record{{"schema","soui.scratch-sdk-diagnostic/v1"},{"diagnosticOnly",true},
    {"formalPerformancePassed",nullptr},{"operations",json::array()},{"failures",json::array()},
    {"nativeWindowObservations",json::array()}};
  clock_type::time_point began{clock_type::now()}, due{}, closing_at{};
  bool preparation_started{}, preparation_completed{}, closing{}, command_pending{}, response_ready{}, granted{};
  bool trace_supported{true}, trace_started{}, trace_complete{}, completed{}, saved{};
  bool grant_preflight_checked{};
  unsigned phase{};
  int exit_code{};
  json response;
  json cpu_profile;
  std::string operation;

  void fail(const std::string& message) {
    record["failures"].push_back(message); exit_code = 2; close();
  }
  auto completion() {
    return [this](soui::sdk::host_result status, std::string raw) {
      command_pending = false;
      if (closing) return;
      record["operations"].push_back({{"operation",operation},{"ok",status.ok},{"code",status.code},{"message",status.message}});
      if (!status.ok) {
        if (operation == "Tracing.getCategories" || operation == "Tracing.start" || operation == "Tracing.end") {
          trace_supported = false; response = json::object();
        } else { fail(status.code + ": " + status.message); return; }
      } else {
        try { response = json::parse(raw); }
        catch (const std::exception& error) { fail(std::string("Invalid diagnostic response: ") + error.what()); return; }
      }
      response_ready = true;
      if (!PostMessageW(window, wake_message, 0, 0)) fail("Diagnostic wake failed");
    };
  }
  void page(action value, const char* name) {
    operation = name; command_pending = true; response_ready = false;
    const auto submitted = access::profile_page(*renderer, grant, value, completion());
    if (!submitted.ok) { command_pending = false; fail(submitted.code + ": " + submitted.message); }
  }
  void command(const wchar_t* name) {
    operation = std::string(name, name + wcslen(name)); command_pending = true; response_ready = false;
    const auto submitted = access::profile_command(*renderer, grant, name, completion());
    if (!submitted.ok) { command_pending = false; fail(submitted.code + ": " + submitted.message); }
  }
  void observe_window() {
    if (record["nativeWindowObservations"].size() >= 2048) return;
    RECT bounds{}; GetClientRect(window, &bounds);
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    record["nativeWindowObservations"].push_back({{"qpc",now.QuadPart},
      {"foreground",GetForegroundWindow() == window},{"visible",IsWindowVisible(window) != FALSE},
      {"minimized",IsIconic(window) != FALSE},{"width",bounds.right},{"height",bounds.bottom}});
  }
  void subscribe_trace() {
    subscriptions.reserve(3); // No allocation may abandon a registered event.
    for (const auto* event : {L"Tracing.dataCollected",L"Tracing.bufferUsage",L"Tracing.tracingComplete"}) {
      std::shared_ptr<subscription> value;
      const auto result = access::profile_subscribe(*renderer, grant, event,
          [this](std::wstring method, std::string raw) {
            if (method == L"Tracing.tracingComplete") trace_complete = true;
            if (!trace.append(std::string(method.begin(), method.end()),std::move(raw)))
              record["traceBufferIncomplete"] = true;
          }, value);
      if (!result.ok) { trace_supported = false; record["traceSubscriptionError"] = result.code + ": " + result.message; break; }
      subscriptions.push_back(std::move(value));
    }
  }
  void advance() {
    if (closing) { close(); return; }
    observe_window();
    if (clock_type::now() - began > std::chrono::seconds(120)) { fail("Diagnostic 120s total deadline exceeded"); return; }
    const bool ready = preparation_completed && host && host->application_ready();
    manual.reflect_readiness(window, ready);
    if (!ready || manual.waiting()) return;
    if (!granted) {
      if (!grant_preflight_checked) {
        const auto before = access::observe(*renderer);
        if (before.pending_callbacks || before.active_callbacks) return;
        const auto called = std::make_shared<bool>(false);
        const auto rejected = access::profile_command(*renderer,grant,L"Profiler.enable",[called](auto,std::string){ *called=true; });
        const auto after = access::observe(*renderer);
        if (rejected.ok || rejected.code != "E_UNAUTHORIZED" || *called || before.pending_callbacks != after.pending_callbacks) {
          fail("Diagnostic unauthorized-command preflight failed"); return;
        }
        record["unauthorizedCommandRejected"] = true; grant_preflight_checked = true;
      }
      const auto result = access::grant(*renderer, grant);
      if (!result.ok) { if (result.code != "E_BUSY" && result.code != "E_NOT_READY") fail(result.code + ": " + result.message); return; }
      granted = true;
      const auto denied = access::profile_command(*renderer,grant,L"Runtime.evaluate",[](auto,std::string){});
      if (denied.ok || denied.code != "E_DIAGNOSTIC_COMMAND") { fail("Diagnostic command allowlist preflight failed"); return; }
      record["arbitraryCommandRejected"] = true;
      record["runtimeVersion"] = renderer->runtime_version();
      page(action::setup,"page.setup"); return;
    }
    if (command_pending) return;
    switch (phase) {
      case 0: if (response_ready) { phase = 1; page(action::query,"page.query"); } break;
      case 1:
        if (response_ready) {
          if (!response.is_object()) { fail("Page query did not return an object"); return; }
          if (!response.value("eligible",false)) { response_ready=false; due=clock_type::now()+std::chrono::milliseconds(500); return; }
          if (response.value("width",0) != 1424 || response.value("height",0) != 861 ||
              response.value("devicePixelRatio",0.0) != 1.0 || response.value("visibility",std::string()) != "visible") {
            fail("Diagnostic viewport, DPR or visibility differs from the fixed protocol"); return;
          }
          if (!manual.page_admitted(soui::scratch_diagnostic::profile_owner_guard(window),
              response.value("hasFocus",false))) {
            response_ready=false; due=clock_type::now()+std::chrono::milliseconds(500); return;
          }
          record["configuration"] = response;
          phase = 2; page(action::install,"page.install");
        } else if (clock_type::now() >= due) page(action::query,"page.query");
        break;
      case 2:
        if (response_ready) { record["warmup"] = response; phase = 3; command(L"Tracing.getCategories"); }
        break;
      case 3:
        if (response_ready) { record["traceCategories"] = response; subscribe_trace(); phase = 4; command(L"Profiler.enable"); } break;
      case 4: if (response_ready) { phase = 5; command(L"Profiler.setSamplingInterval"); } break;
      case 5:
        if (response_ready) { response_ready=false; due=clock_type::now()+std::chrono::seconds(10); phase=6; } break;
      case 6:
        if (clock_type::now() >= due) {
          if (manual.enabled() && !soui::scratch_diagnostic::profile_owner_guard(window)) {
            fail("Manual diagnostic lost foreground during warmup"); return;
          }
          if (trace_supported) { phase = 7; command(L"Tracing.start"); }
          else { phase = 8; command(L"Profiler.start"); }
        } break;
      case 7: if (response_ready) { trace_started = trace_supported; phase = 8; command(L"Profiler.start"); } break;
      case 8: if (response_ready) { phase = 9; page(action::start,"page.start"); } break;
      case 9:
        if (response_ready) { record["captureStart"] = response; response_ready = false; due = clock_type::now() + std::chrono::seconds(10); phase = 10; } break;
      case 10:
        if (manual.enabled() && !soui::scratch_diagnostic::profile_owner_guard(window)) {
          fail("Manual diagnostic lost foreground during capture"); return;
        }
        if (clock_type::now() >= due) { phase = 11; page(action::finish,"page.finish"); } break;
      case 11:
        if (response_ready) {
          record["page"] = response;
          if (manual.enabled() && !response.value("pageGuardsQualified",false)) {
            exit_code=2;record["failures"].push_back("Manual diagnostic page focus/visibility guard failed");
          }
          phase = 12; command(L"Profiler.stop");
        } break;
      case 12:
        if (response_ready) {
          if (!response.is_object() || !response.contains("profile") || !response["profile"].is_object()) {
            fail("Profiler.stop did not return a CPU profile"); return;
          }
          cpu_profile = std::move(response);
          if (trace_started) { phase = 14; command(L"Tracing.end"); }
          else { phase = 17; command(L"Profiler.disable"); }
        } break;
      case 14:
        if (response_ready) { response_ready = false; due = clock_type::now() + std::chrono::seconds(15); phase = 16; } break;
      case 16:
        if (trace_complete || clock_type::now() >= due) { phase = 17; command(L"Profiler.disable"); } break;
      case 17: if (response_ready) { phase = 18; page(action::abort,"page.abort"); } break;
      case 18: if (response_ready) { phase = 15; page(action::stop_workload,"page.stop-workload"); } break;
      case 15:
        if (response_ready) { completed = true; close(); } break;
      default: fail("Unexpected diagnostic phase"); break;
    }
  }
  void close() {
    if (!closing) { closing = true; closing_at = clock_type::now(); }
    if (clock_type::now() - closing_at >= std::chrono::seconds(10)) std::terminate();
    if (preparation_started && !preparation_completed) return;
    for (auto& value : subscriptions) { const auto result = access::profile_unsubscribe(value); if (!result.ok) return; }
    subscriptions.clear();
    if (host) { const auto result = host->destroy(); if (!result.ok) { if (result.code != "E_BUSY") exit_code = 3; return; } }
    if (renderer) { const auto result = renderer->unmount(); if (!result.ok) { if (result.code != "E_BUSY") exit_code = 3; return; } }
    bool no_browser_failure_cleanup=false;
    if (preparation_started && !watch) {
      const auto acquired=renderer->acquire_runtime_exit_watch(watch);
      if (!acquired.ok) {
        exit_code=3;
        if (acquired.code != "E_RUNTIME_WATCH_UNAVAILABLE" || !renderer->failed_preparation_cleanup_complete()) std::terminate();
        no_browser_failure_cleanup=true;
      }
    }
    if (watch) {
      const auto state = watch->observe();
      if (state.phase == soui::sdk::webview2_runtime_exit_phase::failed) std::terminate();
      if (state.phase != soui::sdk::webview2_runtime_exit_phase::finished) {
        const auto result = watch->finish(); if (!result.ok) { if (result.code != "E_BUSY") std::terminate(); return; }
      }
    }
    if (!saved) {
      if (!cpu_profile.is_null()) {
        save(output / "cpu-profile.json",cpu_profile);
        save(output / "cpu-profile.cpuprofile",cpu_profile.at("profile"));
      }
      json chunks = json::array(); for (const auto& chunk : trace.chunks()) chunks.push_back({{"method",chunk.method},{"raw",chunk.raw}});
      save(output / "browser-trace-chunks.json",chunks);
      record["completed"] = completed; record["traceSupported"] = trace_supported;
      record["traceComplete"] = trace_complete; record["traceBytes"] = trace.bytes();
      record["traceOverflow"] = trace.overflow(); record["traceAllocationFailed"] = trace.allocation_failed();
      record["controllerAndRuntimeRetired"] = watch != nullptr;
      record["failedBeforeBrowserCleanupComplete"] = no_browser_failure_cleanup;
      record["exitCode"] = exit_code;
      save(output / "session.json",record); saved = true;
    }
    KillTimer(window,1); DestroyWindow(window);
  }
  void start() {
    record["manualStartRequired"]=manual.enabled();
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
    record["qpcFrequency"]=frequency.QuadPart; record["pid"]=GetCurrentProcessId();
    record["resources"]=resources.u8string();
    record["protocol"]=json::parse(soui::scratch_diagnostic::recipe_input::protocol);
    if (!bridge.register_method("app.info","app.info",[this](std::string_view) {
      return json{{"sdkVersion",soui::sdk::version},{"backend","WebView2"},{"application","SouiScratchDiagnostic"},
          {"buildId",__DATE__ " " __TIME__},{"runtimeVersion",prepared_runtime_version}}.dump();
    })) throw std::runtime_error("Cannot register diagnostic app.info");
    if (!bridge.enable_session_renewal() || !bridge.set_session_capabilities({"bridge.describe","bridge.probe","app.info","bridge.session.renew"}))
      throw std::runtime_error("Cannot initialize diagnostic Bridge");
    auto adapter = std::make_unique<soui::sdk::webview2_adapter>(window,profile);
    renderer = adapter.get(); host = std::make_unique<soui::sdk::webview_host>(std::move(adapter));
    auto result = renderer->configure_profile_access(soui::sdk::webview2_profile_access::exclusive);
    if (result.ok) result = access::configure(*renderer,true,true);
    if (!result.ok) throw std::runtime_error(result.code + ": " + result.message);
    preparation_started = true;
    renderer->prepare_async([this](auto result) {
      preparation_completed = true;
      if (result.ok) result = renderer->acquire_runtime_exit_watch(watch);
      if (result.ok && !closing) {
        prepared_runtime_version = renderer->runtime_version();
        soui::sdk::host_config config;
        config.architecture = sizeof(void*) == 8 ? "x64" : "x86";
        config.owner_window = window; config.resource_root = resources; config.bridge_adapter = &bridge;
        config.resource_policy = soui::sdk::local_resource_policy::isolated_local_editor;
        result = host->start(config);
      }
      if (!result.ok) fail(result.code + ": " + result.message);
      else if (closing) close();
    });
  }
  static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* app = reinterpret_cast<application*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (message == WM_NCCREATE) { app = static_cast<application*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams); app->window=window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app)); }
    if (!app) return DefWindowProcW(window,message,wparam,lparam);
    try {
      switch (message) {
        case WM_COMMAND:
          if (LOWORD(wparam)==soui::scratch_diagnostic::manual_profile_start::command_id && HIWORD(wparam)==0 && !app->closing) {
            const bool ready=app->preparation_completed && app->host && app->host->application_ready();
            if (!app->manual.waiting() || !ready || !soui::scratch_diagnostic::profile_owner_guard(window)) return 0;
            const auto focused=app->host->focus();
            if (!focused.ok) { app->fail("Manual diagnostic focus request failed: "+focused.code); return 0; }
            if (app->closing || !IsWindow(window)) return 0;
            const bool owner_foreground=soui::scratch_diagnostic::profile_owner_guard(window);
            if (app->manual.request(ready,owner_foreground)) {
              LARGE_INTEGER at{};QueryPerformanceCounter(&at);
              app->record["manualStartRequest"]={{"qpc",std::to_string(at.QuadPart)},{"nativeForeground",owner_foreground},{"hardwareInputProven",false}};
              app->manual.reflect_readiness(window,true);
              SetWindowTextW(window,L"SOUI SDK S — 已开始，请保持前台约30秒");
            }
            return 0;
          }
          break;
        case soui::sdk::webview2_event_message: if (app->host && !app->closing) app->host->pump(); if (app->closing) app->close(); return 0;
        case WM_TIMER: case wake_message: app->advance(); return 0;
        case WM_SIZE:
          if (app->host && !app->closing) {
            if (wparam == SIZE_MINIMIZED) (void)app->host->hide();
            else { (void)app->host->show(); (void)app->host->resize(LOWORD(lparam),HIWORD(lparam)); }
          } return 0;
        case WM_SETFOCUS: if (app->host && !app->closing) (void)app->host->focus(); return 0;
        case WM_CLOSE:
          if (!app->closing && !app->completed) { app->record["cancelledByClose"] = true; if (!app->exit_code) app->exit_code=4; }
          app->close(); return 0;
        case WM_DESTROY: PostQuitMessage(app->exit_code); return 0;
        default: break;
      }
    } catch (const std::exception& error) { app->fail(error.what()); return 0; }
    return DefWindowProcW(window,message,wparam,lparam);
  }
};
} // namespace

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
  int argc{}; auto** argv=CommandLineToArgvW(GetCommandLineW(),&argc);
  if (!argv) return 2;
  if ((argc != 5 && argc != 7) || std::wstring(argv[1]) != L"--resources" || std::wstring(argv[3]) != L"--output" ||
      (argc==7 && (std::wstring(argv[5])!=L"--start" || std::wstring(argv[6])!=L"manual"))) { LocalFree(argv); return 2; }
  application app; app.manual=soui::scratch_diagnostic::manual_profile_start(argc==7);
  app.resources=argv[2]; app.output=argv[4]; LocalFree(argv);
  try {
    if (!app.resources.is_absolute() || !app.output.is_absolute() || !std::filesystem::is_directory(app.resources) ||
        !std::filesystem::is_directory(app.output.parent_path()) || app.output.filename().empty() || std::filesystem::exists(app.output)) return 2;
    app.resources=std::filesystem::canonical(app.resources);
    app.output=std::filesystem::weakly_canonical(app.output);
    if (contains_path(app.resources,app.output) || contains_path(app.output,app.resources)) return 2;
    if (!std::filesystem::create_directory(app.output)) return 2;
    app.profile=app.output / "profile";
  } catch (...) { return 2; }
  const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED); if (FAILED(initialized)) return 2;
  int result=2;
  try {
    WNDCLASSW type{}; type.hInstance=instance; type.lpfnWndProc=&application::procedure;
    type.lpszClassName=L"SouiScratchDiagnostic"; type.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if (!RegisterClassW(&type)) throw std::runtime_error("Diagnostic window class registration failed");
    RECT bounds{0,0,1424,861}; if (!AdjustWindowRectEx(&bounds,WS_OVERLAPPEDWINDOW,app.manual.enabled(),0)) throw std::runtime_error("Diagnostic client size failed");
    if (!CreateWindowExW(0,type.lpszClassName,L"SOUI SDK S — diagnostic profiling",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,&app))
      throw std::runtime_error("Diagnostic window creation failed");
    app.manual.attach(app.window);
    if (!SetTimer(app.window,1,100,nullptr)) throw std::runtime_error("Diagnostic timer creation failed");
    app.start(); ShowWindow(app.window,show);
    MSG message{};
    while (IsWindow(app.window)) {
      const auto received=GetMessageW(&message,nullptr,0,0);
      if (received > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
      else { app.exit_code=3; app.close(); if (IsWindow(app.window)) MsgWaitForMultipleObjects(0,nullptr,FALSE,20,QS_ALLINPUT); }
    }
    result=app.exit_code;
  } catch (const std::exception& error) {
    save(app.output / "startup-error.json",{{"error",error.what()},{"diagnosticOnly",true}});
    if (app.preparation_started) std::terminate();
    if (app.window && IsWindow(app.window)) DestroyWindow(app.window);
  }
  app.watch.reset(); app.host.reset(); CoUninitialize(); return result;
}
