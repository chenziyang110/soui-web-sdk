#include <windows.h>
#include <dbghelp.h>
#include <bcrypt.h>
#include <soui/host/soui_miniblink_host.h>
#include <soui/json_bridge.h>
#include "engine_admission.h"
#include <json.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using json = nlohmann::json;
using namespace std::chrono_literals;
using clock_type = std::chrono::steady_clock;
using soui::sdk::host_result;
namespace fs = std::filesystem;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct handle final {
  HANDLE value{nullptr};
  explicit handle(HANDLE input = nullptr) : value(input) {}
  ~handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
  handle(const handle&) = delete; handle& operator=(const handle&) = delete;
};
struct desktop final {
  HDESK value{nullptr};
  explicit desktop(HDESK input = nullptr) : value(input) {}
  ~desktop() { if (value) CloseDesktop(value); }
  desktop(const desktop&)=delete; desktop& operator=(const desktop&)=delete;
};
std::uint64_t qpc() { LARGE_INTEGER value{}; require(QueryPerformanceCounter(&value) != FALSE,"QPC failed"); return value.QuadPart; }
std::wstring object_name(HANDLE object) {
  std::array<wchar_t,256> name{}; DWORD required{};
  require(object && GetUserObjectInformationW(object,UOI_NAME,name.data(),DWORD(sizeof(name)),&required),"Desktop name unavailable");
  return name.data();
}
std::string sha256(const fs::path& path) {
  BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{};
  require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA provider failed");
  struct close_algorithm { BCRYPT_ALG_HANDLE value; ~close_algorithm(){BCryptCloseAlgorithmProvider(value,0);} } algorithm_owner{algorithm};
  require(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0,"SHA hash failed");
  struct close_hash { BCRYPT_HASH_HANDLE value; ~close_hash(){BCryptDestroyHash(value);} } hash_owner{hash};
  std::ifstream stream(path,std::ios::binary); require(bool(stream),"Hash input unavailable");
  std::array<char,65536> bytes{};
  while(stream) { stream.read(bytes.data(),bytes.size()); const auto size=stream.gcount();
    if(size) require(BCryptHashData(hash,reinterpret_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(size),0)>=0,"SHA update failed"); }
  require(stream.eof(),"Hash input read failed");
  std::array<unsigned char,32> digest{}; require(BCryptFinishHash(hash,digest.data(),DWORD(digest.size()),0)>=0,"SHA finish failed");
  const char* hex="0123456789abcdef"; std::string result; result.reserve(64);
  for(auto byte:digest) { result.push_back(hex[byte>>4]); result.push_back(hex[byte&15]); } return result;
}
void write_new(const fs::path& path,const json& value) {
  const auto text=value.dump(2)+"\n";
  handle file(CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
  require(file.value!=INVALID_HANDLE_VALUE,"New evidence file unavailable"); DWORD written{};
  require(WriteFile(file.value,text.data(),DWORD(text.size()),&written,nullptr)&&written==text.size()&&FlushFileBuffers(file.value),"Evidence write failed");
}
void stage(const fs::path& output,const char* name) {
  std::ofstream log(output/L"worker-stages.ndjson",std::ios::app);
  log<<json{{"stage",name},{"qpc",qpc()},{"pid",GetCurrentProcessId()},{"tid",GetCurrentThreadId()}}.dump()<<'\n';log.flush();
}
std::wstring quote(const std::wstring& value) {
  std::wstring result=L"\""; std::size_t slashes=0;
  for(wchar_t c:value) { if(c==L'\\'){++slashes;continue;} result.append(slashes*(c==L'\"'?2:1),L'\\'); slashes=0;
    if(c==L'\"')result+=L'\\';result+=c; }
  result.append(slashes*2,L'\\');return result+L'\"';
}
std::wstring event_name(const std::wstring& run,unsigned index) { return L"Local\\SOUI-Focus-"+run+L"-"+std::to_wstring(index); }
struct events final {
  std::array<HANDLE,5> values{}; // engine entered, focus entered, focus returned, release, engine exited
  events(const std::wstring& run,bool create) {
    try { for(unsigned n=0;n<values.size();++n) {
      values[n]=create?CreateEventW(nullptr,TRUE,FALSE,event_name(run,n).c_str()):OpenEventW(SYNCHRONIZE|EVENT_MODIFY_STATE,FALSE,event_name(run,n).c_str());
      require(values[n] && (!create||GetLastError()!=ERROR_ALREADY_EXISTS),"Unique controller event unavailable");
    }} catch(...) { for(auto value:values)if(value)CloseHandle(value);throw; }
  }
  ~events(){for(auto value:values)if(value)CloseHandle(value);}
  bool signaled(unsigned n)const {return WaitForSingleObject(values[n],0)==WAIT_OBJECT_0;}
  void signal(unsigned n)const {require(SetEvent(values[n])!=FALSE,"Controller event signal failed");}
};
struct options final { fs::path runtime,page,output; std::string hash; };
options arguments(wchar_t** argv,int start) {
  options value{argv[start],argv[start+1],argv[start+2],fs::path(argv[start+3]).string()};
  require(value.runtime.is_absolute()&&value.page.is_absolute()&&value.output.is_absolute(),"Use absolute frozen paths");
  require(fs::is_regular_file(value.runtime/L"node.dll")&&fs::is_regular_file(value.page/L"index.html"),"Frozen runtime/page missing");
  require(value.hash.size()==64&&value.hash.find_first_not_of("0123456789abcdef")==std::string::npos,"Expected lowercase node SHA256");
  return value;
}
soui::sdk::soui_miniblink_host* current_host=nullptr;
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
  if(message==soui::sdk::bridge_event_message&&current_host){current_host->deliver_posted_event(reinterpret_cast<void*>(lparam));return 0;}
  if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
  return DefWindowProcW(window,message,wparam,lparam);
}
void pump(soui::sdk::soui_miniblink_host& host) {
  MSG message{};for(unsigned n=0;n<32&&PeekMessageW(&message,nullptr,0,0,PM_REMOVE);++n){TranslateMessage(&message);DispatchMessageW(&message);}
  host.pump();MsgWaitForMultipleObjectsEx(0,nullptr,1,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
}
int worker(const options& input,const std::wstring& run,const std::wstring& expected_desktop) {
  require(object_name(GetThreadDesktop(GetCurrentThreadId()))==expected_desktop,"Worker is not on its isolated desktop");
  events event(run,false);
  struct release_on_exit { events& event; ~release_on_exit(){SetEvent(event.values[3]);} } release{event};
  json report{{"schema","soui.host-focus-progress.worker/v1"},{"pid",GetCurrentProcessId()},
    {"isolatedDesktop",fs::path(expected_desktop).string()},{"windowShown",false},{"desktopInputInjected",false},{"performanceVerified",false}};
  SetEnvironmentVariableW(L"SOUI_GPU_COMPOSITOR",nullptr);SetEnvironmentVariableW(L"SOUI_GPU_DIRECT_PRESENT",nullptr);
  SetEnvironmentVariableW(L"SOUI_ENTRY_FILE",nullptr);SetEnvironmentVariableW(L"SOUI_ENGINE_SHARED_TURN_BUDGET_US",L"0");
  SetEnvironmentVariableW(L"SOUI_DIAGNOSTIC_SCRIPT",nullptr);
  SetEnvironmentVariableW(L"SOUI_CPU_DRAW_INTERVAL_MS",nullptr);SetEnvironmentVariableW(L"SOUI_COMPOSITOR_FRAME_RATE",nullptr);
  SetEnvironmentVariableW(L"SOUI_SOURCE_MAIN_FRAME_SCHEDULER",nullptr);SetEnvironmentVariableW(L"SOUI_PAINT_CALLBACK_OTHER_THREAD",nullptr);
  require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"Worker COM apartment failed");
  soui::sdk::json_bridge bridge("app://host-focus-progress");
  std::atomic<unsigned> calibrated{0};
  require(bridge.register_method("focus.calibration.ready","focus.calibration",[&](std::string_view params){
    require(json::parse(params)==json{{"ready",true}},"Bad calibration params"); ++calibrated; return std::string("{\"ready\":true}");
  }),"Calibration registration failed");
  auto adapter=std::make_unique<soui::sdk::dynamic_miniblink_adapter>(soui::sdk::diagnostics::mode::scalar);
  auto* actual_adapter=adapter.get();
  auto host=std::make_unique<soui::sdk::soui_miniblink_host>(std::move(adapter));current_host=host.get();
  HWND window=nullptr; bool passed=false;
  try {
    stage(input.output,"runtime.hash");require(sha256(input.runtime/L"node.dll")==input.hash,"Requested node hash changed");
    WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"SOUIHiddenFocusProgress";
    require(RegisterClassW(&cls)!=0,"RegisterClass failed");
    window=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,cls.lpszClassName,L"Hidden focus progress",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
      0,0,400,300,nullptr,nullptr,cls.hInstance,nullptr);
    require(window && !IsWindowVisible(window),"Owned window must remain hidden");
    soui::sdk::host_config config;
#ifdef _WIN64
    config.architecture="x64";
#else
    config.architecture="x86";
#endif
    config.miniblink_binary=input.runtime/L"node.dll";config.resource_root=input.page;config.owner_window=window;config.bridge_adapter=&bridge;
    stage(input.output,"host.start.before");const auto started=host->start(config);report["startCode"]=started.code;
    stage(input.output,"host.start.after");require(started.ok,"Actual public Host start failed");
    const auto ready_deadline=clock_type::now()+15s;
    while(clock_type::now()<ready_deadline){pump(*host);if(host->document_ready()&&host->application_ready()&&calibrated.load()==1)break;}
    report["documentReady"]=host->document_ready();report["applicationReady"]=host->application_ready();report["calibrated"]=calibrated.load();
    require(report["documentReady"].get<bool>()&&report["applicationReady"].get<bool>()&&calibrated==1,"Real bootstrap/probe round trip did not become ready");
    const auto diagnostic=host->application_diagnostics();report["applicationDiagnosticsBeforeHold"]=diagnostic;
    const auto fields=","+diagnostic+",";
    require(fields.find(",frame_negotiated=true,")!=std::string::npos&&fields.find(",frame_applied_hz=60,")!=std::string::npos&&
      fields.find(",frame_feedback_supported=false,")!=std::string::npos,"Candidate frame capability/fallback diagnostic did not match");
    report["negotiatedDefault60WithLegacyBridgeFeedback"]=true;
    require(!IsWindowVisible(window),"Public Host unexpectedly showed parent");
    soui::sdk::diagnostics::renderer_observation renderer;
    (void)soui::sdk::read_miniblink_renderer_metrics(renderer);
    report["loadedModule"]=renderer.module_path;report["loadedIdentityBound"]=renderer.identity_bound;report["loadedModuleMatches"]=renderer.module_matches;
    require(renderer.identity_bound&&renderer.module_matches,"Loaded renderer identity unavailable");
    const auto loaded=fs::u8path(renderer.module_path);
    require(fs::equivalent(loaded,input.runtime/L"node.dll")&&sha256(loaded)==input.hash,"Actual loaded node differs from frozen runtime");
    report["loadedNodeSha256"]=input.hash;
    auto* engine=soui::sdk::detail::engine_admission::host_engine(*actual_adapter);require(engine,"Actual mounted Engine unavailable");
    // Exercise a real native transition instead of focusing an already focused child.
    SetFocus(window);require(GetFocus()==window,"Private-desktop owner focus calibration failed");
    stage(input.output,"engine.hold.queued");
    require(engine->post([&event]{SetEvent(event.values[0]);WaitForSingleObject(event.values[3],10000);SetEvent(event.values[4]);}),"Engine hold admission failed");
    require(WaitForSingleObject(event.values[0],2000)==WAIT_OBJECT_0&&!event.signaled(4),"Actual Engine did not enter held callback");
    stage(input.output,"focus.before");event.signal(1);const auto began=qpc();
    const auto focused=host->focus();const auto ended=qpc();
    const auto native_focus=GetFocus();const bool child_focused=native_focus&&IsChild(window,native_focus);
    const bool before_release=!event.signaled(3)&&!event.signaled(4);event.signal(2);
    report["focusCode"]=focused.code;report["focusOk"]=focused.ok;report["focusBeginQpc"]=began;report["focusReturnQpc"]=ended;
    report["returnedBeforeEngineRelease"]=before_release;stage(input.output,"focus.after");
    report["nativeChildFocused"]=child_focused;
    require(focused.ok&&before_release&&child_focused,"Public focus failed, missed native child, or waited for held Engine");
    require(WaitForSingleObject(event.values[3],5000)==WAIT_OBJECT_0,"Controller failed to release Engine");
    auto completed=std::make_shared<handle>(CreateEventW(nullptr,TRUE,FALSE,nullptr));require(completed->value,"Completion event unavailable");
    require(engine->post([completed]{SetEvent(completed->value);}),"Post-focus FIFO fence rejected");
    const auto fence_deadline=clock_type::now()+5s;
    while(WaitForSingleObject(completed->value,0)!=WAIT_OBJECT_0&&clock_type::now()<fence_deadline)pump(*host);
    require(WaitForSingleObject(completed->value,0)==WAIT_OBJECT_0,"Queued focus failed to reach its FIFO fence");
    report["postFocusFenceCompleted"]=true;passed=true;
  } catch(const std::exception& error){report["error"]=error.what();}
  SetEvent(event.values[3]);stage(input.output,"host.destroy.before");
  auto close_deadline=clock_type::now()+10s;host_result closed;
  do{closed=host->destroy();if(closed.ok)break;pump(*host);}while(clock_type::now()<close_deadline);
  report["destroyCode"]=closed.code;report["destroyed"]=closed.ok;
  if(!closed.ok){report["passed"]=false;write_new(input.output/L"worker-result.json",report);std::_Exit(3);}
  current_host=nullptr;host.reset();if(window)DestroyWindow(window);
  const auto stopped=soui::sdk::wait_for_miniblink_runtime_shutdown(10s);
  report["runtimeShutdown"]=stopped.ok;report["shutdownCode"]=stopped.code;report["passed"]=passed&&stopped.ok;
  stage(input.output,"worker.complete");write_new(input.output/L"worker-result.json",report);CoUninitialize();return passed&&stopped.ok?0:1;
}
int controller(const options& input) {
  require(fs::create_directory(input.output),"Output directory must be fresh");
  json report{{"schema","soui.host-focus-progress.controller/v1"},{"passed",false},{"performanceVerified",false},{"inputDesktopSwitched",false}};
  try {
    desktop active(OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS));
    const auto input_name=object_name(active.value);
    GUID guid{};require(SUCCEEDED(CoCreateGuid(&guid)),"Run identity unavailable");wchar_t text[40]{};StringFromGUID2(guid,text,40);
    std::wstring run;for(wchar_t c:text)if((c>=L'0'&&c<=L'9')||(c>=L'A'&&c<=L'F')||(c>=L'a'&&c<=L'f'))run+=c;
    require(run.size()==32,"Run identity invalid");const auto name=L"SOUI-Focus-"+run;require(name!=input_name,"Refuse input desktop");
    desktop isolated(CreateDesktopW(name.c_str(),nullptr,nullptr,0,DESKTOP_CREATEWINDOW|DESKTOP_READOBJECTS|DESKTOP_WRITEOBJECTS|DESKTOP_ENUMERATE,nullptr));
    require(isolated.value&&object_name(isolated.value)==name,"Private non-input desktop unavailable; no fallback");
    events event(run,true);report["inputDesktop"]=fs::path(input_name).string();report["isolatedDesktop"]=fs::path(name).string();
    handle job(CreateJobObjectW(nullptr,nullptr));require(job.value,"Owned Job unavailable");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    require(SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),"Owned Job limit failed");
    std::array<wchar_t,32768> executable{};const auto length=GetModuleFileNameW(nullptr,executable.data(),DWORD(executable.size()));
    require(length&&length<executable.size(),"Controller executable identity unavailable");
    const auto command=quote(executable.data())+L" --worker "+quote(input.runtime.wstring())+L" "+quote(input.page.wstring())+L" "+quote(input.output.wstring())+
      L" "+quote(std::wstring(input.hash.begin(),input.hash.end()))+L" "+quote(run)+L" "+quote(name);
    std::wstring mutable_command=command;STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.lpDesktop=const_cast<wchar_t*>(name.c_str());
    startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
    require(CreateProcessW(executable.data(),mutable_command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,input.runtime.c_str(),&startup,&process),"Hidden isolated child creation failed");
    handle child(process.hProcess),thread(process.hThread);report["childPid"]=process.dwProcessId;
    if(!AssignProcessToJobObject(job.value,child.value)){
      report["suspendedChildCleanupRequested"]=TerminateProcess(child.value,4)!=FALSE;
      report["suspendedChildCleanupExited"]=WaitForSingleObject(child.value,3000)==WAIT_OBJECT_0;
      throw std::runtime_error("Could not own suspended child in Job");
    }
    if(ResumeThread(thread.value)==DWORD(-1)){
      report["suspendedChildJobCleanupRequested"]=TerminateJobObject(job.value,4)!=FALSE;
      report["suspendedChildCleanupExited"]=WaitForSingleObject(child.value,3000)==WAIT_OBJECT_0;
      throw std::runtime_error("Child resume failed");
    }
    HANDLE startup_wait[]{child.value,event.values[1]};
    const auto starting=WaitForMultipleObjects(2,startup_wait,FALSE,30000);
    bool returned=false;
    if(starting==WAIT_OBJECT_0+1&&event.signaled(0)&&!event.signaled(3)&&!event.signaled(4)){
      HANDLE focus_wait[]{child.value,event.values[2]};
      returned=WaitForMultipleObjects(2,focus_wait,FALSE,2000)==WAIT_OBJECT_0+1&&!event.signaled(3)&&!event.signaled(4);
    }
    report["focusReturnedWhileEngineHeld"]=returned;
    if(!returned){
      report["failureStage"]=starting==WAIT_OBJECT_0+1?"focus-return-watchdog":"startup-watchdog-or-child-exit";
      write_new(input.output/L"watchdog-failure.json",report);
      if(WaitForSingleObject(child.value,0)==WAIT_TIMEOUT){
        // DbgHelp can block while inspecting a stuck process: isolate it too.
        auto dump_command=quote(executable.data())+L" --dump "+std::to_wstring(process.dwProcessId)+L" "+quote((input.output/L"watchdog-failure.dmp").wstring());
        PROCESS_INFORMATION dump_process{};
        if(CreateProcessW(executable.data(),dump_command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,input.runtime.c_str(),&startup,&dump_process)){
          handle dumper(dump_process.hProcess),dump_thread(dump_process.hThread);
          if(AssignProcessToJobObject(job.value,dumper.value)&&ResumeThread(dump_thread.value)!=DWORD(-1)){
            const auto dumped=WaitForSingleObject(dumper.value,3000)==WAIT_OBJECT_0;
            DWORD dump_code=STILL_ACTIVE;GetExitCodeProcess(dumper.value,&dump_code);
            report["minidumpWritten"]=dumped&&dump_code==0;
            if(!dumped){report["minidumpTimedOut"]=true;TerminateProcess(dumper.value,6);WaitForSingleObject(dumper.value,3000);}
          }else{TerminateProcess(dumper.value,6);WaitForSingleObject(dumper.value,3000);report["minidumpWritten"]=false;}
        }else report["minidumpWritten"]=false;
      }
    }
    event.signal(3); // Success OR failure release; a timed-out run stays failed.
    bool exited=WaitForSingleObject(child.value,25000)==WAIT_OBJECT_0;
    if(!exited){report["forcedOwnedJobTermination"]=true;TerminateJobObject(job.value,5);WaitForSingleObject(child.value,3000);}
    DWORD code=STILL_ACTIVE;GetExitCodeProcess(child.value,&code);report["childExitCode"]=code;report["naturalExit"]=exited;
    // A root process can exit while descendants are alive. Job destruction must
    // not silently kill them after a passing result has already been decided.
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};bool job_empty=false,job_query=false;
    const auto job_deadline=clock_type::now()+3s;
    do{
      job_query=QueryInformationJobObject(job.value,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),nullptr)!=FALSE;
      if(!job_query)break;
      if(accounting.ActiveProcesses==0){job_empty=true;break;}
      Sleep(10);
    }while(clock_type::now()<job_deadline);
    report["jobQuerySucceeded"]=job_query;report["activeJobProcessesBeforeCleanup"]=accounting.ActiveProcesses;report["jobEmptyBeforeCleanup"]=job_empty;
    if(!job_empty){report["forcedDescendantCleanup"]=true;TerminateJobObject(job.value,5);}
    desktop after(OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS));report["inputDesktopNameUnchanged"]=object_name(after.value)==input_name;
    report["passed"]=returned&&exited&&code==0&&job_empty&&report["inputDesktopNameUnchanged"].get<bool>();
  }catch(const std::exception& error){report["error"]=error.what();report["notExecutedOrFailed"]=true;}
  write_new(input.output/L"controller-result.json",report);std::cout<<report.dump()<<'\n';return report["passed"].get<bool>()?0:1;
}
}
int wmain(int argc,wchar_t** argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
  try {
    if(argc==4&&std::wstring(argv[1])==L"--dump"){
      const auto pid=std::stoul(argv[2]);require(pid!=0,"Dump PID invalid");
      handle target(OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|PROCESS_DUP_HANDLE,FALSE,pid));
      handle dump(CreateFileW(argv[3],GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
      require(target.value&&dump.value!=INVALID_HANDLE_VALUE,"Dump handles unavailable");
      return MiniDumpWriteDump(target.value,pid,dump.value,static_cast<MINIDUMP_TYPE>(MiniDumpNormal|MiniDumpWithThreadInfo),nullptr,nullptr,nullptr)?0:1;
    }
    if(argc==8&&std::wstring(argv[1])==L"--worker")return worker(arguments(argv,2),argv[6],argv[7]);
    require(argc==5,"Arguments: absolute-runtime absolute-page-root fresh-output expected-node-sha256");
    return controller(arguments(argv,1));
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
