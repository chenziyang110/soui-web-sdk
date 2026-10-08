#include <windows.h>
#include <shellapi.h>
#include <souistd.h>
#include <SApp.h>
#include <core/SHostWnd.h>
#include <com-cfg.h>
#include <helper/SAdapterBase.h>
#include <soui/host/soui_miniblink_host.h>
#include <soui/json_bridge.h>
#include <soui/runtime/services/desktop/window_commands.h>
#include <json.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <algorithm>
#include <chrono>
#include <thread>

namespace {
using json=nlohmann::json;
std::filesystem::path executable_directory() {
  std::wstring value(32768,L'\0');
  const DWORD length=GetModuleFileNameW(nullptr,value.data(),static_cast<DWORD>(value.size()));
  if(!length || length>=value.size())throw std::runtime_error("Cannot resolve executable path");
  value.resize(length);
  return std::filesystem::path(value).parent_path();
}

class starter_window final : public SOUI::SHostWnd {
 public:
  starter_window(soui::sdk::soui_miniblink_host& host,bool smoke,const std::filesystem::path& log)
    : host_(host),smoke_(smoke),log_(log),started_(GetTickCount64()) {}
  void append(const char* text){std::ofstream(log_,std::ios::app)<<text<<'\n';}
 protected:
  void OnSize(UINT type,SOUI::CSize size){if(type!=SIZE_MINIMIZED)(void)host_.resize(std::max(1L,size.cx),std::max(1L,size.cy));}
  void OnSetFocus(HWND){(void)host_.focus();}
  void OnTimer(UINT_PTR){
    host_.pump();
    if(smoke_ && host_.application_ready()) {append("application-ready");OnClose();}
    else if(smoke_ && GetTickCount64()-started_>20000){exit_code_=2;append("application-timeout");OnClose();}
  }
  void OnDpiChanged(WORD,const RECT* rect){
    if(rect)::SetWindowPos(m_hWnd,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);
  }
  LRESULT OnBridgeEvent(UINT,WPARAM,LPARAM value){host_.deliver_posted_event(reinterpret_cast<void*>(value));return 0;}
  LRESULT OnWindowCommand(UINT,WPARAM command,LPARAM payload){
    const auto action=static_cast<soui::runtime::window_command>(command);
    if(action==soui::runtime::window_command::begin_drag && !(GetAsyncKeyState(VK_LBUTTON)&0x8000))return 0;
    return soui::runtime::execute_window_command(m_hWnd,action,payload)?1:0;
  }
  void OnClose(){KillTimer(1);(void)host_.destroy();append("host-close-complete");DestroyWindow();PostQuitMessage(exit_code_);}
  BEGIN_MSG_MAP_EX(starter_window)
    MSG_WM_SIZE(OnSize)
    MSG_WM_SETFOCUS(OnSetFocus)
    MSG_WM_TIMER(OnTimer)
    MSG_WM_DPICHANGED(OnDpiChanged)
    MSG_WM_CLOSE(OnClose)
    MESSAGE_HANDLER_EX(soui::sdk::bridge_event_message,OnBridgeEvent)
    MESSAGE_HANDLER_EX(soui::runtime::window_command_message,OnWindowCommand)
    CHAIN_MSG_MAP(SOUI::SHostWnd)
  END_MSG_MAP()
 private:
  soui::sdk::soui_miniblink_host& host_;
  bool smoke_;
  std::filesystem::path log_;
  ULONGLONG started_;
  int exit_code_{0};
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
  const auto greet_contract=json{
    {"protocol","soui.action/1"},{"title","问候"},{"description","由C++生成问候消息。"},
    {"inputSchema",{{"type","object"},{"properties",{{"name",{{"type","string"},{"minLength",1},{"maxLength",32}}}}},
                    {"required",json::array({"name"})},{"additionalProperties",false}}},
    {"outputSchema",{{"type","object"},{"properties",{{"message",{{"type","string"},{"maxLength",200}}}}},
                     {"required",json::array({"message"})},{"additionalProperties",false}}}
  }.dump();
  if(!bridge.register_action("app.greet","app.greet",greet_contract,[](const soui::runtime::bridge_request& request) {
    const auto value=json::parse(request.params_json);
    return json{{"message","Hello, "+value["name"].get<std::string>()+". This response came from C++."}}.dump();
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
  soui::sdk::soui_miniblink_host host(std::make_unique<soui::sdk::dynamic_miniblink_adapter>());
  starter_window window(host,smoke,log);
  HWND handle=window.Create(nullptr,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,0,CW_USEDEFAULT,CW_USEDEFAULT,1100,720);
  if(!handle)return 5;
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
  if(!result.ok){window.append(result.code.c_str());if(!smoke)MessageBoxA(handle,result.message.c_str(),result.code.c_str(),MB_ICONERROR);window.DestroyWindow();return 6;}
  window.append("host-started");
  ::SetWindowTextW(handle,L"SouiStarter");
  window.ShowWindow(smoke?SW_HIDE:SW_SHOW);
  window.UpdateWindow();
  window.SetTimer(1,16);
  return app.Run(handle);
}
}
int APIENTRY wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR command,int) {
  const bool smoke=std::wstring(command).find(L"--smoke-test")!=std::wstring::npos;
  const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  int result=1;
  try{result=run(instance,smoke);}catch(const std::exception& ex){if(!smoke)MessageBoxA(nullptr,ex.what(),"SouiStarter",MB_ICONERROR);}
  const auto shutdown=soui::sdk::shutdown_miniblink_runtime();
  if(!shutdown.ok && result==0)result=7;
  if(SUCCEEDED(com))CoUninitialize();
  return result;
}
