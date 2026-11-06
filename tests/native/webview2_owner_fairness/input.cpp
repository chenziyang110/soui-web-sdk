#include <windows.h>
#include <json.hpp>
#include "native_input_policy.h"
#include "cold_guard.h"
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using json=nlohmann::json;namespace fs=std::filesystem;
void need(bool b,const char*m){if(!b)throw std::runtime_error(m);}
struct handle{HANDLE h{};explicit handle(HANDLE v=nullptr):h(v){}~handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}handle(const handle&)=delete;};
std::uint64_t qpc(){LARGE_INTEGER q{};need(QueryPerformanceCounter(&q)&&q.QuadPart>0,"QPC");return q.QuadPart;}
std::uint64_t birth(HANDLE h){FILETIME c{},e{},k{},u{};need(GetProcessTimes(h,&c,&e,&k,&u),"process birth");ULARGE_INTEGER v{};v.LowPart=c.dwLowDateTime;v.HighPart=c.dwHighDateTime;return v.QuadPart;}
HANDLE make_timer(){using fn=HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES,LPCWSTR,DWORD,DWORD);const auto f=reinterpret_cast<fn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"CreateWaitableTimerExW"));auto h=f?f(nullptr,nullptr,2,TIMER_ALL_ACCESS):nullptr;return h?h:CreateWaitableTimerW(nullptr,FALSE,nullptr);}
void wait_deadline(HANDLE timer,HANDLE process,std::uint64_t deadline,std::uint64_t frequency){
  for(unsigned attempt=0;attempt<2;++attempt){const auto now=qpc();if(now>=deadline)return;
    const auto interval=std::ceil(static_cast<long double>(deadline-now)*10000000.0L/frequency);
    need(interval>0&&interval<=(std::numeric_limits<LONGLONG>::max)(),"timer interval");LARGE_INTEGER due{};due.QuadPart=-static_cast<LONGLONG>(interval);
    need(SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE),"timer arm");const HANDLE waits[]{process,timer};
    need(WaitForMultipleObjects(2,waits,FALSE,1000)==WAIT_OBJECT_0+1,"target/timer wait");}
  need(qpc()>=deadline,"timer repeatedly early");
}
LONG normalize(int v,int origin,int extent){need(extent>0&&v>=origin&&static_cast<std::int64_t>(v)-origin<extent,"virtual desktop point");const auto n=((static_cast<std::int64_t>(v)-origin)*65536+32768)/extent;need(n>=0&&n<=65535,"normalized bound");return static_cast<LONG>(n);}
void desktop(){wchar_t n[256]{};DWORD size{};BOOL input=FALSE;const auto h=GetThreadDesktop(GetCurrentThreadId());
  need(h&&GetUserObjectInformationW(h,UOI_NAME,n,sizeof(n),&size)&&std::wstring(n)==L"Default"&&
    GetUserObjectInformationW(h,6,&input,sizeof(input),&size)&&input&&!GetSystemMetrics(SM_REMOTESESSION),"explicit Default input desktop/nonremote required");}
