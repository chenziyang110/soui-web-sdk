#include <windows.h>
#include <soui/host/soui_miniblink_host.h>
#include <soui/json_bridge.h>
#include <soui/version.h>
#include <chrono>
#include <json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <stdexcept>
namespace {
struct fixture {
  soui::sdk::soui_miniblink_host host{std::make_unique<soui::sdk::dynamic_miniblink_adapter>()};
  std::filesystem::path log;
  ULONGLONG started=GetTickCount64();
  bool closing=false;
  int exit_code=0;
  void append(const char* value){std::ofstream(log,std::ios::app)<<value<<'\n';}
};
LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
  auto* state=reinterpret_cast<fixture*>(GetWindowLongPtrW(window,GWLP_USERDATA));
  if(message==WM_NCCREATE){state=static_cast<fixture*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));}
  if(state){
    if(message==soui::sdk::bridge_event_message){state->host.deliver_posted_event(reinterpret_cast<void*>(lparam));return 0;}
    if(message==WM_SIZE){(void)state->host.resize(std::max(1,static_cast<int>(LOWORD(lparam))),std::max(1,static_cast<int>(HIWORD(lparam))));return 0;}
    if(message==WM_CLOSE){state->closing=true;return 0;}
    if(message==WM_TIMER){
      state->host.pump();
      if(!state->closing && state->host.application_ready()){state->append("application-ready");state->closing=true;}
      if(!state->closing && GetTickCount64()-state->started>20000){state->append("application-timeout");state->exit_code=2;state->closing=true;}
      if(state->closing){
        const auto closed=state->host.destroy();
        if(closed.ok){state->append("host-close-complete");KillTimer(window,1);DestroyWindow(window);PostQuitMessage(state->exit_code);}
        else if(GetTickCount64()-state->started>25000){state->append("host-close-failed");DestroyWindow(window);PostQuitMessage(3);}
      }
      return 0;
    }
  }
  if(message==WM_NCDESTROY)SetWindowLongPtrW(window,GWLP_USERDATA,0);
  return DefWindowProcW(window,message,wparam,lparam);
}
int run(HINSTANCE instance){
  std::wstring executable(32768,L'\0');
  const DWORD length=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
  if(!length || length>=executable.size())return 4;
  executable.resize(length);
  const auto root=std::filesystem::path(executable).parent_path();
  soui::sdk::json_bridge bridge("app://release-upgrade");
  // Bridge outlives Host even when startup or teardown fails.
  fixture state;state.log=root/"smoke.log";std::ofstream(state.log)<<"process-start\n";
  SetEnvironmentVariableW(L"SOUI_GPU_COMPOSITOR",nullptr);SetEnvironmentVariableW(L"SOUI_GPU_DIRECT_PRESENT",nullptr);
  if(!bridge.register_method("app.greet","app.greet",[](std::string_view input){
    const auto value=nlohmann::json::parse(input,nullptr,false);
    if(!value.is_object() || value.size()!=1 || !value.contains("name") || !value["name"].is_string() || value["name"].get_ref<const std::string&>().size()>128)
      return std::string{"{\"error\":\"E_INVALID_PARAMS\",\"message\":\"Expected a bounded name.\"}"};
    return nlohmann::json{{"message","Hello, "+value["name"].get<std::string>()+"."}}.dump();
  }))return 5;
  WNDCLASSW klass{};klass.lpfnWndProc=window_proc;klass.hInstance=instance;klass.lpszClassName=L"SouiReleaseUpgradeFixture";
  if(!RegisterClassW(&klass))return 6;
  HWND window=CreateWindowW(klass.lpszClassName,L"ReleaseUpgradeCheck",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,900,600,nullptr,nullptr,instance,&state);
  if(!window)return 7;
  soui::sdk::host_config config;
#if defined(_WIN64)
  config.architecture="x64";
#else
  config.architecture="x86";
#endif
  config.miniblink_binary=root/"node.dll";
  config.resource_root=root/"resources";config.owner_window=window;config.bridge_adapter=&bridge;
  const auto mounted=state.host.start(config);
  if(!mounted.ok){state.append(mounted.code.c_str());DestroyWindow(window);return 8;}
  state.append("host-started");ShowWindow(window,SW_HIDE);SetTimer(window,1,16,nullptr);
  MSG message{};BOOL status;
  while((status=GetMessageW(&message,nullptr,0,0))>0){TranslateMessage(&message);DispatchMessageW(&message);}
  return status<0?9:static_cast<int>(message.wParam);
}
}
int APIENTRY wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
  const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  if(FAILED(com))return 10;
  int result=1;try{result=run(instance);}catch(const std::exception&){}
#if SOUI_SDK_VERSION_MAJOR > 0 || SOUI_SDK_VERSION_MINOR >= 5
  const auto shutdown=soui::sdk::wait_for_miniblink_runtime_shutdown(std::chrono::seconds(20));
#else
  const auto shutdown=soui::sdk::shutdown_miniblink_runtime();
#endif
  if(!shutdown.ok && result==0)result=11;
  CoUninitialize();return result;
}