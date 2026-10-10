#include "reference-business.h"
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
#include <string>
#include <string_view>

namespace {
struct application final {
  HWND window{}; bool closing{},smoke{},ready_logged{}; int exit_code{};
  ULONGLONG began{GetTickCount64()};
  std::filesystem::path root,profile;
  // Business context survives the Bridge's real worker joins; Host borrows Bridge.
  std::shared_ptr<reference::business> business;
  std::unique_ptr<soui::sdk::json_bridge> bridge;
#ifdef SOUI_REFERENCE_MODERN
  soui::sdk::webview2_adapter* renderer{};
#endif
  std::unique_ptr<soui::sdk::webview_host> host;
  void log(std::string_view value) const noexcept {
    try {
      std::ofstream output{profile / "host.log",std::ios::app|std::ios::binary}; output<<value<<'\n';
    } catch(...) {} // A diagnostic allocation must not bypass real teardown.
  }
  void close() {
    closing=true;
    const auto result=host?host->destroy():soui::sdk::host_result{true,{},{}};
    if(!result.ok) {
      if(result.code!="E_BUSY") {exit_code=3;log(result.code);}
      if(!SetTimer(window,2,20,nullptr))std::terminate(); // Cannot destroy a still-borrowed native host.
      return;
    }
    KillTimer(window,1);KillTimer(window,2);log("host-close-complete");
    DestroyWindow(window);
  }
  void pump() {
    if(!host||closing)return;
    host->pump();
#ifdef SOUI_REFERENCE_MODERN
    const auto result=renderer->status();
    if(!result.ok&&result.code!="E_PENDING") {exit_code=3;log(result.code);close();return;}
#endif
    if(host->application_ready()&&!ready_logged) {ready_logged=true;log("application-ready");}
    if(smoke&&ready_logged)close();
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
int run(HINSTANCE instance,bool smoke) {
  wchar_t executable[32768]{},local[MAX_PATH]{};
  const auto size=GetModuleFileNameW(nullptr,executable,32768);
  if(!size||size>=32768||FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA|CSIDL_FLAG_CREATE,nullptr,0,local)))return 1;
  application app;app.smoke=smoke;app.root=std::filesystem::path(executable).parent_path();
  app.profile=std::filesystem::path(local)/"SouiReferenceWorkbench"/app.root.filename();
  if(smoke)app.profile/="smoke-"+std::to_string(GetCurrentProcessId());
  std::filesystem::create_directories(app.profile);
  app.business=std::make_shared<reference::business>(reference::read_fixture(app.root/"resources"/"reference-fixture.json"));
  app.bridge=std::make_unique<soui::sdk::json_bridge>("app://reference-workbench");
  if(!reference::register_business(*app.bridge,app.business))return 1;
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
    config.owner_window=window;config.resource_root=app.root/"resources";config.bridge_adapter=app.bridge.get();
#ifdef SOUI_REFERENCE_MODERN
    auto adapter=std::make_unique<soui::sdk::webview2_adapter>(window,app.profile/"browser");
    app.renderer=adapter.get();app.host=std::make_unique<soui::sdk::webview_host>(std::move(adapter));
    app.renderer->prepare_async([&app,config](auto result) {
      if(app.closing)return;
      if(result.ok)result=app.host->start(config);
      if(!result.ok){app.exit_code=3;app.log(result.code);
        if(!PostMessageW(app.window,WM_CLOSE,0,0))std::terminate();}
    });
#else
    config.miniblink_binary=app.root/"node.dll";
    auto adapter=std::make_unique<soui::sdk::dynamic_miniblink_adapter>();
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
#ifdef SOUI_REFERENCE_MODERN
  app.renderer=nullptr;
#endif
  app.host.reset();
  app.bridge.reset(); // The actual task_service destructor joins before runtime shutdown.
  app.log("bridge-join-complete");
#ifndef SOUI_REFERENCE_MODERN
  const auto shutdown=soui::sdk::shutdown_miniblink_runtime();
  if(!shutdown.ok){app.log(shutdown.code);app.exit_code=3;}
  else app.log("runtime-shutdown-complete");
#endif
  if(!UnregisterClassW(class_name,instance))app.exit_code=3;
  return app.exit_code;
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int) {
  int count{};auto** arguments=CommandLineToArgvW(GetCommandLineW(),&count);
  if(!arguments)return 1;
  const bool smoke=count==2&&std::wstring{arguments[1]}==L"--smoke-test";
  LocalFree(arguments);if(count!=1&&!smoke)return 2;
  const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  if(FAILED(initialized))return 1;
  int result=1;try{result=run(instance,smoke);}catch(...){}
  CoUninitialize();return result;
}
