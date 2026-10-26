#define NOMINMAX
#include <windows.h>
#include "trace-identity.h"
#include "burst-desktop-observer.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct handle final {
  HANDLE value{nullptr};
  explicit handle(HANDLE input = nullptr) noexcept : value(input) {}
  ~handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
  handle(const handle&) = delete; handle& operator=(const handle&) = delete;
};
std::uint64_t qpc() {
  LARGE_INTEGER value{}; require(QueryPerformanceCounter(&value) && value.QuadPart > 0, "QPC unavailable");
  return static_cast<std::uint64_t>(value.QuadPart);
}
std::uint64_t created(HANDLE process) {
  FILETIME c{},e{},k{},u{}; require(GetProcessTimes(process,&c,&e,&k,&u), "Process identity unavailable");
  return (static_cast<std::uint64_t>(c.dwHighDateTime) << 32) | c.dwLowDateTime;
}
std::uint32_t integer(const wchar_t* text, std::uint32_t maximum) {
  const auto value = soui::benchmark::parse_expected_filetime(text);
  require(value <= maximum, "Burst argument exceeds bound"); return static_cast<std::uint32_t>(value);
}
struct window_search { DWORD pid; HWND found{nullptr}; bool duplicate{false}; };
BOOL CALLBACK find_window(HWND window, LPARAM parameter) {
  auto& search = *reinterpret_cast<window_search*>(parameter);
  DWORD pid{}; GetWindowThreadProcessId(window,&pid); RECT area{};
  if (pid == search.pid && IsWindowVisible(window) && !GetWindow(window,GW_OWNER) &&
      GetClientRect(window,&area) && area.right == 1100 && area.bottom == 720) {
    if (search.found) search.duplicate = true; else search.found = window;
  }
  return TRUE;
}
struct injection final { std::uint64_t scheduled{},before{},after{}; std::uint32_t sent{}; int x{},y{}; };
void write_line(HANDLE file, const std::string& text) {
  const auto line = text + '\n'; DWORD written{};
  require(line.size() <= 4096 && WriteFile(file,line.data(),static_cast<DWORD>(line.size()),&written,nullptr) &&
      written == line.size(), "Burst trace write failed");
}
std::string quoted(std::uint64_t value) { return '"' + std::to_string(value) + '"'; }
HANDLE make_timer() {
  using create_fn = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES,LPCWSTR,DWORD,DWORD);
  const auto create = reinterpret_cast<create_fn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"CreateWaitableTimerExW"));
  HANDLE result = create ? create(nullptr,nullptr,0x2,TIMER_ALL_ACCESS) : nullptr;
  return result ? result : CreateWaitableTimerW(nullptr,FALSE,nullptr);
}
void wait_deadline(HANDLE timer, HANDLE process, std::uint64_t deadline, std::uint64_t frequency) {
  // At most two arms; an unexpectedly early timer is a failed tool run, not a spin.
  for (unsigned attempt = 0; attempt < 2; ++attempt) {
    const auto now = qpc(); if (now >= deadline) return;
    const long double interval = std::ceil(static_cast<long double>(deadline-now)*10000000.0L/frequency);
    require(interval > 0 && interval <= (std::numeric_limits<LONGLONG>::max)(), "Timer interval invalid");
    LARGE_INTEGER due{}; due.QuadPart = -static_cast<LONGLONG>(interval);
    require(SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE), "Timer arm failed");
    const HANDLE waits[]{process,timer};
    require(WaitForMultipleObjects(2,waits,FALSE,1000) == WAIT_OBJECT_0+1, "Target exited or timer wait failed");
  }
  require(qpc() >= deadline, "Timer repeatedly woke early");
}
void validate_target(HANDLE process, HWND window, DWORD pid, POINT origin) {
  DWORD actual{}; GetWindowThreadProcessId(window,&actual); RECT client{}; POINT current{};
  require(WaitForSingleObject(process,0) == WAIT_TIMEOUT && IsWindow(window) &&
      IsWindowVisible(window) && !IsIconic(window) && actual == pid && GetForegroundWindow() == window &&
      GetClientRect(window,&client) && client.right == 1100 && client.bottom == 720 &&
      ClientToScreen(window,&current) && current.x == origin.x && current.y == origin.y,
      "Target foreground, geometry or held process changed");
}
void validate_hit(HWND owner,DWORD pid,int x,int y) {
  POINT screen{x,y};require(ClientToScreen(owner,&screen),"Input target coordinate unavailable");
  const auto hit=WindowFromPoint(screen);DWORD actual{};GetWindowThreadProcessId(hit,&actual);
  require(hit&&hit!=owner&&IsChild(owner,hit)&&actual==pid,"Input did not hit the actual SDK Host child");
  POINT child=screen;RECT area{};
  require(ScreenToClient(hit,&child)&&GetClientRect(hit,&area)&&area.right==1100&&area.bottom==720&&
    child.x==x&&child.y==y&&child.x>=0&&child.y>=0&&child.x<area.right&&child.y<area.bottom,
    "Parent/child Host content coordinates differ");
}
LONG normalize(int value, int origin, int extent) {
  require(extent > 0 && value >= origin && static_cast<std::int64_t>(value)-origin < extent,
      "Input point is outside frozen virtual desktop");
  const auto normalized = ((static_cast<std::int64_t>(value)-origin)*65536 + 32768) / extent;
  require(normalized >= 0 && normalized <= 65535, "Normalized input coordinate exceeds bound");
  return static_cast<LONG>(normalized);
}
}

