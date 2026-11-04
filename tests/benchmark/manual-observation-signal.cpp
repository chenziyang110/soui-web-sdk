// CPU guard contracts for the production private signal helper. No window or
// event is created; the one actual Win32 case must stop at invalid HWND.
#include "manual-observation-signal.h"
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace model = soui::benchmark::manual_observation;
namespace {
void require(bool value, const char* why = "signal contract") { if (!value) throw std::runtime_error(why); }
HANDLE handle(unsigned n) { return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(n)); }
model::binding specimen() { return {std::string(32, 'a'), std::string(32, 'b'), 7, 8, 10, 20, 30, 1000}; }
struct fake_ops final {
  model::u64 now{100}, final_tick{100};
  DWORD host_open_error{}, controller_open_error{}, event_open_error{}, set_error{}, cleanup[3]{};
  unsigned opens{}, event_opens{}, sets{}, windows{}, closes[3]{}, waits[2]{};
  int identity_bad{-1}, creation_fail{-1}, exited{-1}, wait_failed{-1}, after_exited{-1}, after_wait_failed{-1};
  bool bad_window{}, late_bad_window{}, throw_open_event{}, throw_late_window{};
  std::wstring opened_name;
  model::u64 tick() const noexcept { return event_opens ? final_tick : now; }
  model::handle_result open_process(DWORD pid) noexcept {
    ++opens;const auto e=pid==7?host_open_error:controller_open_error;
    return {e?nullptr:handle(pid==7?1:2),e,0};
  }
  bool creation(HANDLE h, model::u64& value, DWORD& error) const noexcept {
    const int n=h==handle(1)?0:1;value=n==0?10:20;
    if(identity_bad==n)++value;
    if(creation_fail==n){error=ERROR_ACCESS_DENIED;return false;}return true;
  }
  model::wait_result wait_zero(HANDLE h) noexcept {
    const int n=h==handle(1)?0:1;++waits[n];
    if(wait_failed==n||(event_opens&&after_wait_failed==n))return{WAIT_FAILED,ERROR_INVALID_HANDLE};
    if(exited==n||(event_opens&&after_exited==n))return{WAIT_OBJECT_0,0};return{WAIT_TIMEOUT,0};
  }
  bool window_matches(HWND, DWORD pid, DWORD& error) {
    require(pid==7);++windows;
    if(event_opens&&throw_late_window)throw std::runtime_error("window fault");
    if(bad_window||(event_opens&&late_bad_window)){error=ERROR_INVALID_WINDOW_HANDLE;return false;}return true;
  }
  model::handle_result open_event(const std::wstring& name) {
    ++event_opens;opened_name=name;if(throw_open_event)throw std::runtime_error("open fault");
    return{event_open_error?nullptr:handle(3),event_open_error,0};
  }
  DWORD set(HANDLE h) noexcept { if(h!=handle(3))return ERROR_INVALID_HANDLE;++sets;return set_error; }
  DWORD close(HANDLE h) noexcept {
    const unsigned i=h==handle(1)?0:(h==handle(2)?1:2);++closes[i];return cleanup[i];
  }
};
void expect_error(const model::operation_result& r,const char* code) {
  require(!r.ok&&r.code&&std::strcmp(r.code,code)==0);
}
void once_closed(const fake_ops& o,unsigned host,unsigned controller,unsigned event) {
  require(o.closes[0]==host&&o.closes[1]==controller&&o.closes[2]==event,"every acquired handle closed exactly once");
}
void actual_invalid_window() {
  auto b=specimen();b.host_pid=GetCurrentProcessId();b.controller_pid=b.host_pid;
  DWORD error{};require(model::win32_ops{}.creation(GetCurrentProcess(),b.host_creation,error));
  b.controller_creation=b.host_creation;b.hwnd=static_cast<model::u64>(~static_cast<std::uintptr_t>(0));
  b.expires_tick=GetTickCount64()+10000;
  const auto r=model::signal(b);expect_error(r,"E_MANUAL_SIGNAL_WINDOW");require(!r.cleanup_error);
}
}

