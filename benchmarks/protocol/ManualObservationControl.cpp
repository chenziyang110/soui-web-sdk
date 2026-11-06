// Task-private cold helper: query or signal only. No capture/input modes.
#define NOMINMAX
#include <windows.h>
#include "manual-observation-signal.h"
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
std::uint64_t filetime(FILETIME value) {
  return (static_cast<std::uint64_t>(value.dwHighDateTime)<<32)|value.dwLowDateTime;
}
std::uint64_t positive_integer(const wchar_t* text,std::uint64_t maximum) {
  if(!text||!*text||(*text==L'0'&&text[1]))throw std::runtime_error("Invalid manual control integer");
  std::uint64_t value=0;
  for(;*text;++text){
    if(*text<L'0'||*text>L'9')throw std::runtime_error("Invalid manual control integer");
    const auto digit=static_cast<unsigned>(*text-L'0');
    if(value>(maximum-digit)/10)throw std::runtime_error("Manual control integer overflow");
    value=value*10+digit;
  }
  if(!value)throw std::runtime_error("Manual control integer must be positive");
  return value;
}

int signal_manual_observation(int argc,wchar_t** argv) {
  if(argc!=10)throw std::runtime_error("Usage: ManualObservationControl --signal-manual-observation RUN NONCE HOST_PID HOST_CREATION HWND CONTROLLER_PID CONTROLLER_CREATION EXPIRES_TICK");
  const auto hex=[](const wchar_t* value) {
    if(!value||std::wcslen(value)!=32)throw std::runtime_error("Manual signal requires a 32-character identity");
    std::string text;
    for(const wchar_t digit:std::wstring(value)){
      if(!((digit>=L'0'&&digit<=L'9')||(digit>=L'a'&&digit<=L'f')))throw std::runtime_error("Manual signal requires lowercase hex");
      text.push_back(static_cast<char>(digit));
    }
    if(!soui::benchmark::manual_observation::hex32(text))throw std::runtime_error("Manual signal identity cannot be zero");
    return text;
  };
  const auto integer=[](const wchar_t* value,std::uint64_t maximum) {
    if(!value||std::wcslen(value)>20)throw std::runtime_error("Manual signal integer exceeds bound");
    return positive_integer(value,maximum);
  };
  soui::benchmark::manual_observation::binding bound;
  bound.run=hex(argv[2]);bound.nonce=hex(argv[3]);
  bound.host_pid=static_cast<DWORD>(integer(argv[4],MAXDWORD));
  bound.host_creation=integer(argv[5],(std::numeric_limits<std::uint64_t>::max)());
  bound.hwnd=integer(argv[6],static_cast<std::uint64_t>(~static_cast<std::uintptr_t>(0)));
  bound.controller_pid=static_cast<DWORD>(integer(argv[7],MAXDWORD));
  bound.controller_creation=integer(argv[8],(std::numeric_limits<std::uint64_t>::max)());
  bound.expires_tick=integer(argv[9],(std::numeric_limits<std::uint64_t>::max)());
  const auto result=soui::benchmark::manual_observation::signal(bound);
  const bool confirmed=result.ok||(result.code&&std::strcmp(result.code,"E_MANUAL_SIGNAL_CLOSE")==0);
  std::cout<<"{\"schema\":\"soui.manual-observation-signal/v1\",\"passed\":"<<(result.ok?"true":"false")
    <<",\"signalCallConfirmed\":"<<(confirmed?"true":"false")<<",\"code\":";
  if(result.code)std::cout<<'"'<<result.code<<'"';else std::cout<<"null";
  std::cout<<",\"win32_error\":"<<result.win32_error<<",\"cleanup_error\":"<<result.cleanup_error
    <<",\"hostPid\":"<<bound.host_pid<<",\"hostCreation\":\""<<bound.host_creation
    <<"\",\"controllerPid\":"<<bound.controller_pid<<",\"controllerCreation\":\""<<bound.controller_creation
    <<"\",\"run\":\""<<bound.run<<"\",\"nonce\":\""<<bound.nonce
    <<"\",\"hwnd\":\""<<bound.hwnd<<"\",\"expiresTick\":\""<<bound.expires_tick
    <<"\",\"qualificationGranted\":false,\"hostAcceptanceProven\":false,\"windowMessagesPosted\":false,\"inputInjected\":false}\n";
  return result.ok?0:1;
}

int query_manual_controller(int argc,wchar_t** argv) {
  if(argc!=3)throw std::runtime_error("Usage: ManualObservationControl --query-manual-controller PID");
  if(!argv[2]||std::wcslen(argv[2])>10)throw std::runtime_error("Manual controller PID exceeds bound");
  const auto pid=static_cast<DWORD>(positive_integer(argv[2],MAXDWORD));
  const auto process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);
  if(!process)throw std::runtime_error("Manual controller process unavailable");
  FILETIME created{},exited{},kernel{},user{};
  const bool valid=GetProcessId(process)==pid&&GetProcessTimes(process,&created,&exited,&kernel,&user)&&
    filetime(created)!=0&&WaitForSingleObject(process,0)==WAIT_TIMEOUT;
  const bool closed=CloseHandle(process)!=FALSE;
  if(!valid||!closed)throw std::runtime_error("Manual controller identity or checked close unavailable");
  std::cout<<"{\"schema\":\"soui.manual-observation-controller/v1\",\"pid\":"<<pid
    <<",\"creationFileTime\":\""<<filetime(created)<<"\",\"aliveAtQuery\":true,\"handleClosed\":true,\"qualificationGranted\":false}\n";
  return 0;
}

} // namespace

int wmain(int argc,wchar_t** argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
  try {
    if(argc>1&&argv[1]&&std::wstring(argv[1])==L"--signal-manual-observation")return signal_manual_observation(argc,argv);
    if(argc>1&&argv[1]&&std::wstring(argv[1])==L"--query-manual-controller")return query_manual_controller(argc,argv);
    throw std::runtime_error("Explicit manual observation control mode required");
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
