#define NOMINMAX
#include "desktop-observer.h"
#include "trace-identity.h"
#include "passive-observation.h"
#include "passive-wait-timer.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <limits>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace soui::benchmark {
namespace {
template<class T> struct com {
  T* p{};
  ~com() { if (p) p->Release(); }
  com() = default; com(const com&) = delete; com& operator=(const com&) = delete;
  T** out() { if (p) { p->Release(); p = nullptr; } return &p; }
};
struct handle {
  HANDLE p{INVALID_HANDLE_VALUE};
  ~handle() { if (p && p != INVALID_HANDLE_VALUE) CloseHandle(p); }
};
struct library {
  HMODULE p{};
  explicit library(const wchar_t* name) {
    wchar_t system[MAX_PATH]{};
    const UINT size = GetSystemDirectoryW(system, MAX_PATH);
    if (!size || size >= MAX_PATH) throw std::runtime_error("Desktop system directory unavailable");
    const std::wstring full = std::wstring(system) + L"\\" + name;
    p = LoadLibraryW(full.c_str());
    if (!p) { const DWORD error=GetLastError(); throw std::runtime_error("Desktop graphics component unavailable: win32="+std::to_string(error)); }
  }
  ~library() { if (p) FreeLibrary(p); }
  library(const library&) = delete; library& operator=(const library&) = delete;
};
void require(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
void ok(HRESULT hr, const char* text) {
  if(FAILED(hr))throw std::runtime_error(std::string(text)+": HRESULT="+std::to_string(static_cast<std::uint32_t>(hr)));
}
std::uint64_t tick() { LARGE_INTEGER q{}; require(QueryPerformanceCounter(&q) != FALSE, "Desktop QPC failed"); return static_cast<std::uint64_t>(q.QuadPart); }
std::uint64_t ft(FILETIME q) { return (static_cast<std::uint64_t>(q.dwHighDateTime) << 32) | q.dwLowDateTime; }
std::string quoted(std::uint64_t q) { return "\"" + std::to_string(q) + "\""; }
std::string output_name(const wchar_t* text) {
  std::string result="\"";
  for(unsigned n=0;text[n];++n){require(n<32&&text[n]>=32&&text[n]<127,"Desktop output name invalid");if(text[n]=='\\'||text[n]=='\"')result+='\\';result+=static_cast<char>(text[n]);}
  return result+'\"';
}
void line(HANDLE output, const std::string& text) {
  const std::string raw = text + "\n"; DWORD written{};
  require(raw.size() <= 4096 && WriteFile(output, raw.data(), static_cast<DWORD>(raw.size()), &written, nullptr) && written == raw.size(), "Desktop trace write failed");
}
std::uint16_t crc(const std::array<unsigned char,16>& data) {
  std::uint16_t value = 0xffff;
  for (unsigned n = 0; n < 12; ++n) {
    value ^= static_cast<std::uint16_t>(data[n]) << 8;
    for (unsigned b = 0; b < 8; ++b) value = static_cast<std::uint16_t>((value << 1) ^ ((value & 0x8000) ? 0x1021 : 0));
  }
  return value;
}
struct marker { std::uint32_t nonce{}, ack{}, frame{}; unsigned scene{}; bool motion{}; std::array<unsigned char,16> bytes{}; };
std::string marker_hex(const marker& value) {
  std::string result; result.reserve(32); constexpr char digits[]="0123456789abcdef";
  for(auto byte:value.bytes){result+=digits[byte>>4];result+=digits[byte&15];}
  return result;
}
marker decode(const D3D11_MAPPED_SUBRESOURCE& map) {
  require(map.RowPitch >= 64 * 4 && map.pData, "Desktop pixel buffer invalid");
  std::array<unsigned char,16> data{};
  for (unsigned i = 0; i < 128; ++i) {
    const auto* pixel = static_cast<const unsigned char*>(map.pData) + ((i / 16) * 4 + 2) * map.RowPitch + ((i % 16) * 4 + 2) * 4;
    const bool dark = pixel[0] <= 32 && pixel[1] <= 32 && pixel[2] <= 32;
    const bool light = pixel[0] >= 223 && pixel[1] >= 223 && pixel[2] >= 223;
    require(dark || light, "Desktop marker has ambiguous pixels");
    if (light) data[i / 8] |= static_cast<unsigned char>(1U << (i % 8));
  }
  require(data[0] == 0xa7 && data[1] == 0x53 && data[14] == 0 && data[15] == 0, "Desktop marker magic/reserved bits invalid");
  require(crc(data) == (static_cast<unsigned>(data[12]) | (static_cast<unsigned>(data[13]) << 8)), "Desktop marker CRC invalid");
  auto little = [&data](unsigned start, unsigned count) { std::uint32_t value{}; for (unsigned n=0;n<count;++n) value |= static_cast<std::uint32_t>(data[start+n]) << (n*8); return value; };
  const unsigned flags = data[11];
  require((flags & 0xf0) == 0 && (flags & 7) >= 1 && (flags & 7) <= 5, "Desktop marker flags invalid");
  marker result{little(2,4),little(6,3),little(9,2),flags & 7,(flags & 8) != 0};
  // ACK uses 24 bits in the wire marker; only 20 are admitted. Frame uses 16.
  require(result.nonce && result.ack < (1U << 20), "Desktop marker counter invalid");
  result.bytes=data;
  return result;
}
} // namespace

struct desktop_observer::state {
  HANDLE process; DWORD pid; std::uint64_t creation; HWND window;
  std::filesystem::path output; unsigned scene; bool probe;
  std::thread thread; std::mutex mutex; std::condition_variable changed;
  std::atomic<bool> stop{false}, normal_stop{false}; bool ready{}, done{}; std::string failure;
  std::uint32_t acknowledged{};
  marker latest{}; std::uint64_t frames{}, pointer_only{}, started{}, frequency{};
  POINT origin{}; HMONITOR monitor{};
  state(HANDLE p,DWORD id,std::uint64_t c,HWND w,const std::filesystem::path& out,unsigned s,bool short_run)
      :process(p),pid(id),creation(c),window(w),output(out),scene(s),probe(short_run) {}
  void bind_window() {
    DWORD current{}; GetWindowThreadProcessId(window,&current);
    require(IsWindow(window) && IsWindowVisible(window) && !IsIconic(window) && current == pid && WaitForSingleObject(process,0) == WAIT_TIMEOUT, "Desktop owner window/process changed");
    require(GetForegroundWindow() == window, "Desktop foreground changed");
    POINT current_origin{}; require(ClientToScreen(window,&current_origin) != FALSE,"Desktop geometry unavailable");
    require(current_origin.x == origin.x && current_origin.y == origin.y && MonitorFromWindow(window,MONITOR_DEFAULTTONULL) == monitor,"Desktop output/region changed");
    RECT client{}; require(GetClientRect(window,&client) && ((client.right==1100&&client.bottom==720)||(scene==4&&client.right==1060&&client.bottom==700)),"Desktop client geometry changed");
    HDC dc=GetDC(window); require(dc != nullptr,"Desktop DPI unavailable"); const int dpi=GetDeviceCaps(dc,LOGPIXELSX); ReleaseDC(window,dc); require(dpi==96,"Desktop observation requires 96 DPI");
  }
  void run() noexcept {
    handle file;
    try {
      file.p=CreateFileW(output.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
      require(file.p != INVALID_HANDLE_VALUE,"Cannot create new desktop trace");
      LARGE_INTEGER f{}; require(QueryPerformanceFrequency(&f) && f.QuadPart>0,"Desktop QPC frequency unavailable"); frequency=static_cast<std::uint64_t>(f.QuadPart); started=tick();
      FILETIME c{},e{},k{},u{};require(GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u),"Desktop collector identity unavailable");
      origin={};require(ClientToScreen(window,&origin),"Desktop region unavailable");monitor=MonitorFromWindow(window,MONITOR_DEFAULTTONULL);require(monitor != nullptr,"Desktop monitor unavailable");bind_window();
      library dxgi(L"dxgi.dll"), d3d(L"d3d11.dll");
      using factory_fn=HRESULT (WINAPI*)(REFIID,void**);
      using device_fn=HRESULT (WINAPI*)(IDXGIAdapter*,D3D_DRIVER_TYPE,HMODULE,UINT,const D3D_FEATURE_LEVEL*,UINT,UINT,ID3D11Device**,D3D_FEATURE_LEVEL*,ID3D11DeviceContext**);
      auto factory_api=reinterpret_cast<factory_fn>(GetProcAddress(dxgi.p,"CreateDXGIFactory1")); auto device_api=reinterpret_cast<device_fn>(GetProcAddress(d3d.p,"D3D11CreateDevice"));require(factory_api&&device_api,"Desktop graphics API unavailable");
      com<IDXGIFactory1> factory;ok(factory_api(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(factory.out())),"Desktop factory failed");
      com<IDXGIAdapter1> adapter;com<IDXGIOutput> selected;DXGI_OUTPUT_DESC description{};DXGI_ADAPTER_DESC1 adapter_description{};
      bool found=false;
      for(UINT a=0;a<32&&!found;++a){com<IDXGIAdapter1> candidate;HRESULT hr=factory.p->EnumAdapters1(a,candidate.out());if(hr==DXGI_ERROR_NOT_FOUND)break;ok(hr,"Desktop adapter enumeration failed");
        for(UINT o=0;o<32;++o){com<IDXGIOutput> candidate_output;hr=candidate.p->EnumOutputs(o,candidate_output.out());if(hr==DXGI_ERROR_NOT_FOUND)break;ok(hr,"Desktop output enumeration failed");DXGI_OUTPUT_DESC desc{};ok(candidate_output.p->GetDesc(&desc),"Desktop output description failed");
          if(desc.Monitor==monitor&&desc.AttachedToDesktop){require(desc.Rotation==DXGI_MODE_ROTATION_IDENTITY,"Desktop rotated output unsupported");description=desc;selected.p=candidate_output.p;candidate_output.p=nullptr;adapter.p=candidate.p;candidate.p=nullptr;found=true;break;}}}
      require(found,"Desktop owning output not found");ok(adapter.p->GetDesc1(&adapter_description),"Desktop adapter description failed");
      RECT bounds{}; require(GetWindowRect(window,&bounds) && bounds.left>=description.DesktopCoordinates.left && bounds.top>=description.DesktopCoordinates.top && bounds.right<=description.DesktopCoordinates.right && bounds.bottom<=description.DesktopCoordinates.bottom,"Desktop owner must fit one output");
      com<ID3D11Device> device;com<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};ok(device_api(adapter.p,D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,nullptr,0,D3D11_SDK_VERSION,device.out(),&level,context.out()),"Desktop D3D device failed");
      com<IDXGIOutput1> output1;ok(selected.p->QueryInterface(__uuidof(IDXGIOutput1),reinterpret_cast<void**>(output1.out())),"Desktop duplication unavailable");
      com<IDXGIOutputDuplication> duplication;ok(output1.p->DuplicateOutput(device.p,duplication.out()),"Desktop duplication unsupported/unavailable");
      const auto x64=static_cast<std::int64_t>(origin.x)-description.DesktopCoordinates.left+8;
      const auto y64=static_cast<std::int64_t>(origin.y)-description.DesktopCoordinates.top+8;
      require(x64>=0&&y64>=0&&x64<=std::numeric_limits<int>::max()-64&&y64<=std::numeric_limits<int>::max()-32,"Desktop marker coordinates invalid");
      const int x=static_cast<int>(x64),y=static_cast<int>(y64);
      D3D11_TEXTURE2D_DESC staging_desc{};staging_desc.Width=64;staging_desc.Height=32;staging_desc.MipLevels=1;staging_desc.ArraySize=1;staging_desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;staging_desc.SampleDesc.Count=1;staging_desc.Usage=D3D11_USAGE_STAGING;staging_desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
      com<ID3D11Texture2D> staging;ok(device.p->CreateTexture2D(&staging_desc,nullptr,staging.out()),"Desktop staging texture failed");
      line(file.p,"{\"event\":\"desktop.begin\",\"schema\":\"soui.desktop-visual-trace/v1\",\"pid\":"+std::to_string(pid)+",\"creationFileTime\":"+quoted(creation)+",\"collectorPid\":"+std::to_string(GetCurrentProcessId())+",\"collectorCreationFileTime\":"+quoted(ft(c))+",\"hwnd\":"+quoted(reinterpret_cast<std::uintptr_t>(window))+",\"scene\":"+std::to_string(scene)+",\"probe\":"+(probe?"true":"false")+",\"qpcFrequency\":"+quoted(frequency)+",\"startedQpc\":"+quoted(started)+",\"originX\":"+std::to_string(origin.x)+",\"originY\":"+std::to_string(origin.y)+",\"dpi\":96,\"rotation\":0,\"region\":[8,8,64,32],\"adapterLow\":"+std::to_string(adapter_description.AdapterLuid.LowPart)+",\"adapterHigh\":"+std::to_string(adapter_description.AdapterLuid.HighPart)+"}");
      line(file.p,"{\"event\":\"desktop.output\",\"deviceName\":"+output_name(description.DeviceName)+",\"left\":"+std::to_string(description.DesktopCoordinates.left)+",\"top\":"+std::to_string(description.DesktopCoordinates.top)+",\"right\":"+std::to_string(description.DesktopCoordinates.right)+",\"bottom\":"+std::to_string(description.DesktopCoordinates.bottom)+",\"adapterLow\":"+std::to_string(adapter_description.AdapterLuid.LowPart)+",\"adapterHigh\":"+std::to_string(adapter_description.AdapterLuid.HighPart)+"}");
      const auto deadline=GetTickCount64()+(probe?15000:scene==5?140000:80000);
      std::uint64_t last{}, acquisitions{};
      while(!stop.load()) {
        require(GetTickCount64()<deadline,"Desktop observation exceeded its deadline");bind_window();
        DXGI_OUTPUT_DESC current_output{};
        ok(selected.p->GetDesc(&current_output),"Desktop output recheck failed");
        require(current_output.Monitor==monitor && current_output.AttachedToDesktop &&
            current_output.Rotation==description.Rotation &&
            EqualRect(&current_output.DesktopCoordinates,&description.DesktopCoordinates) &&
            std::wstring(current_output.DeviceName)==description.DeviceName,
            "Desktop output configuration changed");
        DXGI_OUTDUPL_FRAME_INFO info{};com<IDXGIResource> resource;
        const HRESULT acquired=duplication.p->AcquireNextFrame(50,&info,resource.out());if(acquired==DXGI_ERROR_WAIT_TIMEOUT)continue;ok(acquired,"Desktop frame acquisition failed");
        struct frame_release {IDXGIOutputDuplication* p;bool active{true};~frame_release(){if(active)p->ReleaseFrame();}void release(){const auto hr=p->ReleaseFrame();active=false;ok(hr,"Desktop frame release failed");}} release{duplication.p};
        require(++acquisitions<=20000,"Desktop acquisition sample limit exceeded");
        if(!info.LastPresentTime.QuadPart){++pointer_only;require(pointer_only<=20000,"Desktop pointer sample limit exceeded");line(file.p,"{\"event\":\"desktop.pointer\",\"observedQpc\":"+quoted(tick())+",\"lastPresentQpc\":\"0\",\"accumulatedFrames\":"+std::to_string(info.AccumulatedFrames)+"}");require(info.AccumulatedFrames==0,"Desktop pointer-only metadata invalid");release.release();continue;}
        const auto present=static_cast<std::uint64_t>(info.LastPresentTime.QuadPart);
        require(present>last,"Desktop timestamps reordered");last=present;
        line(file.p,"{\"event\":\"desktop.acquire\",\"lastPresentQpc\":"+quoted(present)+",\"accumulatedFrames\":"+std::to_string(info.AccumulatedFrames)+",\"protected\":"+(info.ProtectedContentMaskedOut?"true":"false")+"}");
        require(info.AccumulatedFrames==1&&!info.ProtectedContentMaskedOut,"Desktop accumulated/protected frame invalidates observation");
        require(resource.p!=nullptr,"Desktop frame resource unavailable");
        com<ID3D11Texture2D> texture;ok(resource.p->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(texture.out())),"Desktop frame texture unavailable");D3D11_TEXTURE2D_DESC actual{};texture.p->GetDesc(&actual);require(actual.Format==DXGI_FORMAT_B8G8R8A8_UNORM&&actual.Width<=static_cast<UINT>(std::numeric_limits<int>::max())&&actual.Height<=static_cast<UINT>(std::numeric_limits<int>::max())&&x+64<=static_cast<int>(actual.Width)&&y+32<=static_cast<int>(actual.Height),"Desktop texture region invalid");
        const D3D11_BOX box{static_cast<UINT>(x),static_cast<UINT>(y),0,static_cast<UINT>(x+64),static_cast<UINT>(y+32),1};context.p->CopySubresourceRegion(staging.p,0,0,0,0,texture.p,0,&box);context.p->Flush();
        D3D11_MAPPED_SUBRESOURCE map{};HRESULT mapped{};const auto map_deadline=GetTickCount64()+100;
        do {mapped=context.p->Map(staging.p,0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&map);if(mapped!=DXGI_ERROR_WAS_STILL_DRAWING)break;require(GetTickCount64()<map_deadline,"Desktop GPU read timeout");Sleep(1);}while(true);ok(mapped,"Desktop GPU read failed");
        marker value;
        try{value=decode(map);}catch(...){
          context.p->Unmap(staging.p,0);
          if(!ready&&tick()-started<frequency*5){line(file.p,"{\"event\":\"desktop.baseline-reject\",\"lastPresentQpc\":"+quoted(present)+"}");release.release();continue;}
          throw;
        }
        context.p->Unmap(staging.p,0);
        if(!ready)line(file.p,"{\"event\":\"desktop.ready\",\"lastPresentQpc\":"+quoted(present)+",\"nonce\":"+std::to_string(value.nonce)+",\"ack\":"+std::to_string(value.ack)+"}");
        {std::lock_guard<std::mutex> lock(mutex);if(ready)require(value.nonce==latest.nonce&&value.ack>=latest.ack&&value.frame>=latest.frame,"Desktop marker lifetime/counters changed");latest=value;ready=true;}changed.notify_all();
        ++frames;require(frames<=20000,"Desktop sample limit exceeded");
        RECT client{};require(GetClientRect(window,&client),"Desktop client dimensions unavailable");
        line(file.p,"{\"event\":\"desktop.frame\",\"lastPresentQpc\":"+quoted(present)+",\"observedQpc\":"+quoted(tick())+",\"nonce\":"+std::to_string(value.nonce)+",\"ack\":"+std::to_string(value.ack)+",\"frame\":"+std::to_string(value.frame)+",\"scene\":"+std::to_string(value.scene)+",\"motion\":"+(value.motion?"true":"false")+",\"markerHex\":\""+marker_hex(value)+"\",\"width\":"+std::to_string(client.right)+",\"height\":"+std::to_string(client.bottom)+"}");
        release.release();
      }
      line(file.p,"{\"event\":\"desktop.end\",\"completed\":"+std::string(normal_stop.load()?"true":"false")+",\"frames\":"+std::to_string(frames)+",\"pointerOnly\":"+std::to_string(pointer_only)+",\"endedQpc\":"+quoted(tick())+"}");require(FlushFileBuffers(file.p),"Desktop trace flush failed");
    } catch(const std::exception& error) {
      {std::lock_guard<std::mutex> lock(mutex);failure=error.what();}
      if(file.p!=INVALID_HANDLE_VALUE){try{line(file.p,"{\"event\":\"desktop.end\",\"completed\":false,\"frames\":"+std::to_string(frames)+",\"pointerOnly\":"+std::to_string(pointer_only)+",\"endedQpc\":"+quoted(tick())+"}");FlushFileBuffers(file.p);}catch(...){}}
    } catch(...) {std::lock_guard<std::mutex> lock(mutex);failure="Desktop observation failed";}
    {std::lock_guard<std::mutex> lock(mutex);done=true;}changed.notify_all();
  }
};
desktop_observer::desktop_observer(HANDLE p,DWORD id,std::uint64_t c,HWND w,const std::filesystem::path& out,unsigned scene,bool probe)
    :state_(std::make_unique<state>(p,id,c,w,out,scene,probe)) {
  state_->thread=std::thread([s=state_.get()]{s->run();});
  std::unique_lock<std::mutex> lock(state_->mutex);
  const bool ready=state_->changed.wait_for(lock,std::chrono::seconds(5),[this]{return state_->ready||state_->done;});
  if(!ready||!state_->failure.empty()||!state_->ready){const std::string reason=state_->failure.empty()?"Desktop marker baseline timeout":state_->failure;lock.unlock();state_->stop=true;state_->thread.join();throw std::runtime_error(reason);}
  state_->acknowledged=state_->latest.ack;
}
desktop_observer::~desktop_observer(){if(state_){state_->stop=true;if(state_->thread.joinable())state_->thread.join();}}
void desktop_observer::check(){std::lock_guard<std::mutex> lock(state_->mutex);if(!state_->failure.empty())throw std::runtime_error(state_->failure);require(!state_->done,"Desktop observer stopped early");}
std::uint32_t desktop_observer::next_event(){check();std::lock_guard<std::mutex> lock(state_->mutex);require(state_->latest.ack==state_->acknowledged,"Desktop unexpected input ACK");require(state_->latest.ack+1<(1U<<20),"Desktop event counter exhausted");return state_->latest.ack+1;}
void desktop_observer::wait_ack(std::uint32_t event){std::unique_lock<std::mutex> lock(state_->mutex);const bool ready=state_->changed.wait_for(lock,std::chrono::milliseconds(500),[this,event]{return state_->latest.ack>=event||state_->done;});require(ready&&state_->failure.empty()&&!state_->done&&state_->latest.ack==event,"Desktop visual ACK failed/timeout");state_->acknowledged=event;}
void desktop_observer::finish(){state_->normal_stop=true;state_->stop=true;if(state_->thread.joinable())state_->thread.join();std::lock_guard<std::mutex> lock(state_->mutex);require(state_->failure.empty()&&state_->done,"Desktop observer failed at finish");}