int main() {
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
  const std::vector<std::pair<const char*,std::function<void()>>> cases={
    {"valid tuple uses computed name one Set and three checked closes",[]{fake_ops o;const auto b=specimen();const auto r=model::signal_with_ops(b,o);require(r.ok&&!r.code&&!r.win32_error&&!r.cleanup_error);require(o.opened_name==model::event_name(b)&&o.sets==1&&o.windows==2&&o.waits[0]==2&&o.waits[1]==2);once_closed(o,1,1,1);}},
    {"invalid binding never opens",[]{for(int n=0;n<3;++n){fake_ops o;auto b=specimen();if(n==0)b.nonce="bad";if(n==1)b.hwnd=0;if(n==2)b.run=std::string(32,'0');expect_error(model::signal_with_ops(b,o),"E_MANUAL_SIGNAL_BINDING");require(!o.opens&&!o.event_opens&&!o.sets);}}},
    {"expired or unbounded original expiry never opens",[]{for(auto deadline:{100ull,150101ull}){fake_ops o;auto b=specimen();b.expires_tick=deadline;expect_error(model::signal_with_ops(b,o),"E_MANUAL_SIGNAL_EXPIRY");require(!o.opens&&!o.sets);}}},
    {"process open failures preserve API error and retire earlier handles",[]{for(int n=0;n<2;++n){fake_ops o;(n?o.controller_open_error:o.host_open_error)=ERROR_ACCESS_DENIED;const auto r=model::signal_with_ops(specimen(),o);expect_error(r,n?"E_MANUAL_SIGNAL_CONTROLLER_OPEN":"E_MANUAL_SIGNAL_HOST_OPEN");require(r.win32_error==ERROR_ACCESS_DENIED&&!o.event_opens&&!o.sets);once_closed(o,n?1:0,0,0);}}},
    {"both process incarnations and creation API failures fail before event open",[]{for(int n=0;n<4;++n){fake_ops o;if(n<2)o.identity_bad=n;else o.creation_fail=n-2;const auto r=model::signal_with_ops(specimen(),o);expect_error(r,(n%2)?"E_MANUAL_SIGNAL_CONTROLLER_IDENTITY":"E_MANUAL_SIGNAL_HOST_IDENTITY");require(!o.event_opens&&!o.sets);if(n>=2)require(r.win32_error==ERROR_ACCESS_DENIED);}}},
    {"both process terminal and wait failures are rejected before and after OpenEvent",[]{for(int n=0;n<8;++n){fake_ops o;const int process=n%2;if(n<2)o.exited=process;else if(n<4)o.wait_failed=process;else if(n<6)o.after_exited=process;else o.after_wait_failed=process;const auto r=model::signal_with_ops(specimen(),o);expect_error(r,process?"E_MANUAL_SIGNAL_CONTROLLER_NOT_ALIVE":"E_MANUAL_SIGNAL_HOST_NOT_ALIVE");require(!o.sets);if((n>=2&&n<4)||n>=6)require(r.win32_error==ERROR_INVALID_HANDLE);once_closed(o,1,(n<4&&process==0)?0:1,n>=4?1:0);}}},
    {"HWND owner or class mismatch before and after event open never signals",[]{for(bool late:{false,true}){fake_ops o;o.bad_window=!late;o.late_bad_window=late;const auto r=model::signal_with_ops(specimen(),o);expect_error(r,"E_MANUAL_SIGNAL_WINDOW");require(r.win32_error==ERROR_INVALID_WINDOW_HANDLE&&!o.sets);once_closed(o,1,1,late?1:0);}}},
    {"missing event is not created or replaced",[]{fake_ops o;o.event_open_error=ERROR_FILE_NOT_FOUND;const auto r=model::signal_with_ops(specimen(),o);expect_error(r,"E_MANUAL_SIGNAL_EVENT_OPEN");require(r.win32_error==ERROR_FILE_NOT_FOUND&&!o.sets);once_closed(o,1,1,0);}},
    {"expiry at Set boundary or clock regression never extends deadline",[]{for(bool backwards:{false,true}){fake_ops o;o.final_tick=backwards?99:1000;const auto r=model::signal_with_ops(specimen(),o);expect_error(r,backwards?"E_MANUAL_SIGNAL_CLOCK":"E_MANUAL_SIGNAL_EXPIRED");require(!o.sets);once_closed(o,1,1,1);}}},
    {"Set error remains primary across checked close failure",[]{fake_ops o;o.set_error=ERROR_ACCESS_DENIED;o.cleanup[2]=ERROR_INVALID_HANDLE;const auto r=model::signal_with_ops(specimen(),o);expect_error(r,"E_MANUAL_SIGNAL_SET");require(o.sets==1&&r.win32_error==ERROR_ACCESS_DENIED&&r.cleanup_error==ERROR_INVALID_HANDLE);once_closed(o,1,1,1);}},
    {"signal may succeed while checked close makes the operation fail",[]{for(int n=0;n<4;++n){fake_ops o;if(n<3)o.cleanup[n]=ERROR_INVALID_HANDLE;else{o.cleanup[2]=ERROR_INVALID_HANDLE;o.cleanup[1]=ERROR_ACCESS_DENIED;o.cleanup[0]=ERROR_GEN_FAILURE;}const auto r=model::signal_with_ops(specimen(),o);expect_error(r,"E_MANUAL_SIGNAL_CLOSE");require(o.sets==1&&r.win32_error==ERROR_INVALID_HANDLE);if(n==3)require(r.cleanup_error==ERROR_ACCESS_DENIED);once_closed(o,1,1,1);}}},
    {"exceptions retire every already acquired handle without signaling",[]{for(bool late:{false,true}){fake_ops o;o.throw_open_event=!late;o.throw_late_window=late;const auto r=model::signal_with_ops(specimen(),o);expect_error(r,"E_MANUAL_SIGNAL_EXCEPTION");require(r.win32_error==ERROR_UNHANDLED_EXCEPTION&&!o.sets);once_closed(o,1,1,late?1:0);}}},
    {"actual held self processes stop at invalid HWND without event or window creation",actual_invalid_window}
  };
  unsigned passed{};
  for(const auto& item:cases){try{item.second();++passed;}catch(const std::exception& error){std::cerr<<item.first<<": "<<error.what()<<'\n';return 1;}}
  std::cout<<"{\"schema\":\"soui.manual-observation-signal-contracts/v1\",\"passed\":"<<passed
      <<",\"fakeCases\":12,\"actualWin32Cases\":1,\"guiExecuted\":false,\"positiveHwndVerified\":false,\"inputInjected\":false,\"performanceGate\":false}\n";
  return 0;
}
