#include <windows.h>
#include <shellapi.h>
#include <souistd.h>
#include <SApp.h>
#include <core/SHostWnd.h>
#include <core/SMsgLoop.h>
#include <com-cfg.h>
#include <helper/SAdapterBase.h>
#include <soui/host/soui_miniblink_host.h>
#include <soui/json_bridge.h>
#ifdef SOUI_DEVTOOLS_PACKAGE_AVAILABLE
#include <soui_devtools_package.h>
#endif
#include "generated/actions.hpp"
#include <soui/runtime/services/desktop/window_commands.h>
#include <json.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <algorithm>
#include <chrono>
#include <thread>

namespace {
// Replace with the application's trusted native identity/policy decision.
// Debug and Release use this same code; page/CLI/environment flags cannot enable it.
constexpr bool allow_native_devtools = false;
// Independent native opt-in; requires a matching local-resource-capable kernel.
constexpr bool use_local_resource_origin = false;
constexpr UINT devtools_open_command = 0x6D01, devtools_end_command = 0x6D02;
using json=nlohmann::json;
std::filesystem::path executable_directory() {
  std::wstring value(32768,L'\0');
  const DWORD length=GetModuleFileNameW(nullptr,value.data(),static_cast<DWORD>(value.size()));
  if(!length || length>=value.size())throw std::runtime_error("Cannot resolve executable path");
  value.resize(length);
  return std::filesystem::path(value).parent_path();
}

class starter_window final : public SOUI::SHostWnd, public SOUI::IMessageFilter {
 public:
  starter_window(soui::sdk::soui_miniblink_host& host,bool smoke,const std::filesystem::path& log)
    : host_(host),smoke_(smoke),log_(log),started_(GetTickCount64()) {}
  void append(const char* text){std::ofstream(log_,std::ios::app)<<text<<'\n';}
  void begin_shutdown(int exit_code){if(!exit_code_ && exit_code)exit_code_=exit_code;OnClose();}
  BOOL PreTranslateMessage(MSG* message) override {
    if (!message || message->message != WM_KEYDOWN || closing_ ||
        (message->hwnd != m_hWnd && !::IsChild(m_hWnd,message->hwnd))) return FALSE;
    if (!host_.devtools_shortcut(static_cast<unsigned>(message->wParam),
        (GetKeyState(VK_CONTROL)&0x8000)!=0,(GetKeyState(VK_SHIFT)&0x8000)!=0,
        (GetKeyState(VK_MENU)&0x8000)!=0,(message->lParam&(1LL<<30))!=0)) return FALSE;
    request_devtools(); return TRUE;
  }
  void request_devtools() {
    if (!allow_native_devtools || closing_ || diagnostic_prompt_ || !host_.application_ready()) return;
    diagnostic_prompt_=true;
    struct prompt_guard {bool& active;~prompt_guard(){active=false;}} prompt{diagnostic_prompt_};
    if (MessageBoxW(m_hWnd,L"允许调试当前业务窗口？调试者可以查看页面数据并执行代码。结束诊断需要关闭此业务窗口，请先保存工作。",
        L"当前窗口开发者工具",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES) return;
    soui::sdk::devtools::grant grant;
    const auto authorized=host_.authorize_devtools(grant);
    if (!authorized.ok) { MessageBoxA(m_hWnd,authorized.message.c_str(),authorized.code.c_str(),MB_ICONERROR); return; }
    const auto opened=host_.open_devtools(grant);
    if (!opened.accepted()) MessageBoxA(m_hWnd,opened.message.c_str(),opened.code.c_str(),MB_ICONERROR);
    else {append("devtools-open-accepted");update_devtools_status();}
  }
  void update_devtools_status() {
    if(!allow_native_devtools || !::IsWindow(m_hWnd))return;
    const auto status=host_.observe_devtools();
    if(diagnostic_state_==static_cast<int>(status.state))return;
    diagnostic_state_=static_cast<int>(status.state);
    using soui::sdk::devtools::phase;
    const wchar_t* title=L"SouiStarter";
    switch(status.state) {
      case phase::opening: title=L"SouiStarter — 正在准备开发者工具";break;
      case phase::attached: title=L"SouiStarter — 开发者工具已连接";break;
      case phase::revoking: title=L"SouiStarter — 正在结束诊断";break;
      case phase::failed: title=L"SouiStarter — 诊断失败，请结束诊断";break;
      case phase::drained: title=L"SouiStarter — 诊断已结束";break;
      default: break;
    }
    ::SetWindowTextW(m_hWnd,title);
  }
  void install_devtools_menu() {
    if (!allow_native_devtools) return;
    HMENU bar=CreateMenu(),menu=CreatePopupMenu();
    if (!bar || !menu) {if(menu)DestroyMenu(menu);if(bar)DestroyMenu(bar);throw std::runtime_error("Cannot create diagnostic menu");}
    const bool attached=AppendMenuW(menu,MF_STRING,devtools_open_command,L"打开当前窗口开发者工具") &&
        AppendMenuW(menu,MF_STRING,devtools_end_command,L"结束诊断并关闭当前窗口") &&
        AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(menu),L"诊断");
    if (!attached || !::SetMenu(m_hWnd,bar)) {
      if(!attached)DestroyMenu(menu);
      DestroyMenu(bar);throw std::runtime_error("Cannot attach diagnostic menu");
    }
    devtools_menu_bar_=bar; // Attached menus remain owned by this HWND.
    if(!::SetWindowPos(m_hWnd,nullptr,0,0,0,0,
        SWP_FRAMECHANGED|SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE))
      throw std::runtime_error("Cannot calculate the native diagnostic menu frame");
    DrawMenuBar(m_hWnd);
  }
  int finish_shutdown_before_return(SOUI::SApplication& app,int exit_code) noexcept {
    try{begin_shutdown(exit_code);}catch(...){std::terminate();}
    while(::IsWindow(m_hWnd)) {
      MSG message{};
      const auto status=::GetMessageW(&message,nullptr,0,0);
      if(status<0)std::terminate();
      if(status==0){
        if(!exit_code_ && message.wParam)exit_code_=static_cast<int>(message.wParam);
        try{OnClose();}catch(...){std::terminate();}
        continue;
      }
      try{app.GetMsgLoop()->OnMsg(&message);}catch(...){if(!exit_code_)exit_code_=1;}
      try{OnClose();}catch(...){std::terminate();}
    }
    try{
      if(!host_.destroy().ok) {
        const auto diagnostic=host_.observe_devtools();
        if(diagnostic.state!=soui::sdk::devtools::phase::failed || diagnostic.resources_retained)std::terminate();
        if(!exit_code_)exit_code_=1;
      }
    }catch(...){std::terminate();}
    return exit_code_;
  }
 protected:
  LRESULT OnNativeNonClient(UINT message,WPARAM wparam,LPARAM lparam) {
    if(!allow_native_devtools || !devtools_menu_bar_ || ::GetMenu(m_hWnd)!=devtools_menu_bar_) {
      SetMsgHandled(FALSE);return 0;
    }
    // SHostWnd otherwise consumes the native menu's layout and hit testing.
    return DefWindowProc(message,wparam,lparam);
  }
  void OnSize(UINT type,SOUI::CSize size){if(type!=SIZE_MINIMIZED)(void)host_.resize(std::max(1L,size.cx),std::max(1L,size.cy));}
  void OnSetFocus(HWND){(void)host_.focus();}
  void OnTimer(UINT_PTR){
    if(closing_) {OnClose();return;}
    host_.pump();
    update_devtools_status();
    if(smoke_ && host_.application_ready()) {append("application-ready");OnClose();}
    else if(smoke_ && GetTickCount64()-started_>20000){exit_code_=2;append("application-timeout");OnClose();}
  }
  void OnDpiChanged(WORD,const RECT* rect){
    if(rect)::SetWindowPos(m_hWnd,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);
  }
  LRESULT OnBridgeEvent(UINT,WPARAM,LPARAM value){host_.deliver_posted_event(reinterpret_cast<void*>(value));return 0;}
  LRESULT OnNativeCommand(UINT,WPARAM command,LPARAM) {
    if (LOWORD(command)==devtools_open_command) request_devtools();
    else if (LOWORD(command)==devtools_end_command && allow_native_devtools) begin_shutdown(0);
    else SetMsgHandled(FALSE);
    return 0;
  }
  LRESULT OnWindowCommand(UINT,WPARAM command,LPARAM payload){
    const auto action=static_cast<soui::runtime::window_command>(command);
    if(action==soui::runtime::window_command::begin_drag && !(GetAsyncKeyState(VK_LBUTTON)&0x8000))return 0;
    return soui::runtime::execute_window_command(m_hWnd,action,payload)?1:0;
  }
  void OnClose(){
    if(!closing_) {closing_=true;append("host-close-start");if(::IsWindow(m_hWnd) && !SetTimer(1,50))std::terminate();}
    const auto closed=host_.destroy();
    if(!closed.ok) {
      if(close_error_!=closed.code) {close_error_=closed.code;append(closed.code.c_str());}
      const auto diagnostic=host_.observe_devtools();
      if(diagnostic.state!=soui::sdk::devtools::phase::failed || diagnostic.resources_retained)return;
      if(!exit_code_)exit_code_=1; // Physical ownership ended; retain the failed result.
    }
    const auto diagnostic=host_.observe_devtools();
    // pump() may already have completed failed retirement, after which an
    // idempotent destroy succeeds. Preserve that earlier terminal failure.
    if(diagnostic.state==soui::sdk::devtools::phase::failed && !diagnostic.resources_retained && !exit_code_)exit_code_=1;
    if(!::IsWindow(m_hWnd))return;
    if(!DestroyWindow()) {append("E_WINDOW_CLOSE_FAILED");return;}
    append("host-close-complete");PostQuitMessage(exit_code_);
  }
  BEGIN_MSG_MAP_EX(starter_window)
    MESSAGE_HANDLER_EX(WM_NCCALCSIZE,OnNativeNonClient)
    MESSAGE_HANDLER_EX(WM_NCHITTEST,OnNativeNonClient)
    MESSAGE_HANDLER_EX(WM_NCACTIVATE,OnNativeNonClient)
    MESSAGE_HANDLER_EX(WM_NCPAINT,OnNativeNonClient)
    MSG_WM_SIZE(OnSize)
    MSG_WM_SETFOCUS(OnSetFocus)
    MSG_WM_TIMER(OnTimer)
    MSG_WM_DPICHANGED(OnDpiChanged)
    MSG_WM_CLOSE(OnClose)
    MESSAGE_HANDLER_EX(soui::sdk::bridge_event_message,OnBridgeEvent)
    MESSAGE_HANDLER_EX(WM_COMMAND,OnNativeCommand)
    MESSAGE_HANDLER_EX(soui::runtime::window_command_message,OnWindowCommand)
    CHAIN_MSG_MAP(SOUI::SHostWnd)
  END_MSG_MAP()
 private:
  soui::sdk::soui_miniblink_host& host_;
  bool smoke_;
  std::filesystem::path log_;
  ULONGLONG started_;
  int exit_code_{0};
  bool closing_{false};
  bool diagnostic_prompt_{false};
  HMENU devtools_menu_bar_{nullptr};
  int diagnostic_state_{-1};
  std::string close_error_;
};

int run(HINSTANCE instance,bool smoke) {
  const auto root=executable_directory();
  const auto log=root/"smoke.log";
  std::ofstream(log)<<"process-start\n";
  // The portable template defaults to the stable software presentation path.
  // GPU compositor modes remain an SDK/kernel validation surface until their
  // close path passes the same lifecycle and long-run gates.
  SetEnvironmentVariableW(L"SOUI_GPU_COMPOSITOR",nullptr);
  SetEnvironmentVariableW(L"SOUI_GPU_DIRECT_PRESENT",nullptr);
  SComMgr components;
  components.SetDllPath(SOUI::SStringT(root.wstring().c_str()));
  SOUI::CAutoRefPtr<SOUI::IRenderFactory> render;
  if(!components.CreateRender_GDI(reinterpret_cast<IObjRef**>(&render)))return 3;
  SOUI::SApplication app(render,instance,_T("SOUI_STARTER"));
  app.SetAppDir(SOUI::SStringT(root.wstring().c_str()));
  SOUI::CAutoRefPtr<SOUI::IResProvider> resources;
  SOUI::CreateResProvider(SOUI::RES_PE,reinterpret_cast<IObjRef**>(&resources));
  if(!resources || !resources->Init(reinterpret_cast<WPARAM>(instance),0))return 4;
  app.AddResProvider(resources);

  soui::sdk::json_bridge bridge("app://starter");
  if(!bridge.enable_session_renewal())return 11;
  if(!bridge.enable_directory_service())return 10;
  if(!bridge.enable_clipboard_service())return 8;
  if(!bridge.enable_window_service())return 9;
  if(!app_actions::register_Greet(bridge,[](const app_actions::GreetInput& input,
                                         const soui::runtime::bridge_request&) {
    return app_actions::GreetOutput{"Hello, "+input.name+". This response came from C++."};
  }))return 11;
  (void)bridge.register_task_handler("app.count", [](const soui::runtime::task_invocation& invocation,
                                                     const soui::runtime::task_control& control) {
    const auto input=json::parse(invocation.parameters_json, nullptr, false);
    if(!input.is_object() || input.size()!=1 || !input.contains("steps") ||
       !input["steps"].is_number_integer() || input["steps"].get<int>()<1 ||
       input["steps"].get<int>()>20)
      return soui::runtime::task_outcome{soui::runtime::result_kind::failure,"{}","Expected 1–20 steps."};
    const int steps=input["steps"].get<int>();
    for(int index=1;index<=steps;++index){
      if(control.cancellation_requested())
        return soui::runtime::task_outcome{soui::runtime::result_kind::task_cancelled,"{}","Cancelled."};
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      if(!control.report_progress(static_cast<std::uint32_t>(index*100/steps),"Counting"))
        return soui::runtime::task_outcome{soui::runtime::result_kind::task_cancelled,"{}","Cancelled."};
    }
    return soui::runtime::task_outcome{soui::runtime::result_kind::success,
                                      json{{"count",steps}}.dump(),"Completed."};
  });
  auto adapter = use_local_resource_origin
      ? std::make_unique<soui::sdk::dynamic_miniblink_adapter>(
          soui::sdk::diagnostics::mode::disabled, soui::sdk::miniblink_local_resource_options{})
      : std::make_unique<soui::sdk::dynamic_miniblink_adapter>();
  soui::sdk::soui_miniblink_host host(std::move(adapter));
  starter_window window(host,smoke,log);
  struct native_filter final {
    SOUI::SMessageLoop* loop; SOUI::IMessageFilter* filter; bool registered{false};
    native_filter(SOUI::SMessageLoop* owner,SOUI::IMessageFilter* value):loop(owner),filter(value) {
      if (allow_native_devtools) {
        if (!loop->AddMessageFilter(filter)) throw std::runtime_error("Cannot register window key routing");
        registered=true;
      }
    }
    ~native_filter(){if(registered)loop->RemoveMessageFilter(filter);}
  } filter(app.GetMsgLoop(),&window);
  try {
  HWND handle=window.Create(nullptr,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,0,CW_USEDEFAULT,CW_USEDEFAULT,1100,720);
  if(!handle)return 5;
  if(allow_native_devtools) {
    soui::sdk::devtools::configuration diagnostics;
    diagnostics.access=soui::sdk::devtools::policy::application_controlled;
    diagnostics.ending=soui::sdk::devtools::termination::destroy_target;
#ifdef SOUI_DEVTOOLS_PACKAGE_AVAILABLE
    diagnostics.frontend_root=root/soui::sdk::devtools::package::frontend_subdir;
    diagnostics.frontend_manifest_sha256=soui::sdk::devtools::package::frontend_manifest_sha256;
    diagnostics.kernel_sha256=soui::sdk::devtools::package::kernel_sha256;
#else
    throw std::runtime_error("Native DevTools requires an explicitly approved matching resource package.");
#endif
    diagnostics.f12=diagnostics.ctrl_shift_i=true;
    const auto configured=host.configure_devtools(diagnostics);
    if(!configured.ok)throw std::runtime_error(configured.code+": "+configured.message);
    window.install_devtools_menu();
  }
  soui::sdk::host_config config;
#ifdef _WIN64
  config.architecture="x64";
#else
  config.architecture="x86";
#endif
  config.miniblink_binary=root/"node.dll";
  config.resource_root=root/"resources";
  config.owner_window=handle;
  config.bridge_adapter=&bridge;
  const auto result=host.start(config);
  if(!result.ok){
    window.append(result.code.c_str());
    if(!smoke)MessageBoxA(handle,result.message.c_str(),result.code.c_str(),MB_ICONERROR);
    window.begin_shutdown(6);
    const auto loop_result=::IsWindow(handle)?app.GetMsgLoop()->Run():6;
    return window.finish_shutdown_before_return(app,loop_result);
  }
  window.append("host-started");
  ::SetWindowTextW(handle,L"SouiStarter");
  window.ShowWindow(smoke?SW_HIDE:SW_SHOW);
  window.UpdateWindow();
  if(!window.SetTimer(1,16))throw std::runtime_error("Cannot start the Host timer");
  const auto loop_result=app.GetMsgLoop()->Run();
  return window.finish_shutdown_before_return(app,loop_result);
  } catch(...) {
    try{window.append("E_APPLICATION_EXCEPTION");}catch(...){ }
    return window.finish_shutdown_before_return(app,1);
  }
}
}
int APIENTRY wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR command,int) {
  const bool smoke=std::wstring(command).find(L"--smoke-test")!=std::wstring::npos;
  const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  int result=1;
  try{result=run(instance,smoke);}catch(const std::exception& ex){if(!smoke)MessageBoxA(nullptr,ex.what(),"SouiStarter",MB_ICONERROR);}
  const auto shutdown=soui::sdk::wait_for_miniblink_runtime_shutdown(std::chrono::seconds{20});
  if(!shutdown.ok && result==0)result=7;
  if(SUCCEEDED(com))CoUninitialize();
  return result;
}
