// CPU/no-window tests of the production consumer gate, not a second state
// machine. Fake Ops control only Win32 results; two cases use actual Win32.
#include "manual-observation-gate.h"
#include <aclapi.h>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace model = soui::benchmark::manual_observation;
namespace {
void require(bool value, const char* message = "contract failed") { if (!value) throw std::runtime_error(message); }
HANDLE controller_handle() { return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(11)); }
HANDLE event_handle() { return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(22)); }
model::binding specimen() { return {std::string(32, 'a'), std::string(32, 'b'), 7, 8, 10, 20, 30, 1000}; }
struct fake_state final {
  model::u64 now{100}, controller_creation{20}; DWORD self_pid{7}; model::u64 self_creation{10};
  DWORD open_error{}, create_error{}, create_cleanup{}, controller_wait{WAIT_TIMEOUT}, event_wait{WAIT_TIMEOUT};
  DWORD wait_error{ERROR_INVALID_HANDLE}, creation_error{}, event_close_error{}, controller_close_error{};
  bool throw_create{}, creation_ok{true}; unsigned self_calls{}, opens{}, creates{}, waits{}, closes{}, event_closes{}, controller_closes{};
  std::wstring last_name;
};
struct fake_ops final {
  fake_state* state{};
  model::u64 tick() const noexcept { return state->now; }
  bool self(DWORD& pid, model::u64& born, DWORD&) const noexcept { ++state->self_calls; pid=state->self_pid; born=state->self_creation; return true; }
  model::handle_result open_controller(DWORD) const noexcept { ++state->opens; return {state->open_error?nullptr:controller_handle(),state->open_error,0}; }
  bool creation(HANDLE, model::u64& born, DWORD& error) const noexcept { born=state->controller_creation;error=state->creation_error;return state->creation_ok; }
  model::wait_result wait_zero(HANDLE h) const noexcept {
    ++state->waits;const auto result=h==controller_handle()?state->controller_wait:state->event_wait;
    return {result,result==WAIT_FAILED?state->wait_error:0};
  }
  model::handle_result create_event(const std::wstring& name) const {
    ++state->creates;state->last_name=name;if(state->throw_create)throw std::runtime_error("creation exception");
    return {state->create_error&&state->create_error!=ERROR_ALREADY_EXISTS?nullptr:event_handle(),state->create_error,state->create_cleanup};
  }
  DWORD close(HANDLE h) const noexcept {
    ++state->closes;if(h==event_handle()){++state->event_closes;return state->event_close_error;}
    ++state->controller_closes;return state->controller_close_error;
  }
};
using fake_gate=model::basic_gate<fake_ops>;
void code(const model::check_result& r, const char* expected) { require(r.status==model::state::unavailable&&r.code&&std::strcmp(r.code,expected)==0); }
struct owned_handle final {
  HANDLE value{};
  ~owned_handle(){if(value)CloseHandle(value);}
  void close(){const auto h=value;value=nullptr;require(!h||CloseHandle(h),"checked CloseHandle");}
};
model::u64 creation(HANDLE h){model::u64 born{};DWORD error{};require(model::win32_ops{}.creation(h,born,error),"process creation");return born;}
std::string unique_nonce(){std::ostringstream out;out<<std::hex<<std::setfill('0')<<std::setw(16)<<creation(GetCurrentProcess())<<std::setw(16)<<GetTickCount64();return out.str();}
model::binding real_binding(DWORD controller_pid, model::u64 controller_creation){
  auto b=specimen();b.run=unique_nonce();b.nonce=b.run;b.host_pid=GetCurrentProcessId();b.host_creation=creation(GetCurrentProcess());
  // No HWND exists in this fixture. This nonzero value tests tuple/name binding
  // only; the production Host must verify its real HWND before cold creation.
  b.hwnd=1;b.controller_pid=controller_pid;b.controller_creation=controller_creation;b.expires_tick=GetTickCount64()+150000;return b;
}
void actual_event_case(){
  const auto b=real_binding(GetCurrentProcessId(),creation(GetCurrentProcess()));model::gate gate;
  require(gate.create(b).ok);require(gate.check_now().status==model::state::pending);
  model::gate collision;const auto exists=collision.create(b);
  require(!exists.ok&&exists.win32_error==ERROR_ALREADY_EXISTS&&std::strcmp(exists.code,"E_MANUAL_GATE_ALREADY_EXISTS")==0);
  require(gate.check_now().status==model::state::pending,"Collision must not signal/reset original");
  owned_handle setter{OpenEventW(EVENT_MODIFY_STATE|SYNCHRONIZE|READ_CONTROL,FALSE,gate.name().c_str())};require(setter.value,"Open existing event");
  PSECURITY_DESCRIPTOR sd{};PACL dacl{};
  require(GetSecurityInfo(setter.value,SE_KERNEL_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&dacl,nullptr,&sd)==ERROR_SUCCESS,"Read explicit ACL");
  struct local_guard{HLOCAL p;~local_guard(){if(p)LocalFree(p);}} memory{sd};
  SECURITY_DESCRIPTOR_CONTROL control{};DWORD revision{};require(GetSecurityDescriptorControl(sd,&control,&revision));
  require((control&SE_DACL_PROTECTED)&&dacl&&dacl->AceCount==1,"Only protected logon ACE");
  void* raw{};require(GetAce(dacl,0,&raw));const auto* ace=static_cast<const ACCESS_ALLOWED_ACE*>(raw);
  require(ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE&&ace->Mask==(SYNCHRONIZE|EVENT_MODIFY_STATE|READ_CONTROL));
  const auto sid=const_cast<DWORD*>(&ace->SidStart);require(IsValidSid(sid));
  const SID_IDENTIFIER_AUTHORITY authority=SECURITY_NT_AUTHORITY;
  require(std::memcmp(GetSidIdentifierAuthority(sid),&authority,sizeof(authority))==0&&
      *GetSidSubAuthorityCount(sid)==3&&*GetSidSubAuthority(sid,0)==SECURITY_LOGON_IDS_RID,"Logon SID not Everyone/user-wide ACE");
  require(SetEvent(setter.value));require(gate.check_now().status==model::state::released);
  require(gate.check_now().status==model::state::released,"Manual-reset remains released across Finish checks");
  setter.close();require(gate.close_checked().ok);require(gate.close_checked().ok);require(collision.close_checked().ok);
}
void actual_controller_exit(){
  const auto suffix=unique_nonce();const auto release_name=L"Local\\SOUI-Gate-Contract-Release-"+std::wstring(suffix.begin(),suffix.end());
  SetLastError(ERROR_SUCCESS);owned_handle release{CreateEventW(nullptr,TRUE,FALSE,release_name.c_str())};const auto create_error=GetLastError();
  require(release.value&&create_error!=ERROR_ALREADY_EXISTS);
  wchar_t path[32768]{};const auto count=GetModuleFileNameW(nullptr,path,32768);require(count&&count<32768);
  std::wstring command=L"\""+std::wstring(path,count)+L"\" --gate-child \""+release_name+L"\"";
  STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
  require(CreateProcessW(path,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child),"Create own no-window controller child");
  owned_handle process{child.hProcess},thread{child.hThread};
  // Any failed assertion still releases/joins only this owned child. No kill,
  // foreground operation, HWND, injected input, COM or SDK is involved.
  struct child_cleanup{HANDLE process,release;bool active{true};~child_cleanup(){if(active){SetEvent(release);WaitForSingleObject(process,5000);}}} cleanup{process.value,release.value};
  thread.close();const auto born=creation(process.value);model::gate gate;require(gate.create(real_binding(child.dwProcessId,born)).ok);
  require(gate.check_now().status==model::state::pending);require(SetEvent(release.value));
  require(WaitForSingleObject(process.value,5000)==WAIT_OBJECT_0,"Own child natural terminal");DWORD exit{};
  require(GetExitCodeProcess(process.value,&exit)&&exit==0&&creation(process.value)==born,"Exact own child exit0");
  code(gate.check_now(),"E_MANUAL_GATE_CONTROLLER_EXITED");require(gate.close_checked().ok);
  cleanup.active=false;process.close();release.close();
}
}

