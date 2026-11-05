#define NOMINMAX
#include <windows.h>
#include <soui/host/soui_miniblink_host.h>
#include <soui/json_bridge.h>
#include <soui/version.h>
#include <soui/diagnostics/input_frame_trace.h>
#include <soui/diagnostics/miniblink_trace_source.h>
#include "trace_capture.h"
#include "harness_options.h"
#include "profile_options.h"
#include "manual_report.h"
#include "manual-observation-gate.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string>

namespace trace=soui::sdk::diagnostics::input_frame_trace;
namespace manual_gate=soui::benchmark::manual_observation;
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
  std::string run,kind,trace_mode,observation_owner,observation_nonce;
  DWORD observation_controller_pid{};
  std::uint64_t observation_controller_creation{};
  std::uint64_t high{0},low{0};
  soui::benchmark::harness_options diagnostics;
  soui::benchmark::profile_options profile;
};
std::string ascii(const std::wstring& value) {
  std::string result;for(wchar_t c:value){require(c>=32&&c<=126,"ASCII option required");result+=static_cast<char>(c);}return result;
}
options parse(int argc,wchar_t** argv) {
  require(argc>=7&&argc<=14,"Expected frozen path/run/kind/trace options and explicit optional switches");
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
      string_option(L"--kind=",result.kind)||string_option(L"--trace=",result.trace_mode)||
      string_option(L"--manual-observation-owner=",result.observation_owner)||
      result.diagnostics.consume(argument)||result.profile.consume(argument),"Unknown harness option");
  }
  const auto page_html=result.profile.manual_input?L"manual-input-page.html":L"burst-page.html";
  const auto page_script=result.profile.manual_input?L"manual-input-page.js":L"burst-page.js";
  require(result.binary.is_absolute()&&result.page_root.is_absolute()&&result.output.is_absolute()&&
    std::filesystem::is_regular_file(result.binary)&&std::filesystem::is_directory(result.output)&&
    std::filesystem::is_regular_file(result.page_root/page_html)&&
    std::filesystem::is_regular_file(result.page_root/page_script),"Frozen harness inputs/output directory missing");
  require(result.run.size()==32&&(result.kind=="pointer_move"||result.kind=="wheel")&&
    (result.trace_mode=="full"||result.trace_mode=="e2e"),"Invalid run/kind/trace mode");
  for(std::size_t n=0;n<result.run.size();++n) {
    const char digit=result.run[n];require((digit>='0'&&digit<='9')||(digit>='a'&&digit<='f'),"Run id must be lowercase hex");
    auto& part=n<16?result.high:result.low;part=(part<<4)|static_cast<unsigned>(digit<='9'?digit-'0':digit-'a'+10);
  }
  require(result.high||result.low,"Zero run id is invalid");
  result.profile.validate(result.diagnostics.foreground);
  if(!result.observation_owner.empty()) {
    require(result.profile.manual_input,"Observation completion gate requires manual-input");
    const auto& text=result.observation_owner;
    require(text.size()<=64,"Observation owner tuple exceeds bound");
    const auto first=text.find(':'),second=first==std::string::npos?first:text.find(':',first+1);
    require(first!=std::string::npos&&second!=std::string::npos&&text.find(':',second+1)==std::string::npos,
      "Observation owner requires PID:creation:nonce");
    const auto decimal=[](const std::string& digits,std::uint64_t maximum) {
      require(!digits.empty()&&digits.size()<=20&&digits.front()!='0',"Invalid observation owner integer");
      std::uint64_t value=0;
      for(const char digit:digits){require(digit>='0'&&digit<='9',"Invalid observation owner integer");
        const auto part=static_cast<unsigned>(digit-'0');require(value<=(maximum-part)/10,"Observation owner integer overflow");value=value*10+part;}
      require(value!=0,"Zero observation owner integer");return value;
    };
    result.observation_controller_pid=static_cast<DWORD>(decimal(text.substr(0,first),MAXDWORD));
    result.observation_controller_creation=decimal(text.substr(first+1,second-first-1),(std::numeric_limits<std::uint64_t>::max)());
    result.observation_nonce=text.substr(second+1);require(manual_gate::hex32(result.observation_nonce),"Invalid observation owner nonce");
  }
  return result;
}
void write_new(const std::filesystem::path& path,const std::string& bytes) {
  require(bytes.size()<=65536,"Harness metadata bound exceeded");
  const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  require(file!=INVALID_HANDLE_VALUE,"Cannot create new harness metadata");DWORD written{};
  const bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(file);
  CloseHandle(file);require(ok,"Cannot persist harness metadata");
}
std::string compiled_sdk_identity() {
  // These literals come from the matched installed target's actual headers,
  // not from command-line metadata or files next to the running executable.
  return std::string("\"compiled_sdk_version\":\"")+SOUI_SDK_VERSION_STRING+
    "\",\"compiled_sdk_package_version\":\""+std::to_string(SOUI_SDK_VERSION_MAJOR)+"."+
    std::to_string(SOUI_SDK_VERSION_MINOR)+"."+std::to_string(SOUI_SDK_VERSION_PATCH)+"\"";
}
std::string observation_gate_metadata(const manual_gate::gate* gate) {
  if(!gate)return {};
  const auto& b=gate->identity();
  return ",\"manual_observation_gate\":"+nlohmann::json{{"mode","controller-terminated-v1"},
    {"eventName",std::string(gate->name().begin(),gate->name().end())},{"run_id",b.run},{"nonce",b.nonce},
    {"host_pid",b.host_pid},{"host_creation",std::to_string(b.host_creation)},{"hwnd",std::to_string(b.hwnd)},
    {"controller_pid",b.controller_pid},{"controller_creation",std::to_string(b.controller_creation)},
    {"expires_tick",std::to_string(b.expires_tick)},{"signalsQualification",false}}.dump();
}
std::string cold_profile(const soui::benchmark::profile_options& profile) {
  // This whitelist contains only this task's numeric/boolean controls. Unknown
  // values are classified, never copied into evidence as arbitrary strings.
  const auto read=[](const wchar_t* name) {
    wchar_t value[32]{};SetLastError(ERROR_SUCCESS);
    const auto count=GetEnvironmentVariableW(name,value,32);
    if(!count)return GetLastError()==ERROR_ENVVAR_NOT_FOUND?std::wstring{}:std::wstring(L"unrecognized");
    return count<32?std::wstring(value,count):std::wstring(L"unrecognized");
  };
  auto budget=read(L"SOUI_ENGINE_SHARED_TURN_BUDGET_US");
  if(profile.budget_present) {
    const auto requested=std::to_wstring(profile.budget_us);
    require(budget.empty()||budget==requested,"Cold engine budget differs from explicit profile");
    require(SetEnvironmentVariableW(L"SOUI_ENGINE_SHARED_TURN_BUDGET_US",requested.c_str())!=FALSE,"Cannot set child engine budget");
    budget=read(L"SOUI_ENGINE_SHARED_TURN_BUDGET_US");
    require(budget==requested,"Cold engine budget readback differs");
  }
  const auto budget_json=budget.empty()?"null":budget==L"0"?"\"0\"":budget==L"1000"?"\"1000\"":budget==L"2000"?"\"2000\"":"\"unrecognized\"";
  const auto boolean_json=[&](const wchar_t* name){const auto value=read(name);
    return value.empty()?"null":value==L"0"?"\"0\"":value==L"1"?"\"1\"":"\"unrecognized\"";};
  return std::string("{\"schema\":\"soui.input-ui-cold-profile/v1\",\"requested_budget_us\":")+
    (profile.budget_present?std::to_string(profile.budget_us):"null")+",\"observed_env\":"+budget_json+
    ",\"engine_effective_observed\":false,\"environment\":{\"SOUI_ENGINE_SHARED_TURN_BUDGET_US\":"+budget_json+
    ",\"SOUI_GPU_COMPOSITOR\":"+boolean_json(L"SOUI_GPU_COMPOSITOR")+
    ",\"SOUI_GPU_DIRECT_PRESENT\":"+boolean_json(L"SOUI_GPU_DIRECT_PRESENT")+"}}";
}
std::filesystem::path prepare_page(const options& settings) {
  const auto bundle=settings.output/L"page-bundle";
  require(std::filesystem::create_directory(bundle),"Page bundle already exists");
  const auto html=settings.profile.manual_input?L"manual-input-page.html":L"burst-page.html";
  const auto script=settings.profile.manual_input?L"manual-input-page.js":L"burst-page.js";
  require(CopyFileW((settings.page_root/html).c_str(),(bundle/html).c_str(),TRUE)&&
    CopyFileW((settings.page_root/html).c_str(),(bundle/L"index.html").c_str(),TRUE)&&
    CopyFileW((settings.page_root/script).c_str(),(bundle/script).c_str(),TRUE),"Cannot freeze measurement page bytes");
  // Public SDK's trusted entry remains index.html. Parameters are data in a
  // bounded seed, not a new navigation privilege or raw WKE loadURL bypass.
  const auto seed=settings.profile.manual_input?L"manual-input-config.js":L"burst-config.js";
  const auto global=settings.profile.manual_input?"souiManualInputConfig":"souiBurstConfig";
  write_new(bundle/seed,std::string("window.")+global+"={run:'"+settings.run+"',kind:'"+settings.kind+"'};\n");
  return bundle;
}
class harness_window final {
 public:
  harness_window(soui::sdk::soui_miniblink_host& host,const options& settings,const trace::identity& identity,bool capability,
      soui::benchmark::manual_report_collector* manual_report,const manual_gate::gate* observation_gate,
      trace::trace_source* kernel_source)
      :host_(host),settings_(settings),identity_(identity),kernel_source_(kernel_source),manual_report_(manual_report),
       observation_gate_(observation_gate),detailed_capability_(capability){}
  void request_close(){if(!closing_){if(manual_report_)manual_report_->close();closing_=true;close_started_=GetTickCount64();}}
  int exit_code()const noexcept{return exit_code_;}
  bool diagnostic_boundaries_ok()const noexcept{return !diagnostic_boundaries_failed_;}
  void diagnostic_checkpoint(soui::benchmark::diagnostic_scalar_phase phase)noexcept {
    if(!settings_.diagnostics.logging)return;
    try {
      const auto boundary=soui::benchmark::capture_scalar_boundary(identity_,trace::process_recorder(),kernel_source_);
      soui::benchmark::write_diagnostic_scalar_boundary(settings_.output,boundary,phase);
    }catch(...) {
      // A failed diagnostic must not recurse through WndProc's fail/close path
      // or skip normal Runtime shutdown. It still makes the final run fail.
      diagnostic_boundaries_failed_=true;
      std::fputs("Diagnostic scalar boundary persistence failed\n",stderr);
    }
  }
  ULONGLONG activity_expires_tick()const noexcept{return started_+150000;}
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
          if(self->settings_.diagnostics.logging&&!self->timer_logged_){std::cerr<<"stage ui.timer.first\n";self->timer_logged_=true;}
          if(!self->published_&&self->host_.document_ready()) {
            self->diagnostic_checkpoint(soui::benchmark::diagnostic_scalar_phase::document_ready);
            if(self->settings_.diagnostics.logging)std::cerr<<"stage document.ready\n";
            if(self->settings_.diagnostics.foreground) {
              const auto foreground_ready=SetForegroundWindow(window)!=FALSE;
              const auto active_ready=SetActiveWindow(window)!=nullptr;
              if(self->settings_.diagnostics.logging)std::cerr<<"stage host.foreground.ready hwnd="<<reinterpret_cast<unsigned long long>(window)
                <<" foreground="<<(foreground_ready?"true":"false")
                <<" active="<<(active_ready?"true":"false")<<'\n';
            }
            if(self->settings_.diagnostics.logging)std::cerr<<"stage host-ready.write.before\n";
            write_new(self->settings_.output/L"host-ready.json","{\"schema\":\"soui.input-ui-host-ready/v1\",\"run_id\":\""+
              self->settings_.run+"\",\"pid\":"+std::to_string(self->identity_.process_id)+
              ",\"creationFileTime\":\""+std::to_string(self->identity_.process_incarnation)+"\",\"hwnd\":\""+
              std::to_string(reinterpret_cast<std::uintptr_t>(window))+"\",\"qpc_frequency\":\""+
              std::to_string(self->identity_.qpc_frequency)+"\",\"stage_evidence\":\""+
              (self->detailed_capability_?"trace-capability-confirmed-links-unqualified":"unavailable")+
              "\",\"measurement_path\":\""+(self->settings_.diagnostics.diagnostic()?"diagnostic":"public-sdk")+
              "\",\"diagnostics\":"+self->settings_.diagnostics.json()+","+self->settings_.profile.input_json()+
              ","+compiled_sdk_identity()+(self->manual_report_?",\"manual_report_capability\":\"manual-page-snapshot-v1\"":"")+
              observation_gate_metadata(self->observation_gate_)+"}\n");
            if(self->settings_.diagnostics.logging)std::cerr<<"stage host-ready.write.after\n";
            self->published_=true;
          }
          if(GetTickCount64()-self->started_>150000){self->exit_code_=3;self->request_close();}
          return 0;
        case WM_CLOSE:self->request_close();return 0;
        case WM_SIZE:if(wparam!=SIZE_MINIMIZED)(void)self->host_.resize((std::max)(1,static_cast<int>(LOWORD(lparam))),
          (std::max)(1,static_cast<int>(HIWORD(lparam))));return 0;
        case WM_SETFOCUS:
          if(self->settings_.diagnostics.suppress_focus) {
            if(self->settings_.diagnostics.logging)std::cerr<<"stage host.focus.forward.suppressed\n";
          } else (void)self->host_.focus();
          return 0;
        case WM_DESTROY:PostQuitMessage(self->exit_code_);return 0;
        case WM_NCDESTROY:SetWindowLongPtrW(window,GWLP_USERDATA,0);break;
        default:break;
      }
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';self->fail();return 0;}
    return DefWindowProcW(window,message,wparam,lparam);
  }
 private:
  void close_step(HWND window) {
    if(!before_destroy_sampled_) {
      before_destroy_sampled_=true;
      diagnostic_checkpoint(soui::benchmark::diagnostic_scalar_phase::before_destroy);
    }
    const auto closed=host_.destroy();
    if(!closed.ok) {
      if((closed.code=="E_BUSY"||closed.code=="E_CURSOR_CLOSE_PENDING")&&GetTickCount64()-close_started_<20000)return;
      std::cerr<<closed.code<<": "<<closed.message<<'\n';exit_code_=2;KillTimer(window,1);PostQuitMessage(exit_code_);return;
    }
    if(!after_destroy_sampled_) {
      after_destroy_sampled_=true;
      diagnostic_checkpoint(soui::benchmark::diagnostic_scalar_phase::after_destroy);
    }
    KillTimer(window,1);DestroyWindow(window);
  }
  soui::sdk::soui_miniblink_host& host_;const options& settings_;trace::identity identity_;
  trace::trace_source* kernel_source_;
  soui::benchmark::manual_report_collector* manual_report_;
  const manual_gate::gate* observation_gate_;
  ULONGLONG started_{GetTickCount64()},close_started_{0};bool closing_{false},published_{false},timer_logged_{false};int exit_code_{0};
  bool before_destroy_sampled_{false},after_destroy_sampled_{false},diagnostic_boundaries_failed_{false};
  bool detailed_capability_{false};
};
int run(const options& settings) {
  const auto run_started=trace::now_qpc();require(run_started!=0,"Harness start clock unavailable");
  for(const auto* name:{L"SOUI_GPU_COMPOSITOR",L"SOUI_GPU_DIRECT_PRESENT",L"SOUI_ENTRY_FILE"}) {
    (void)SetEnvironmentVariableW(name,nullptr);SetLastError(ERROR_SUCCESS);
    const auto remaining=GetEnvironmentVariableW(name,nullptr,0);
    require(!remaining&&GetLastError()==ERROR_ENVVAR_NOT_FOUND,"Cannot establish the harness software/local-resource environment");
  }
  const auto cold=cold_profile(settings.profile); // Actual readback before kernel DLL/Runtime construction.
  write_new(settings.output/L"cold-profile.json",cold+"\n");
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
  const auto initial=soui::benchmark::capture_scalar_boundary(identity,trace::process_recorder(),kernel.get());
  const auto& sdk_capability=initial.sdk;
  const auto& kernel_capability=initial.kernel;
  const bool detailed_capability=sdk_capability.available&&kernel_capability.available&&
    sdk_capability.recording_mode==trace::mode::detailed&&kernel_capability.recording_mode==trace::mode::detailed;
  require(settings.trace_mode!="full"||detailed_capability,"Full capture requires confirmed detailed capabilities");
  if(settings.trace_mode=="e2e")soui::benchmark::write_scalar_initial(settings.output,initial,cold);
  const auto bundle=prepare_page(settings);
  std::unique_ptr<soui::benchmark::trace_capture> capture;
  if(settings.trace_mode=="full")capture=std::make_unique<soui::benchmark::trace_capture>(settings.output,trace::process_recorder(),*kernel);
  require(trace::register_current_thread(),"Harness UI trace registration failed");
  // This cold gate outlives the Bridge and every callback referencing it.
  manual_gate::gate observation_gate;
  const bool observation_gated=!settings.observation_owner.empty();
  soui::benchmark::manual_report_collector manual_report(settings.run,settings.kind);
  soui::sdk::json_bridge bridge("app://input-ui-benchmark");
  if(settings.profile.manual_input) {
    require(bridge.register_contextual_method("benchmark.manualInputReport","benchmark.manualInputReport",
      [&](const soui::runtime::bridge_request& request){
        if(!observation_gated)return manual_report.submit_request(request.params_json);
        const auto checked=observation_gate.check_now();
        if(checked.status==manual_gate::state::pending)
          return nlohmann::json{{"accepted",false},{"persisted",false},{"submission","pending"},{"retryable",true}}.dump();
        if(checked.status!=manual_gate::state::released)
          return nlohmann::json{{"accepted",false},{"persisted",false},{"submission","unavailable"},{"retryable",false},
            {"code",checked.code},{"win32_error",checked.win32_error}}.dump();
        auto response=nlohmann::json::parse(manual_report.submit_request(request.params_json));
        response["observationEnded"]=true;return response.dump();
      }),"Manual report registration failed");
    require(bridge.set_session_capabilities({"benchmark.manualInputReport"}),"Manual report grant failed");
  }
  soui::sdk::soui_miniblink_host host(std::make_unique<soui::sdk::dynamic_miniblink_adapter>());
  harness_window window(host,settings,identity,detailed_capability,settings.profile.manual_input?&manual_report:nullptr,
    observation_gated?&observation_gate:nullptr,kernel.get());
  WNDCLASSEXW description{};description.cbSize=sizeof(description);description.hInstance=GetModuleHandleW(nullptr);
  description.lpfnWndProc=harness_window::procedure;description.lpszClassName=window_class;description.hCursor=LoadCursorW(nullptr,IDC_ARROW);
  require(RegisterClassExW(&description)!=0,"Harness window class unavailable");
  RECT bounds{0,0,1100,720};
  // Retain the legacy VM diagnostic without changing the formal host path.
  const auto style=(settings.diagnostics.borderless?WS_POPUP:WS_OVERLAPPEDWINDOW)|WS_CLIPCHILDREN;
  require(AdjustWindowRectEx(&bounds,style,FALSE,0),"Harness sizing failed");
  const auto handle=CreateWindowExW(0,window_class,L"SOUI input to visible content",style,
    80,80,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,GetModuleHandleW(nullptr),&window);
  require(handle!=nullptr,"Harness window unavailable");
  if(observation_gated) {
    DWORD owner_pid{};
    require(GetWindowThreadProcessId(handle,&owner_pid)==GetCurrentThreadId()&&owner_pid==identity.process_id,
      "Observation gate requires this Host's own HWND");
    manual_gate::binding bound;bound.run=settings.run;bound.nonce=settings.observation_nonce;
    bound.host_pid=identity.process_id;bound.host_creation=identity.process_incarnation;
    bound.hwnd=reinterpret_cast<std::uintptr_t>(handle);bound.controller_pid=settings.observation_controller_pid;
    bound.controller_creation=settings.observation_controller_creation;bound.expires_tick=window.activity_expires_tick();
    const auto created=observation_gate.create(bound);
    if(!created.ok){DestroyWindow(handle);throw std::runtime_error(std::string(created.code)+":"+
      std::to_string(created.win32_error)+" cleanup:"+std::to_string(created.cleanup_error));}
  }
  RECT content{};require(GetClientRect(handle,&content),"Harness content geometry unavailable");
  const auto log_geometry=[&](const char* stage) {
    if(!settings.diagnostics.logging)return;
    RECT outer{};GetWindowRect(handle,&outer);
    std::cerr<<"geometry "<<stage<<" screen="<<GetSystemMetrics(SM_CXSCREEN)<<"x"<<GetSystemMetrics(SM_CYSCREEN)
      <<" work="<<GetSystemMetrics(SM_CXFULLSCREEN)<<"x"<<GetSystemMetrics(SM_CYFULLSCREEN)
      <<" outer="<<(outer.right-outer.left)<<"x"<<(outer.bottom-outer.top)
      <<" client="<<content.right<<"x"<<content.bottom<<" style="<<std::hex
      <<static_cast<unsigned long long>(GetWindowLongPtrW(handle,GWL_STYLE))<<std::dec<<'\n';
  };
  log_geometry("initial");
  // Windows theme/DPI metrics can round the non-client frame differently on
  // the clean VM. Re-measure after each outer-frame correction; the bounded
  // loop keeps the frozen workload in physical 1100x720 pixels without
  // weakening the final contract.
  for(unsigned attempt=0;attempt<4&&(content.right!=1100||content.bottom!=720);++attempt) {
    RECT outer{};require(GetWindowRect(handle,&outer),"Harness outer geometry unavailable");
    const int width=(outer.right-outer.left)+(1100-content.right);
    const int height=(outer.bottom-outer.top)+(720-content.bottom);
    require(SetWindowPos(handle,nullptr,outer.left,outer.top,width,height,SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,
      "Harness geometry correction failed");
    require(GetClientRect(handle,&content),"Harness content geometry unavailable after correction");
    log_geometry("corrected");
  }
  require(content.right==1100&&content.bottom==720,
    "Harness content geometry differs from frozen workload after correction");
  soui::sdk::host_config config;
#ifdef _WIN64
  config.architecture="x64";
#else
  config.architecture="x86";
#endif
  config.miniblink_binary=settings.binary;config.resource_root=bundle;config.owner_window=handle;config.bridge_adapter=&bridge;
  if(settings.diagnostics.logging)std::cerr<<"stage host.start.before\n";
  window.diagnostic_checkpoint(soui::benchmark::diagnostic_scalar_phase::before_start);
  const auto started=host.start(config);
  if(settings.diagnostics.logging)std::cerr<<"stage host.start.after ok="<<(started.ok?"true":"false")<<"\n";
  require(started.ok,"Public SDK Host failed to start");
  window.diagnostic_checkpoint(soui::benchmark::diagnostic_scalar_phase::after_start);
  settings.profile.cursor_setup([&]{POINT setup{90,60};
    require(ClientToScreen(handle,&setup)&&SetCursorPos(setup.x,setup.y),"Harness initial cursor placement failed");});
  ShowWindow(handle,settings.profile.manual_input?SW_SHOWNOACTIVATE:SW_SHOW);UpdateWindow(handle);
  if(settings.diagnostics.foreground) {
    const auto foreground_self=SetForegroundWindow(handle)!=FALSE;
    const auto active_self=SetActiveWindow(handle)!=nullptr;
    if(settings.diagnostics.logging)std::cerr<<"stage host.foreground.self hwnd="<<reinterpret_cast<unsigned long long>(handle)
      <<" foreground="<<(foreground_self?"true":"false")
      <<" active="<<(active_self?"true":"false")<<'\n';
  }
  require(SetTimer(handle,1,16,nullptr)!=0,"Harness owner pump timer unavailable");
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
  window.diagnostic_checkpoint(soui::benchmark::diagnostic_scalar_phase::after_runtime_shutdown);
  trace::detach_current_thread();trace::process_recorder().stop();
  const bool backend_stopped=!kernel||kernel->stop();
  const bool trace_complete=!capture||capture->finish(window.remaining_close_budget());
  soui::benchmark::scalar_export_result scalar;
  if(settings.trace_mode=="e2e")scalar=soui::benchmark::finish_scalar_capture(settings.output,initial,
    trace::process_recorder(),kernel.get(),window.remaining_close_budget());
  std::string manual_status;
  if(settings.profile.manual_input) {
    require(backend_stopped&&trace_complete&&trace::process_recorder().snapshot().quiescent&&
      (!kernel||kernel->snapshot().quiescent),"Manual report requires stopped, quiescent trace producers");
    const auto saved=soui::benchmark::persist_manual_report(manual_report,identity,settings.run,
      [&]{return window.remaining_close_budget();},[&](const std::string& bytes){write_new(settings.output/L"manual-input-result.json",bytes);});
    manual_status=",\"manual_report_capability\":\"manual-page-snapshot-v1\",\"manual_report_submission\":"+
      saved.at("submission").dump()+",\"manual_report_persisted\":"+saved.at("persisted").dump();
  }
  if(observation_gated) {
    // Runtime shutdown above has quiesced all request callbacks. Never close
    // these handles in request_close or concurrently with check_now.
    const auto closed_gate=observation_gate.close_checked();
    if(!closed_gate.ok)throw std::runtime_error(std::string(closed_gate.code)+":"+
      std::to_string(closed_gate.win32_error)+" cleanup:"+std::to_string(closed_gate.cleanup_error));
  }
  const auto run_ended=trace::now_qpc();require(run_ended>=run_started,"Harness end clock unavailable");
  write_new(settings.output/L"host-result.json","{\"schema\":\"soui.input-ui-host-result/v1\",\"run_id\":\""+settings.run+
    "\",\"process_id\":"+std::to_string(identity.process_id)+",\"process_incarnation\":\""+std::to_string(identity.process_incarnation)+
    "\",\"started_qpc\":\""+std::to_string(run_started)+"\",\"ended_qpc\":\""+std::to_string(run_ended)+
    "\",\"normal_runtime_close\":true,\"trace_complete\":"+std::string(settings.trace_mode=="full"&&trace_complete&&backend_stopped?"true":"false")+
    ",\"trace_capability_confirmed\":"+std::string(detailed_capability?"true":"false")+
    ",\"stage_evidence\":\""+(detailed_capability&&trace_complete?"records-exported-links-unqualified":"unavailable")+
    "\",\"measurement_path\":\""+(settings.diagnostics.diagnostic()?"diagnostic":"public-sdk")+
    "\",\"diagnostics\":"+settings.diagnostics.json()+","+settings.profile.input_json()+","+compiled_sdk_identity()+
    ",\"scalar_io_complete\":"+(settings.trace_mode=="e2e"&&scalar.io_complete?"true":"false")+
    ",\"scalar_usable\":"+(settings.trace_mode=="e2e"&&scalar.usable?"true":"false")+
    manual_status+",\"manual_observation_gate_enabled\":"+(observation_gated?"true":"false")+
    ",\"manual_observation_gate_checked_close\":"+(observation_gated?"true":"null")+
    ",\"diagnostic_scalar_boundaries_ok\":"+(settings.diagnostics.logging?(window.diagnostic_boundaries_ok()?"true":"false"):"null")+
    ",\"performanceGate\":false,\"releaseReady\":false}\n");
  if(!backend_stopped||!trace_complete||seen_capture_failure||!window.diagnostic_boundaries_ok()||
    (settings.trace_mode=="e2e"&&!scalar.ok)||window.remaining_close_budget().count()==0)return 2;
  return code?code:window.exit_code();
}
}
int wmain(int argc,wchar_t** argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetProcessDPIAware();
  const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(com))return 1;
  int code=1;try{code=run(parse(argc,argv));}catch(const std::exception& error){std::cerr<<error.what()<<'\n';}
  CoUninitialize();return code;
}