struct target{HANDLE process{},load_event{};DWORD pid{},thread{};HWND owner{},strip{};POINT origin{};};
void validate(const target&t,bool foreground=true){DWORD a{},b{};const auto ta=GetWindowThreadProcessId(t.owner,&a),tb=GetWindowThreadProcessId(t.strip,&b);RECT o{},s{};POINT p{},r{};wchar_t oc[80]{},sc[80]{};DWORD ownSession{},targetSession{};
  need((GetAsyncKeyState(VK_ESCAPE)&0x8001)==0,"E_USER_ESCAPE_ABORT");
  need(!t.load_event||WaitForSingleObject(t.load_event,0)==WAIT_OBJECT_0,"E_HOST_CLOSE_ABORT");
  need(WaitForSingleObject(t.process,0)==WAIT_TIMEOUT&&IsWindow(t.owner)&&IsWindow(t.strip)&&IsWindowVisible(t.owner)&&IsWindowVisible(t.strip)&&!IsIconic(t.owner)&&
    a==t.pid&&b==t.pid&&ta==tb&&ta==t.thread&&GetParent(t.strip)==t.owner&&GetAncestor(t.strip,GA_ROOT)==t.owner&&(!foreground||GetForegroundWindow()==t.owner)&&
    GetClassNameW(t.owner,oc,80)&&std::wstring(oc)==L"SouiR41NativeFairnessOwner"&&GetClassNameW(t.strip,sc,80)&&std::wstring(sc)==L"SouiR41NativeInputStrip"&&
    GetClientRect(t.owner,&o)&&o.left==0&&o.top==0&&o.right==1100&&o.bottom==792&&GetClientRect(t.strip,&s)&&s.left==0&&s.top==0&&s.right==1100&&s.bottom==72&&
    ClientToScreen(t.strip,&p)&&ClientToScreen(t.owner,&r)&&p.x==t.origin.x&&p.y==t.origin.y&&p.x==r.x&&p.y==r.y+720&&
    ProcessIdToSessionId(GetCurrentProcessId(),&ownSession)&&ProcessIdToSessionId(t.pid,&targetSession)&&ownSession==targetSession,
    "R41 target foreground/geometry/class/child/session/held process changed");
}

