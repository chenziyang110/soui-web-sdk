#include "reference-business.h"
#include "reference-agent.h"
#include <soui/host/soui_miniblink_host.h>
#ifdef SOUI_REFERENCE_MODERN
#include <soui/host/webview2_host.h>
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <functional>
#include <string>
#include <string_view>
#include <json.hpp>
#include <cstdint>
#include <stdexcept>

namespace {
bool observation_enabled() noexcept {
  wchar_t value[2]{};
  return GetEnvironmentVariableW(L"SOUI_REFERENCE_OBSERVATION",value,2)==1&&value[0]==L'1';
}
struct application final {
  HWND window{}; bool closing{},smoke{},ready_logged{}; int exit_code{};
  ULONGLONG began{GetTickCount64()};
  std::filesystem::path root,profile;
  // Business context survives the Bridge's real worker joins; Host borrows Bridge.
  std::shared_ptr<reference::business> business;
  std::unique_ptr<soui::sdk::json_bridge> bridge;
  std::unique_ptr<reference::agent> agent;
#ifdef SOUI_REFERENCE_MODERN
  soui::sdk::webview2_adapter* renderer{};
  std::unique_ptr<soui::sdk::webview2_runtime_exit_watch> runtime_watch;
  bool preparation_started{},preparation_completed{},controller_closed{};
  ULONGLONG close_began{};
#else
  soui::sdk::dynamic_miniblink_adapter* renderer{};
#endif
  std::unique_ptr<soui::sdk::webview_host> host;
  bool agent_stopped_logged{},gui_bound{},gui_closed{};
  const bool observations{observation_enabled()}; std::uint64_t observation_sequence{};
  std::function<bool()> gui_current;
  bool document_current(DWORD owner,HWND expected,const std::string& session_id) const {
    if(GetCurrentThreadId()!=owner||closing||window!=expected||!IsWindow(expected)||!host||!renderer)return false;
    DWORD process{};
    if(GetWindowThreadProcessId(expected,&process)!=owner||process!=GetCurrentProcessId())return false;
    const auto* session=host->session();
    return session&&!session->terminal()&&session->session_id()==session_id&&host->document_ready();
  }
  void log(std::string_view value) const noexcept {
    try {
      std::ofstream output{profile / "host.log",std::ios::app|std::ios::binary}; output<<value<<'\n';
    } catch(...) {} // A diagnostic allocation must not bypass real teardown.
  }
  // Private opt-in observation. It neither rechecks the document proof nor changes admission.
  void observe(const char* phase,const std::string* captured_session=nullptr,
               const std::string* captured_generation=nullptr) noexcept {
    if(!observations)return;
    try {
      using json=nlohmann::json;
      const auto pid=GetCurrentProcessId(),thread=GetCurrentThreadId();
      FILETIME created{},exited{},kernel{},user{};
      const bool times=GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)!=FALSE;
      LARGE_INTEGER counter{};const bool timed=QueryPerformanceCounter(&counter)!=FALSE&&counter.QuadPart>=0;
      const std::string_view point{phase};
      const bool withdrawn=point=="bridge-join-complete"||point=="runtime-shutdown-complete"||point=="native.end";
      const HWND observed_window=withdrawn?nullptr:window;
      DWORD window_pid{};const DWORD window_thread=observed_window?GetWindowThreadProcessId(observed_window,&window_pid):0;
      json row{{"schema","soui.reference-application-observation/1"},{"phase",phase},
        {"sequence",++observation_sequence},{"pid",std::to_string(pid)},
        {"creationFileTime",nullptr},{"qpc",nullptr},{"currentThread",std::to_string(thread)},
        {"ownerHwnd",observed_window?json(std::to_string(reinterpret_cast<std::uintptr_t>(observed_window))):json(nullptr)},
        {"ownerThread",window_thread?json(std::to_string(window_thread)):json(nullptr)},
        {"ownerProcess",window_thread?json(std::to_string(window_pid)):json(nullptr)},
        {"ownerValid",observed_window&&IsWindow(observed_window)!=FALSE},{"closing",closing},{"guiBound",gui_bound},
        {"sessionId",nullptr},{"contextReceipt",nullptr},{"sessionDigest",nullptr},
        {"runtimeVersion",nullptr},{"proofRecheckedForObservation",false},{"nonAtomic",true},
        {"qualificationPassed",false},{"releaseReady",false},{"exitCode",exit_code}};
      if(times)row["creationFileTime"]=std::to_string((static_cast<std::uint64_t>(created.dwHighDateTime)<<32U)|created.dwLowDateTime);
      if(timed)row["qpc"]=std::to_string(counter.QuadPart);
#ifdef SOUI_REFERENCE_MODERN
      row["renderer"]="modern";
#else
      row["renderer"]="compat";
#endif
      if(captured_session&&captured_generation) {
        // These values were read by the unchanged successful native bind branch.
        row["sessionId"]=*captured_session;
#ifdef SOUI_REFERENCE_MODERN
        row["sessionDigest"]=captured_generation->substr(7);
#else
        row["contextReceipt"]=captured_generation->substr(7);
#endif
      }
      std::ofstream output{profile/"app-observation.ndjson",std::ios::app|std::ios::binary};
      output<<row.dump()<<'\n';
    } catch(...) {} // Missing observation fails the external reader, never skips teardown.
  }
#ifdef SOUI_REFERENCE_MODERN
  void observe_runtime_exit(const char* phase,const soui::sdk::webview2_runtime_exit_observation& value) noexcept {
    if(!observations)return;
    try {
      using json=nlohmann::json;
      LARGE_INTEGER counter{};FILETIME created{},exited{},kernel{},user{};
      if(!QueryPerformanceCounter(&counter)||counter.QuadPart<0||
         !GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))return;
      DWORD owner_pid{};const auto owner_thread=GetWindowThreadProcessId(window,&owner_pid);
      json row{{"schema","soui.reference-runtime-exit/1"},{"phase",phase},
        {"pid",std::to_string(GetCurrentProcessId())},
        {"creationFileTime",std::to_string((static_cast<std::uint64_t>(created.dwHighDateTime)<<32U)|created.dwLowDateTime)},
        {"qpc",std::to_string(counter.QuadPart)},{"currentThread",std::to_string(GetCurrentThreadId())},
        {"ownerHwnd",std::to_string(reinterpret_cast<std::uintptr_t>(window))},
        {"ownerThread",std::to_string(owner_thread)},{"ownerProcess",std::to_string(owner_pid)},
        {"ownerValid",IsWindow(window)!=FALSE},{"profileAccess","exclusive"},
        {"watchPhase",static_cast<int>(value.phase)},{"watchError",static_cast<int>(value.error)},
        {"hresult",value.hresult},{"win32Error",value.win32_error},
        {"browserPid",std::to_string(value.browser_process_id)},
        {"browserCreationFileTime",std::to_string(value.creation_file_time)},
        {"browserImage",value.image_path.u8string()},
        {"eventBrowserPid",std::to_string(value.event_browser_process_id)},
        {"eventKind",static_cast<int>(value.exit_kind)},{"eventObserved",value.event_observed},
        {"processSignaled",value.process_signaled},{"registered",value.registered},
        {"activeCallbacks",value.active_callbacks},{"nonAtomic",true},
        {"qualificationPassed",false},{"releaseReady",false},{"exitCode",exit_code}};
      std::ofstream output{profile/"runtime-exit.ndjson",std::ios::app|std::ios::binary};
      output<<row.dump()<<'\n';
    }catch(...){} // Evidence absence fails the external reader; it never releases owners.
  }
