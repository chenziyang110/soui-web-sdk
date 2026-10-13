#define NOMINMAX
#include "desktop-observer.h"
#include "trace-identity.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <limits>
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
} // namespace soui::benchmark