void cold_validate(const target&t,r42_cold::state&state,const char*checkpoint,bool foreground=true){
  need(state.count<state.rows.size(),"cold diagnostic capacity");auto&snap=state.rows[state.count++];snap.checkpoint=checkpoint;snap.foreground_required=foreground;
  snap.expected_owner=reinterpret_cast<std::uintptr_t>(t.owner);snap.expected_strip=reinterpret_cast<std::uintptr_t>(t.strip);snap.expected_pid=t.pid;snap.expected_thread=t.thread;snap.expected_origin=t.origin;
  unsigned tid_call=0,window_call=0,visible_call=0,class_call=0,rect_call=0,origin_call=0,session_call=0;
  auto GetWindowThreadProcessId=[&](HWND h,DWORD*out)->DWORD{const auto v=::GetWindowThreadProcessId(h,out);const bool owner=tid_call++==0;r42_cold::save(owner?snap.owner_tid:snap.strip_tid,v);r42_cold::output(owner?snap.owner_pid:snap.strip_pid,*out,v!=0);return v;};
  auto GetAsyncKeyState=[&](int key)->SHORT{return r42_cold::save(snap.escape,::GetAsyncKeyState(key));};
  auto WaitForSingleObject=[&](HANDLE h,DWORD ms)->DWORD{return r42_cold::save(h==t.process?snap.process_wait:snap.load_wait,::WaitForSingleObject(h,ms));};
  auto IsWindow=[&](HWND h)->BOOL{return r42_cold::save(window_call++==0?snap.owner_window:snap.strip_window,::IsWindow(h));};
  auto IsWindowVisible=[&](HWND h)->BOOL{return r42_cold::save(visible_call++==0?snap.owner_visible:snap.strip_visible,::IsWindowVisible(h));};
  auto IsIconic=[&](HWND h)->BOOL{return r42_cold::save(snap.owner_iconic,::IsIconic(h));};
  auto GetParent=[&](HWND h)->HWND{return r42_cold::save(snap.parent,::GetParent(h));};
  auto GetAncestor=[&](HWND h,UINT flags)->HWND{return r42_cold::save(snap.root,::GetAncestor(h,flags));};
  auto GetForegroundWindow=[&]()->HWND{return r42_cold::save(snap.foreground,::GetForegroundWindow());};
  auto GetClassNameW=[&](HWND h,LPWSTR out,int size)->int{const auto v=::GetClassNameW(h,out,size);const bool owner=class_call++==0;r42_cold::save(owner?snap.owner_class_result:snap.strip_class_result,v);std::array<wchar_t,80>a{};if(v>0)for(unsigned i=0;i<a.size();++i)a[i]=out[i];r42_cold::output(owner?snap.owner_class:snap.strip_class,a,v>0);return v;};
  auto GetClientRect=[&](HWND h,LPRECT out)->BOOL{const auto v=::GetClientRect(h,out);const bool owner=rect_call++==0;r42_cold::save(owner?snap.owner_rect_result:snap.strip_rect_result,v);r42_cold::output(owner?snap.owner_rect:snap.strip_rect,*out,v!=FALSE);return v;};
  auto ClientToScreen=[&](HWND h,LPPOINT out)->BOOL{const auto v=::ClientToScreen(h,out);const bool owner=origin_call++!=0;r42_cold::save(owner?snap.owner_origin_result:snap.strip_origin_result,v);r42_cold::output(owner?snap.owner_origin:snap.strip_origin,*out,v!=FALSE);return v;};
  auto GetCurrentProcessId=[&]()->DWORD{return r42_cold::save(snap.current_pid,::GetCurrentProcessId());};
  auto ProcessIdToSessionId=[&](DWORD pid,DWORD*out)->BOOL{const auto v=::ProcessIdToSessionId(pid,out);const bool owner=session_call++==0;r42_cold::save(owner?snap.own_session_result:snap.target_session_result,v);r42_cold::output(owner?snap.own_session:snap.target_session,*out,v!=FALSE);return v;};
  auto guard_term=[&](const char*name,bool value){if(!value&&!snap.first_failed)snap.first_failed=name;return value;};
  // COLD_ORIGINAL_BODY_BEGIN
  {DWORD a{},b{};const auto ta=GetWindowThreadProcessId(t.owner,&a),tb=GetWindowThreadProcessId(t.strip,&b);RECT o{},s{};POINT p{},r{};wchar_t oc[80]{},sc[80]{};DWORD ownSession{},targetSession{};
  need(guard_term("escape",((GetAsyncKeyState(VK_ESCAPE)&0x8001)==0)),"E_USER_ESCAPE_ABORT");
  need(guard_term("load-event",(!t.load_event||WaitForSingleObject(t.load_event,0)==WAIT_OBJECT_0)),"E_HOST_CLOSE_ABORT");
  need(guard_term("process-alive",(WaitForSingleObject(t.process,0)==WAIT_TIMEOUT))&&guard_term("owner-window",(IsWindow(t.owner)))&&guard_term("strip-window",(IsWindow(t.strip)))&&guard_term("owner-visible",(IsWindowVisible(t.owner)))&&guard_term("strip-visible",(IsWindowVisible(t.strip)))&&guard_term("owner-not-iconic",(!IsIconic(t.owner)))&&
    guard_term("owner-pid",(a==t.pid))&&guard_term("strip-pid",(b==t.pid))&&guard_term("same-thread",(ta==tb))&&guard_term("owner-thread",(ta==t.thread))&&guard_term("parent",(GetParent(t.strip)==t.owner))&&guard_term("root",(GetAncestor(t.strip,GA_ROOT)==t.owner))&&(guard_term("foreground",(!foreground||GetForegroundWindow()==t.owner)))&&
    guard_term("owner-class-query",(GetClassNameW(t.owner,oc,80)))&&guard_term("owner-class",(std::wstring(oc)==L"SouiR41NativeFairnessOwner"))&&guard_term("strip-class-query",(GetClassNameW(t.strip,sc,80)))&&guard_term("strip-class",(std::wstring(sc)==L"SouiR41NativeInputStrip"))&&
    guard_term("owner-rect-query",(GetClientRect(t.owner,&o)))&&guard_term("owner-left",(o.left==0))&&guard_term("owner-top",(o.top==0))&&guard_term("owner-width",(o.right==1100))&&guard_term("owner-height",(o.bottom==792))&&guard_term("strip-rect-query",(GetClientRect(t.strip,&s)))&&guard_term("strip-left",(s.left==0))&&guard_term("strip-top",(s.top==0))&&guard_term("strip-width",(s.right==1100))&&guard_term("strip-height",(s.bottom==72))&&
    guard_term("strip-origin-query",(ClientToScreen(t.strip,&p)))&&guard_term("owner-origin-query",(ClientToScreen(t.owner,&r)))&&guard_term("stored-origin-x",(p.x==t.origin.x))&&guard_term("stored-origin-y",(p.y==t.origin.y))&&guard_term("owner-offset-x",(p.x==r.x))&&guard_term("owner-offset-y",(p.y==r.y+720))&&
    guard_term("own-session-query",(ProcessIdToSessionId(GetCurrentProcessId(),&ownSession)))&&guard_term("target-session-query",(ProcessIdToSessionId(t.pid,&targetSession)))&&guard_term("same-session",(ownSession==targetSession)),
    "R41 target foreground/geometry/class/child/session/held process changed");
}
  // COLD_ORIGINAL_BODY_END
  snap.complete=true;
}

