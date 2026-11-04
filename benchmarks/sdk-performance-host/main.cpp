#include "support.h"
#include "scalar_capture.h"
#include "native_phase_capture.h"
#include <soui/version.h>
#include <soui/host/soui_miniblink_host.h>
#include <soui/host/webview2_host.h>
#include <soui/host/frame_pacer.h>
#include <soui/json_bridge.h>
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <vector>

namespace {
using namespace perf;
namespace sdk=soui::sdk;
using namespace std::chrono_literals;
constexpr wchar_t window_class[]=L"SouiSdkPerformanceHostV1";
#ifdef _WIN64
constexpr const char* architecture="x64";
#else
constexpr const char* architecture="x86";
#endif
struct options final {
  std::string backend;fs::path page,output,miniblink,fixed_runtime;
  unsigned budget{},frame_rate{60};bool self_test{},input_trace{};sdk::diagnostics::mode observation{sdk::diagnostics::mode::disabled};
};
std::string ascii(const std::wstring& text){std::string result;for(auto c:text){require(c>=32&&c<=126,"ASCII option expected");result+=static_cast<char>(c);}return result;}
options parse(int argc,wchar_t** argv){
  options result;std::map<std::wstring,bool> seen;
  for(int i=1;i<argc;++i){const std::wstring arg=argv[i];
    if(arg==L"--self-test"){require(!result.self_test,"Duplicate self-test");result.self_test=true;continue;}
    const auto split=arg.find(L'=');require(split!=std::wstring::npos,"Expected --name=value");
    const auto name=arg.substr(0,split),value=arg.substr(split+1);require(!value.empty()&&!seen[name],"Missing or duplicate option");seen[name]=true;
    if(name==L"--backend")result.backend=ascii(value);
    else if(name==L"--page-root")result.page=value;
    else if(name==L"--result-dir")result.output=value;
    else if(name==L"--miniblink")result.miniblink=value;
    else if(name==L"--fixed-webview2")result.fixed_runtime=value;
    else if(name==L"--engine-budget"){
      require(value==L"0"||value==L"1000"||value==L"2000","Engine budget must be 0, 1000, or 2000");result.budget=static_cast<unsigned>(std::stoul(value));
    }else if(name==L"--observation"){
      require(value==L"off"||value==L"scalar","Observation must be off or scalar");result.observation=value==L"scalar"?sdk::diagnostics::mode::scalar:sdk::diagnostics::mode::disabled;
    }else if(name==L"--frame-rate"){
      require(value.size()<=4&&value.find_first_not_of(L"0123456789")==std::wstring::npos,"Decimal frame target required");
      result.frame_rate=static_cast<unsigned>(std::stoul(value));require(sdk::is_supported_frame_rate(result.frame_rate),"Frame target must be 30..1000");
    }else if(name==L"--input-trace"){
      require(value==L"off"||value==L"scalar","Input trace must be off or scalar");result.input_trace=value==L"scalar";
    }else throw std::runtime_error("Unknown option");
  }
  require(result.output.is_absolute()&&!fs::exists(result.output)&&fs::is_directory(result.output.parent_path()),"Result directory must be a fresh absolute path with existing parent");
  if(result.self_test)return result;
  require((result.backend=="miniblink"||result.backend=="webview2")&&result.page.is_absolute()&&fs::is_directory(result.page),"Explicit backend and absolute page root required");
  for(const auto* file:{L"index.html",L"benchmark.js",L"benchmark.css"})require(fs::is_regular_file(result.page/file),"Page bundle incomplete");
  if(result.backend=="miniblink")require(result.miniblink.is_absolute()&&fs::is_regular_file(result.miniblink),"Explicit absolute MiniBlink DLL required");
  if(!result.fixed_runtime.empty())require(result.backend=="webview2"&&result.fixed_runtime.is_absolute()&&fs::is_directory(result.fixed_runtime),"Fixed WebView2 runtime directory invalid");
  return result;
}
std::uint64_t filetime(FILETIME value){return (std::uint64_t(value.dwHighDateTime)<<32)|value.dwLowDateTime;}
json process_snapshot(){
  FILETIME created{},exited{},kernel{},user{};require(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user),"Process identity unavailable");
  PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
  const bool available=GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))!=FALSE;
  return {{"pid",GetCurrentProcessId()},{"creationFileTime",std::to_string(filetime(created))},{"cpu100ns",filetime(kernel)+filetime(user)},
    {"memoryAvailable",available},{"privateBytes",available?memory.PrivateUsage:0},{"workingSetBytes",available?memory.WorkingSetSize:0},
    {"memoryScope","native host process only; renderer tree not included"},{"uptimeMs",GetTickCount64()}};
}
json environment_snapshot(){
  json result=json::object();
  for(const auto* key:{"SOUI_ENGINE_SHARED_TURN_BUDGET_US","SOUI_GPU_COMPOSITOR","SOUI_GPU_DIRECT_PRESENT","SOUI_CPU_DRAW_INTERVAL_MS",
      "SOUI_DISPLAY_REFRESH_OVERRIDE","SOUI_COMPOSITOR_FRAME_RATE","SOUI_SOURCE_MAIN_FRAME_SCHEDULER","SOUI_DIAGNOSTIC_SCRIPT","SOUI_PAINT_CALLBACK_OTHER_THREAD",
      "SOUI_WEBVIEW2_PREPARE_OUTPUT","SOUI_WEBVIEW2_OWNER_BUDGET_US","SOUI_WEBVIEW2_OUTPUT_TIMING","SOUI_DIAGNOSTIC_BRIDGE_PHASES"}){
    std::array<char,256> value{};const auto size=GetEnvironmentVariableA(key,value.data(),static_cast<DWORD>(value.size()));
    result[key]=size>=value.size()?json("unavailable: exceeds 255 bytes"):size?json(std::string(value.data(),size)):json(nullptr);
  }return result;
}
class application;
struct slot;
class benchmark_bridge final:public sdk::bridge_adapter {
 public:
  explicit benchmark_bridge(std::string origin,sdk::diagnostics::mode mode,unsigned rate):inner_(std::move(origin),mode),active_rate_(rate),requested_rate_(rate){}
  sdk::json_bridge& methods(){return inner_;}
  std::string handle_json(std::string_view text,bool gesture=false)override{dispatch_gate::scope ingress(dispatch_);return inner_.handle_json(text,gesture);}
  void shutdown_session(const std::string& session)override{
    dispatch_gate::revocation pending(dispatch_);inner_.shutdown_session(session);pending.complete();
  }
  void set_event_sink(event_sink sink)override{inner_.set_event_sink(std::move(sink));}
  void set_owner_window(void* owner)noexcept override{inner_.set_owner_window(owner);}
  unsigned active_frame_rate()const noexcept override{return active_rate_.load(std::memory_order_relaxed);}
#if SOUI_PERF_OPTIONAL_FRAME_FEEDBACK
  unsigned requested_frame_rate()const noexcept override{return requested_rate_;}
  bool set_backend_frame_rate_limit(unsigned limit)noexcept override{backend_limit_.store(limit,std::memory_order_release);return true;}
#endif
  unsigned desired_rate()const noexcept{
    const auto target=sdk::clamp_frame_rate_to_display(requested_rate_,display_rate_.load(std::memory_order_acquire));
    const auto limit=backend_limit_.load(std::memory_order_acquire);return limit?(std::min)(target,limit):target;
  }
  void display_rate(unsigned rate)noexcept{display_rate_.store(rate,std::memory_order_release);}
  void applied_pacer(unsigned rate)noexcept{active_rate_.store(rate,std::memory_order_release);}
  std::uint64_t epoch()const noexcept{return dispatch_.epoch();}
  dispatch_gate& dispatch()noexcept{return dispatch_;}
 private:
  sdk::json_bridge inner_;std::atomic<unsigned> backend_limit_{0},active_rate_{60};const unsigned requested_rate_;
  std::atomic<unsigned> display_rate_{60};dispatch_gate dispatch_;
};
struct slot final {
  explicit slot(application& owner,unsigned number,sdk::diagnostics::mode mode,unsigned rate):app(owner),index(number),bridge("app://sdk-performance-"+std::to_string(number),mode,rate){}
  application& app;unsigned index;HWND window{};
  benchmark_bridge bridge;std::unique_ptr<sdk::webview_host> host;sdk::webview2_adapter* modern{};
  std::unique_ptr<sdk::webview2_runtime_exit_watch> runtime_watch;sdk::frame_pacer pacer;
  bool preparing{},prepared{},mounted{},closing{},controller_closed{},closed{},close_running{},display_dirty{true};
  ULONGLONG close_started{},restore_at{},snapshot_at{};
  ULONGLONG reload_deadline{};
  std::uint64_t reload_original_epoch{};
  bool reload_pending{},reload_polling{};
  mutable std::mutex snapshot_mutex;json snapshot=json::object();
  json runtime_identity=json::object();
  json copy_snapshot()const{std::lock_guard<std::mutex> lock(snapshot_mutex);return snapshot;}
  json copy_wire_snapshot()const{return native_phase_wire_snapshot(copy_snapshot());}
  void publish_snapshot();void request_close();void close_step();void pulse();void housekeeping();void reload_step();
};
struct action final{unsigned slot_index;dispatch_gate::receipt receipt;ULONGLONG due;std::string name;};
class application final {
 public:
  explicit application(options settings):settings(std::move(settings)){}
  options settings;fs::path page;json identity;std::unique_ptr<report_writer> writer;
  scalar_capture input_capture;
  std::array<std::unique_ptr<slot>,2> slots;std::atomic<unsigned> created_count{0};std::atomic<bool> accepting{true};int exit_code{};
  std::mutex actions_mutex;std::deque<action> actions;bool processing_actions{};
  void log(const json& value){std::ofstream file(settings.output/L"lifecycle.ndjson",std::ios::app);file<<value.dump()<<'\n';file.flush();}
  [[noreturn]] void fatal(const char* message){exit_code=3;try{log({{"stage","fatal"},{"error",message},{"passed",false}});}catch(...){}std::cerr<<message<<'\n';std::_Exit(3);}
  void fail(slot& item,const sdk::host_result& result){exit_code=3;log({{"stage","host-failure"},{"slot",item.index},{"code",result.code},{"message",result.message}});item.request_close();}
  void initialize(){
    require(fs::create_directory(settings.output),"Fresh output could not be created");
    page=settings.output/L"page-bundle";require(fs::create_directory(page),"Page freeze directory unavailable");json files=json::array();std::string page_seed;
    for(const auto* name:{L"index.html",L"benchmark.js",L"benchmark.css"}){
      fs::copy_file(settings.page/name,page/name);const auto digest=sha256(page/name);
      files.push_back({{"path",fs::path(name).u8string()},{"bytes",fs::file_size(page/name)},{"sha256",digest}});page_seed+=fs::path(name).u8string()+":"+digest+"\n";
    }
    write_new(settings.output/L"page-hash-input.txt",page_seed);
    input_capture.configure(settings.input_trace,settings.backend=="miniblink"?settings.miniblink:fs::path{});
    std::array<wchar_t,32768> exe{};const auto size=GetModuleFileNameW(nullptr,exe.data(),static_cast<DWORD>(exe.size()));require(size&&size<exe.size(),"Executable path unavailable");
    identity={{"schema","soui.sdk-performance-identity/v1"},{"backend",settings.backend},{"buildIdentity",SOUI_PERF_BUILD_IDENTITY},
      {"architecture",architecture},
      {"sdkVersion",SOUI_SDK_VERSION_STRING},{"sdkPackageVersion",SOUI_PERF_PACKAGE_VERSION},{"optionalFeedback",bool(SOUI_PERF_OPTIONAL_FRAME_FEEDBACK)},
      {"engineBudgetUs",settings.budget},{"pageHash",sha256(settings.output/L"page-hash-input.txt")},{"pageFiles",files},{"executableSha256",sha256(exe.data())},
      {"requestedFrameRate",settings.frame_rate},
      {"observation",settings.observation==sdk::diagnostics::mode::scalar?"scalar":"off"},
      {"inputTrace",settings.input_trace?"scalar":"off"},{"inputTraceDescriptor",input_capture.descriptor()},
      {"environment",environment_snapshot()},{"detailedTraceConfiguredByConsumer",false},
      {"requestedNodePath",settings.miniblink.u8string()},{"nodeSha256",settings.miniblink.empty()?std::string{}:sha256(settings.miniblink)},
      {"fixedWebView2",settings.fixed_runtime.u8string()},{"initialProcess",process_snapshot()},{"canvasViewport",{{"width",480},{"height",360}}},
      {"physicalPresentationMeasured",false},{"physicalInputLatencyMeasured",false},{"performanceVerified",false}};
    write_new(settings.output/L"identity.json",identity.dump(2)+"\n");writer=std::make_unique<report_writer>(settings.output);
    WNDCLASSW cls{};cls.lpfnWndProc=&procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=window_class;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    require(RegisterClassW(&cls)!=0,"Window class unavailable");create_slot();
  }
  void register_methods(slot& item){
    auto& methods=item.bridge.methods();
    require(methods.register_method("benchmark.echo","benchmark.echo",[](std::string_view params){return echo(params).dump();}),"Echo registration failed");
    require(methods.register_method("benchmark.identity","benchmark.read",[this,&item](std::string_view params){
      require(parse_object(params).empty(),"Identity expects empty params");auto value=identity;value["native"]=item.copy_wire_snapshot();value["hostIndex"]=item.index;value["sessionGeneration"]=item.bridge.epoch();return value.dump();
    }),"Identity registration failed");
    require(methods.register_method("benchmark.report","benchmark.report",[this,&item](std::string_view params){
      require(accepting.load(),"E_REPORT_CLOSING");const auto value=parse_object(params);
      require(value.size()==1&&value.contains("report")&&value["report"].is_object(),"E_REPORT_PARAMS");
      require(value["report"].dump().size()<=60000,"E_REPORT_PAGE_SIZE");
      return writer->enqueue({{"schema","soui.sdk-performance-report/v1"},{"identity",identity},{"hostIndex",item.index},
        {"nativeSnapshot",item.copy_snapshot()},{"pageReport",value["report"]},{"performanceVerified",false}}).dump();
    }),"Report registration failed");
    require(methods.register_method("benchmark.reportStatus","benchmark.report",[this](std::string_view params){
      const auto value=parse_object(params);require(value.size()==1&&value.contains("id")&&value["id"].is_number_unsigned()&&value["id"].get<std::uint64_t>()<=UINT32_MAX,"E_REPORT_ID");return writer->status(value["id"].get<unsigned>()).dump();
    }),"Report status registration failed");
    require(methods.register_contextual_method("benchmark.action","benchmark.action",[this,&item](const soui::runtime::bridge_request& request){
      const auto original=item.bridge.dispatch().original(request.session_id);
      const auto value=parse_object(request.params_json);require(value.size()==1&&value.contains("action")&&value["action"].is_string(),"E_ACTION_PARAMS");
      const auto name=value["action"].get<std::string>();require(name=="second-host"||name=="reload"||name=="hide-restore","E_ACTION_UNKNOWN");
      if(!accepting.load())return json{{"accepted",false},{"error","E_ACTION_CLOSING"}}.dump();
      action queued{item.index,original,GetTickCount64()+150,name};bool accepted=false;
      const bool current=item.bridge.dispatch().publish(original,[&]{
        std::lock_guard<std::mutex> lock(actions_mutex);
        if(actions.size()<8&&(name!="second-host"||created_count.load()<2)){actions.push_back(std::move(queued));accepted=true;}
      });
      if(!current||!accepted)return json{{"accepted",false},{"error",current?"E_ACTION_CAPACITY":"E_ACTION_STALE"}}.dump();
      return json{{"accepted",true},{"action",name},{"completion","UI claim rechecks original request receipt before lifecycle execution"}}.dump();
    }),"Action registration failed");
    for(bool begin:{true,false})require(methods.register_contextual_method(begin?"benchmark.measurementBegin":"benchmark.measurementEnd","benchmark.measure",
      [this,&item,begin](const soui::runtime::bridge_request& request){
        const auto original=item.bridge.dispatch().original(request.session_id);
        require(accepting.load(),"E_MEASUREMENT_CLOSING");const auto params=parse_object(request.params_json);unsigned id=0;
        if(begin)require(params.empty(),"E_MEASUREMENT_BEGIN_PARAMS");
        else{require(params.size()==1&&params.contains("id")&&params["id"].is_number_unsigned()&&params["id"].get<std::uint64_t>()<=UINT32_MAX,"E_MEASUREMENT_END_PARAMS");id=params["id"].get<unsigned>();}
        auto source=identity;source["nativeHost"]=item.copy_snapshot();
        return input_capture.measurement(begin,item.index,item.bridge.dispatch(),original,id,*writer,source).dump();
      }),"Measurement registration failed");
    require(methods.set_session_capabilities({"bridge.probe","benchmark.echo","benchmark.read","benchmark.report","benchmark.action","benchmark.measure"}),"Session capabilities failed");
  }
  void create_slot(){
    const unsigned index=created_count.load();require(index<slots.size(),"At most two Host lifetimes per run");
    slots[index]=std::make_unique<slot>(*this,index,settings.observation,settings.frame_rate);auto& item=*slots[index];created_count.store(index+1);register_methods(item);
    const DWORD style=WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN;RECT bounds{0,0,1100,760};require(AdjustWindowRectEx(&bounds,style,FALSE,0),"Window sizing failed");
    const std::wstring title=L"SOUI SDK · "+std::wstring(settings.backend.begin(),settings.backend.end())+L" · "+std::wstring(SOUI_PERF_BUILD_IDENTITY,SOUI_PERF_BUILD_IDENTITY+sizeof(SOUI_PERF_BUILD_IDENTITY)-1)+L" · Host "+std::to_wstring(index+1);
    item.window=CreateWindowExW(0,window_class,title.c_str(),style,CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,
      nullptr,nullptr,GetModuleHandleW(nullptr),&item);require(item.window,"Window creation failed");
    if(settings.backend=="miniblink")item.host=std::make_unique<sdk::webview_host>(std::make_unique<sdk::dynamic_miniblink_adapter>(settings.observation));
    else{
      const auto profile=settings.output/("webview2-profile-"+std::to_string(index));
      auto adapter=std::make_unique<sdk::webview2_adapter>(item.window,profile,std::string{},settings.fixed_runtime,settings.observation);
      adopt_adapter(item.host,item.modern,std::move(adapter),[](auto value){return std::make_unique<sdk::webview_host>(std::move(value));});
      const auto configured=item.modern->configure_profile_access(sdk::webview2_profile_access::exclusive);
      require(configured.ok,"Exclusive WebView2 profile required");
    }
    require(SetTimer(item.window,1,100,nullptr)!=0,"Lifecycle timer failed");
    if(item.modern){
      item.preparing=true;item.modern->prepare_async([this,&item](sdk::host_result result)noexcept{
        item.prepared=true;
        try{
          if(result.ok)result=item.modern->acquire_runtime_exit_watch(item.runtime_watch);
          if(result.ok&&(!item.runtime_watch||item.runtime_watch->observe().phase==sdk::webview2_runtime_exit_phase::failed))
            result={false,"E_RUNTIME_WATCH_FAILED","Exclusive runtime exit identity unavailable"};
          if(result.ok&&!item.closing)result=mount(item);
          if(!result.ok)fail(item,result);
          else if(item.closing)item.request_close();
        }catch(const std::exception& error){try{fail(item,{false,"E_PREPARE_CALLBACK",error.what()});}catch(...){fatal("E_PREPARE_CALLBACK_FAILURE");}}
        catch(...){fatal("E_PREPARE_CALLBACK_FAILURE");}
      });
    }else{const auto result=mount(item);if(!result.ok)fail(item,result);}
    ShowWindow(item.window,SW_SHOWNOACTIVATE); // Only the operator manually activates a benchmark window.
    log({{"stage","window-created"},{"slot",index},{"backend",settings.backend},{"activatedByTool",false}});
  }
  sdk::host_result mount(slot& item){
    sdk::host_config config;
    config.architecture=architecture;
    config.miniblink_binary=settings.miniblink;config.resource_root=page;config.owner_window=item.window;config.bridge_adapter=&item.bridge;
    item.bridge.display_rate(sdk::frame_pacer::query_display_refresh(item.window));item.display_dirty=false;
    item.bridge.applied_pacer(item.bridge.desired_rate());
    auto result=item.host->start(config);if(!result.ok)return result;item.mounted=true;
    if(item.modern){
      const auto observed=item.runtime_watch->observe();item.runtime_identity={{"runtimeVersion",item.modern->runtime_version()},{"browserPid",observed.browser_process_id},
        {"browserCreationFileTime",std::to_string(observed.creation_file_time)},{"browserImagePath",observed.image_path.u8string()}};
      if(fs::is_regular_file(observed.image_path))item.runtime_identity["browserImageSha256"]=sha256(observed.image_path);
    }else{
      const auto module=GetModuleHandleW(settings.miniblink.c_str());std::array<wchar_t,32768> actual{};
      const auto length=module?GetModuleFileNameW(module,actual.data(),static_cast<DWORD>(actual.size())):0;
      if(!length||length>=actual.size()||!fs::equivalent(actual.data(),settings.miniblink))return {false,"E_KERNEL_IDENTITY","Loaded kernel identity differs"};
      const auto digest=sha256(actual.data());if(digest!=identity["nodeSha256"].get<std::string>())return {false,"E_KERNEL_CHANGED","Kernel file changed during mount"};
      item.runtime_identity={{"loadedNodePath",fs::path(actual.data()).u8string()},{"loadedNodeSha256",digest},{"modulePathVerified",true}};
    }
    if(!item.pacer.start(item.window,item.bridge.desired_rate()))return {false,"E_PACER","Native pacing failed"};
    item.bridge.applied_pacer(item.pacer.target());item.publish_snapshot();return result;
  }
  void process_actions(){
    if(processing_actions)return;processing_actions=true;struct done{bool& flag;~done(){flag=false;}} guard{processing_actions};
    for(unsigned n=0;n<4;++n){action next;{
        std::lock_guard<std::mutex> lock(actions_mutex);if(actions.empty()||actions.front().due>GetTickCount64())return;next=std::move(actions.front());actions.pop_front();}
      auto& item=*slots.at(next.slot_index);if(item.closing||item.closed||!item.mounted||!item.bridge.dispatch().claim(next.receipt)){log({{"stage","action-cancelled"},{"action",next.name},{"slot",next.slot_index},{"requestGeneration",std::to_string(next.receipt.epoch)}});continue;}
      log({{"stage","action-begin"},{"action",next.name},{"slot",next.slot_index},{"requestGeneration",std::to_string(next.receipt.epoch)},{"authorization","original receipt claimed before reentrant Host call"}});
      if(next.name=="second-host"){if(created_count.load()<2)create_slot();}
      else if(next.name=="reload"){
        if(item.reload_deadline){fail(item,{false,"E_RELOAD_ALREADY_OWNED","A reload is already being observed."});continue;}
        item.reload_original_epoch=next.receipt.epoch;
        item.reload_deadline=GetTickCount64()+15000;
        item.reload_pending=true;
        item.reload_step();
      }
      else{const auto result=item.host->hide();if(!result.ok){fail(item,result);continue;}ShowWindow(item.window,SW_HIDE);item.restore_at=GetTickCount64()+1500;}
    }
  }
  bool all_closed()const{for(const auto& item:slots)if(item&&!item->closed)return false;return created_count.load()!=0;}
  void close_all(){accepting.store(false);for(auto& item:slots)if(item&&!item->closed){item->request_close();item->close_step();}}
  int loop(){
    MSG message{};
    while(!all_closed()){
      const auto result=GetMessageW(&message,nullptr,0,0);
      if(result>0){TranslateMessage(&message);DispatchMessageW(&message);}
      else{exit_code=3;close_all();while(!all_closed()){
          for(unsigned i=0;i<32&&PeekMessageW(&message,nullptr,0,0,PM_REMOVE);++i)if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}
          for(auto& item:slots)if(item&&!item->closed)item->close_step();MsgWaitForMultipleObjectsEx(0,nullptr,20,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        }}
    }
    accepting.store(false);
    // Each closed Host no longer owns callbacks. Destroy its Bridge services
    // before final trace quiescence; do not retain their worker TLS until exit.
    for(auto& item:slots)item.reset();
    writer->stop();const auto writer_deadline=GetTickCount64()+20000;
    while(!writer->stopped()){if(GetTickCount64()>writer_deadline)fatal("E_REPORT_WRITER_CLOSE_DEADLINE");MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_ALLINPUT,MWMO_INPUTAVAILABLE);while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
    if(writer->failed())exit_code=3;writer.reset();
    if(!input_capture.before_runtime_shutdown())fatal("E_TRACE_FINAL_UI_REGISTRATION");
    sdk::host_result shutdown{true,{},{}};
    if(settings.backend=="miniblink")shutdown=sdk::wait_for_miniblink_runtime_shutdown(20s);
    if(!shutdown.ok)exit_code=3;
    const bool trace_complete=input_capture.finish(settings.output);if(!trace_complete)exit_code=3;
    write_new(settings.output/L"exit.json",json{{"schema","soui.sdk-performance-exit/v1"},{"exitCode",exit_code},{"allHostsClosed",all_closed()},
      {"runtimeShutdown",shutdown.ok},{"runtimeShutdownCode",shutdown.code},{"inputTraceEnabled",settings.input_trace},
      {"inputTraceExportSucceeded",settings.input_trace?json(trace_complete):json(nullptr)},
      {"inputTraceFinalComplete",settings.input_trace?json(trace_complete&&input_capture.final_usable()):json(nullptr)},
      {"performanceVerified",false},{"releaseReady",false}}.dump(2)+"\n");
    if(!shutdown.ok)fatal("E_GLOBAL_RUNTIME_SHUTDOWN");return exit_code;
  }
  static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
    auto* item=reinterpret_cast<slot*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){item=static_cast<slot*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);item->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(item));}
    if(!item)return DefWindowProcW(window,message,wparam,lparam);
    try{
      if(message==sdk::bridge_event_message){if(item->host)item->host->deliver_posted_event(reinterpret_cast<void*>(lparam));return 0;}
      if(message==sdk::webview2_event_message){if(item->host)item->host->pump();return 0;}
      if(message==sdk::frame_pulse_message){item->pacer.acknowledge();item->pulse();return 0;}
      switch(message){
        case WM_TIMER:item->housekeeping();return 0;
        case WM_CLOSE:item->request_close();return 0;
        case WM_SIZE:if(item->mounted&&!item->closing&&wparam!=SIZE_MINIMIZED)(void)item->host->resize((std::max)(1,int(LOWORD(lparam))),(std::max)(1,int(HIWORD(lparam))));return 0;
        case WM_SETFOCUS:if(item->mounted&&!item->closing)(void)item->host->focus();return 0;
        case WM_DISPLAYCHANGE:case WM_WINDOWPOSCHANGED:item->display_dirty=true;break;
        case WM_NCDESTROY:SetWindowLongPtrW(window,GWLP_USERDATA,0);break;
        default:break;
      }
    }catch(const std::exception& failure){item->app.exit_code=3;item->app.log({{"stage","window-exception"},{"error",failure.what()}});item->app.close_all();return 0;}
    return DefWindowProcW(window,message,wparam,lparam);
  }
};
void slot::publish_snapshot(){
  auto value=process_snapshot();RECT client{};GetClientRect(window,&client);
  value["clientWidth"]=client.right;value["clientHeight"]=client.bottom;value["windowVisible"]=bool(IsWindowVisible(window));value["windowIconic"]=bool(IsIconic(window));
  value["pacerHz"]=pacer.target();value["pacerBackend"]=pacer.backend();value["pacerFailed"]=pacer.failed();value["sessionGeneration"]=bridge.epoch();
  value["runtimeIdentity"]=runtime_identity;
  if(const auto dc=GetDC(window)){value["windowDpiX"]=GetDeviceCaps(dc,LOGPIXELSX);value["windowDpiY"]=GetDeviceCaps(dc,LOGPIXELSY);ReleaseDC(window,dc);}
  if(host&&mounted&&!closing){value["documentReady"]=host->document_ready();value["applicationReady"]=host->application_ready();value["applicationDiagnostics"]=host->application_diagnostics();}
  const auto phase_cache=native_phase_summary(value.value("applicationDiagnostics",std::string{}),app.identity["environment"]);
  if(!phase_cache.is_null())value["nativePhaseCache"]=phase_cache;
  if(modern)value["runtimeVersion"]=modern->runtime_version();
  else if(mounted){sdk::diagnostics::renderer_observation renderer;(void)sdk::read_miniblink_renderer_metrics(renderer);value["loadedNodePath"]=renderer.module_path;value["loadedIdentityBound"]=renderer.identity_bound;value["loadedModuleMatches"]=renderer.module_matches;}
  std::lock_guard<std::mutex> lock(snapshot_mutex);snapshot=std::move(value);snapshot_at=GetTickCount64();
}
void slot::request_close(){if(closed)return;if(!closing){closing=true;close_started=GetTickCount64();restore_at=0;app.log({{"stage","close-requested"},{"slot",index}});} }
void slot::close_step(){
  if(closed||!closing||close_running)return;close_running=true;struct release_close{bool& value;~release_close(){value=false;}} guard{close_running};
  if(GetTickCount64()-close_started>20000)app.fatal("E_HOST_CLOSE_DEADLINE");
  if(host)host->pump();if(preparing&&!prepared)return;
  if(!controller_closed){auto result=host?host->destroy():sdk::host_result{true,{},{}};
    if(result.ok&&modern)result=modern->unmount();
    if(!result.ok){if(result.code!="E_BUSY"&&result.code!="E_CURSOR_CLOSE_PENDING")app.exit_code=3;return;}
    controller_closed=true;pacer.stop();
  }
  if(modern&&preparing&&!runtime_watch){const auto acquired=modern->acquire_runtime_exit_watch(runtime_watch);
    if(!acquired.ok){app.exit_code=3;if(acquired.code!="E_RUNTIME_WATCH_UNAVAILABLE"||!modern->failed_preparation_cleanup_complete())app.fatal("E_RUNTIME_WATCH_UNAVAILABLE");}}
  if(runtime_watch){const auto observed=runtime_watch->observe();
    if(observed.phase==sdk::webview2_runtime_exit_phase::failed)app.fatal("E_RUNTIME_WATCH_FAILED");
    if(observed.phase!=sdk::webview2_runtime_exit_phase::finished){const auto done=runtime_watch->finish();if(!done.ok){if(done.code!="E_BUSY")app.fatal("E_RUNTIME_WATCH_FINISH");return;}}
    runtime_watch.reset();
  }
  modern=nullptr;host.reset();KillTimer(window,1);DestroyWindow(window);window=nullptr;closed=true;
  app.log({{"stage","host-closed"},{"slot",index},{"natural",true}});
}
void slot::pulse(){
  if(closing){close_step();return;}if(!mounted||!host)return;
  if(display_dirty){bridge.display_rate(sdk::frame_pacer::query_display_refresh(window));display_dirty=false;}
  const auto desired=bridge.desired_rate();if(pacer.target()!=desired){if(!pacer.start(window,desired)){app.fail(*this,{false,"E_PACER","Pacer update failed"});return;}bridge.applied_pacer(pacer.target());}
  if(pacer.failed()){app.fail(*this,{false,"E_PACER","Pacer clock failed"});return;}host->pump();
}
void slot::reload_step(){
  if(!reload_deadline||closing||closed||reload_polling)return;
  if(GetTickCount64()>=reload_deadline){
    reload_deadline=0;app.fail(*this,{false,"E_RELOAD_DEADLINE","Reload or subsequent application readiness exceeded the original 15-second window."});return;
  }
  reload_polling=true;
  struct release_poll{bool& value;~release_poll(){value=false;}} guard{reload_polling};
  if(reload_pending){
    const auto result=host->reload(); // Retain the original SDK operation; same UI owner.
    if(closing||closed)return;
    if(!result.ok){
      if(result.code=="E_RELOAD_PENDING")return;
      reload_deadline=0;app.fail(*this,result);return;
    }
    reload_pending=false;
    app.log({{"stage","reload-operation-complete"},{"slot",index},{"previousGeneration",std::to_string(reload_original_epoch)}});
  }
  if(GetTickCount64()>=reload_deadline){
    reload_deadline=0;app.fail(*this,{false,"E_RELOAD_DEADLINE","Reload operation completed after its original deadline."});return;
  }
  if(host->document_ready()&&host->application_ready()&&bridge.epoch()>reload_original_epoch){
    const auto generation=bridge.epoch();
    reload_deadline=0;
    publish_snapshot();
    app.log({{"stage","reload-ready"},{"slot",index},{"previousGeneration",std::to_string(reload_original_epoch)},
             {"generation",std::to_string(generation)}});
  }
}