int wmain(int argc,wchar_t** argv){
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
  if(argc==3&&std::wstring(argv[1])==L"--gate-child"){
    owned_handle release{OpenEventW(SYNCHRONIZE,FALSE,argv[2])};if(!release.value)return 2;
    const auto waited=WaitForSingleObject(release.value,5000);try{release.close();}catch(...){return 3;}
    return waited==WAIT_OBJECT_0?0:4;
  }
  if(argc!=1)return 2;
  const std::vector<std::pair<const char*,std::function<void()>>> tests={
    {"pending never consumes the manual-reset state",[]{fake_state s;fake_gate g(fake_ops{&s});require(g.create(specimen()).ok);for(int i=0;i<3;++i)require(g.check_now().status==model::state::pending);require(s.creates==1&&s.closes==0);require(g.close_checked().ok);}},
    {"signal releases repeated checks without resetting",[]{fake_state s;fake_gate g(fake_ops{&s});require(g.create(specimen()).ok);s.event_wait=WAIT_OBJECT_0;require(g.check_now().status==model::state::released);require(g.check_now().status==model::state::released);}},
    {"invalid binding and expiry do not open handles",[]{for(int i=0;i<4;++i){fake_state s;fake_gate g(fake_ops{&s});auto b=specimen();if(i==0)b.nonce="bad";if(i==1)b.hwnd=0;if(i==2)b.expires_tick=100;if(i==3)b.expires_tick=150101;require(!g.create(b).ok);require(!s.opens&&!s.creates);}}},
    {"exact own Host identity is required",[]{fake_state s;s.self_creation++;fake_gate g(fake_ops{&s});require(!g.create(specimen()).ok&&!s.opens);}},
    {"controller open incarnation and liveness fail before event creation",[]{for(int mode=0;mode<4;++mode){fake_state s;if(mode==0)s.controller_creation++;if(mode==1)s.controller_wait=WAIT_OBJECT_0;if(mode==2)s.controller_wait=WAIT_FAILED;if(mode==3)s.open_error=ERROR_ACCESS_DENIED;fake_gate g(fake_ops{&s});require(!g.create(specimen()).ok&&s.controller_closes==(mode==3?0U:1U)&&!s.creates);}}},
    {"already-existing event is rejected and its returned handle retired",[]{fake_state s;s.create_error=ERROR_ALREADY_EXISTS;fake_gate g(fake_ops{&s});auto r=g.create(specimen());require(!r.ok&&r.win32_error==ERROR_ALREADY_EXISTS&&s.event_closes==1&&s.controller_closes==1);}},
    {"cold error retains original failure across cleanup",[]{fake_state s;s.create_error=ERROR_ACCESS_DENIED;s.controller_close_error=ERROR_INVALID_HANDLE;fake_gate g(fake_ops{&s});auto r=g.create(specimen());require(!r.ok&&r.win32_error==ERROR_ACCESS_DENIED&&r.cleanup_error==ERROR_INVALID_HANDLE);}},
    {"original expiry is not renewed by repeated Finish",[]{fake_state s;fake_gate g(fake_ops{&s});require(g.create(specimen()).ok);s.now=999;require(g.check_now().status==model::state::pending);const auto waits=s.waits;s.now=1000;code(g.check_now(),"E_MANUAL_GATE_EXPIRED");require(s.waits==waits);}},
    {"clock regression cannot be pending or released",[]{fake_state s;fake_gate g(fake_ops{&s});require(g.create(specimen()).ok);s.now=99;code(g.check_now(),"E_MANUAL_GATE_CLOCK");}},
    {"controller termination outranks a signaled event",[]{fake_state s;fake_gate g(fake_ops{&s});require(g.create(specimen()).ok);s.event_wait=WAIT_OBJECT_0;s.controller_wait=WAIT_OBJECT_0;code(g.check_now(),"E_MANUAL_GATE_CONTROLLER_EXITED");}},
    {"Wait failures retain actual error rather than pending",[]{for(bool controller:{false,true}){fake_state s;fake_gate g(fake_ops{&s});require(g.create(specimen()).ok);if(controller)s.controller_wait=WAIT_FAILED;else s.event_wait=WAIT_FAILED;const auto r=g.check_now();require(r.status==model::state::unavailable&&r.win32_error==ERROR_INVALID_HANDLE);}}},
    {"checked close reports before return and consumes each handle only once",[]{fake_state s;{fake_gate g(fake_ops{&s});require(g.create(specimen()).ok);s.event_close_error=ERROR_ACCESS_DENIED;s.controller_close_error=ERROR_INVALID_HANDLE;const auto r=g.close_checked();require(!r.ok&&r.win32_error==ERROR_ACCESS_DENIED&&r.cleanup_error==ERROR_INVALID_HANDLE);require(g.close_checked().ok);code(g.check_now(),"E_MANUAL_GATE_UNINITIALIZED");}require(s.closes==2);}},
    {"cold exception retires held controller and preserves exception",[]{fake_state s;s.throw_create=true;fake_gate g(fake_ops{&s});bool caught=false;try{g.create(specimen());}catch(const std::runtime_error& e){caught=std::strcmp(e.what(),"creation exception")==0;}require(caught&&s.controller_closes==1&&!s.event_closes);}},
    {"reconfiguration cannot replace a live or closed gate",[]{fake_state s;fake_gate g(fake_ops{&s});require(g.create(specimen()).ok);require(!g.create(specimen()).ok);require(g.close_checked().ok);require(!g.create(specimen()).ok&&s.creates==1);}},
    {"actual Win32 event ACL conflict Wait0 Set and checked close",actual_event_case},
    {"actual owned no-window controller child exits naturally",actual_controller_exit}
  };
  unsigned passed{};
  for(const auto& test:tests){try{test.second();++passed;}catch(const std::exception& e){std::cerr<<test.first<<": "<<e.what()<<'\n';return 1;}}
  std::cout<<"{\"schema\":\"soui.manual-observation-gate-contracts/v1\",\"passed\":"<<passed
      <<",\"fakeCases\":14,\"actualWin32Cases\":2,\"childProcessesCreated\":1,\"guiExecuted\":false,\"hwndOwnershipVerified\":false,\"inputInjected\":false,\"performanceGate\":false}\n";
  return 0;
}