int wmain(int argc, wchar_t** argv) {
  try {
    require(argc == 9, "Usage: BurstInput PID EXPECTED_CREATION RUN_ID HZ COUNT NEW_INPUT NEW_DESKTOP pointer_move|wheel");
    const auto pid = integer(argv[1],0xffffffffU);
    const auto expected = soui::benchmark::parse_expected_filetime(argv[2]);
    std::string run;
    for (const wchar_t* c = argv[3]; *c; ++c) {
      require(run.size() < 32 && ((*c >= L'0' && *c <= L'9') || (*c >= L'a' && *c <= L'f')), "Run id must be 32 lowercase hex digits");
      run += static_cast<char>(*c);
    }
    require(run.size() == 32 && run.find_first_not_of('0') != std::string::npos, "Run id is missing");
    const auto hz = integer(argv[4],1000), count = integer(argv[5],100000);
    require((hz == 125 || hz == 500 || hz == 1000) && count <= hz*120, "Unsupported rate or duration above120s");
    const std::wstring kind_arg(argv[8]);
    require(kind_arg == L"pointer_move" || kind_arg == L"wheel", "Unsupported burst kind");
    const bool wheel = kind_arg == L"wheel";
    const char* kind = wheel ? "wheel" : "pointer_move";
    handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));
    require(process.value && created(process.value) == expected && WaitForSingleObject(process.value,0) == WAIT_TIMEOUT,
      "Held target identity differs or target is not live");
    DWORD target_session{}, injector_session{};
    require(ProcessIdToSessionId(pid,&target_session) && ProcessIdToSessionId(GetCurrentProcessId(),&injector_session) &&
      target_session == injector_session && injector_session != 0, "Target has no matching interactive session");
    require(GetSystemMetrics(SM_REMOTESESSION)==0,"RDP invalidates physical-target burst observation");
    // Named, process-lifetime mutex prevents a second instance targeting this birth.
    const auto mutex_name = L"Local\\SOUI-Burst-" + std::to_wstring(pid) + L"-" + std::to_wstring(expected);
    handle lease(CreateMutexW(nullptr,TRUE,mutex_name.c_str()));
    require(lease.value && GetLastError() != ERROR_ALREADY_EXISTS, "Concurrent injector invalidates run");
    window_search search{pid}; require(EnumWindows(find_window,reinterpret_cast<LPARAM>(&search)) &&
      search.found && !search.duplicate, "Target needs exactly one1100x720 content window");
    wchar_t target_class[64]{};
    require(GetClassNameW(search.found,target_class,64) && std::wstring(target_class)==L"SouiInputUiHostV1",
      "Burst tool requires its public SDK harness window");
    require(SetProcessDPIAware() || GetLastError() == ERROR_ACCESS_DENIED, "Injector DPI policy unavailable");
    ShowWindow(search.found,SW_RESTORE); SetForegroundWindow(search.found);
    POINT origin{}; require(ClientToScreen(search.found,&origin), "Target origin unavailable");
    validate_target(process.value,search.found,pid,origin);
    const auto nonce_raw = std::stoul(run.substr(24),nullptr,16);
    const auto nonce = static_cast<std::uint32_t>(nonce_raw ? nonce_raw : 1);
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if(wheel) {
      INPUT position{};position.type=INPUT_MOUSE;
      position.mi.dx=normalize(origin.x+100,vx,vw);position.mi.dy=normalize(origin.y+100,vy,vh);
      position.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
      require(SendInput(1,&position,sizeof(INPUT))==1,"Wheel baseline cursor placement failed");
    }
    std::vector<injection> rows(count); // All allocation occurs before injection.
    handle timer(make_timer()); require(timer.value != nullptr, "Waitable timer unavailable");
    LARGE_INTEGER frequency{}; require(QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0,
      "QPC frequency unavailable");
    const auto hz_qpc = static_cast<std::uint64_t>(frequency.QuadPart);
    require(hz_qpc <= (std::numeric_limits<std::uint64_t>::max)()/121, "QPC frequency exceeds bounded timer arithmetic");
    const std::filesystem::path output(argv[6]), desktop(argv[7]);
    require(output.is_absolute() && desktop.is_absolute() && output != desktop, "Distinct absolute new outputs required");
    // Baseline marker must belong to this run before any tagged input is sent.
    soui::benchmark::burst_desktop_observer observer(process.value,pid,expected,search.found,desktop,run,nonce);
    handle file(CreateFileW(output.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    require(file.value != INVALID_HANDLE_VALUE, "Cannot create new burst trace");
    const auto start = qpc();
    write_line(file.value,"{\"event\":\"burst.begin\",\"schema\":\"soui.burst-input/v1\",\"run_id\":\""+run+
      "\",\"target_pid\":"+std::to_string(pid)+",\"process_incarnation\":"+quoted(expected)+
      ",\"injector_pid\":"+std::to_string(GetCurrentProcessId())+",\"injector_incarnation\":"+quoted(created(GetCurrentProcess()))+
      ",\"qpc_frequency\":"+quoted(hz_qpc)+",\"started_qpc\":"+quoted(start)+",\"hz\":"+std::to_string(hz)+
      ",\"requested\":"+std::to_string(count)+",\"kind\":\""+kind+"\",\"concurrent_injectors\":1,\"nonce\":"+std::to_string(nonce)+
      ",\"injection_tag_magic\":1396703232,\"content_width\":1100,\"content_height\":720}");
    std::uint32_t attempted = 0, completed = 0; bool success = false;
    try {
      for (std::uint32_t index = 0; index < count; ++index) {
        observer.check(); validate_target(process.value,search.found,pid,origin);
        auto& row = rows[index]; row.x = wheel?100:100+static_cast<int>(index%1000); row.y = wheel?100:100+static_cast<int>(index/1000);
        const auto offset = (index/hz)*hz_qpc + (index%hz)*hz_qpc/hz;
        require(start <= (std::numeric_limits<std::uint64_t>::max)()-offset, "QPC deadline overflow");
        row.scheduled = start+offset; wait_deadline(timer.value,process.value,row.scheduled,hz_qpc);
        validate_hit(search.found,pid,row.x,row.y);
        INPUT packets[1]{}; packets[0].type = INPUT_MOUSE;
        packets[0].mi.dx = normalize(origin.x+row.x,vx,vw); packets[0].mi.dy = normalize(origin.y+row.y,vy,vh);
        packets[0].mi.dwFlags = MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK|MOUSEEVENTF_MOVE_NOCOALESCE;
        const auto tag = static_cast<ULONG_PTR>(0x53400000U | (index+1));
        if (wheel) { packets[0].mi.dwFlags = MOUSEEVENTF_WHEEL;
          packets[0].mi.mouseData = static_cast<DWORD>(-WHEEL_DELTA);packets[0].mi.dwExtraInfo = tag; }
        else packets[0].mi.dwExtraInfo = tag;
        row.before = qpc(); row.sent = SendInput(1,packets,sizeof(INPUT)); row.after = qpc();
        ++attempted; require(row.sent == 1, "SendInput partial/failure"); ++completed;
      }
      observer.wait_final_ack(count); success = true;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    const auto ended = qpc();
    // Batch formatting/persistence happens after injection; never per-input I/O.
    for (std::uint32_t index = 0; index < attempted; ++index) {
      const auto& row = rows[index];
      write_line(file.value,"{\"event\":\"input.inject\",\"sequence\":"+quoted(index+1)+
        ",\"scheduled_qpc\":"+quoted(row.scheduled)+",\"before_qpc\":"+quoted(row.before)+",\"after_qpc\":"+quoted(row.after)+
        ",\"sent\":"+std::to_string(row.sent)+",\"client_x\":"+std::to_string(row.x)+",\"client_y\":"+std::to_string(row.y)+
        ",\"wheel_delta\":"+std::to_string(wheel?-WHEEL_DELTA:0)+"}");
    }
    try { observer.finish(); } catch (const std::exception& error) { std::cerr << error.what() << '\n'; success = false; }
    write_line(file.value,"{\"event\":\"burst.end\",\"completed\":"+std::string(success ? "true" : "false")+
      ",\"attempted\":"+std::to_string(attempted)+",\"sent\":"+std::to_string(completed)+",\"ended_qpc\":"+quoted(ended)+"}");
    require(FlushFileBuffers(file.value), "Burst trace flush failed");
    const auto close_name=L"Local\\SOUI-InputUi-Finish-"+std::to_wstring(pid)+L"-"+std::to_wstring(expected)+L"-"+
      std::wstring(run.begin(),run.end());
    handle close_event(OpenEventW(EVENT_MODIFY_STATE,FALSE,close_name.c_str()));
    if(!close_event.value||!SetEvent(close_event.value)){std::cerr<<"Harness normal close signal failed\n";success=false;}
    return success ? 0 : 1;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
