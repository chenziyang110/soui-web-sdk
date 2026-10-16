#include "public-endpoint-client.h"
#include <vector>
#include <utility>
using public_endpoint_tests::client;
using public_endpoint_tests::handle;
using public_endpoint_tests::json;
using public_endpoint_tests::require;
namespace {
std::uint64_t creation(HANDLE process) {
  FILETIME a{},b{},c{},d{};require(GetProcessTimes(process,&a,&b,&c,&d)!=FALSE,"held process identity unavailable");
  return (static_cast<std::uint64_t>(a.dwHighDateTime)<<32U)|a.dwLowDateTime;
}
std::wstring quote(const std::wstring& input) {
  std::wstring out=L"\"";unsigned slashes=0;
  for(const auto ch:input){if(ch==L'\\'){++slashes;continue;}out.append(slashes*(ch==L'"'?2:1),L'\\');slashes=0;if(ch==L'"')out+=L'\\';out+=ch;}
  out.append(slashes*2,L'\\');return out+L'"';
}
void emit(json row){LARGE_INTEGER tick{};require(QueryPerformanceCounter(&tick)!=FALSE,"QPC unavailable");row["qpc"]=std::to_string(tick.QuadPart);std::cout<<row.dump()<<'\n'<<std::flush;}
BOOL CALLBACK close_window(HWND window,LPARAM selected){DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid==static_cast<DWORD>(selected)&&GetWindow(window,GW_OWNER)==nullptr)PostMessageW(window,WM_CLOSE,0,0);return TRUE;}
ULONGLONG workflow_deadline=0;unsigned serial=0;
void deadline(){require(GetTickCount64()<workflow_deadline,"fixed 60 second workflow deadline");}
json call(client& pipe,const std::string& method,json params){
  deadline();const auto request="release-gui-"+std::to_string(++serial);
  require(pipe.write_raw(json{{"kind","request"},{"schemaVersion","soui.bridge/1"},{"requestId",request},{"method",method},{"params",std::move(params)},{"timeoutMs",8000}}.dump()),"actual framed write failed");
  const auto raw=pipe.read_raw();require(!raw.empty(),"actual response missing");const auto result=json::parse(raw);
  require(result.at("requestId")==request&&!result.contains("token")&&!result.contains("sessionId")&&!result.contains("origin")&&!result.contains("subject"),"response identity or disclosure failure");
  if(result.value("ok",false)!=true){emit({{"event","request-failed"},{"method",method},{"response",result}});throw std::runtime_error("native request rejected");}
  return result.at("result");
}
json gui(client& pipe,const std::string& family,json input){
  const auto op="release-operation-"+std::to_string(serial+1),base="reference.gui."+family;
  const auto start=call(pipe,base+".start",{{"operationId",op},{"input",std::move(input)}});
  require(start.at("accepted")==true&&start.at("taskId").is_string(),"GUI admission missing");const auto id=start.at("taskId").get<std::string>();
  for(unsigned attempt=0;attempt<80;++attempt){
    const auto state=call(pipe,base+".query",{{"taskId",id}});
    if(state.at("state")=="succeeded"){
      const auto receipt=state.at("result");require(receipt.at("confirmed")==true,"GUI confirmation missing");
      emit({{"event","gui-confirmed"},{"family",family},{"operationId",op},{"taskId",id},{"receipt",receipt}});return receipt;
    }
    require(state.at("state")!="failed"&&state.at("state")!="cancelled","GUI operation failed");Sleep(50);
  }
  throw std::runtime_error("GUI operation exceeded 80 actual queries");
}
json page_result(const json& receipt){return json::parse(receipt.at("guiResultJson").get<std::string>());}
json effect_result(const json& receipt){require(receipt.at("effectState")=="committed","native effect was not committed");return json::parse(receipt.at("effectResultJson").get<std::string>());}
void workflow(client& pipe){
  pipe.hello();const auto description=call(pipe,"reference.fixture.describe",json::object());require(description.at("rowCount")==10000,"actual fixture differs");
  require(page_result(gui(pipe,"status",json::object())).at("ready")==true,"actual GUI not ready");
  require(page_result(gui(pipe,"filter",{{"query",""},{"category","all"}})).at("changed")==true,"filter not applied");
  require(page_result(gui(pipe,"dialog_open",json::object())).at("open")==true,"dialog did not open");
  require(page_result(gui(pipe,"status",json::object())).at("dialogOpen")==true,"dialog state differs");
  require(page_result(gui(pipe,"dialog_close",json::object())).at("open")==false,"dialog did not close");
  const json form{{"name","发布验证中文"},{"notes","真实 GUI 和原生业务回执"}};
  require(page_result(gui(pipe,"form_set",form)).at("changed")==true,"form was not set");
  auto submit=form;submit["category"]="素材";const auto formReceipt=gui(pipe,"form_submit",submit);
  const auto committed=effect_result(formReceipt);require(page_result(formReceipt).at("accepted")==true&&committed.at("accepted")==true&&committed.at("receiptId").get<std::string>().size()>0,"real native form receipt missing");
  for(const bool cancel:{false,true}){
    const auto start=gui(pipe,"summary_start",{{"fixtureId","electron-smoothness-10k-v1"},{"category","all"},{"durationMs",cancel?60000:500}});
    const auto admitted=effect_result(start),view=page_result(start);const auto task=admitted.at("taskId").get<std::string>();
    require(admitted.at("accepted")==true&&view.at("admitted")==true&&view.at("taskId")==task,"native/GUI task admission mismatch");
    bool cancelled=false,complete=false;
    for(unsigned attempt=0;attempt<80;++attempt){
      const auto queried=gui(pipe,"summary_query",{{"taskId",task}}),native=effect_result(queried),shown=page_result(queried);
      require(native.at("taskId")==task&&shown.at("taskId")==task&&native.at("state")==shown.at("state")&&native.at("progress")==shown.at("progress"),"GUI/native task state mismatch");
      const auto state=native.at("state").get<std::string>();
      if(cancel&&!cancelled&&state=="running"&&native.at("progress").get<unsigned>()>0){
        const auto receipt=gui(pipe,"summary_cancel",{{"taskId",task}}),effect=effect_result(receipt);
        require(effect.at("cancelRequested")==true&&page_result(receipt).at("cancelRequested")==true,"actual cancellation request absent");cancelled=true;
      }
      if((!cancel&&state=="succeeded")||(cancel&&cancelled&&state=="cancelled")){
        if(!cancel){const auto result=native.at("result");require(result.at("fixtureId")=="electron-smoothness-10k-v1"&&result.at("processed")==10000&&result.at("matched")==10000&&result.at("groups").size()==5,"real summary result differs");}
        else require(native.at("result")==json::object(),"cancelled result was fabricated");
        emit({{"event","business-terminal"},{"taskId",task},{"cancel",cancel},{"native",native},{"gui",shown}});complete=true;break;
      }
      require(state!="failed"&&state!="partial","actual business failed");Sleep(50);
    }
    require(complete,"actual business never reached required terminal");
  }
}
}
int wmain(int argc,wchar_t** argv){
  HANDLE appHandle=nullptr;DWORD appPid=0;
  try{
    require(argc==2,"Usage: ReferenceGuiController ABS_APP_EXE");BOOL owned=FALSE;require(IsProcessInJob(GetCurrentProcess(),nullptr,&owned)&&owned,"real outer owned job required");
    const std::wstring app=argv[1];require(app.size()>3&&app[1]==L':'&&app.find(L'"')==std::wstring::npos,"absolute application required");
    const auto component=public_endpoint_tests::component();std::wstring cmd=quote(app)+L" --agent-pipe "+std::wstring(component.begin(),component.end())+L" --agent-allow-process "+std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};const auto cwd=app.substr(0,app.find_last_of(L"\\/"));
    require(CreateProcessW(app.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,cwd.c_str(),&startup,&process)!=FALSE,"actual app creation failed");
    appHandle=process.hProcess;appPid=process.dwProcessId;handle thread{process.hThread};BOOL childOwned=FALSE;require(IsProcessInJob(appHandle,nullptr,&childOwned)&&childOwned,"actual app escaped inherited job");
    const auto appFt=creation(appHandle);emit({{"event","app-created"},{"coordinatorPid",GetCurrentProcessId()},{"coordinatorCreationFileTime",std::to_string(creation(GetCurrentProcess()))},{"appPid",appPid},{"appCreationFileTime",std::to_string(appFt)},{"owned",true}});
    workflow_deadline=GetTickCount64()+60000;require(ResumeThread(thread.get())!=static_cast<DWORD>(-1),"actual app resume failed");
    const auto pipe=public_endpoint_tests::pipe_name(component);bool available=false;
    while(GetTickCount64()<workflow_deadline){require(WaitForSingleObject(appHandle,0)==WAIT_TIMEOUT,"app exited before endpoint ready");if(WaitNamedPipeW(pipe.c_str(),10)){available=true;break;}Sleep(10);}
    require(available,"authorized endpoint never ready");{client connection{component};workflow(connection);}
    EnumWindows(close_window,static_cast<LPARAM>(appPid));const auto remaining=workflow_deadline>GetTickCount64()?workflow_deadline-GetTickCount64():0;
    require(remaining&&WaitForSingleObject(appHandle,static_cast<DWORD>(remaining))==WAIT_OBJECT_0,"app did not close naturally within workflow deadline");DWORD exitCode=STILL_ACTIVE;require(GetExitCodeProcess(appHandle,&exitCode)&&exitCode==0&&creation(appHandle)==appFt,"actual app exit/identity differs");
    emit({{"event","complete"},{"appPid",appPid},{"appCreationFileTime",std::to_string(appFt)},{"appExitCode",exitCode},{"finiteGuiWorkflowPassed",true},{"physicalInputQualified",false},{"releaseReady",false}});
    CloseHandle(appHandle);return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';if(appHandle){EnumWindows(close_window,static_cast<LPARAM>(appPid));(void)WaitForSingleObject(appHandle,2000);CloseHandle(appHandle);}return 1;}
}