namespace {
// No target messages, input, activation, geometry writes or SDK calls are used
// below. Out-of-context WinEvents make a witnessed destroy/blur sticky, including
// when the numeric HWND is reused before the next polling check.
void passive_ok(HRESULT value, const char* reason) { passive_require(SUCCEEDED(value), reason); }
struct passive_clock {
  std::uint64_t last{};
  std::uint64_t read() {
    LARGE_INTEGER value{};
    passive_require(QueryPerformanceCounter(&value) && value.QuadPart > 0, "observer-failed");
    const auto now = static_cast<std::uint64_t>(value.QuadPart);
    passive_require(now >= last, "observer-failed"); last = now; return now;
  }
};
struct passive_watch {
  HWND window{};
  HWINEVENTHOOK destroy{}, foreground{}, geometry{}, hidden{};
  bool destroyed{}, blurred{}, moved{}, concealed{}, active{};
  DWORD armed_at{};
  static thread_local passive_watch* current;
  static void CALLBACK event(HWINEVENTHOOK, DWORD kind, HWND window, LONG object,
                             LONG child, DWORD, DWORD time) {
    auto* self = current;
    if (!self) return;
    if (kind == EVENT_OBJECT_DESTROY && window == self->window && object == OBJID_WINDOW && child == CHILDID_SELF)
      self->destroyed = true;
    if (!self->active || static_cast<LONG>(time - self->armed_at) < 0) return;
    if (kind == EVENT_SYSTEM_FOREGROUND && window != self->window) self->blurred = true;
    if (window == self->window && object == OBJID_WINDOW && child == CHILDID_SELF) {
      if (kind == EVENT_OBJECT_LOCATIONCHANGE) self->moved = true;
      if (kind == EVENT_OBJECT_HIDE) self->concealed = true;
    }
  }
  passive_watch(HWND value, DWORD pid) : window(value) {
    passive_require(current == nullptr, "observer-failed"); current = this;
    destroy = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY, nullptr, event, pid, 0, WINEVENT_OUTOFCONTEXT);
    foreground = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, event, 0, 0, WINEVENT_OUTOFCONTEXT);
    geometry = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, event, pid, 0, WINEVENT_OUTOFCONTEXT);
    hidden = SetWinEventHook(EVENT_OBJECT_HIDE, EVENT_OBJECT_HIDE, nullptr, event, pid, 0, WINEVENT_OUTOFCONTEXT);
    if (!destroy || !foreground || !geometry || !hidden) { close(); throw passive_error("observer-failed"); }
  }
  void close() noexcept {
    if (hidden) UnhookWinEvent(hidden); if (geometry) UnhookWinEvent(geometry);
    if (foreground) UnhookWinEvent(foreground); if (destroy) UnhookWinEvent(destroy);
    hidden = geometry = foreground = destroy = nullptr; current = nullptr;
  }
  ~passive_watch() { close(); }
  void check_sticky() const {
    passive_require(!destroyed, "window-destroyed");
    passive_require(!blurred, "foreground-lost");
    passive_require(!moved, "geometry-changed");
    passive_require(!concealed, "window-hidden");
  }
  void pump() {
    MSG message{};
    unsigned messages{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      // A flooding event queue fails closed instead of extending the deadline.
      passive_require(++messages <= 64, "observer-failed");
      passive_require(message.message != WM_QUIT, "observer-failed");
      TranslateMessage(&message); DispatchMessageW(&message);
      check_sticky();
    }
    check_sticky();
  }
  void arm() { pump(); armed_at = GetTickCount(); active = true; }
};
thread_local passive_watch* passive_watch::current = nullptr;

