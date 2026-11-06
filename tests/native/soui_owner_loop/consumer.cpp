#if defined(_MSC_VER) && !defined(__clang__)
#include <core/SSingleton.h>
namespace SOUI {
class SApplication;
class SNotifyCenter;
// Same real DLL data imports as soui_dependency_tests.cpp, before SApp.h.
// Do not define local singleton storage to hide a dependency ABI failure.
extern template class __declspec(dllimport) SSingleton<SApplication>;
extern template class __declspec(dllimport) SSingleton<SNotifyCenter>;
}
#endif

#include <soui/host/soui_owner_application.h>
#include <event/NotifyCenter.h>
#include <com-cfg.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class T>struct singleton_data:SOUI::SSingleton<T>{
  static T* get(){return SOUI::SSingleton<T>::ms_Singleton;}
};
constexpr UINT filtered_message=WM_APP+0x551,quit_message=WM_APP+0x552;
constexpr UINT start_nested=WM_APP+0x553,nested_filtered=WM_APP+0x554;

std::filesystem::path executable_directory(){
  std::wstring text(32768,L'\0');
  const auto size=GetModuleFileNameW(nullptr,text.data(),static_cast<DWORD>(text.size()));
  require(size&&size<text.size(),"executable directory");text.resize(size);
  return std::filesystem::path(text).parent_path();
}
struct com_scope{
  com_scope(){require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"UI STA initialization");}
  ~com_scope(){CoUninitialize();}
};
struct thread_windows{
  std::array<HWND,128> values{};std::size_t count{};bool overflow{};
  bool add(HWND window)noexcept{
    if(std::find(values.begin(),values.begin()+count,window)!=values.begin()+count)return true;
    if(count==values.size()){overflow=true;return false;}
    values[count++]=window;return true;
  }
  static BOOL CALLBACK collect(HWND window,LPARAM value){
    if(reinterpret_cast<thread_windows*>(value)->add(window))return TRUE;
    SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;
  }
  static thread_windows capture(){
    thread_windows result;const auto thread=GetCurrentThreadId();
    SetLastError(ERROR_SUCCESS);
    const auto enumerated=EnumThreadWindows(thread,collect,reinterpret_cast<LPARAM>(&result));
    const auto error=GetLastError();
    require(!result.overflow,"thread window enumeration overflow");
    // EnumThreadWindows also returns FALSE for a legitimate empty thread.
    // A partial/aborted result or an observed error is not an empty success.
    require(enumerated||(result.count==0&&error==ERROR_SUCCESS),"thread window enumeration error/incomplete result");
    HWND previous{};
    while(const auto window=FindWindowExW(HWND_MESSAGE,previous,nullptr,nullptr)){
      if(GetWindowThreadProcessId(window,nullptr)==thread)require(result.add(window),"message-only window bound");
      previous=window;
    }
    require(!result.overflow,"thread window bound");
    std::sort(result.values.begin(),result.values.begin()+result.count,std::less<HWND>{});return result;
  }
  bool operator==(const thread_windows& other)const noexcept{
    return count==other.count&&std::equal(values.begin(),values.begin()+count,other.values.begin());
  }
};
struct driver_state{
  std::function<void(UINT,WPARAM)> receive;
  SOUI::SMessageLoop* outer{};SOUI::SMessageLoop* nested{};
  std::exception_ptr failure;
};
LRESULT CALLBACK driver_proc(HWND window,UINT message,WPARAM w,LPARAM l){
  if(message==WM_NCCREATE){
    SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));return TRUE;
  }
  auto* state=reinterpret_cast<driver_state*>(GetWindowLongPtrW(window,GWLP_USERDATA));
  if(message==WM_NCDESTROY){SetWindowLongPtrW(window,GWLP_USERDATA,0);return DefWindowProcW(window,message,w,l);}
  if(state&&(message==filtered_message||message==quit_message||message==start_nested||message==nested_filtered)){
    try{require(static_cast<bool>(state->receive),"unexpected driver message");state->receive(message,w);}
    catch(...){
      state->failure=std::current_exception();
      if(state->nested&&state->nested->IsRunning())state->nested->Quit();
      if(state->outer&&state->outer->IsRunning())state->outer->Quit();
    }
    return 0;
  }
  return DefWindowProcW(window,message,w,l);
}
class driver_window{
  HWND window_{};HINSTANCE instance_=GetModuleHandleW(nullptr);
  bool registered_{};
  static constexpr wchar_t name[]=L"SouiOwnerLoopContractDriver";
 public:
  explicit driver_window(driver_state& state){
    WNDCLASSW type{};type.hInstance=instance_;type.lpfnWndProc=driver_proc;type.lpszClassName=name;
    require(RegisterClassW(&type)!=0,"driver class registration");
    registered_=true;
    window_=CreateWindowExW(0,name,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,instance_,&state);
    if(!window_){UnregisterClassW(name,instance_);throw std::runtime_error("message-only driver window");}
  }
  ~driver_window(){if(window_)DestroyWindow(window_);if(registered_)UnregisterClassW(name,instance_);}
  HWND get()const noexcept{return window_;}
  void close(){
    require(DestroyWindow(window_)!=FALSE,"driver window destruction");window_=nullptr;
    require(UnregisterClassW(name,instance_)!=FALSE,"driver class cleanup");registered_=false;
  }
};
struct filter final:SOUI::IMessageFilter{
  HWND window{};UINT message{};unsigned calls{};
  BOOL PreTranslateMessage(MSG* value)override{
    if(value->hwnd!=window||value->message!=message)return FALSE;
    ++calls;return TRUE;
  }
};
struct idle final:SOUI::IIdleHandler{
  std::function<void()> call;unsigned calls{};
  BOOL OnIdle()override{++calls;call();return FALSE;}
};
class loop_hooks{
  SOUI::SMessageLoop& loop_;filter& filter_;idle& idle_;
  bool attached_{true};
 public:
  loop_hooks(SOUI::SMessageLoop& loop,filter& f,idle& i):loop_(loop),filter_(f),idle_(i){
    require(loop_.AddMessageFilter(&filter_)!=FALSE,"register real SOUI filter");
    if(!loop_.AddIdleHandler(&idle_)){loop_.RemoveMessageFilter(&filter_);throw std::runtime_error("register real SOUI idle");}
  }
  ~loop_hooks(){if(attached_){loop_.RemoveIdleHandler(&idle_);loop_.RemoveMessageFilter(&filter_);}}
  void close(){
    require(loop_.RemoveIdleHandler(&idle_)!=FALSE,"remove real SOUI idle");
    require(loop_.RemoveMessageFilter(&filter_)!=FALSE,"remove real SOUI filter");attached_=false;
  }
};
void main_filter_idle_quit(SOUI::SMessageLoop& loop,driver_state& state,HWND window){
  filter f;f.window=window;f.message=filtered_message;
  idle i;i.call=[&]{require(i.calls==1,"business idle repeated without a new episode");
    require(PostMessageW(window,quit_message,0,0)!=FALSE,"post actual dispatch");};
  unsigned dispatched{};state.receive=[&](UINT message,WPARAM){
    require(message==quit_message,"filter failed to consume its message");++dispatched;PostQuitMessage(17);
  };
  loop_hooks hooks(loop,f,i);
  require(PostMessageW(window,filtered_message,0,0)!=FALSE,"post filter message");
  const auto code=loop.Run();if(state.failure)std::rethrow_exception(state.failure);
  require(code==17&&!loop.IsRunning(),"WM_QUIT result/running restoration");
  require(f.calls==1&&i.calls==1&&dispatched==1,"real filter/idle/dispatch path");
  hooks.close();state.receive={};std::puts("PASS main-filter-idle-wm-quit");
}
void factory_nested_quit(soui::sdk::soui_owner_application& app,driver_state& state,HWND window){
  auto* factory=app.GetMsgLoopFactory();auto* outer=app.GetMsgLoop();
  require(factory&&outer,"actual application factory/main loop");
  const auto destroy=[factory](SOUI::SMessageLoop* value){factory->DestoryMsgLoop(value);};
  std::unique_ptr<SOUI::SMessageLoop,decltype(destroy)> nested(factory->CreateMsgLoop(),destroy);
  require(nested&&nested.get()!=outer,"factory-created distinct nested loop");
  filter f;f.window=window;f.message=nested_filtered;
  idle i;i.call=[&]{require(i.calls==1&&outer->IsRunning()&&nested->IsRunning(),"real nested loop/idle state");nested->Quit();};
  loop_hooks hooks(*nested,f,i);int nested_code{};unsigned nested_entries{};
  state.receive=[&](UINT message,WPARAM){
    require(message==start_nested&&outer->IsRunning(),"outer dispatch did not enter nested loop");
    ++nested_entries;state.nested=nested.get();
    require(PostMessageW(window,nested_filtered,23,0)!=FALSE,"post nested filter message");
    nested_code=nested->Run();state.nested=nullptr;
    require(!nested->IsRunning()&&outer->IsRunning(),"nested quit damaged outer running state");
    outer->Quit();
  };
  require(PostMessageW(window,start_nested,31,0)!=FALSE,"post actual nested entry");
  const auto code=outer->Run();state.nested=nullptr;if(state.failure)std::rethrow_exception(state.failure);
  // Quit preserves SOUI's last retrieved wParam; it is not PostQuitMessage.
  require(code==31&&nested_code==23&&!outer->IsRunning(),"SOUI Quit/last-message return semantics");
  require(nested_entries==1&&f.calls==1&&i.calls==1,"factory nested filter/idle path");
  hooks.close();state.receive={};std::puts("PASS factory-nested-quit-and-outer-resume");
}
int run(){
  com_scope com;
  SComMgr components;const auto root=executable_directory();
  components.SetDllPath(SOUI::SStringT(root.wstring().c_str()));
  SOUI::CAutoRefPtr<SOUI::IRenderFactory> render;
  require(components.CreateRender_GDI(reinterpret_cast<IObjRef**>(&render))!=FALSE&&render,"normal render-gdi factory");
  require(!singleton_data<SOUI::SApplication>::get()&&!singleton_data<SOUI::SNotifyCenter>::get(),"real imported singleton initially nonempty");
  const auto before=thread_windows::capture();
  {
    soui::sdk::soui_owner_application app(render,GetModuleHandleW(nullptr),_T("SOUI_OWNER_LOOP_CONTRACT"));
    require(singleton_data<SOUI::SApplication>::get()==&app,"application did not use real imported singleton");
    require(app.owner_loop_status().ok,"normal SDK owner optimizer unavailable");
    driver_state state;state.outer=app.GetMsgLoop();require(state.outer!=nullptr,"actual SDK SOUI main loop");
    driver_window window(state);
    main_filter_idle_quit(*state.outer,state,window.get());
    factory_nested_quit(app,state,window.get());
    require(app.owner_loop_status().ok,"owner loop failed after normal nested use");
    window.close();
  }
  require(!singleton_data<SOUI::SApplication>::get()&&!singleton_data<SOUI::SNotifyCenter>::get(),"real imported singleton not released");
  require(thread_windows::capture()==before,"application left an observable thread window");
  std::puts("PASS real-app-singletons-and-window-cleanup");
  std::puts("SOUI owner application: 3 behavior cases passed; no SDK performance qualification");return 0;
}
}
int main(){try{return run();}catch(const std::exception& error){std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}catch(...){std::fputs("FAIL unexpected exception\n",stderr);return 1;}}