#endif

  void close() {
#ifdef SOUI_REFERENCE_MODERN
    if(!closing)close_began=GetTickCount64();
#endif
    closing=true;
#ifdef SOUI_REFERENCE_MODERN
    if(GetTickCount64()-close_began>=10000) {
      exit_code=3;log("E_RUNTIME_WATCH_DEADLINE");
      if(runtime_watch)observe_runtime_exit("deadline",runtime_watch->observe());
      std::terminate(); // No deadline releases actual retained borrows.
    }
#endif
    if(bridge&&!gui_closed) {
      bridge->close_gui_commands();
      const auto stopped=bridge->poll_gui_commands();
      if(stopped!=soui::runtime::gui_owner_result::closed) {
        if(stopped==soui::runtime::gui_owner_result::wrong_owner)std::terminate();
        if(!SetTimer(window,2,20,nullptr))std::terminate();
        return; // A live GUI owner borrow retains Host/Bridge/HWND and its actual proof.
      }
      gui_closed=true;log("gui-stop-complete");observe("gui-stop-complete");
    }
    if(agent) {
      const auto stopped=agent->stop_owner();
      if(stopped!=soui::runtime::agent::local_endpoint_stop::stopped) {
        if(stopped==soui::runtime::agent::local_endpoint_stop::wrong_owner_thread)std::terminate();
        if(!SetTimer(window,2,20,nullptr))std::terminate();
        return; // Host/window/policy/Bridge still owned until actual endpoint join.
      }
      if(agent->enabled()&&!agent_stopped_logged){agent_stopped_logged=true;log("agent-stop-complete");observe("agent-stop-complete");}
    }
#ifdef SOUI_REFERENCE_MODERN
    if(preparation_started&&!preparation_completed) {
      if(!SetTimer(window,2,20,nullptr))std::terminate();
      return; // Keep the initializer and owner until a real exit identity is available.
    }
    if(!controller_closed) {
      auto result=host?host->destroy():soui::sdk::host_result{true,{},{}};
      if(result.ok&&renderer)result=renderer->unmount(); // Also closes a prepared, never-mounted adapter.
      if(!result.ok) {
        if(result.code!="E_BUSY") {if(result.code!="E_CURSOR_CLOSE_PENDING")exit_code=3;log(result.code);}
        if(!SetTimer(window,2,20,nullptr))std::terminate();
        return; // Real controller/callback/worker borrows are still owned.
      }
      controller_closed=true;KillTimer(window,1);
      log("host-close-complete");observe("host-close-complete");
    }
    if(preparation_started&&!runtime_watch) {
      const auto acquired=renderer?renderer->acquire_runtime_exit_watch(runtime_watch):
        soui::sdk::host_result{false,"E_RUNTIME_WATCH_UNAVAILABLE","No actual adapter remains."};
      if(!acquired.ok) {
        exit_code=3;log(acquired.code);
        if(acquired.code!="E_RUNTIME_WATCH_UNAVAILABLE"||!renderer||!renderer->failed_preparation_cleanup_complete())std::terminate();
        log("failed-prepare-cleanup-complete"); // Original failed exit remains 3; not runtime-finished.
      }else observe_runtime_exit("armed-during-cleanup",runtime_watch->observe());
    }
    if(runtime_watch) {
    const auto observed=runtime_watch->observe();
    if(observed.phase==soui::sdk::webview2_runtime_exit_phase::failed) {
      exit_code=3;log("E_RUNTIME_WATCH_FAILED");observe_runtime_exit("failed",observed);std::terminate();
    }
    if(observed.phase!=soui::sdk::webview2_runtime_exit_phase::finished) {
      const auto finished=runtime_watch->finish();
      if(!finished.ok) {
        if(finished.code!="E_BUSY") {exit_code=3;log(finished.code);observe_runtime_exit("failed",runtime_watch->observe());std::terminate();}
        if(!SetTimer(window,2,20,nullptr))std::terminate();
        return; // Normal event, held process and callback release are still pending.
      }
      observe_runtime_exit("finished",runtime_watch->observe());
    }
    }
    KillTimer(window,2);
#else
    const auto result=host?host->destroy():soui::sdk::host_result{true,{},{}};
    if(!result.ok) {
      if(result.code!="E_BUSY") {if(result.code!="E_CURSOR_CLOSE_PENDING")exit_code=3;log(result.code);}
      if(!SetTimer(window,2,20,nullptr))std::terminate(); // Cannot destroy a still-borrowed native host.
      return;
    }
    KillTimer(window,1);KillTimer(window,2);log("host-close-complete");observe("host-close-complete");
#endif
    if(agent)agent->set_owner_window(nullptr);
    DestroyWindow(window);
  }
  void pump() {
    if(!host||closing)return;
    try {
    host->pump();
    if(closing)return;
#ifdef SOUI_REFERENCE_MODERN
    const auto result=renderer->status();
    if(!result.ok&&result.code!="E_PENDING") {exit_code=3;log(result.code);close();return;}
#endif
    if(host->application_ready()&&!ready_logged) {ready_logged=true;log("application-ready");observe("application-ready");}
    if(!gui_bound&&host->document_ready()&&host->application_ready()&&host->session()) {
      const auto session_id=host->session()->session_id();
      const auto owner=GetCurrentThreadId();const auto expected=window;
      std::string generation;
#ifdef SOUI_REFERENCE_MODERN
      const auto digest=renderer->session_digest();
      if(!digest.empty()) {
        generation="modern:"+digest;
        gui_current=[this,owner,expected,session_id,digest] {
          return document_current(owner,expected,session_id)&&renderer->status().ok&&renderer->session_digest()==digest&&
            host->application_ready()&&document_current(owner,expected,session_id)&&renderer->status().ok&&renderer->session_digest()==digest;
        };
      }
#else
      const auto receipt=renderer->execution_context_receipt();
      if(receipt&&renderer->accepts_execution_context_receipt(receipt)) {
        generation="compat:"+std::to_string(receipt);
        gui_current=[this,owner,expected,session_id,receipt] {
          return document_current(owner,expected,session_id)&&renderer->accepts_execution_context_receipt(receipt)&&
            host->application_ready()&&document_current(owner,expected,session_id)&&renderer->accepts_execution_context_receipt(receipt);
        };
      }
#endif
      if(gui_current) {
        // Page bind completed before bridge.probe.report. This native proof
        // grants one document; neither the page nor an Agent chooses it.
        if(!gui_current()||!bridge->bind_gui_document(generation,gui_current)) {
          exit_code=3;log("E_REFERENCE_GUI_BIND");close();return;
        }
        gui_bound=true;log("gui-native-document-bound");observe("gui-native-document-bound",&session_id,&generation);
        if(agent&&agent->enabled()&&!agent->started()&&!agent->closed()) {
          if(!agent->start_bound_document(gui_current)){exit_code=3;log("E_REFERENCE_AGENT_START");close();return;}
          log("agent-ready-native-document-bound");observe("agent-ready-native-document-bound",&session_id,&generation);
        }
      }
    }
    if(gui_bound) {
      const auto state=bridge->poll_gui_commands(); // Owner only; SDK limits each dispatch and retains the callback borrow.
      if(state==soui::runtime::gui_owner_result::closed) {log("gui-document-revoked");close();return;}
      if(state==soui::runtime::gui_owner_result::wrong_owner)std::terminate();
    }
    if(agent&&agent->enabled())agent->pump_owner(); // Native-only dispatch, bounded 8 callbacks / 4ms.
    if(smoke&&ready_logged&&gui_bound)close();
    } catch(...) {exit_code=3;log("E_REFERENCE_PUMP");close();}
  }
};
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
  auto* app=reinterpret_cast<application*>(GetWindowLongPtrW(window,GWLP_USERDATA));
  if(message==WM_NCCREATE) {
    app=static_cast<application*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    app->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));
  }
  if(!app)return DefWindowProcW(window,message,wparam,lparam);
  // Miniblink's public adapter owns its real input child and message routing.
  // Do not feed owner messages into that child handler (WM_NCHITTEST routes back to this owner).
  switch(message) {
    case soui::sdk::bridge_event_message:
      if(app->host)app->host->deliver_posted_event(reinterpret_cast<void*>(lparam));return 0;
#ifdef SOUI_REFERENCE_MODERN
    case soui::sdk::webview2_event_message:app->pump();return 0;
#endif
    case WM_SIZE:
      if(app->host&&!app->closing) {
        if(wparam==SIZE_MINIMIZED)(void)app->host->hide();
        else {(void)app->host->show();if(LOWORD(lparam)&&HIWORD(lparam))(void)app->host->resize(LOWORD(lparam),HIWORD(lparam));}
      }return 0;
    case WM_DPICHANGED: {
      const auto* rect=reinterpret_cast<const RECT*>(lparam);
      SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);
      return 0;
    }
    case WM_SETFOCUS:if(app->host&&!app->closing)(void)app->host->focus();return 0;
    case WM_CLOSE:app->close();return 0;
    case WM_TIMER:
      if(app->closing)app->close();
      else if(app->smoke&&GetTickCount64()-app->began>30000) {app->exit_code=2;app->log("application-timeout");app->close();}
      else app->pump();
      return 0;
    case WM_DESTROY:PostQuitMessage(app->exit_code);return 0;
    case WM_NCDESTROY:SetWindowLongPtrW(window,GWLP_USERDATA,0);return DefWindowProcW(window,message,wparam,lparam);
    default:return DefWindowProcW(window,message,wparam,lparam);
  }
}
int run(HINSTANCE instance,const reference::command_line& arguments) {
  const bool smoke=arguments.smoke;
  wchar_t executable[32768]{},local[MAX_PATH]{};
  const auto size=GetModuleFileNameW(nullptr,executable,32768);
  if(!size||size>=32768||FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA|CSIDL_FLAG_CREATE,nullptr,0,local)))return 1;
  application app;app.smoke=smoke;app.root=std::filesystem::path(executable).parent_path();
  app.profile=std::filesystem::path(local)/"SouiReferenceWorkbench"/app.root.filename();
  if(smoke)app.profile/="smoke-"+std::to_string(GetCurrentProcessId());
  std::filesystem::create_directories(app.profile);app.observe("native.begin");
  app.business=std::make_shared<reference::business>(reference::read_fixture(app.root/"resources"/"reference-fixture.json"));
  app.bridge=std::make_unique<soui::sdk::json_bridge>("app://reference-workbench");
  if(!reference::register_business(*app.bridge,app.business))return 1;
  // All business/local declarations precede factory; factory precedes Host bootstrap.
  app.agent=std::make_unique<reference::agent>(*app.bridge,arguments.agent,true);
  const auto class_name=L"SouiReferenceWorkbench";
  WNDCLASSW type{};type.lpfnWndProc=procedure;type.hInstance=instance;type.lpszClassName=class_name;type.hCursor=LoadCursorW(nullptr,IDC_ARROW);
  if(!RegisterClassW(&type))return 1;
  const auto window=CreateWindowExW(0,class_name,L"SOUI 万行资料工作台",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
    CW_USEDEFAULT,CW_USEDEFAULT,1260,880,nullptr,nullptr,instance,&app);
  if(!window){UnregisterClassW(class_name,instance);return 1;}
  // All failures after a window is acquired use the same retryable real teardown.
  try {
    soui::sdk::host_config config;
#ifdef _WIN64
    config.architecture="x64";
#else
    config.architecture="x86";
#endif
    app.agent->set_owner_window(window);
    config.owner_window=window;config.resource_root=app.root/"resources";config.bridge_adapter=app.agent.get();
#ifdef SOUI_REFERENCE_MODERN
    auto adapter=std::make_unique<soui::sdk::webview2_adapter>(window,app.profile/"browser");
    app.renderer=adapter.get();app.host=std::make_unique<soui::sdk::webview_host>(std::move(adapter));
    const auto access=app.renderer->configure_profile_access(soui::sdk::webview2_profile_access::exclusive);
    if(!access.ok)throw std::runtime_error(access.code);
    app.preparation_started=true;
    app.renderer->prepare_async([&app,config](auto result) {
      app.preparation_completed=true;
      if(result.ok)result=app.renderer->acquire_runtime_exit_watch(app.runtime_watch);
      if(result.ok) {
        const auto actual=app.runtime_watch->observe();app.observe_runtime_exit("armed",actual);
        if(actual.phase==soui::sdk::webview2_runtime_exit_phase::failed)
          result={false,"E_RUNTIME_WATCH_FAILED","The exclusive runtime watch failed before mount."};
        else if(!app.closing)result=app.host->start(config);
      }
      if(!result.ok){app.exit_code=3;app.log(result.code);
        if(!PostMessageW(app.window,WM_CLOSE,0,0))std::terminate();}
      else if(app.closing&&!PostMessageW(app.window,WM_CLOSE,0,0))std::terminate();
    });
#else
    config.miniblink_binary=app.root/"node.dll";
    auto adapter=std::make_unique<soui::sdk::dynamic_miniblink_adapter>();
    app.renderer=adapter.get();
    app.host=std::make_unique<soui::sdk::webview_host>(std::move(adapter));
    const auto result=app.host->start(config);
    if(!result.ok){app.exit_code=3;app.log(result.code);app.close();}
#endif
    if(IsWindow(window)){ShowWindow(window,smoke?SW_SHOWNOACTIVATE:SW_SHOW);if(!SetTimer(window,1,50,nullptr)){app.exit_code=3;app.close();}}
  } catch(...) {app.exit_code=3;app.log("E_REFERENCE_START");app.close();}
  MSG message{};
  while(IsWindow(window)) {
    const auto result=GetMessageW(&message,nullptr,0,0);
    if(result>0){TranslateMessage(&message);DispatchMessageW(&message);}
    else {
      // WM_QUIT or message-loop failure cannot abandon a retained Host.
      app.exit_code=3;app.close();
      while(IsWindow(window)) {
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}
        if(IsWindow(window)){app.close();MsgWaitForMultipleObjects(0,nullptr,FALSE,20,QS_ALLINPUT);}
      }
    }
  }
  app.renderer=nullptr;
  app.agent->set_owner_window(nullptr); // No further posts to an HWND which may be reused.
  app.host.reset();
  app.bridge.reset(); // The actual task_service destructor joins before runtime shutdown.
  app.gui_current={}; // Actual GUI/endpoint/task borrows have ended; release the Host proof only now.
  app.log("bridge-join-complete");app.observe("bridge-join-complete");
  app.agent.reset(); // Policy and held process remained alive through all Bridge worker joins.
  app.business.reset();
#ifndef SOUI_REFERENCE_MODERN
  const auto shutdown=soui::sdk::wait_for_miniblink_runtime_shutdown(std::chrono::seconds{20});
  if(!shutdown.ok){app.log(shutdown.code);app.exit_code=3;}
  else {app.log("runtime-shutdown-complete");app.observe("runtime-shutdown-complete");}
#endif
  if(!UnregisterClassW(class_name,instance))app.exit_code=3;app.observe("native.end");
  return app.exit_code;
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int) {
  int count{};auto** arguments=CommandLineToArgvW(GetCommandLineW(),&count);
  if(!arguments)return 1;
  const auto options=reference::parse_command_line(count,arguments);
  LocalFree(arguments);if(!options.valid)return 2;
  const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  if(FAILED(initialized))return 1;
  int result=1;try{result=run(instance,options);}catch(...){}
  CoUninitialize();return result;
}