struct passive_binding {
  HANDLE process; const passive_options& options; HWND window;
  DWORD thread{}; POINT origin{}; RECT client{}; HMONITOR monitor{};
  passive_binding(HANDLE held, const passive_options& selected)
      : process(held), options(selected), window(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(selected.hwnd))) {
    DWORD pid{}; thread = GetWindowThreadProcessId(window, &pid);
    passive_require(thread && pid == options.pid && IsWindow(window), "window-identity");
    wchar_t name[64]{};
    passive_require(GetClassNameW(window, name, 64) && std::wstring(name) == L"SouiInputUiHostV1", "window-identity");
    passive_require(ClientToScreen(window, &origin) && GetClientRect(window, &client), "geometry-changed");
    passive_require(client.left == 0 && client.top == 0 && client.right == 1100 && client.bottom == 720, "geometry-changed");
    monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
    passive_require(monitor != nullptr, "geometry-changed");
    check(false);
  }
  void check(bool foreground) const {
    passive_require(WaitForSingleObject(process, 0) == WAIT_TIMEOUT, "target-closed");
    DWORD pid{}; const DWORD current_thread = GetWindowThreadProcessId(window, &pid);
    passive_require(IsWindow(window) && pid == options.pid && current_thread == thread, "window-identity");
    passive_require(IsWindowVisible(window) && !IsIconic(window), "window-hidden");
    passive_require(!foreground || GetForegroundWindow() == window, "foreground-lost");
    POINT position{}; RECT area{};
    passive_require(ClientToScreen(window, &position) && GetClientRect(window, &area) &&
        position.x == origin.x && position.y == origin.y && EqualRect(&area, &client) &&
        MonitorFromWindow(window, MONITOR_DEFAULTTONULL) == monitor, "geometry-changed");
    HDC dc = GetDC(window); passive_require(dc != nullptr, "dpi-changed");
    const int x = GetDeviceCaps(dc, LOGPIXELSX), y = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(window, dc);
    passive_require(x == 96 && y == 96, "dpi-changed");
  }
};

