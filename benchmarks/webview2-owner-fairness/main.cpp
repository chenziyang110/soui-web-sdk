// Task-only diagnostic consumer. Public SDK lifecycle, no private SDK state.
#include "paint_probe.h"
#include <soui/host/webview2_host.h>
#include <soui/json_bridge.h>
#include <soui/version.h>
#include <json.hpp>
#include <atomic>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace {
namespace fs = std::filesystem;
namespace sdk = soui::sdk;
using json = nlohmann::json;
constexpr UINT close_message = WM_APP + 73;
constexpr wchar_t owner_class[] = L"SouiWebView2OwnerFairness";
fs::path failure_output;
const std::uint64_t process_started = GetTickCount64();
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
std::string wide(std::uint64_t value) { return std::to_string(value); }
void publish(const fs::path& output, const json& value, std::uint64_t deadline = 0) {
  const auto bytes = value.dump(2) + "\n";
  require(bytes.size() <= 65536, "bounded result exceeded");
  auto temporary = output; temporary += L".pending";
  const auto file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  require(file != INVALID_HANDLE_VALUE, "result CREATE_NEW");
  DWORD written{};
  const bool good = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
      written == bytes.size() && FlushFileBuffers(file);
  const bool closed = CloseHandle(file) != FALSE;
  require(good && closed, "result write/flush/close");
  require(!deadline || GetTickCount64()<deadline, "result completed after original deadline");
  require(MoveFileExW(temporary.c_str(), output.c_str(), 0) != FALSE, "result no-overwrite rename");
  require(!deadline || GetTickCount64()<deadline, "result rename finished after original deadline");
}
[[noreturn]] void fail(const char* why) noexcept {
  try { if (!failure_output.empty()) publish(failure_output, {{"schema","soui.webview2-owner-fairness/v1"},
      {"diagnosticExecutionComplete",false},{"error",why},{"normalRuntimeClose",false},
      {"performanceVerified",false},{"hardwareInputMeasured",false},{"physicalPresentationMeasured",false}}); } catch (...) {}
  std::cerr << "Owner fairness diagnostic failed: " << why << '\n' << std::flush;
  ExitProcess(91); // Preserve owner lifetime on failure, as in the real-COM fixture.
}
struct retain_owner_on_exception {
  const int entered = std::uncaught_exceptions();
  ~retain_owner_on_exception() noexcept { if (std::uncaught_exceptions() > entered) fail("owner operation failed before validated close"); }
};
std::string private_desktop() {
  const auto desktop = GetThreadDesktop(GetCurrentThreadId());
  wchar_t name[256]{}; DWORD bytes{}; BOOL input = TRUE;
  require(desktop && GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &bytes), "desktop name unavailable");
  const std::wstring text(name);
  require(text.rfind(L"AgentGui_",0) == 0, "refuse non-task desktop");
  bytes = 0;
  require(GetUserObjectInformationW(desktop, 6 /* UOI_IO */, &input, sizeof(input), &bytes) &&
      bytes == sizeof(input) && !input, "desktop isolation unavailable/input desktop");
  require(text.find_first_not_of(L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") == std::wstring::npos,
      "unexpected desktop name");
  return std::string(text.begin(), text.end());
}
std::string creation_time() {
  FILETIME created{}, exited{}, kernel{}, user{};
  require(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) != FALSE, "process FILETIME missing");
  ULARGE_INTEGER value{}; value.LowPart = created.dwLowDateTime; value.HighPart = created.dwHighDateTime;
  return wide(value.QuadPart);
}
json environment() {
  json result = json::object();
  for (const auto* name : {L"SOUI_WEBVIEW2_PREPARE_OUTPUT",L"SOUI_WEBVIEW2_OWNER_BUDGET_US",L"SOUI_WEBVIEW2_OUTPUT_TIMING",
                           L"SOUI_DIAGNOSTIC_SCRIPT",L"SOUI_DIAGNOSTIC_BRIDGE_PHASES"}) {
    wchar_t buffer[16]{}; SetLastError(ERROR_SUCCESS);
    const auto length = GetEnvironmentVariableW(name, buffer, 16);
    const std::wstring key(name); const std::string narrow(key.begin(), key.end());
    if (!length && GetLastError() == ERROR_ENVVAR_NOT_FOUND) result[narrow] = nullptr;
    else {
      require(length == 1 && buffer[0] == L'0' && key.rfind(L"SOUI_WEBVIEW2_",0) == 0, "unexpected diagnostic environment");
      result[narrow] = "0";
    }
  }
  return result;
}
struct options { fs::path profile, resources, output, runtime; };
options parse(int argc, wchar_t** argv) {
  require(argc == 9 && std::wstring(argv[1]) == L"--profile" && std::wstring(argv[3]) == L"--resources" &&
      std::wstring(argv[5]) == L"--output" && std::wstring(argv[7]) == L"--runtime", "expected profile/resources/output/runtime");
  options result{argv[2],argv[4],argv[6],argv[8]};
  require(result.profile.is_absolute() && !fs::exists(result.profile) && result.resources.is_absolute() &&
      fs::is_regular_file(result.resources / "index.html") && fs::is_regular_file(result.resources / "fairness.js") &&
      result.output.is_absolute() &&
      !fs::exists(result.output) && !fs::exists(result.output.wstring()+L".pending") &&
      fs::is_directory(result.output.parent_path()) && result.runtime.is_absolute() && fs::is_directory(result.runtime), "fresh frozen paths required");
  return result;
}
std::uint64_t bounded_count(const json& value, const char* key) {
  require(value.contains(key) && value.at(key).is_number_unsigned(), "missing unsigned page count");
  const auto count = value.at(key).get<std::uint64_t>(); require(count <= 1000000, "page count bound"); return count;
}
struct application {
  sdk::json_bridge bridge{"app://webview2-owner-fairness",sdk::diagnostics::mode::disabled};
  owner_fairness::paint_probe probe;
  HWND window{};
  std::unique_ptr<sdk::webview_host> host;
  sdk::webview2_adapter* renderer{};
  std::unique_ptr<sdk::webview2_runtime_exit_watch> watch;
  HANDLE stop_event{}, thread{};
  std::atomic<bool> begun{false}, closing{false}, close_posted{false}, deadline_failed{false}, monitor_failed{false};
  std::atomic<std::uint64_t> business{0};
  bool prepared{}, closed{}, close_running{}, controller_closed{}, report_received{}, done_received{};
  std::mutex report_mutex;
  json page_report;
  std::string runtime_version;
  const std::uint64_t started = process_started;
  const std::string payload = std::string(4096,'x');
  application() {
    require(bridge.register_method("fairness.mark","fairness.mark",[this](std::string_view raw) {
      const auto params = json::parse(raw);
      require(params.is_object() && params.size()==1, "mark shape");
      const auto next = bounded_count(params,"stage"); require(next >= 1 && next <= 5, "mark stage");
      require(!closing.load() && GetTickCount64()-started<(next==1?15000U:45000U), "late phase mark");
      const auto tick = probe.advance(static_cast<unsigned>(next));
      if (next == 1) begun.store(true);
      return json{{"stage",next},{"nativeQpc",wide(tick)}}.dump();
    }), "mark registration");
    require(bridge.register_method("fairness.echo","fairness.echo",[this](std::string_view raw) {
      const auto params = json::parse(raw);
      require(params.is_object() && params.size()==2 && params.at("payload")==payload, "echo payload differs");
      const auto sequence = bounded_count(params,"sequence");
      const auto stage = probe.stage();
      require((stage==2 || stage==3) && sequence==business.load() && sequence<1000000, "echo sequence/phase differs");
      business.fetch_add(1); return params.dump();
    }), "echo registration");
    require(bridge.register_method("fairness.report","fairness.report",[this](std::string_view raw) {
      require(raw.size() <= 8192, "page report bound");
      const auto value = json::parse(raw);
      require(value.is_object() && value.value("schema","")=="soui.owner-fairness-page/v1" &&
          value.value("payloadBytes",0)==4096 && value.value("inflightLimit",0)==8, "page report identity");
      require(value.contains("valid") && value.at("valid").is_boolean() && value.contains("nativeTerminalUnknown") &&
          value.at("nativeTerminalUnknown").is_boolean(), "page terminal qualification missing");
      for (const auto* key : {"attempted","success","failed","timeouts","mismatches","inflight","peakInflight"}) (void)bounded_count(value,key);
      require(bounded_count(value,"peakInflight")<=8 && bounded_count(value,"inflight")<=8, "page inflight bound");
      std::lock_guard<std::mutex> lock(report_mutex);
      require(!report_received, "duplicate page report"); page_report=value; report_received=true;
      return std::string{"{\"recorded\":true}"};
    }), "report registration");
    require(bridge.register_method("fairness.done","fairness.done",[this](std::string_view raw) {
      require(json::parse(raw)==json::object(), "done shape");
      { std::lock_guard<std::mutex> lock(report_mutex); require(report_received && !done_received, "done before report/duplicate"); done_received=true; }
      closing.store(true); post_close(); return std::string{"{\"closing\":true}"};
    }), "done registration");
    require(bridge.set_session_capabilities({"bridge.probe","fairness.mark","fairness.echo","fairness.report","fairness.done"}), "grant registration");
  }
  void post_close() noexcept {
    if (!close_posted.exchange(true) && !PostMessageW(window,close_message,0,0)) {
      close_posted.store(false); monitor_failed.store(true);
    }
  }
  static DWORD WINAPI monitor(void* context) noexcept {
    auto& app=*static_cast<application*>(context);
    for (;;) {
      const auto wait=WaitForSingleObject(app.stop_event,20);
      if(wait==WAIT_OBJECT_0)return 0;
      if(wait!=WAIT_TIMEOUT){app.monitor_failed.store(true);app.closing.store(true);app.post_close();return 1;}
      const auto now=GetTickCount64();
      if ((!app.begun.load() && now-app.started>=15000) || now-app.started>=45000) {
        if (!app.closing.load()) app.deadline_failed.store(true);
        app.closing.store(true);
      }
      if (app.closing.load()) app.post_close();
      else app.probe.invalidate();
    }
  }
  void close_step() {
    if (closed || close_running || !closing.load()) return;
    close_running=true; struct reset {bool& value;~reset(){value=false;}} guard{close_running};
    require(GetTickCount64()-started<55000, "absolute lifecycle deadline");
    if (!prepared) return; // Own pending prepare callback; never unwind early.
    if (!controller_closed) {
      const auto result=host->destroy(); require(result.ok || result.code=="E_BUSY", "Host destroy failed");
      if (!result.ok) return;
      require(host->destroy().ok, "Host destroy not idempotent"); controller_closed=true;
    }
    const auto finished=watch->finish(); require(finished.ok || finished.code=="E_BUSY", "runtime watch failed");
    if (!finished.ok) return;
    closed=true;
  }
  static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    auto* app=reinterpret_cast<application*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (message==WM_NCCREATE) {
      app=static_cast<application*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
      SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window,message,wparam,lparam);
    try {
      if (message==sdk::webview2_event_message) {
        app->probe.owner_event(); if(app->host)app->host->pump(); app->close_step(); return 0;
      }
      if (message==close_message) { app->close_posted.store(false); app->close_step(); return 0; }
      if (message==WM_CLOSE) { app->deadline_failed.store(true);app->closing.store(true);app->post_close();return 0; }
    } catch (const std::exception& error) { fail(error.what()); }
    return DefWindowProcW(window,message,wparam,lparam);
  }
};
json phase_json(const owner_fairness::phase_sample& value) {
  return {{"beginQpc",wide(value.begin)},{"endQpc",wide(value.end)},{"closed",value.begin>0 && value.end>=value.begin},
    {"paintDispatchCount",wide(value.paints)},{"timerDispatchCount",wide(value.timers)},{"invalidationCount",wide(value.invalidations)},
    {"ownerEventCount",wide(value.owner_events)},{"maxPaintGapQpc",wide(value.max_paint_gap)},
    {"maxTimerGapQpc",wide(value.max_timer_gap)}};
}
int run(int argc,wchar_t** argv) {
  const auto cfg=parse(argc,argv);failure_output=cfg.output;
  const auto desktop=private_desktop(), subject=creation_time();const auto env=environment();
  require(std::string(SOUI_SDK_VERSION_STRING)==SOUI_FAIRNESS_RAW_VERSION, "compiled SDK raw version differs");
  require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)), "UI STA initialization");
  application app;retain_owner_on_exception retain;
  const auto instance=GetModuleHandleW(nullptr);
  WNDCLASSW type{};type.hInstance=instance;type.lpfnWndProc=application::procedure;type.lpszClassName=owner_class;
  require(RegisterClassW(&type)!=0,"owner RegisterClass");
  RECT bounds{0,0,480,320};const DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN;
  require(AdjustWindowRect(&bounds,style,FALSE)!=FALSE,"owner bounds");
  app.window=CreateWindowExW(0,owner_class,L"Private SDK owner progress diagnostic",style,0,0,
      bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,&app);
  require(app.window!=nullptr,"owner CreateWindow");app.probe.create(app.window,instance);
  app.stop_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);require(app.stop_event!=nullptr,"monitor stop event");
  app.thread=CreateThread(nullptr,0,application::monitor,&app,0,nullptr);require(app.thread!=nullptr,"monitor thread");
  auto adapter=std::make_unique<sdk::webview2_adapter>(app.window,cfg.profile,std::string{},cfg.runtime,sdk::diagnostics::mode::disabled);
  app.renderer=adapter.get();require(app.renderer->configure_profile_access(sdk::webview2_profile_access::exclusive).ok,"exclusive profile");
  app.host=std::make_unique<sdk::webview_host>(std::move(adapter));
  app.renderer->prepare_async([&](auto result)noexcept{
    try {
      require(result.ok && app.renderer->acquire_runtime_exit_watch(app.watch).ok,"prepare/runtime watch");
      sdk::host_config config;
#ifdef _WIN64
    config.architecture="x64";
#else
    config.architecture="x86";
#endif
      config.owner_window=app.window;config.resource_root=cfg.resources;config.bridge_adapter=&app.bridge;
      require(app.host->start(config).ok && app.host->resize(480,288).ok,"mount/viewport");
      app.runtime_version=app.renderer->runtime_version();require(!app.runtime_version.empty(),"runtime version unavailable");
      app.prepared=true;ShowWindow(app.window,SW_SHOWNOACTIVATE);
    } catch(const std::exception& error){fail(error.what());}catch(...){fail("prepare callback exception");}
  });
  MSG message{};
  while(!app.closed) {
    require(GetTickCount64()-app.started<55000,"absolute owner deadline");
    const auto status=GetMessageW(&message,nullptr,0,0);require(status>0,"owner message loop ended early");
    TranslateMessage(&message);DispatchMessageW(&message);
  }
  require(GetTickCount64()-app.started<55000,"late lifecycle completion");
  const auto browser=app.watch->observe();
  require(browser.phase==sdk::webview2_runtime_exit_phase::finished && browser.event_observed && browser.process_signaled &&
      browser.active_callbacks==0 && browser.exit_kind==sdk::webview2_browser_exit_kind::normal,"browser not normally closed");
  const auto runtime=app.runtime_version;
  require(SetEvent(app.stop_event)!=FALSE && WaitForSingleObject(app.thread,1000)==WAIT_OBJECT_0,"monitor did not stop");
  require(CloseHandle(app.thread) && CloseHandle(app.stop_event),"monitor handle close");
  const auto snapshot=app.probe.snapshot();json page;
  {std::lock_guard<std::mutex> lock(app.report_mutex);page=app.page_report;}
  const bool accounting=app.report_received && app.done_received && page.value("valid",false) && !page.value("nativeTerminalUnknown",true) &&
      bounded_count(page,"attempted")==bounded_count(page,"success") && bounded_count(page,"success")==app.business.load() &&
      app.business.load()>0 && bounded_count(page,"failed")==0 && bounded_count(page,"timeouts")==0 &&
      bounded_count(page,"mismatches")==0 && bounded_count(page,"inflight")==0;
  const auto seconds=[&](unsigned phase){const auto& value=snapshot.phases[phase];
    return value.end>=value.begin && snapshot.frequency ? (value.end-value.begin)/snapshot.frequency : 0;};
  const bool complete=accounting && snapshot.stage==5 && !snapshot.fault && seconds(0)>=2 && seconds(1)>=10 && seconds(3)>=2 &&
      !app.deadline_failed.load() && !app.monitor_failed.load();
  const bool idle_progress=snapshot.phases[0].paints && snapshot.phases[0].timers && snapshot.phases[3].paints && snapshot.phases[3].timers;
  app.watch.reset();app.host.reset();app.renderer=nullptr;app.probe.destroy();
  require(DestroyWindow(app.window) && UnregisterClassW(owner_class,instance),"owner cleanup");
  require(private_desktop()==desktop,"desktop identity changed");CoUninitialize();
  publish(cfg.output,{{"schema","soui.webview2-owner-fairness/v1"},{"diagnosticExecutionComplete",complete},
    {"probeQualified",complete && idle_progress},{"normalRuntimeClose",true},{"deadlineFailed",app.deadline_failed.load()},
    {"monitorFailed",app.monitor_failed.load()},{"counterFault",snapshot.fault},{"subjectPid",GetCurrentProcessId()},
    {"subjectCreationFileTime",subject},{"desktop",desktop},{"privateNonInputDesktop",true},
    {"sdkRawVersion",SOUI_SDK_VERSION_STRING},{"sdkPackageVersion",SOUI_FAIRNESS_PACKAGE_VERSION},{"buildId",SOUI_FAIRNESS_BUILD_ID},
    {"runtimeVersion",runtime},{"browserPid",browser.browser_process_id},{"browserCreationFileTime",wide(browser.creation_file_time)},
    {"browserImage",browser.image_path.generic_string()},{"environment",env},{"qpcFrequency",wide(snapshot.frequency)},
    {"nativeEchoCalls",wide(app.business.load())},{"phaseStage",snapshot.stage},
    {"reportReplyConfirmed",app.done_received},{"finalDoneControlReplyConfirmed",nullptr},
    {"phases",{{"preIdle",phase_json(snapshot.phases[0])},{"load",phase_json(snapshot.phases[1])},
               {"drain",phase_json(snapshot.phases[2])},{"postIdle",phase_json(snapshot.phases[3])}}},{"page",page},
    {"paintProgressDuringLoad",snapshot.phases[1].paints>0},{"timerProgressDuringLoad",snapshot.phases[1].timers>0},
    {"limits",{"One private current-candidate diagnostic; no A/B or throughput improvement.",
      "WM_PAINT/timer and invalidations may coalesce; counts are not unique frames.",
      "No hardware QS_INPUT, effective FPS, physical presentation or whole-queue fairness qualification."}},
    {"performanceVerified",false},{"fairnessVerified",false},{"hardwareInputMeasured",false},{"physicalPresentationMeasured",false}},app.started+55000);
  std::cout<<"Owner paint/timer diagnostic complete="<<(complete?"true":"false")<<"; performanceVerified=false\n";
  return complete ? 0 : 3;
}
} // namespace
int wmain(int argc,wchar_t** argv) {
  try {return run(argc,argv);}catch(const std::exception& error){fail(error.what());}catch(...){fail("unexpected failure");}
}