void slot::housekeeping(){
  if(closing){close_step();return;}
  reload_step();if(closing||closed)return;
  app.process_actions();if(closing||closed)return;
  if(restore_at&&GetTickCount64()>=restore_at){restore_at=0;const auto shown=host->show();if(!shown.ok){app.fail(*this,shown);return;}ShowWindow(window,SW_SHOWNOACTIVATE);app.log({{"stage","hidden-restored"},{"slot",index},{"activatedByTool",false}});}
  if(GetTickCount64()-snapshot_at>=1000)publish_snapshot();
  if(modern&&prepared){const auto status=modern->status();if(!status.ok&&status.code!="E_PENDING")app.fail(*this,status);}
}
unsigned boundary_contracts(scalar_capture& capture,report_writer& writer){
  unsigned passed=0;
  struct tracked_adapter{explicit tracked_adapter(bool& value):destroyed(value){}bool& destroyed;~tracked_adapter(){destroyed=true;}};
  struct tracked_owner{std::unique_ptr<tracked_adapter> adapter;};
  bool destroyed=false,threw=false;std::unique_ptr<tracked_owner> owner;tracked_adapter* alias=nullptr;
  try{adopt_adapter(owner,alias,std::make_unique<tracked_adapter>(destroyed),
    [](std::unique_ptr<tracked_adapter>)->std::unique_ptr<tracked_owner>{throw std::runtime_error("injected owner construction failure");});}
  catch(const std::exception&){threw=true;}
  require(threw&&destroyed&&!owner&&!alias,"Failed owner construction published a dangling adapter");++passed;
  destroyed=false;
  adopt_adapter(owner,alias,std::make_unique<tracked_adapter>(destroyed),
    [](auto adapter){return std::make_unique<tracked_owner>(tracked_owner{std::move(adapter)});});
  require(owner&&alias==owner->adapter.get()&&!destroyed,"Successful owner construction lost its adapter alias");owner.reset();alias=nullptr;++passed;

  dispatch_gate gate,other;dispatch_gate::receipt original;
  {dispatch_gate::scope outer(gate);original=gate.original("outer");
    {dispatch_gate::scope inner(other);require(gate.original("outer").epoch==original.epoch&&other.original("inner").owner==&other,"Nested Bridge receipt crossed instances");}
    {dispatch_gate::scope nested(gate);require(gate.original("nested").session=="nested","Nested request lost its own original session");}
    require(gate.original("outer").session=="outer","Nested dispatch did not restore outer scope");}
  bool no_scope=false;try{(void)gate.original("outside");}catch(const std::exception&){no_scope=true;}
  require(no_scope&&gate.claim(original)&&!other.claim(original),"Receipt escaped its original Bridge scope");++passed;
  unsigned publications=0;require(gate.publish(original,[&]{++publications;}),"Current original receipt was rejected");
  gate.revoke();require(!gate.publish(original,[&]{++publications;})&&!gate.claim(original)&&publications==1,"Revoked queued receipt remained executable");++passed;
  {dispatch_gate::revocation first(gate),second(gate);first.complete();
    {dispatch_gate::scope ingress(gate);require(gate.original("during-nested-revocation").epoch==0,"One revocation reopened another pending revocation");}
    second.complete();require(gate.epoch()!=0,"Completed revocations did not reopen admission");}++passed;
  dispatch_gate failed;
  try{dispatch_gate::revocation pending(failed);throw std::runtime_error("injected inner shutdown failure");}catch(const std::exception&){}
  {dispatch_gate::scope ingress(failed);const auto invalid=failed.original("after-throw");require(!failed.claim(invalid)&&invalid.epoch==0,"Failed shutdown reopened admission");}++passed;

  // Actual json_bridge dispatch: revoke after native authorization but before
  // the contextual handler obtains its receipt. The ingress epoch must survive.
  benchmark_bridge bridge("app://sdk-performance-contract",sdk::diagnostics::mode::disabled,60);
  handle entered(CreateEventW(nullptr,TRUE,FALSE,nullptr)),revoked(CreateEventW(nullptr,TRUE,FALSE,nullptr));
  require(entered.value&&revoked.value,"Dispatch barrier creation failed");
  std::uint64_t handler_epoch{};std::string handler_session;bool handler_entered=false,action_published=false,measurement_rejected=false;
  require(bridge.methods().register_contextual_method("contract.receipt","contract.receipt",[&](const soui::runtime::bridge_request& request){
    handler_entered=true;SetEvent(entered.value);require(WaitForSingleObject(revoked.value,5000)==WAIT_OBJECT_0,"Revocation controller deadline");
    const auto receipt=bridge.dispatch().original(request.session_id);handler_epoch=receipt.epoch;handler_session=receipt.session;
    action_published=bridge.dispatch().publish(receipt,[&]{++publications;});
    try{(void)capture.measurement(true,0,bridge.dispatch(),receipt,0,writer,json::object());}
    catch(const std::exception& error){measurement_rejected=std::string(error.what())=="E_MEASUREMENT_STALE";}
    return json{{"published",action_published}}.dump();
  }),"Receipt contract registration failed");
  require(bridge.methods().set_session_capabilities({"contract.receipt"}),"Receipt contract capabilities failed");
  const auto bootstrap=json::parse(bridge.handle_json(json{{"requestId","contract:bootstrap"},{"sessionId",""},{"origin",""},{"token",""},
    {"method","bridge.bootstrap"},{"params",{{"protocol","soui.bridge/1"}}}}.dump()));
  require(bootstrap.value("ok",false),"Actual json_bridge bootstrap failed");const auto credentials=bootstrap.at("result");
  const auto session=credentials.at("sessionId").get<std::string>();const auto ingress_epoch=bridge.epoch();std::atomic<bool> controller_ok{false};
  std::thread controller([&]{
    const bool registered=input_trace::register_current_thread();
    if(registered&&WaitForSingleObject(entered.value,5000)==WAIT_OBJECT_0){try{bridge.shutdown_session(session);controller_ok.store(true);}catch(...){}}
    if(registered)input_trace::detach_current_thread();
    SetEvent(revoked.value);
  });
  try{(void)bridge.handle_json(json{{"requestId","contract:old"},{"sessionId",session},{"origin",credentials.at("origin")},{"token",credentials.at("token")},
    {"method","contract.receipt"},{"params",json::object()},{"timeoutMs",5000}}.dump());}catch(...){SetEvent(entered.value);controller.join();throw;}
  controller.join();require(controller_ok&&handler_entered&&handler_epoch==ingress_epoch&&handler_session==session&&bridge.epoch()!=handler_epoch,
    "Old authorized request was relabeled with the revoked epoch");
  require(!action_published&&measurement_rejected&&publications==1,"Old request mutated action or measurement after revocation");++passed;
  // Exact complementary interleaving: handle_json enters after the gate closes
  // but before the real SDK permission revocation. Its SDK authorization still
  // succeeds, yet the original ingress receipt must remain zero/invalid.
  const auto next_bootstrap=json::parse(bridge.handle_json(json{{"requestId","contract:bootstrap-next"},{"sessionId",""},{"origin",""},{"token",""},
    {"method","bridge.bootstrap"},{"params",{{"protocol","soui.bridge/1"}}}}.dump()));
  require(next_bootstrap.value("ok",false),"Next actual session bootstrap failed");const auto next_credentials=next_bootstrap.at("result");
  handler_entered=false;action_published=false;measurement_rejected=false;
  {dispatch_gate::revocation pending(bridge.dispatch());
    (void)bridge.handle_json(json{{"requestId","contract:gap"},{"sessionId",next_credentials.at("sessionId")},{"origin",next_credentials.at("origin")},
      {"token",next_credentials.at("token")},{"method","contract.receipt"},{"params",json::object()},{"timeoutMs",5000}}.dump());
    require(handler_entered&&handler_epoch==0&&!action_published&&measurement_rejected&&publications==1,"Request entering permission-revocation gap obtained new authority");
    bridge.methods().shutdown_session(next_credentials.at("sessionId").get<std::string>());pending.complete();
  }++passed;
  return passed;
}
int self_test(const options& settings){
  require(fs::create_directory(settings.output),"Fresh contract result directory required");unsigned passed=0;
  for(std::size_t size:{std::size_t{0},std::size_t{16},std::size_t{4096},std::size_t{60000}}){auto value=json{{"payload",std::string(size,'x')},{"sequence",UINT32_MAX}};require(echo(value.dump())==value,"Echo boundary mismatch");++passed;}
  for(const auto& value:{json{{"payload",std::string(60001,'x')},{"sequence",1}},json{{"payload","x"},{"sequence",-1}},json{{"payload","\n"},{"sequence",1}},json{{"payload","x"},{"sequence",4294967296ULL}}}){
    bool rejected=false;try{(void)echo(value.dump());}catch(const std::exception&){rejected=true;}require(rejected,"Invalid echo admitted");++passed;
  }
  report_writer writer(settings.output);const auto accepted=writer.enqueue({{"contract",true}});require(accepted["accepted"].get<bool>(),"Writer admission failed");writer.stop();
  const auto deadline=GetTickCount64()+5000;while(!writer.stopped()&&GetTickCount64()<deadline)Sleep(1);
  if(!writer.stopped())std::_Exit(3);require(!writer.failed()&&writer.status(accepted["id"].get<unsigned>())["state"]=="saved","Writer result missing");++passed;
  bool overwrite=false;try{write_new(settings.output/L"report-1.json","overwrite");}catch(const std::exception&){overwrite=true;}require(overwrite,"Report overwrite allowed");++passed;
  require(!writer.enqueue({{"late",true}})["accepted"].get<bool>(),"Stopped writer admitted report");++passed;
  const auto trace_root=settings.output/L"trace-contracts";require(fs::create_directory(trace_root),"Trace contract directory already exists");
  report_writer trace_writer(trace_root);scalar_capture capture;dispatch_gate gate;dispatch_gate::scope dispatch(gate);const auto original=gate.original("test-session");
  bool disabled_rejected=false;try{(void)capture.measurement(true,0,gate,original,0,trace_writer,json::object());}catch(const std::exception&){disabled_rejected=true;}
  require(disabled_rejected&&!capture.enabled(),"Off measurement unexpectedly configured trace");++passed;
  const auto absent=scalar_json(input_trace::scalar_snapshot{});require(!absent["available"].get<bool>()&&absent["counters"].is_null()&&absent["durations"].is_null(),"Unavailable source converted into zero measurements");++passed;
  capture.configure(true,{});passed+=boundary_contracts(capture,trace_writer);
  auto actual=input_trace::process_recorder().snapshot();actual.attempted=42;actual.recorded=0;
  require(scalar_source_status(actual,actual.session,true)["complete"]==true,"Normal scalar-only attempted events were mislabeled as loss");++passed;
  require(scalar_source_status({},actual.session,true)["complete"]==false&&scalar_source_status({},actual.session,false)["complete"].is_null(),"Missing requested source was accepted or not-applicable source fabricated");++passed;
  for(unsigned fault=0;fault<12;++fault){auto value=actual;
    switch(fault){case 0:value.counter_saturated=true;break;case 1:value.owner_violation=true;break;case 2:value.invalid_event=true;break;
      case 3:value.clock_invalid=true;break;case 4:value.dropped=1;break;case 5:value.unregistered_writes=1;break;case 6:value.registration_rejections=1;break;
      case 7:value.causal_lost=1;break;case 8:++value.session.run_id_low;break;case 9:value.recording_mode=input_trace::mode::detailed;break;
      case 10:value.stopped=true;break;case 11:value.counters[static_cast<std::size_t>(input_trace::counter::causal_ranges_dropped)]=1;break;}
    require(scalar_source_status(value,actual.session,true)["complete"]==false,"Faulted scalar source was marked usable");
  }++passed;
  const std::uint64_t exact_large=(std::uint64_t{1}<<53)+17;
  input_trace::add(input_trace::counter::copy_bytes,exact_large);
  const auto begin=capture.measurement(true,0,gate,original,0,trace_writer,{{"contractOnly",true}});
  require(begin["snapshotUsable"]==true&&begin["sourceStatus"]["kernel"]["complete"].is_null(),"Actual scalar boundary receipt lacks source qualification");++passed;
  bool duplicate=false;try{(void)capture.measurement(true,0,gate,original,0,trace_writer,json::object());}catch(const std::exception&){duplicate=true;}
  require(duplicate,"Duplicate measurement Begin accepted");++passed;
  dispatch_gate other;dispatch_gate::scope other_dispatch(other);const auto unrelated=other.original("other-session");
  bool stale=false;try{(void)capture.measurement(false,0,other,unrelated,begin["id"].get<unsigned>(),trace_writer,json::object());}catch(const std::exception&){stale=true;}
  require(stale,"Stale measurement End accepted");++passed;
  input_trace::add(input_trace::counter::copy_bytes,7);input_trace::observe_duration(input_trace::duration_metric::input_queue,8);
  const auto end=capture.measurement(false,0,gate,original,begin["id"].get<unsigned>(),trace_writer,{{"contractOnly",true}});
  trace_writer.stop();const auto trace_deadline=GetTickCount64()+5000;while(!trace_writer.stopped()&&GetTickCount64()<trace_deadline)Sleep(1);
  if(!trace_writer.stopped())std::_Exit(3);require(!trace_writer.failed(),"Trace boundary writer failed");
  std::ifstream input(fs::u8path(end["path"].get<std::string>()));json stored;input>>stored;
  require(stored["sdk"]["counters"]["copy_bytes"]==std::to_string(exact_large+7)&&stored["sdk"]["durations"]["input_queue"][3]=="1", "Scalar counter precision/histogram contract changed");++passed;
  require(stored["kernel"]["available"]==false&&stored["kernel"]["counters"].is_null(),"No-kernel contract fabricated kernel metrics");++passed;
  require(stored["snapshotUsable"]==end["snapshotUsable"]&&stored["sourceStatus"]==end["sourceStatus"],"Persisted source qualification differs from actual receipt");++passed;
  const auto environment=environment_snapshot();for(const auto* name:{"SOUI_WEBVIEW2_PREPARE_OUTPUT","SOUI_WEBVIEW2_OWNER_BUDGET_US","SOUI_WEBVIEW2_OUTPUT_TIMING"})require(environment.contains(name),"W02 cold option missing from process identity");++passed;
  require(capture.before_runtime_shutdown()&&capture.finish(trace_root),"Actual scalar SDK did not quiesce/export");++passed;
  const json phase_environment={{native_phase_environment,"1"},{"SOUI_DIAGNOSTIC_SCRIPT","0"}};
  json phase_value={{"schema","soui.miniblink-native-phases/v1"},{"requested",true},{"enabled",true},{"status","ready"},{"usable",true},{"coherent",true},
    {"observerIncarnation","1"},{"qpcFrequency","1000"},{"captureBeginQpc","100"},{"captureEndQpc","101"},{"publicationSequence","0"},{"snapshotAttempts",1U},
    {"faultMask",0U},{"counterSaturated",false},{"clockInvalid",false},{"ownerViolation",false},{"sequenceExhausted",false},{"closing",false},
    {"inclusiveWallTime",true},{"wholeHostAtomic",false},{"quiescent",false},{"phases",json::array()}};
  for(const auto* name:native_phase_names)phase_value["phases"].push_back({{"phase",name},{"started","0"},{"completed","0"},{"returned","0"},
    {"unwound","0"},{"inFlight","0"},{"nestedStarts","0"},{"sumElapsedQpc","0"},{"buckets",std::vector<std::string>(64,"0")}});
  const auto diagnostic="document=ready,native_phases="+phase_value.dump();
  const auto summary=native_phase_summary(diagnostic,phase_environment);require(summary["usable"]==true&&!summary.contains("phases"),"Native phase summary failed");
  const json native={{"applicationDiagnostics",diagnostic},{"nativePhaseCache",summary},{"sessionGeneration",2U}};
  const auto script_diagnostic=diagnostic+",script={\"old\":true}";
  const auto script_summary=native_phase_summary(script_diagnostic,phase_environment);
  const json script_native={{"applicationDiagnostics",script_diagnostic},{"nativePhaseCache",script_summary},{"sessionGeneration",2U}};
  require(script_summary["usable"]==false&&script_summary["reasons"]==json::array({"unexpected-script-observation"}),"Actual script observation contradicted frozen off profile");
  require(native_phase_wire_snapshot(script_native)["applicationDiagnostics"]=="document=ready,script={\"old\":true}"&&script_native["applicationDiagnostics"]==script_diagnostic,"Wire projection changed native evidence/script tail");
  auto script_environment=phase_environment;script_environment["SOUI_DIAGNOSTIC_SCRIPT"]="1";
  require(native_phase_summary(script_diagnostic,script_environment)["usable"]==false,"Script-enabled profile qualified");
  require(native_phase_summary("document=waiting",json::object()).is_null(),"Disabled phase changed old snapshot shape");++passed;
  const auto phase_boundary=native_phase_boundary({{"nativeHost",native}},120,121,1000,2);
  require(phase_boundary["qualified"]==true&&phase_boundary["cacheAgeAtBoundaryBeginQpc"]=="19","Native cache age is not exact QPC");
  require(native_phase_boundary({{"nativeHost",script_native}},120,121,1000,2)["qualified"]==false,"Actual script phase boundary qualified");
  require(native_phase_boundary({{"nativeHost",native}},99,100,1000,2)["qualified"]==false&&
    native_phase_boundary({{"nativeHost",native}},120,121,2000,2)["qualified"]==false&&
    native_phase_boundary({{"nativeHost",native}},120,121,1000,3)["qualified"]==false,"Invalid cache clock/session accepted");++passed;
  auto malformed=phase_value;malformed["phases"][0]["buckets"].erase(malformed["phases"][0]["buckets"].begin());
  require(native_phase_summary("document=ready,native_phases="+malformed.dump(),phase_environment)["usable"]==false,"Unbounded native phase shape admitted");
  require(native_phase_summary("document=ready,native_phases="+std::string(native_phase_limit+1,'x'),phase_environment)["usable"]==false,"Oversize phase extension admitted");++passed;
  write_new(settings.output/L"contracts.json",json{{"passed",passed},{"guiExecuted",false},{"sdkVersion",SOUI_SDK_VERSION_STRING},{"buildIdentity",SOUI_PERF_BUILD_IDENTITY}}.dump(2)+"\n");
  std::cout<<"Native report/echo contracts passed: "<<passed<<'\n';return 0;
}
}
int wmain(int argc,wchar_t** argv){
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
  try{
    const auto settings=parse(argc,argv);if(settings.self_test)return self_test(settings);
    require(GetEnvironmentVariableW(L"SOUI_ENTRY_FILE",nullptr,0)==0,"Unset SOUI_ENTRY_FILE: benchmark requires the frozen index.html");
    require(SetEnvironmentVariableW(L"SOUI_ENGINE_SHARED_TURN_BUDGET_US",std::to_wstring(settings.budget).c_str()),"Cannot configure cold Engine budget");
    (void)SetProcessDPIAware();
    require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"UI STA COM initialization failed");
    application app(settings);
    const auto finish_loop=[&](){try{return app.loop();}catch(...){app.fatal("E_NATIVE_LOOP_OR_EXIT_EVIDENCE");}};
    try{app.initialize();const auto code=finish_loop();CoUninitialize();return code;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';if(app.created_count.load()){app.exit_code=3;app.close_all();const auto code=finish_loop();CoUninitialize();return code;}if(app.writer){app.writer->stop();const auto until=GetTickCount64()+5000;while(!app.writer->stopped()&&GetTickCount64()<until)Sleep(1);if(!app.writer->stopped())std::_Exit(3);}CoUninitialize();return 2;}
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