// Cursor shape bounds are conservatively treated as occlusion even where a
// transparent cursor pixel might leave the sampled color intact. Never move it.
const char* passive_cursor(const POINT& origin) {
  CURSORINFO cursor{}; cursor.cbSize = sizeof(cursor);
  if (!GetCursorInfo(&cursor)) return "cursor-unavailable";
  if (!(cursor.flags & CURSOR_SHOWING)) return nullptr;
  ICONINFO icon{};
  if (!GetIconInfo(cursor.hCursor, &icon)) return "cursor-unavailable";
  struct bitmaps {
    HBITMAP color, mask;
    ~bitmaps() { if (color) DeleteObject(color); if (mask) DeleteObject(mask); }
  } cleanup{icon.hbmColor, icon.hbmMask};
  BITMAP bitmap{};
  if (!GetObjectW(icon.hbmColor ? icon.hbmColor : icon.hbmMask, sizeof(bitmap), &bitmap) ||
      bitmap.bmWidth <= 0 || bitmap.bmHeight <= 0) return "cursor-unavailable";
  const auto left = static_cast<std::int64_t>(cursor.ptScreenPos.x) - icon.xHotspot;
  const auto top = static_cast<std::int64_t>(cursor.ptScreenPos.y) - icon.yHotspot;
  const auto height = icon.hbmColor ? bitmap.bmHeight : bitmap.bmHeight / 2;
  return left < static_cast<std::int64_t>(origin.x) + 72 && left + bitmap.bmWidth > static_cast<std::int64_t>(origin.x) + 8 &&
      top < static_cast<std::int64_t>(origin.y) + 368 && top + height > static_cast<std::int64_t>(origin.y) + 328 ? "cursor-overlap" : nullptr;
}
struct passive_desktop_pointer {
  std::array<unsigned char, 65536> buffer{};
  DXGI_OUTDUPL_POINTER_POSITION position{};
  UINT width{}, height{};
  bool shape_known{}, position_known{};
  void update(IDXGIOutputDuplication* duplication, const DXGI_OUTDUPL_FRAME_INFO& frame) {
    if (frame.LastMouseUpdateTime.QuadPart) { position = frame.PointerPosition; position_known = true; }
    if (!shape_known || frame.PointerShapeBufferSize) {
      UINT needed{}; DXGI_OUTDUPL_POINTER_SHAPE_INFO shape{};
      const auto result = duplication->GetFramePointerShape(static_cast<UINT>(buffer.size()), buffer.data(), &needed, &shape);
      shape_known = SUCCEEDED(result) && needed <= buffer.size() && shape.Width && shape.Width <= 4096 && shape.Height && shape.Height <= 8192 &&
          (shape.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR || shape.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR ||
           (shape.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME && shape.Height % 2 == 0));
      if (shape_known) { width = shape.Width; height = shape.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME ? shape.Height / 2 : shape.Height; }
    }
  }
  const char* overlap(std::int64_t x, std::int64_t y) const {
    if (!position_known) return "cursor-unavailable";
    if (!position.Visible) return nullptr;
    if (!shape_known) return "cursor-unavailable";
    // DXGI's Position is the shape's top left in output coordinates; HotSpot
    // is not subtracted. Conservatively include transparent shape pixels.
    const auto left = static_cast<std::int64_t>(position.Position.x), top = static_cast<std::int64_t>(position.Position.y);
    return left < x + 64 && left + width > x && top < y + 40 && top + height > y ? "cursor-overlap" : nullptr;
  }
};
struct passive_writer {
  HANDLE file; std::uint64_t bytes{};
  void write(const std::string& text) {
    passive_require(text.size() + 1 <= 4096 && bytes + text.size() + 1 <= 64U * 1024U * 1024U, "output-limit");
    const auto raw = text + "\n"; DWORD written{};
    passive_require(WriteFile(file, raw.data(), static_cast<DWORD>(raw.size()), &written, nullptr) && written == raw.size(), "persist-failed");
    bytes += raw.size();
  }
};
std::string passive_prefix(const passive_options& options, const char* event) {
  return "{\"schema\":\"soui.manual-desktop-observation/v1\",\"event\":\"" + std::string(event) + "\",\"run_id\":\"" + options.run + "\"";
}
std::uint64_t passive_deadline(std::uint64_t begin, std::uint64_t frequency, unsigned ms) {
  passive_require(frequency <= std::numeric_limits<std::uint64_t>::max() / 200000, "observer-failed");
  const auto duration = frequency * ms / 1000;
  passive_require(duration && begin <= std::numeric_limits<std::uint64_t>::max() - duration, "observer-failed");
  return begin + duration;
}
void passive_wait(HANDLE process, passive_watch& watch, passive_clock& clock, passive_wait_timer& timer,
                  std::uint64_t until, std::uint64_t frequency) {
  passive_wait_until(process, clock, timer, until, frequency, [&watch] { watch.pump(); });
}
struct passive_counts {
  std::uint64_t samples{}, startup{}, capture{}, valid{}, changes{}, marker_only{}, duplicates{}, pointers{}, timeouts{}, unknown{};
  std::uint64_t accumulated{}, unobserved{}, frame_gaps{}, first{}, last{};
};
} // namespace

