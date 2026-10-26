#define NOMINMAX
#include <windows.h>
#include <soui/host/soui_miniblink_host.h>
#include <soui/json_bridge.h>
#include <soui/diagnostics/input_frame_trace.h>
#include <soui/diagnostics/miniblink_trace_source.h>
#include "trace_capture.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace trace=soui::sdk::diagnostics::input_frame_trace;
namespace {
constexpr wchar_t window_class[]=L"SouiInputUiHostV1";
struct scoped_handle final {
  explicit scoped_handle(HANDLE handle):value(handle){}~scoped_handle(){if(value)CloseHandle(value);}
  scoped_handle(const scoped_handle&)=delete;scoped_handle& operator=(const scoped_handle&)=delete;
  HANDLE value;
};
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct options final {
  std::filesystem::path binary,page_root,output;
  std::string run,kind,trace_mode;
  std::uint64_t high{0},low{0};
};
std::string ascii(const std::wstring& value) {
  std::string result;for(wchar_t c:value){require(c>=32&&c<=126,"ASCII option required");result+=static_cast<char>(c);}return result;
}
options parse(int argc,wchar_t** argv) {
  require(argc==7,"Expected --miniblink=PATH --page-root=PATH --result-dir=PATH --run=HEX --kind=pointer_move|wheel --trace=full|e2e");
  options result;
  for(int n=1;n<argc;++n) {
    const std::wstring argument(argv[n]);
    auto path_option=[&](const wchar_t* name,std::filesystem::path& field){
      const std::wstring prefix(name);if(argument.rfind(prefix,0)!=0)return false;
      require(field.empty()&&argument.size()>prefix.size(),"Duplicate/missing path option");field=argument.substr(prefix.size());return true;
    };
    auto string_option=[&](const wchar_t* name,std::string& field){
      const std::wstring prefix(name);if(argument.rfind(prefix,0)!=0)return false;
      require(field.empty()&&argument.size()>prefix.size(),"Duplicate/missing string option");field=ascii(argument.substr(prefix.size()));return true;
    };
    require(path_option(L"--miniblink=",result.binary)||path_option(L"--page-root=",result.page_root)||
      path_option(L"--result-dir=",result.output)||string_option(L"--run=",result.run)||
      string_option(L"--kind=",result.kind)||string_option(L"--trace=",result.trace_mode),"Unknown harness option");
  }
  require(result.binary.is_absolute()&&result.page_root.is_absolute()&&result.output.is_absolute()&&
    std::filesystem::is_regular_file(result.binary)&&std::filesystem::is_directory(result.output)&&
    std::filesystem::is_regular_file(result.page_root/L"burst-page.html")&&
    std::filesystem::is_regular_file(result.page_root/L"burst-page.js"),"Frozen harness inputs/output directory missing");
  require(result.run.size()==32&&(result.kind=="pointer_move"||result.kind=="wheel")&&
    (result.trace_mode=="full"||result.trace_mode=="e2e"),"Invalid run/kind/trace mode");
  for(std::size_t n=0;n<result.run.size();++n) {
    const char digit=result.run[n];require((digit>='0'&&digit<='9')||(digit>='a'&&digit<='f'),"Run id must be lowercase hex");
    auto& part=n<16?result.high:result.low;part=(part<<4)|static_cast<unsigned>(digit<='9'?digit-'0':digit-'a'+10);
  }
  require(result.high||result.low,"Zero run id is invalid");return result;
}
void write_new(const std::filesystem::path& path,const std::string& bytes) {
  require(bytes.size()<=65536,"Harness metadata bound exceeded");
  const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  require(file!=INVALID_HANDLE_VALUE,"Cannot create new harness metadata");DWORD written{};
  const bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(file);
  CloseHandle(file);require(ok,"Cannot persist harness metadata");
}
std::filesystem::path prepare_page(const options& settings) {
  const auto bundle=settings.output/L"page-bundle";
  require(std::filesystem::create_directory(bundle),"Page bundle already exists");
  require(CopyFileW((settings.page_root/L"burst-page.html").c_str(),(bundle/L"burst-page.html").c_str(),TRUE)&&
    CopyFileW((settings.page_root/L"burst-page.html").c_str(),(bundle/L"index.html").c_str(),TRUE)&&
    CopyFileW((settings.page_root/L"burst-page.js").c_str(),(bundle/L"burst-page.js").c_str(),TRUE),"Cannot freeze burst page bytes");
  // Public SDK's trusted entry remains index.html. Parameters are data in a
  // bounded seed, not a new navigation privilege or raw WKE loadURL bypass.
  write_new(bundle/L"burst-config.js","window.souiBurstConfig={run:'"+settings.run+"',kind:'"+settings.kind+"'};\n");
  return bundle;
}
class harness_window final {
 public:
  harness_window(soui::sdk::soui_miniblink_host& host,const options& settings,const trace::identity& identity,bool capability)
      :host_(host),settings_(settings),identity_(identity),detailed_capability_(capability){}
  void request_close(){if(!closing_){closing_=true;close_started_=GetTickCount64();}}
  int exit_code()const noexcept{return exit_code_;}
  std::chrono::milliseconds remaining_close_budget() const noexcept {
    const auto elapsed=closing_?GetTickCount64()-close_started_:0;
    return std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(elapsed>=20000?0:20000-elapsed));
  }
  void fail()noexcept{exit_code_=2;request_close();}
  static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    auto* self=reinterpret_cast<harness_window*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<harness_window*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
      SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(window,message,wparam,lparam);
    if(message==soui::sdk::bridge_event_message){self->host_.deliver_posted_event(reinterpret_cast<void*>(lparam));return 0;}
    try {
      switch(message) {
        case WM_TIMER:
          if(self->closing_){self->close_step(window);return 0;}
          self->host_.pump();
          if(!self->published_&&self->host_.document_ready()) {
            write_new(self->settings_.output/L"host-ready.json","{\"schema\":\"soui.input-ui-host-ready/v1\",\"run_id\":\""+
              self->settings_.run+"\",\"pid\":"+std::to_string(self->identity_.process_id)+
              ",\"creationFileTime\":\""+std::to_string(self->identity_.process_incarnation)+"\",\"hwnd\":\""+
              std::to_string(reinterpret_cast<std::uintptr_t>(window))+"\",\"qpc_frequency\":\""+
              std::to_string(self->identity_.qpc_frequency)+"\",\"stage_evidence\":\""+
              (self->detailed_capability_?"trace-capability-confirmed-links-unqualified":"unavailable")+"\"}\n");
            self->published_=true;
          }
          if(GetTickCount64()-self->started_>150000){self->exit_code_=3;self->request_close();}
          return 0;
        case WM_CLOSE:self->request_close();return 0;
        case WM_SIZE:if(wparam!=SIZE_MINIMIZED)(void)self->host_.resize((std::max)(1,static_cast<int>(LOWORD(lparam))),
          (std::max)(1,static_cast<int>(HIWORD(lparam))));return 0;
        case WM_SETFOCUS:(void)self->host_.focus();return 0;
        case WM_DESTROY:PostQuitMessage(self->exit_code_);return 0;
        case WM_NCDESTROY:SetWindowLongPtrW(window,GWLP_USERDATA,0);break;
        default:break;
      }
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';self->fail();return 0;}
    return DefWindowProcW(window,message,wparam,lparam);
  }
 private:
  void close_step(HWND window) {
    const auto closed=host_.destroy();
    if(!closed.ok) {
      if((closed.code=="E_BUSY"||closed.code=="E_CURSOR_CLOSE_PENDING")&&GetTickCount64()-close_started_<20000)return;
      std::cerr<<closed.code<<": "<<closed.message<<'\n';exit_code_=2;KillTimer(window,1);PostQuitMessage(exit_code_);return;
    }
    KillTimer(window,1);DestroyWindow(window);
  }
  soui::sdk::soui_miniblink_host& host_;const options& settings_;trace::identity identity_;
  ULONGLONG started_{GetTickCount64()},close_started_{0};bool closing_{false},published_{false};int exit_code_{0};
  bool detailed_capability_{false};
};
int run(const options& settings) {
  const auto run_started=trace::now_qpc();require(run_started!=0,"Harness start clock unavailable");
  SetEnvironmentVariableW(L"SOUI_GPU_COMPOSITOR",nullptr);SetEnvironmentVariableW(L"SOUI_GPU_DIRECT_PRESENT",nullptr);
  SetEnvironmentVariableW(L"SOUI_ENTRY_FILE",nullptr);
  const auto identity=trace::current_identity(settings.high,settings.low);
  require(identity.process_id&&identity.process_incarnation&&identity.qpc_frequency,"Harness QPC/process identity unavailable");
  const auto requested=settings.trace_mode=="full"?trace::mode::detailed:trace::mode::scalar;
  const auto close_name=L"Local\\SOUI-InputUi-Finish-"+std::to_wstring(identity.process_id)+L"-"+
    std::to_wstring(identity.process_incarnation)+L"-"+std::wstring(settings.run.begin(),settings.run.end());
  scoped_handle finish_event(CreateEventW(nullptr,FALSE,FALSE,close_name.c_str()));
  require(finish_event.value&&GetLastError()!=ERROR_ALREADY_EXISTS,"Harness close event already exists/unavailable");
  require(trace::process_recorder().configure(identity,requested),"SDK trace cold configuration rejected");
  trace::backend_status backend{};
  auto kernel=trace::miniblink_trace_source::load(settings.binary.c_str(),identity,requested,backend);
  require(kernel||(settings.trace_mode=="e2e"&&backend==trace::backend_status::api_unavailable),
    "Trace mode requires a valid kernel or explicit legacy E2E-only capability absence");
  const auto sdk_capability=trace::process_recorder().snapshot();
  const auto kernel_capability=kernel?kernel->snapshot():trace::scalar_snapshot{};
  const bool detailed_capability=sdk_capability.available&&kernel_capability.available&&
    sdk_capability.recording_mode==trace::mode::detailed&&kernel_capability.recording_mode==trace::mode::detailed;
  require(settings.trace_mode!="full"||detailed_capability,"Full capture requires confirmed detailed capabilities");
  const auto bundle=prepare_page(settings);
  std::unique_ptr<soui::benchmark::trace_capture> capture;
  if(settings.trace_mode=="full")capture=std::make_unique<soui::benchmark::trace_capture>(settings.output,trace::process_recorder(),*kernel);
  require(trace::register_current_thread(),"Harness UI trace registration failed");
  soui::sdk::json_bridge bridge("app://input-ui-benchmark");
  soui::sdk::soui_miniblink_host host(std::make_unique<soui::sdk::dynamic_miniblink_adapter>());
  harness_window window(host,settings,identity,detailed_capability);
  WNDCLASSEXW description{};description.cbSize=sizeof(description);description.hInstance=GetModuleHandleW(nullptr);
  description.lpfnWndProc=harness_window::procedure;description.lpszClassName=window_class;description.hCursor=LoadCursorW(nullptr,IDC_ARROW);
  require(RegisterClassExW(&description)!=0,"Harness window class unavailable");
  RECT bounds{0,0,1100,720};require(AdjustWindowRectEx(&bounds,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,FALSE,0),"Harness sizing failed");
  const auto handle=CreateWindowExW(0,window_class,L"SOUI input to visible content",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
    80,80,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,GetModuleHandleW(nullptr),&window);
  require(handle!=nullptr,"Harness window unavailable");
  RECT content{};require(GetClientRect(handle,&content)&&content.right==1100&&content.bottom==720,
    "Harness content geometry differs from frozen workload");
  soui::sdk::host_config config;
#ifdef _WIN64
  config.architecture="x64";
#else
  config.architecture="x86";
#endif
  config.miniblink_binary=settings.binary;config.resource_root=bundle;config.owner_window=handle;config.bridge_adapter=&bridge;
  const auto started=host.start(config);require(started.ok,"Public SDK Host failed to start");
  POINT setup{90,60};require(ClientToScreen(handle,&setup)&&SetCursorPos(setup.x,setup.y),"Harness initial cursor placement failed");
  ShowWindow(handle,SW_SHOW);UpdateWindow(handle);require(SetTimer(handle,1,16,nullptr)!=0,"Harness owner pump timer unavailable");
  bool quit=false,seen_capture_failure=false;int code=0;
  while(!quit) {
    HANDLE waits[2]{finish_event.value,capture?capture->failure_event():nullptr};
    const DWORD count=capture&&!seen_capture_failure?2U:1U;
    const auto waited=MsgWaitForMultipleObjectsEx(count,waits,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    require(waited!=WAIT_FAILED,"Harness message wait failed");
    if(waited==WAIT_OBJECT_0)window.request_close();
    if(count==2&&waited==WAIT_OBJECT_0+1){seen_capture_failure=true;window.fail();}
    MSG message{};
    for(unsigned batch=0;batch<32&&PeekMessageW(&message,nullptr,0,0,PM_REMOVE);++batch) {
      if(message.message==WM_QUIT){quit=true;code=static_cast<int>(message.wParam);break;}
      TranslateMessage(&message);DispatchMessageW(&message);
    }
  }
  // Last Host unmount may pause its shared UI registration. Resume the SAME
  // cold TLS cookie while the public final-shutdown helper pumps late UI work.
  require(trace::register_current_thread(),"Harness final UI trace registration failed");
  const auto closed=soui::sdk::wait_for_miniblink_runtime_shutdown(window.remaining_close_budget());
  require(closed.ok,"Public SDK runtime did not close normally");
  trace::detach_current_thread();trace::process_recorder().stop();
  const bool backend_stopped=!kernel||kernel->stop();
  const bool trace_complete=!capture||capture->finish(window.remaining_close_budget());
  const auto run_ended=trace::now_qpc();require(run_ended>=run_started,"Harness end clock unavailable");
  write_new(settings.output/L"host-result.json","{\"schema\":\"soui.input-ui-host-result/v1\",\"run_id\":\""+settings.run+
    "\",\"process_id\":"+std::to_string(identity.process_id)+",\"process_incarnation\":\""+std::to_string(identity.process_incarnation)+
    "\",\"started_qpc\":\""+std::to_string(run_started)+"\",\"ended_qpc\":\""+std::to_string(run_ended)+
    "\",\"normal_runtime_close\":true,\"trace_complete\":"+std::string(settings.trace_mode=="full"&&trace_complete&&backend_stopped?"true":"false")+
    ",\"trace_capability_confirmed\":"+std::string(detailed_capability?"true":"false")+
    ",\"stage_evidence\":\""+(detailed_capability&&trace_complete?"records-exported-links-unqualified":"unavailable")+"\",\"performanceGate\":false,\"releaseReady\":false}\n");
  if(!backend_stopped||!trace_complete||seen_capture_failure||window.remaining_close_budget().count()==0)return 2;
  return code?code:window.exit_code();
}
}
int wmain(int argc,wchar_t** argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetProcessDPIAware();
  const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(com))return 1;
  int code=1;try{code=run(parse(argc,argv));}catch(const std::exception& error){std::cerr<<error.what()<<'\n';}
  CoUninitialize();return code;
}