struct count_windows{DWORD pid{};const wchar_t*name{};unsigned count{};};
BOOL CALLBACK count_class(HWND w,LPARAM p){auto&c=*reinterpret_cast<count_windows*>(p);DWORD pid{};wchar_t n[80]{};GetWindowThreadProcessId(w,&pid);if(pid==c.pid&&GetClassNameW(w,n,80)&&std::wstring(n)==c.name)++c.count;return TRUE;}
// Cold baseline/final only. Original16-byte header/CRC/content encoding;
// no screenshots or continuous observer inside the timed SendInput loop.
enum marker_check:std::uint32_t{pixels=1,magic=2,flags=4,crc_valid=8,nonce_nonzero=16,frame_nonzero=32,delivered_matches=64,content_matches=128,expected_identity=256};
struct marker{
  std::array<unsigned char,16> bytes{};
  std::uint32_t nonce{},ack{},delivered{},frame{},content{},evaluated{},passed{},expected_nonce{};
  std::uint64_t begin{},end{};unsigned bits_decoded{};
  bool attempted{},fields_decoded{},zero_identity{};const char*failed_stage{};
};
void marker_check_value(marker&m,std::uint32_t bit,bool good){m.evaluated|=bit;if(good)m.passed|=bit;}
void capture(const target&t,marker&m,r42_cold::state*cold=nullptr){m={};m.attempted=true;
 try{if(cold)cold_validate(t,*cold,"zero-capture",true);else validate(t);m.begin=qpc();
  struct gdi{HDC screen{},memory{};HBITMAP bitmap{};HGDIOBJ previous{};~gdi(){if(previous)SelectObject(memory,previous);if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);if(screen)ReleaseDC(nullptr,screen);}}g;
  g.screen=GetDC(nullptr);need(g.screen!=nullptr&&GetDeviceCaps(g.screen,LOGPIXELSX)==96,"screen DC/96DPI profile");g.memory=CreateCompatibleDC(g.screen);need(g.memory!=nullptr,"memory DC");BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=64;info.bmiHeader.biHeight=-36;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* bits{};
  g.bitmap=CreateDIBSection(g.screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);need(g.bitmap&&bits,"native marker bitmap");g.previous=SelectObject(g.memory,g.bitmap);need(g.previous&&g.previous!=HGDI_ERROR,"native marker select");
  need(BitBlt(g.memory,0,0,64,36,g.screen,t.origin.x+8,t.origin.y+8,SRCCOPY|CAPTUREBLT)&&GdiFlush(),"native marker capture");
  const auto* p=static_cast<const std::uint32_t*>(bits);for(unsigned bit=0;bit<128;++bit){const auto c=p[(bit/16*4+2)*64+(bit%16*4+2)];const unsigned b=c&255,gc=(c>>8)&255,r=(c>>16)&255;const bool white=r>=223&&gc>=223&&b>=223,black=r<=32&&gc<=32&&b<=32;
    if(!white&&!black){marker_check_value(m,pixels,false);m.failed_stage="pixel-decode";need(false,"marker bit undecodable");}
    if(white)m.bytes[bit/8]|=static_cast<unsigned char>(1U<<(bit%8));++m.bits_decoded;}
  marker_check_value(m,pixels,true);
  std::uint16_t crc=65535;for(unsigned i=0;i<16;++i){if(i==12||i==13)continue;crc^=static_cast<std::uint16_t>(m.bytes[i])<<8;for(unsigned k=0;k<8;++k)crc=static_cast<std::uint16_t>((crc<<1)^((crc&32768)?4129:0));}
  for(unsigned i=0;i<4;++i)m.nonce|=static_cast<std::uint32_t>(m.bytes[2+i])<<(8*i);for(unsigned i=0;i<3;++i)m.ack|=static_cast<std::uint32_t>(m.bytes[6+i])<<(8*i);
  m.frame=m.bytes[9]|(static_cast<unsigned>(m.bytes[10])<<8);m.delivered=m.bytes[14]|(static_cast<unsigned>(m.bytes[15])<<8)|(static_cast<unsigned>(m.bytes[11]>>4)<<16);
  const auto c=p[34*64+2];m.content=((c>>16)&255)|((c&65280))|((c&255)<<16);m.fields_decoded=true;
  marker_check_value(m,magic,m.bytes[0]==167&&m.bytes[1]==83);marker_check_value(m,flags,(m.bytes[11]&15)==1);
  marker_check_value(m,crc_valid,m.bytes[12]==(crc&255)&&m.bytes[13]==(crc>>8));
  if((m.passed&(magic|flags|crc_valid))!=(magic|flags|crc_valid)){m.failed_stage="header";need(false,"marker magic/flags/CRC");}
  marker_check_value(m,nonce_nonzero,m.nonce!=0);marker_check_value(m,frame_nonzero,m.frame!=0);
  marker_check_value(m,delivered_matches,m.delivered==m.ack);marker_check_value(m,content_matches,m.content==m.ack);
  if((m.passed&(nonce_nonzero|frame_nonzero|delivered_matches|content_matches))!=(nonce_nonzero|frame_nonzero|delivered_matches|content_matches)){
    m.failed_stage="nonce-counters-content";need(false,"marker nonce/counters/content");
  }
  m.end=qpc();
 }catch(...){LARGE_INTEGER ended{};if(QueryPerformanceCounter(&ended)&&ended.QuadPart>0)m.end=ended.QuadPart;throw;}
}
void check_marker_identity(marker&m,std::uint32_t nonce,bool zero){
  m.expected_nonce=nonce;m.zero_identity=zero;
  const bool good=m.fields_decoded&&m.nonce==nonce&&(zero?m.ack==0:m.ack<=owner_fairness::input_count);
  marker_check_value(m,expected_identity,good);
  if(!good)m.failed_stage="expected-identity";
  need(good,zero?"fresh native zero marker required":"native final marker identity");
}
void write_new(const fs::path&p,const json&j){const auto s=j.dump()+"\n";need(s.size()<=1048576,"input output budget");handle f(CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));need(f.h!=INVALID_HANDLE_VALUE,"input CREATE_NEW");DWORD n{};need(WriteFile(f.h,s.data(),static_cast<DWORD>(s.size()),&n,nullptr)&&n==s.size()&&FlushFileBuffers(f.h),"input write/flush");}
struct row{std::uint64_t scheduled{},before{},after{};LONG dx{},dy{};UINT sent{};};
std::uint64_t number(const wchar_t*s){const std::wstring text(s);need(!text.empty()&&text.size()<=20&&text.find_first_not_of(L"0123456789")==std::wstring::npos,"canonical unsigned argument");const auto value=std::stoull(text);need(value&&text==std::to_wstring(value),"positive canonical unsigned argument");return value;}
json marker_json(const marker&m){
  if(!m.attempted)return nullptr;
  const auto field=[&](std::uint32_t value){return m.fields_decoded?json(value):json(nullptr);};
  const auto checked=[&](std::uint32_t bit){return m.evaluated&bit?json((m.passed&bit)!=0):json(nullptr);};
  json partial=json::array();for(unsigned i=0;i<m.bits_decoded/8;++i)partial.push_back(m.bytes[i]);
  return {{"bytes",m.bits_decoded==128?json(m.bytes):json(nullptr)},{"decodedWholeBytes",partial},{"decodedBits",m.bits_decoded},
    {"partialLastByte",m.bits_decoded%8?json(m.bytes[m.bits_decoded/8]):json(nullptr)},{"partialLastByteKnownBits",m.bits_decoded%8},
    {"nonce",field(m.nonce)},{"ack",field(m.ack)},{"delivered",field(m.delivered)},{"frame",field(m.frame)},{"content",field(m.content)},
    {"beginQpc",m.begin?json(std::to_string(m.begin)):json(nullptr)},{"endQpc",m.end?json(std::to_string(m.end)):json(nullptr)},
    {"fieldsDecoded",m.fields_decoded},{"evaluatedMask",m.evaluated},{"passedMask",m.passed},{"failedMask",m.evaluated&~m.passed},
    {"failedStage",m.failed_stage?json(m.failed_stage):json(nullptr)},{"checks",{{"pixels",checked(pixels)},{"magic",checked(magic)},{"flags",checked(flags)},{"crc",checked(crc_valid)},
      {"nonceNonzero",checked(nonce_nonzero)},{"frameNonzero",checked(frame_nonzero)},{"deliveredEqualsAck",checked(delivered_matches)},
      {"contentEqualsAck",checked(content_matches)},{"expectedIdentity",checked(expected_identity)}}},
    {"expectedNonce",m.evaluated&expected_identity?json(m.expected_nonce):json(nullptr)},{"zeroIdentity",m.evaluated&expected_identity?json(m.zero_identity):json(nullptr)}};
}
}
int wmain(int argc,wchar_t**argv){fs::path output;std::string run,error;json result=json::object();r42_cold::state cold{};handle ready,sender_done,finished;bool readySent=false,senderDoneAttempted=false,senderDoneSignaled=false;std::uint64_t ackDeadlineTick{},senderDoneBeforeQpc{},senderDoneAfterQpc{};marker zero{},final{};std::array<row,owner_fairness::input_count> rows{};unsigned attempted=0,sent=0;
  try{
    need(argc==16&&std::wstring(argv[1])==L"--interaction-profile"&&std::wstring(argv[2])==L"r45-native-drain-strip-v2"&&std::wstring(argv[3])==L"--pid"&&std::wstring(argv[5])==L"--birth"&&std::wstring(argv[7])==L"--run"&&std::wstring(argv[9])==L"--owner"&&std::wstring(argv[11])==L"--strip"&&std::wstring(argv[13])==L"--output"&&std::wstring(argv[15])==L"--authorize-frozen-task-input","explicit frozen R45 v2 task input arguments required");
    const auto pid64=number(argv[4]);need(pid64<=MAXDWORD,"PID bound");const auto pid=static_cast<DWORD>(pid64);const auto expected=number(argv[6]);const std::wstring wr(argv[8]);need(wr.size()==32&&wr.find_first_not_of(L"0123456789abcdef")==std::wstring::npos,"run");run.assign(wr.begin(),wr.end());const auto nonce=static_cast<std::uint32_t>(std::stoul(run.substr(24),nullptr,16));need(nonce!=0,"run nonce");
    output=argv[14];need(output.is_absolute()&&!fs::exists(output),"fresh absolute output");desktop();handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));need(process.h&&birth(process.h)==expected,"held PID/birth");
    const auto ownerNumber=number(argv[10]),stripNumber=number(argv[12]);
    need(ownerNumber<=(std::numeric_limits<std::uintptr_t>::max)()&&stripNumber<=(std::numeric_limits<std::uintptr_t>::max)(),"HWND pointer-width bound");
    target t{};t.process=process.h;t.pid=pid;t.owner=reinterpret_cast<HWND>(static_cast<std::uintptr_t>(ownerNumber));t.strip=reinterpret_cast<HWND>(static_cast<std::uintptr_t>(stripNumber));t.thread=GetWindowThreadProcessId(t.owner,nullptr);
    need(SetProcessDPIAware()!=FALSE,"existing DPI-aware setter");need(ClientToScreen(t.strip,&t.origin),"strip origin");cold_validate(t,cold,"pre-foreground",false);
    count_windows roots{pid,L"SouiR41NativeFairnessOwner",0},strips{pid,L"SouiR41NativeInputStrip",0};
    need(EnumWindows(count_class,reinterpret_cast<LPARAM>(&roots))&&EnumChildWindows(t.owner,count_class,reinterpret_cast<LPARAM>(&strips))&&roots.count==1&&strips.count==1,"unique actual task owner/native strip required");
    ShowWindow(t.owner,SW_RESTORE);(void)SetForegroundWindow(t.owner);cold_validate(t,cold,"post-restore-foreground",true);
    const auto stem=std::wstring(L"Local\\SOUI-R45-")+std::to_wstring(pid)+L"-"+std::to_wstring(expected)+L"-"+wr;
    ready.h=OpenEventW(EVENT_MODIFY_STATE,FALSE,(stem+L"-Ready").c_str());handle start(OpenEventW(SYNCHRONIZE,FALSE,(stem+L"-LoadActive").c_str()));finished.h=OpenEventW(EVENT_MODIFY_STATE,FALSE,(stem+L"-Finish").c_str());
    sender_done.h=OpenEventW(EVENT_MODIFY_STATE,FALSE,(stem+L"-SenderDone").c_str());
    need(ready.h&&start.h&&sender_done.h&&finished.h,"exact R45 task events");LARGE_INTEGER f{};need(QueryPerformanceFrequency(&f)&&f.QuadPart>0&&static_cast<std::uint64_t>(f.QuadPart)<UINT64_MAX/125,"QPF");const auto frequency=static_cast<std::uint64_t>(f.QuadPart);
    capture(t,zero,&cold);check_marker_identity(zero,nonce,true);need(SetEvent(ready.h),"input-ready signal");readySent=true;
    need(WaitForSingleObject(start.h,15000)==WAIT_OBJECT_0,"pre-frozen LoadActive wait15s");t.load_event=start.h;validate(t);
    const int vx=GetSystemMetrics(SM_XVIRTUALSCREEN),vy=GetSystemMetrics(SM_YVIRTUALSCREEN),vw=GetSystemMetrics(SM_CXVIRTUALSCREEN),vh=GetSystemMetrics(SM_CYVIRTUALSCREEN);need(vw>0&&vh>0,"virtual metrics");handle timer(make_timer());need(timer.h,"timer");const auto began=qpc();
    try{for(unsigned i=0;i<rows.size();++i){auto&r=rows[i];const int x=40+16*static_cast<int>(i%64),y=64;r.scheduled=began+(i/125)*frequency+(i%125)*frequency/125;wait_deadline(timer.h,process.h,r.scheduled,frequency);validate(t);POINT at{t.origin.x+x,t.origin.y+y};need(WindowFromPoint(at)==t.strip,"native strip child hit changed");INPUT packet{};packet.type=INPUT_MOUSE;r.dx=packet.mi.dx=normalize(at.x,vx,vw);r.dy=packet.mi.dy=normalize(at.y,vy,vh);packet.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK|MOUSEEVENTF_MOVE_NOCOALESCE;packet.mi.dwExtraInfo=static_cast<ULONG_PTR>(owner_fairness::tag_magic|(i+1));r.before=qpc();++attempted;r.sent=SendInput(1,&packet,sizeof(packet));r.after=qpc();sent+=r.sent;need(r.sent==1,"SendInput rejected");}}catch(const std::exception&e){error=e.what();}
    if(!error.empty())need(ResetEvent(ready.h),"signal input-aborted attempt");
    // Fix ACK500 before the new signal. Signal/API overhead consumes this same
    // original budget; SenderDone is not a claim that every SendInput succeeded.
    const auto deadline=GetTickCount64()+500;
    ackDeadlineTick=deadline;senderDoneBeforeQpc=qpc();senderDoneAttempted=true;
    senderDoneSignaled=SetEvent(sender_done.h)!=FALSE;senderDoneAfterQpc=qpc();
    need(senderDoneSignaled,"sender-done signal");
    bool ack=false;if(error.empty()){do{need(GetTickCount64()<deadline,"Native final ACK500ms failed");capture(t,final);check_marker_identity(final,nonce,false);if(final.ack==owner_fairness::input_count){ack=GetTickCount64()<deadline;break;}need(WaitForSingleObject(process.h,8)==WAIT_TIMEOUT,"held target exited");}while(GetTickCount64()<deadline);if(!ack)error="Native final ACK500ms failed";}
    json a=json::array();for(unsigned i=0;i<attempted;++i){const auto&r=rows[i];a.push_back({i+1,std::to_string(r.scheduled),std::to_string(r.before),std::to_string(r.after),r.sent,r.dx,r.dy});}
    result={{"schema","soui.r45-native-input/v2"},{"profile","r45-native-drain-strip-v2"},{"run",run},{"targetPid",pid},{"targetBirth",std::to_string(expected)},{"inputPid",GetCurrentProcessId()},{"inputBirth",std::to_string(birth(GetCurrentProcess()))},{"ownerHwnd",std::to_string(reinterpret_cast<std::uintptr_t>(t.owner))},{"stripHwnd",std::to_string(reinterpret_cast<std::uintptr_t>(t.strip))},{"qpcFrequency",std::to_string(frequency)},{"requested",owner_fairness::input_count},{"hz",125},{"attempted",attempted},{"sent",sent},{"finalAck",ack},{"senderDoneSignaled",senderDoneSignaled},{"error",error.empty()?json(nullptr):json(error)},
      {"zeroCapture",{std::to_string(zero.begin),std::to_string(zero.end)}},{"finalCapture",{std::to_string(final.begin),std::to_string(final.end)}},
      {"zeroMarker",marker_json(zero)},{"finalMarker",marker_json(final)},{"ownerThread",t.thread},
      {"senderDoneAttempted",senderDoneAttempted},{"senderDoneBeforeQpc",std::to_string(senderDoneBeforeQpc)},{"senderDoneAfterQpc",std::to_string(senderDoneAfterQpc)},{"ackDeadlineTick",std::to_string(ackDeadlineTick)},
      {"inputFlags",MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK|MOUSEEVENTF_MOVE_NOCOALESCE},
      {"virtualMetrics",{vx,vy,vw,vh}},{"stripOrigin",{t.origin.x,t.origin.y}},{"columns",{"sequence","scheduledQpc","beforeQpc","afterQpc","sent","dx","dy"}},{"rows",a},{"performanceGate",false}};
    result["coldDiagnostics"]=r42_cold::json(cold);write_new(output,result);need(SetEvent(finished.h),"finish signal after persistence");return error.empty()&&ack&&sent==rows.size()?0:1;
  }catch(const std::exception&e){std::cerr<<e.what()<<'\n';error=e.what();}
  if(readySent&&ready.h&&(error=="E_USER_ESCAPE_ABORT"||error=="E_HOST_CLOSE_ABORT"||error.find("target")!=std::string::npos))
    (void)ResetEvent(ready.h);
  // No normal-close success is invented. Keep actual partial rows if an outer
  // check failed; the Host's original fixed deadline handles absent readiness.
  if(!output.empty()&&!fs::exists(output))try{json a=json::array();for(unsigned i=0;i<attempted;++i){const auto&r=rows[i];a.push_back({i+1,std::to_string(r.scheduled),std::to_string(r.before),std::to_string(r.after),r.sent,r.dx,r.dy});}
    write_new(output,{{"schema","soui.r45-native-input-failure/v2"},{"profile","r45-native-drain-strip-v2"},{"run",run},{"attempted",attempted},{"sent",sent},{"inputReadySent",readySent},
      {"senderDoneAttempted",senderDoneAttempted},{"senderDoneSignaled",senderDoneSignaled},
      {"senderDoneBeforeQpc",senderDoneBeforeQpc?json(std::to_string(senderDoneBeforeQpc)):json(nullptr)},
      {"senderDoneAfterQpc",senderDoneAfterQpc?json(std::to_string(senderDoneAfterQpc)):json(nullptr)},
      {"ackDeadlineTick",ackDeadlineTick?json(std::to_string(ackDeadlineTick)):json(nullptr)},
      {"zeroMarker",marker_json(zero)},{"finalMarker",marker_json(final)},{"coldDiagnostics",r42_cold::json(cold)},{"rows",a},{"error",error},{"performanceGate",false}});
    if(finished.h)(void)SetEvent(finished.h); // Finished attempt, never a success receipt.
  }catch(...){}
  return 1;
}