int observe_manual_desktop(HANDLE process, const passive_options& options) {
  handle file;
  file.p = CreateFileW(options.output.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file.p == INVALID_HANDLE_VALUE) { std::cerr << "Cannot create new passive observation\n"; return 2; }
  passive_writer writer{file.p}; passive_counts count; passive_clock clock; passive_wait_timer wait_timer;
  std::uint64_t frequency{}, startup_begin{}, phase_begin{}, phase_deadline{}, phase_end{};
  const auto end = [&](bool completed, const char* reason) {
    passive_require(!phase_begin || (phase_end >= phase_begin && phase_end >= count.last), "observer-failed");
    const bool qualified = completed && count.changes >= 2 && !count.unknown && !count.unobserved;
    const auto optional_tick = [](std::uint64_t value) { return value ? quoted(value) : std::string("null"); };
    wait_timer.close();
    writer.write(passive_prefix(options, "manual-observation.end") +
        ",\"phaseBeginQpc\":" + optional_tick(phase_begin) + ",\"phaseEndQpc\":" + optional_tick(phase_begin ? phase_end : 0) +
        ",\"phaseDeadlineQpc\":" + optional_tick(phase_deadline) + ",\"samples\":" + std::to_string(count.samples) +
        ",\"startupSamples\":" + std::to_string(count.startup) + ",\"captureSamples\":" + std::to_string(count.capture) +
        ",\"validSamples\":" + std::to_string(count.valid) + ",\"contentChanges\":" + std::to_string(count.changes) +
        ",\"markerOnlySamples\":" + std::to_string(count.marker_only) + ",\"duplicateSamples\":" + std::to_string(count.duplicates) +
        ",\"pointerOnlySamples\":" + std::to_string(count.pointers) + ",\"timeoutPolls\":" + std::to_string(count.timeouts) +
        ",\"unknownSamples\":" + std::to_string(count.unknown) + ",\"accumulatedDesktopFrames\":" + std::to_string(count.accumulated) +
        ",\"unobservedDesktopFrames\":" + std::to_string(count.unobserved) + ",\"frameSequenceGapTotal\":" + std::to_string(count.frame_gaps) +
        ",\"firstContentQpc\":" + optional_tick(count.first) + ",\"lastContentQpc\":" + optional_tick(count.last) +
        ",\"headTicks\":" + (count.first ? quoted(count.first - phase_begin) : "null") +
        ",\"tailTicks\":" + (count.last ? quoted(phase_end - count.last) : "null") +
        ",\"completed\":" + (completed ? "true" : "false") + ",\"observationQualified\":" + (qualified ? "true" : "false") +
        ",\"reason\":" + (reason ? "\"" + std::string(reason) + "\"" : "null") +
        ",\"physicalPresentation\":false,\"inputToPhotonMeasured\":false,\"performanceGate\":false}");
    passive_require(FlushFileBuffers(file.p) != FALSE, "persist-failed");
    // A raw end line is not the process receipt: final close can still fail.
    // Detach both references first so a failed close is never retried by RAII.
    const HANDLE closing = file.p;
    file.p = INVALID_HANDLE_VALUE; writer.file = INVALID_HANDLE_VALUE;
    passive_require(CloseHandle(closing) != FALSE, "persist-failed");
    return qualified ? 0 : 3;
  };
  try {
    LARGE_INTEGER qpf{}; passive_require(QueryPerformanceFrequency(&qpf) && qpf.QuadPart >= 1000, "observer-failed");
    frequency = static_cast<std::uint64_t>(qpf.QuadPart); startup_begin = clock.read();
    const auto startup_deadline = passive_deadline(startup_begin, frequency, options.startup_ms);
    wait_timer.create();
    FILETIME creation{}, exited{}, kernel{}, user{};
    passive_require(GetProcessTimes(GetCurrentProcess(), &creation, &exited, &kernel, &user) != FALSE, "observer-failed");
    writer.write(passive_prefix(options, "manual-observation.begin") +
        ",\"targetPid\":" + std::to_string(options.pid) + ",\"targetCreationFileTime\":" + quoted(options.creation) + ",\"hwnd\":" + quoted(options.hwnd) +
        ",\"observerPid\":" + std::to_string(GetCurrentProcessId()) + ",\"observerCreationFileTime\":" + quoted(ft(creation)) +
        ",\"qpcFrequency\":" + quoted(frequency) + ",\"startupBeginQpc\":" + quoted(startup_begin) + ",\"startupDeadlineQpc\":" + quoted(startup_deadline) +
        ",\"intervalMs\":" + std::to_string(options.interval_ms) + ",\"durationMs\":" + std::to_string(options.duration_ms) + ",\"startupMs\":" + std::to_string(options.startup_ms) +
        ",\"waitTimerBackend\":\"" + wait_timer.backend() + "\"" +
        ",\"markerVersion\":\"manual-feedback-v1\",\"markerRegion\":[8,336,64,32],\"contentProbe\":[10,330],\"captureRegion\":[8,328,64,40]}");
    const auto window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(options.hwnd));
    passive_watch watch(window, options.pid); passive_binding binding(process, options);
    while (GetForegroundWindow() != window) {
      binding.check(false); passive_require(clock.read() < startup_deadline, "startup-timeout");
      passive_wait(process, watch, clock, wait_timer, (std::min)(startup_deadline, passive_deadline(clock.read(), frequency, 20)), frequency);
    }
    binding.check(true); watch.arm(); binding.check(true);

    library dxgi(L"dxgi.dll"), d3d(L"d3d11.dll");
    using factory_fn = HRESULT (WINAPI*)(REFIID, void**);
    using device_fn = HRESULT (WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
    const auto factory_api = reinterpret_cast<factory_fn>(GetProcAddress(dxgi.p, "CreateDXGIFactory1"));
    const auto device_api = reinterpret_cast<device_fn>(GetProcAddress(d3d.p, "D3D11CreateDevice"));
    passive_require(factory_api && device_api, "graphics-failed");
    com<IDXGIFactory1> factory; passive_ok(factory_api(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory.out())), "graphics-failed");
    com<IDXGIAdapter1> adapter; com<IDXGIOutput> output; DXGI_OUTPUT_DESC description{};
    for (UINT a = 0; a < 32 && !output.p; ++a) {
      com<IDXGIAdapter1> candidate; const auto hr = factory.p->EnumAdapters1(a, candidate.out());
      if (hr == DXGI_ERROR_NOT_FOUND) break; passive_ok(hr, "graphics-failed");
      for (UINT o = 0; o < 32; ++o) {
        com<IDXGIOutput> selected; const auto result = candidate.p->EnumOutputs(o, selected.out());
        if (result == DXGI_ERROR_NOT_FOUND) break; passive_ok(result, "graphics-failed");
        DXGI_OUTPUT_DESC desc{}; passive_ok(selected.p->GetDesc(&desc), "graphics-failed");
        if (desc.Monitor == binding.monitor && desc.AttachedToDesktop) {
          passive_require(desc.Rotation == DXGI_MODE_ROTATION_IDENTITY, "output-changed");
          description = desc; output.p = selected.p; selected.p = nullptr; adapter.p = candidate.p; candidate.p = nullptr; break;
        }
      }
    }
    passive_require(output.p != nullptr, "graphics-failed");
    RECT outer{}; passive_require(GetWindowRect(window, &outer) && outer.left >= description.DesktopCoordinates.left && outer.top >= description.DesktopCoordinates.top &&
        outer.right <= description.DesktopCoordinates.right && outer.bottom <= description.DesktopCoordinates.bottom, "geometry-changed");
    com<ID3D11Device> device; com<ID3D11DeviceContext> context; D3D_FEATURE_LEVEL feature{};
    passive_ok(device_api(adapter.p, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.out(), &feature, context.out()), "graphics-failed");
    com<IDXGIOutput1> output1; passive_ok(output.p->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(output1.out())), "graphics-failed");
    com<IDXGIOutputDuplication> duplication; passive_ok(output1.p->DuplicateOutput(device.p, duplication.out()), "graphics-failed");
    const auto x = static_cast<std::int64_t>(binding.origin.x) - description.DesktopCoordinates.left + 8;
    const auto y = static_cast<std::int64_t>(binding.origin.y) - description.DesktopCoordinates.top + 328;
    passive_require(x >= 0 && y >= 0 && x <= std::numeric_limits<int>::max() - 64 && y <= std::numeric_limits<int>::max() - 40, "geometry-changed");
    D3D11_TEXTURE2D_DESC staging_desc{};
    staging_desc.Width = 64; staging_desc.Height = 40; staging_desc.MipLevels = 1; staging_desc.ArraySize = 1;
    staging_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; staging_desc.SampleDesc.Count = 1;
    staging_desc.Usage = D3D11_USAGE_STAGING; staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    com<ID3D11Texture2D> staging; passive_ok(device.p->CreateTexture2D(&staging_desc, nullptr, staging.out()), "graphics-failed");
    passive_sequence sequence; passive_desktop_pointer desktop_pointer;
    auto next = clock.read(); std::uint64_t last_present{};
    for (;;) {
      const auto deadline = phase_begin ? phase_deadline : startup_deadline;
      if (phase_begin && clock.read() >= deadline) break;
      passive_require(phase_begin || clock.read() < deadline, "startup-timeout");
      passive_wait(process, watch, clock, wait_timer, (std::min)(next, deadline), frequency);
      watch.pump(); binding.check(true);
      if (clock.read() >= deadline) { if (phase_begin) break; throw passive_error("startup-timeout"); }
      DXGI_OUTPUT_DESC current{}; passive_ok(output.p->GetDesc(&current), "output-changed");
      passive_require(current.Monitor == description.Monitor && current.AttachedToDesktop && current.Rotation == description.Rotation &&
          EqualRect(&current.DesktopCoordinates, &description.DesktopCoordinates) && std::wstring(current.DeviceName) == description.DeviceName, "output-changed");
      const bool capture = phase_begin != 0;
      const auto before = clock.read();
      if (before >= deadline) { if (capture) break; throw passive_error("startup-timeout"); }
      const char* cursor_before = passive_cursor(binding.origin);
      const auto acquire_now = clock.read();
      if (acquire_now >= deadline) { if (capture) break; throw passive_error("startup-timeout"); }
      auto remaining_ms = (deadline - acquire_now) * 1000 / frequency;
      if (remaining_ms > 50) remaining_ms = 50;
      DXGI_OUTDUPL_FRAME_INFO info{}; com<IDXGIResource> resource;
      const HRESULT acquired = duplication.p->AcquireNextFrame(static_cast<UINT>(remaining_ms), &info, resource.out());
      const bool timeout = acquired == DXGI_ERROR_WAIT_TIMEOUT;
      if (!timeout) passive_ok(acquired, "capture-failed");
      struct frame_release {
        IDXGIOutputDuplication* duplication; bool active;
        ~frame_release() { if (active) duplication->ReleaseFrame(); }
        void release() { if (active) { const auto hr = duplication->ReleaseFrame(); active = false; passive_ok(hr, "capture-failed"); } }
      } release{duplication.p, !timeout};
      if (!timeout) desktop_pointer.update(duplication.p, info);
      passive_pixels pixels; bool have_pixels{}; passive_transition transition;
      const char* reason{}; const char* status = timeout ? "wait-timeout" : "pointer-only";
      const bool pointer = !timeout && info.LastPresentTime.QuadPart == 0;
      std::uint64_t present{};
      if (!timeout && !pointer) {
        passive_require(info.LastPresentTime.QuadPart > 0, "present-regressed");
        present = static_cast<std::uint64_t>(info.LastPresentTime.QuadPart);
        passive_require(present > last_present && present <= clock.read(), "present-regressed"); last_present = present;
        passive_require(resource.p != nullptr, "capture-failed");
        com<ID3D11Texture2D> texture;
        passive_ok(resource.p->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(texture.out())), "capture-failed");
        D3D11_TEXTURE2D_DESC actual{}; texture.p->GetDesc(&actual);
        passive_require(actual.Format == DXGI_FORMAT_B8G8R8A8_UNORM && x + 64 <= actual.Width && y + 40 <= actual.Height, "capture-failed");
        const D3D11_BOX box{static_cast<UINT>(x), static_cast<UINT>(y), 0, static_cast<UINT>(x + 64), static_cast<UINT>(y + 40), 1};
        context.p->CopySubresourceRegion(staging.p, 0, 0, 0, 0, texture.p, 0, &box); context.p->Flush();
        D3D11_MAPPED_SUBRESOURCE map{}; HRESULT mapped{};
        const auto map_deadline = (std::min)(deadline, passive_deadline(clock.read(), frequency, 100));
        do {
          mapped = context.p->Map(staging.p, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &map);
          if (mapped != DXGI_ERROR_WAS_STILL_DRAWING) break;
          passive_require(clock.read() < map_deadline, "gpu-read-timeout");
          passive_wait(process, watch, clock, wait_timer, (std::min)(map_deadline, passive_deadline(clock.read(), frequency, 1)), frequency);
        } while (true);
        passive_ok(mapped, "capture-failed");
        struct unmap { ID3D11DeviceContext* context; ID3D11Texture2D* texture; ~unmap() { context->Unmap(texture, 0); } } mapped_scope{context.p, staging.p};
        pixels = decode_passive_pixels(map.pData, map.RowPitch, passive_nonce(options.run)); have_pixels = true;
        reason = cursor_before ? cursor_before : passive_cursor(binding.origin);
        if (!reason) reason = desktop_pointer.overlap(x, y);
        if (!reason && info.ProtectedContentMaskedOut) reason = "protected-content";
        if (!reason) reason = pixels.reason;
        if (!reason) {
          // Rejected startup acquisitions never establish a document epoch.
          if (capture || info.AccumulatedFrames == 1) transition = sequence.accept(pixels);
          if (info.AccumulatedFrames != 1) reason = "accumulated-frames";
          else if (transition.reason) reason = transition.reason;
          else if (capture && present < phase_begin) reason = "present-before-phase";
        }
        status = reason ? (capture ? "unknown" : "baseline-reject") : "valid";
      } else if (pointer && info.AccumulatedFrames) { reason = "accumulated-frames"; status = capture ? "unknown" : "baseline-reject"; }
      release.release(); watch.pump(); binding.check(true);
      const auto after = clock.read();
      if (!capture) passive_require(after < startup_deadline, "startup-timeout");
      passive_require(present <= after, "present-regressed");
      passive_require(++count.samples <= 250000, "output-limit");
      if (capture) {
        ++count.capture;
        if (timeout) ++count.timeouts;
        if (pointer) ++count.pointers;
        count.accumulated += info.AccumulatedFrames;
        if (info.AccumulatedFrames > 1) count.unobserved += info.AccumulatedFrames - 1;
        count.frame_gaps += transition.frame_gap; // Local page renders, not desktop loss.
        if (std::string(status) == "unknown") ++count.unknown;
        if (!reason && have_pixels) {
          ++count.valid;
          if (transition.marker_only) ++count.marker_only;
          if (transition.duplicate) ++count.duplicates;
          if (transition.content_change) { ++count.changes; if (!count.first) count.first = present; count.last = present; }
        }
      } else ++count.startup;
      const auto nullable_number = [have_pixels, &pixels](unsigned value) { return have_pixels && pixels.marker_readable ? std::to_string(value) : std::string("null"); };
      writer.write(passive_prefix(options, "manual-observation.sample") +
          ",\"phase\":\"" + (capture ? "capture" : "startup") + "\",\"sampleIndex\":" + std::to_string(count.samples) +
          ",\"captureBeginQpc\":" + quoted(before) + ",\"captureEndQpc\":" + quoted(after) + ",\"lastPresentQpc\":" + quoted(present) +
          ",\"accumulatedFrames\":" + std::to_string(info.AccumulatedFrames) + ",\"pointerOnly\":" + (pointer ? "true" : "false") +
          ",\"status\":\"" + status + "\",\"reason\":" + (reason ? "\"" + std::string(reason) + "\"" : "null") +
          ",\"markerHex\":" + (have_pixels && pixels.marker_readable ? "\"" + pixels.marker_hex() + "\"" : "null") +
          ",\"contentRgb\":" + (have_pixels ? "[" + std::to_string(pixels.rgb[0]) + "," + std::to_string(pixels.rgb[1]) + "," + std::to_string(pixels.rgb[2]) + "]" : "null") +
          ",\"captureBgraCrc32\":" + (have_pixels ? "\"" + passive_hex(pixels.crc32, 8) + "\"" : "null") +
          ",\"revision\":" + nullable_number(pixels.revision) + ",\"frame\":" + nullable_number(pixels.frame) +
          ",\"flags\":" + nullable_number(pixels.flags) + ",\"pageEpoch\":" + nullable_number(pixels.epoch) +
          ",\"contentChange\":" + (capture && !reason && transition.content_change ? "true" : "false") +
          ",\"markerOnly\":" + (transition.marker_only ? "true" : "false") + ",\"frameSequenceGap\":" + std::to_string(transition.frame_gap) +
          ",\"revisionDelta\":" + std::to_string(transition.revision_delta) + "}");
      if (!phase_begin && !reason && have_pixels) {
        binding.check(true);
        phase_begin = after; phase_deadline = passive_deadline(phase_begin, frequency, options.duration_ms);
        writer.write(passive_prefix(options, "manual-observation.phase") + ",\"phase\":\"capture\",\"phaseBeginQpc\":" + quoted(phase_begin) + ",\"phaseDeadlineQpc\":" + quoted(phase_deadline) + "}");
      }
      // Keep the absolute interval lattice; a late acquisition skips due slots.
      const auto period = frequency * options.interval_ms / 1000;
      const auto now = clock.read();
      if (next <= now) next += ((now - next) / period + 1) * period;
    }
    watch.pump(); binding.check(true); phase_end = clock.read();
    const bool gaps = count.unknown || count.unobserved;
    return end(true, gaps ? "observation-gaps" : count.changes < 2 ? "insufficient-content-changes" : nullptr);
  } catch (const std::exception& error) {
    const auto* known = dynamic_cast<const passive_error*>(&error);
    const char* reason = known ? known->what() : "observer-failed";
    // Pure sequence errors are fixed codes, never arbitrary target/page strings.
    if (!known && (std::string(error.what()) == "epoch-changed" || std::string(error.what()) == "counter-regressed")) reason = error.what();
    std::cerr << "Passive observation failed: " << reason << '\n';
    try { wait_timer.close(); }
    catch (const std::exception& cleanup) { std::cerr << "Passive cleanup failed: " << cleanup.what() << '\n'; }
    try { phase_end = clock.read(); end(false, reason); } catch (...) {}
    return 2;
  }
}
} // namespace soui::benchmark
