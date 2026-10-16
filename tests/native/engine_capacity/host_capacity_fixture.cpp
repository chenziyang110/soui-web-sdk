#include <soui/host/soui_miniblink_host.h>
#include <soui/json_bridge.h>
#include "engine_admission.h"
#include <soui/diagnostics/renderer_model_binding.h>
#include "../../acceptance-app/native-owner-metrics.h"
#include "../../acceptance-app/shutdown-poll.h"
#include <windows.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <json.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
using json = nlohmann::json;
using namespace std::chrono_literals;
namespace fs = std::filesystem;
using soui::sdk::detail::engine_admission;
using soui::sdk::detail::engine_admission_result;
using soui::sdk::detail::engine_lane;
[[noreturn]] void fatal(const char* code) noexcept {
  std::fprintf(stderr,"engine-host-capacity.failed:%s\n",code); std::fflush(stderr);
  ExitProcess(91); // No unresolved real Host/Bridge/HWND/module may unwind.
}
void require(bool value,const char* code) { if (!value) fatal(code); }

std::string hash_file(HANDLE file) {
  BCRYPT_ALG_HANDLE provider{}; BCRYPT_HASH_HANDLE hash{}; DWORD object_bytes{}, got{};
  require(BCryptOpenAlgorithmProvider(&provider,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"hash-provider");
  require(BCryptGetProperty(provider,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&object_bytes),sizeof(object_bytes),&got,0)>=0 && got==sizeof(object_bytes),"hash-object");
  std::vector<unsigned char> object(object_bytes),buffer(1024U*1024U);
  require(BCryptCreateHash(provider,&hash,object.data(),object_bytes,nullptr,0,0)>=0,"hash-create");
  for (;;) { DWORD count{}; require(ReadFile(file,buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr)!=FALSE,"hash-read");
    if (!count) break; require(BCryptHashData(hash,buffer.data(),count,0)>=0,"hash-data"); }
  std::array<unsigned char,32> digest{};
  require(BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0,"hash-finish");
  require(BCryptDestroyHash(hash)>=0 && BCryptCloseAlgorithmProvider(provider,0)>=0,"hash-close");
  constexpr char hex[]="0123456789abcdef"; std::string text(64,'0');
  for (std::size_t i=0;i!=digest.size();++i) { text[2*i]=hex[digest[i]>>4]; text[2*i+1]=hex[digest[i]&15]; }
  return text;
}
struct settings final { fs::path kernel,resources,output; std::string kernel_sha,selected; };
settings arguments() {
  int count{}; auto** values=CommandLineToArgvW(GetCommandLineW(),&count);
  require(values!=nullptr,"arguments"); std::vector<std::wstring> rows;
  try { rows.assign(values,values+count); } catch (...) { LocalFree(values); throw; }
  LocalFree(values);
  require(rows.size()==11 && rows[1]==L"--kernel" && rows[3]==L"--kernel-sha" && rows[5]==L"--resources" && rows[7]==L"--output" && rows[9]==L"--case","argument-shape");
  settings out{rows[2],rows[6],rows[8],std::string(rows[4].begin(),rows[4].end()),std::string(rows[10].begin(),rows[10].end())};
  require(out.selected=="reply-full-zero-business" || out.selected=="cleanup-full-retains-owner" ||
    out.selected=="renderer-model-abi" || out.selected=="renderer-cache-abi","fixed-case");
  require(out.kernel.is_absolute() && out.resources.is_absolute() && out.output.is_absolute() && fs::is_regular_file(out.kernel) &&
    fs::is_regular_file(out.resources/"index.html") && !fs::exists(out.output) && fs::is_directory(out.output.parent_path()),"actual-inputs-fresh-output");
  require(out.kernel_sha.size()==64 && out.kernel_sha.find_first_not_of("0123456789abcdef")==std::string::npos,"kernel-hash-format");
  return out;
}

// Read-only app adapter: all real authorization/task ownership stays in json_bridge.
// The actual bootstrap reply is retained privately, never emitted in a receipt.
class observed_bridge final : public soui::sdk::bridge_adapter {
 public:
  soui::sdk::json_bridge real{"app://engine-capacity"};
  std::string handle_json(std::string_view request,bool gesture) override {
    auto response=real.handle_json(request,gesture);
    const auto envelope=json::parse(request,nullptr,false);
    if (envelope.is_object() && envelope.value("method",std::string{})=="bridge.bootstrap") {
      const auto parsed=json::parse(response,nullptr,false);
      if (parsed.is_object() && parsed.value("ok",false) && parsed.contains("result") && parsed["result"].is_object()) {
        std::lock_guard<std::mutex> lock(mutex_); grant_=parsed["result"];
      }
    }
    return response;
  }
  void shutdown_session(const std::string& session) override { real.shutdown_session(session); }
  void set_owner_window(void* owner) noexcept override { real.set_owner_window(owner); }
  void set_event_sink(event_sink sink) override { real.set_event_sink(std::move(sink)); }
  std::string request() {
    std::lock_guard<std::mutex> lock(mutex_);
    require(grant_.is_object() && grant_.contains("sessionId") && grant_.contains("origin") && grant_.contains("token"),"actual-bootstrap-grant");
    return json{{"requestId","capacity-refused"},{"sessionId",grant_.at("sessionId")},{"origin",grant_.at("origin")},{"token",grant_.at("token")},
      {"method","capacity.business"},{"params",{{"value",7}}},{"timeoutMs",5000}}.dump();
  }
 private:
  std::mutex mutex_; json grant_;
};
struct application final {
  std::atomic<unsigned> business{0},confirmed{0}; // Handler storage outlives Bridge task-service join.
  observed_bridge bridge;
  std::unique_ptr<soui::sdk::webview_host> host;
  soui::sdk::dynamic_miniblink_adapter* renderer{};
  HWND owner{}; bool withdrawal_allowed{false};
};
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) noexcept {
  auto* app=reinterpret_cast<application*>(GetWindowLongPtrW(window,GWLP_USERDATA));
  if (message==WM_NCCREATE) { app=static_cast<application*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    SetLastError(0); const auto old=SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));
    require(old!=0 || GetLastError()==0,"owner-store"); app->owner=window; }
  if (app) {
    try {
      if (message==soui::sdk::bridge_event_message) { require(app->host!=nullptr,"posted-event-owner"); app->host->deliver_posted_event(reinterpret_cast<void*>(lparam)); return 0; }
      if (message==WM_CLOSE) return 0;
      if (message==WM_DESTROY) require(app->withdrawal_allowed,"owner-premature-withdrawal");
      if (message==WM_NCDESTROY) SetWindowLongPtrW(window,GWLP_USERDATA,0);
    } catch (...) { fatal("owner-callback-exception"); }
  }
  return DefWindowProcW(window,message,wparam,lparam);
}
void pump(application& app) {
  MSG message{};
  for (unsigned i=0;i!=64 && PeekMessageW(&message,app.owner,0,0,PM_REMOVE);++i) { TranslateMessage(&message); DispatchMessageW(&message); }
  app.host->pump();
}
void close_retained(application& app) {
  const auto began=GetTickCount64();
  for (;;) {
    const auto closed=app.host->destroy();
    if (closed.ok) break;
    require(closed.code=="E_BUSY" || closed.code=="E_CURSOR_CLOSE_PENDING","unknown-close-failure-retained");
    require(GetTickCount64()-began<10000,"real-close-budget-retained");
    pump(app); require(MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT)!=WAIT_FAILED,"real-close-owner-wait");
  }
  app.host.reset(); app.renderer=nullptr; // Only actual completed destroy releases the borrowed adapter.
}
struct engine_hold final {
  std::mutex mutex; std::condition_variable changed; bool started{false},released{false};
  void enter() { std::unique_lock<std::mutex> lock(mutex); started=true; changed.notify_all();
    if (!changed.wait_for(lock,8s,[&]{return released;})) fatal("actual-Engine-release-deadline"); }
  void begin(soui::sdk::engine_thread& engine) { require(engine.post([this]{enter();}),"actual-Engine-blocker-admitted");
    std::unique_lock<std::mutex> lock(mutex); require(changed.wait_for(lock,2s,[&]{return started;}),"actual-Engine-blocker-started"); }
  void release() { { std::lock_guard<std::mutex> lock(mutex); released=true; } changed.notify_all(); }
};
bool output_new(const fs::path& path,const std::string& text) {
  const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if (file==INVALID_HANDLE_VALUE) return false; DWORD written{};
  const bool valid=text.size()<=MAXDWORD && WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr) && written==text.size();
  return CloseHandle(file)!=FALSE && valid;
}
const char* architecture() noexcept {
#ifdef _WIN64
  return "x64";
#else
  return "x86";
#endif
}
constexpr ULONGLONG case_budget_ms=30000;
void within_case(ULONGLONG began) { require(GetTickCount64()-began<case_budget_ms,"original-30-second-case-deadline"); }
void service_retired_owner() {
  MSG message{};
  for(unsigned i=0;i!=64 && PeekMessageW(&message,nullptr,0,0,PM_REMOVE);++i) {
    TranslateMessage(&message); DispatchMessageW(&message);
  }
  require(MsgWaitForMultipleObjects(0,nullptr,FALSE,1,QS_ALLINPUT)!=WAIT_FAILED,"runtime-owner-wait");
}
void shutdown_within_case(ULONGLONG began) {
  const auto result=soui::acceptance::poll_runtime_shutdown([&](auto completed) {
    for(;;) { within_case(began); if(completed()) return; service_retired_owner(); }
  },[]{return soui::sdk::shutdown_miniblink_runtime();});
  require(result.ok,"actual-runtime-shutdown-nonpending-failure");
}

static_assert(sizeof(wkeRendererModelSnapshot)==264);
static_assert(sizeof(wkeRendererProcessSnapshot)==168);
static_assert(sizeof(wkeRendererModelDiagnosticsApi)==24);
static_assert(offsetof(wkeRendererModelDiagnosticsApi,readProcessModel)==16);
static_assert(offsetof(wkeRendererModelSnapshot,compositor)==16);
static_assert(offsetof(wkeRendererModelSnapshot,providerId)==184);
static_assert(offsetof(wkeRendererModelSnapshot,backendInitializationQpc)==200);
static_assert(offsetof(wkeRendererModelSnapshot,unknownRouteTotal)==256);
class real_renderer_model_probe final {
 public:
  explicit real_renderer_model_probe(const settings& options) {
    module_=LoadLibraryW(options.kernel.c_str()); require(module_!=nullptr,"actual-abi-module-load");
    std::array<wchar_t,32768> path{};
    const auto count=GetModuleFileNameW(module_,path.data(),static_cast<DWORD>(path.size()));
    require(count>0&&count<path.size()&&fs::equivalent(fs::path(path.data()),options.kernel),"actual-abi-module-route");
    module_path_=fs::weakly_canonical(fs::path(path.data())).u8string();
    get_=reinterpret_cast<get_api>(GetProcAddress(module_,"wkeGetRendererModelDiagnosticsApi"));
    require(get_!=nullptr,"required-real-model-export-missing");
    table_=get_(2); require(soui::sdk::diagnostics::detail::renderer_model_api_supported(table_),"real-model-table-contract");
    using get_v1_api=const wkeRendererDiagnosticsApi* (__cdecl*)(unsigned);
    const auto get_v1=reinterpret_cast<get_v1_api>(GetProcAddress(module_,"wkeGetRendererDiagnosticsApi"));
    require(get_v1!=nullptr,"required-real-v1-export-missing"); v1_=get_v1(1);
    require(soui::sdk::diagnostics::detail::renderer_api_supported(v1_),"real-embedded-v1-table-contract");
    invalid_outputs();
    preinit_=read();
    require(preinit_.status==WKE_RENDER_DIAGNOSTICS_NOT_STARTED&&
      preinit_.compiledGpuSupport==WKE_RENDER_MODEL_GPU_UNKNOWN&&(preinit_.flags&19U)==0&&
      preinit_.compositor.structSize==168&&preinit_.compositor.abiVersion==1&&
      preinit_.compositor.status==WKE_RENDER_DIAGNOSTICS_NOT_STARTED&&preinit_.compositor.flags==0&&
      preinit_.compositor.processId==0&&preinit_.compositor.processCreationFileTime==0&&
      preinit_.compositor.recordingEpoch==0&&preinit_.compositor.initializedQpc==0&&
      preinit_.compositor.rasterDrawsTotal==0&&preinit_.compositor.gpuSkiaDrawsTotal==0,"raw-preinit-is-unknown-not-ready");
  }
  ~real_renderer_model_probe() { if(module_)require(FreeLibrary(module_)!=FALSE,"probe-extra-module-reference-release"); }
  real_renderer_model_probe(const real_renderer_model_probe&)=delete;
  real_renderer_model_probe& operator=(const real_renderer_model_probe&)=delete;
  void invalid_outputs() const {
    for(const auto version:std::array<unsigned,4>{0,1,3,(std::numeric_limits<unsigned>::max)()})
      require(get_(version)==nullptr,"wrong-real-model-version-not-rejected");
    require(table_->readProcessModel(nullptr)==WKE_RENDER_DIAGNOSTICS_INVALID_ARGUMENT,"actual-null-output-rejected");
    struct guarded final { std::array<unsigned char,16> before; wkeRendererModelSnapshot value; std::array<unsigned char,16> after; };
    for(const auto size:std::array<unsigned,3>{0,263,265}) {
      guarded output; std::memset(&output,0xa5,sizeof(output)); output.value.structSize=size;
      std::array<unsigned char,sizeof(guarded)> original{}; std::memcpy(original.data(),&output,sizeof(output));
      require(table_->readProcessModel(&output.value)==WKE_RENDER_DIAGNOSTICS_INVALID_ARGUMENT&&
        std::memcmp(original.data(),&output,sizeof(output))==0,"actual-bad-size-rejected-with-no-buffer-write");
    }
  }
  wkeRendererModelSnapshot read() const {
    require(get_(2)==table_,"real-api-table-pointer-changed");
    wkeRendererModelSnapshot value{}; value.structSize=sizeof(value);
    const auto state=table_->readProcessModel(&value);
    require(value.structSize==264&&value.abiVersion==2&&value.status==state,"actual-model-envelope-return-differs");
    return value;
  }
  bool observe(unsigned expected_state,DWORD owner,std::uint64_t creation,ULONGLONG frequency,
               wkeRendererModelSnapshot& output,bool terminal) {
    wkeRendererProcessSnapshot before{},after{}; before.structSize=after.structSize=168;
    const auto before_state=v1_->readProcessSnapshot(&before);
    const auto value=read(); const auto after_state=v1_->readProcessSnapshot(&after);
    if(before_state==WKE_RENDER_DIAGNOSTICS_BUSY||value.status==WKE_RENDER_DIAGNOSTICS_BUSY||
      after_state==WKE_RENDER_DIAGNOSTICS_BUSY)return false;
    if(std::memcmp(&before,&after,sizeof(before))!=0||std::memcmp(&after,&value.compositor,sizeof(after))!=0)return false;
    require(value.status==expected_state,"actual-model-state-does-not-match-phase");
    soui::sdk::diagnostics::renderer_model_observation scalar;
    const auto sdk_status=soui::sdk::read_miniblink_renderer_model_metrics(scalar);
    if(sdk_status==soui::sdk::diagnostics::renderer_status::busy)return false;
    require((sdk_status==soui::sdk::diagnostics::renderer_status::ready||sdk_status==soui::sdk::diagnostics::renderer_status::closed)&&
      scalar.module_matches&&scalar.identity_bound&&fs::equivalent(fs::u8path(scalar.module_path),fs::u8path(module_path_)),
      "actual-sdk-model-binding-unavailable");
    const auto latest=read();
    if(std::memcmp(&value,&latest,sizeof(value))!=0||std::memcmp(&scalar.native,&value,sizeof(value))!=0)return false;
    const auto& raw=value.compositor;
    LARGE_INTEGER captured{}; require(QueryPerformanceCounter(&captured)&&captured.QuadPart>0,"actual-model-capture-qpc");
    require(raw.processId==GetCurrentProcessId()&&raw.processCreationFileTime==creation&&raw.engineOwnerThreadId==owner&&owner>0&&
      raw.qpcFrequency==frequency&&raw.recordingEpoch==raw.initializedQpc&&raw.initializedQpc>0&&
      value.backendInitializationQpc>0&&value.backendInitializationQpc<=raw.initializedQpc&&
      raw.initializedQpc<=static_cast<std::uint64_t>(captured.QuadPart),
      "actual-model-process-owner-clock-epoch-differs");
    require(value.flags==19&&value.providerId==1&&value.backendId==1&&value.compiledGpuSupport==0&&value.factoryCoverageMask==127&&
      value.mediaFactoryAttempts==0&&value.pluginFactoryAttempts==0&&value.offscreenFactoryAttempts==0&&
      value.sharedContextFactoryAttempts==0&&value.externalFactoryAttempts==0&&value.platformMismatchTotal==0&&value.unknownRouteTotal==0,
      "actual-compatibility-model-differs");
    require(binding_.observe(value,GetCurrentProcessId(),creation),"actual-model-history-binding-differs");
    if(expected_state==WKE_RENDER_DIAGNOSTICS_READY&&raw.drawEventsTotal==0) {
      require(raw.renderersCreatedTotal>0&&raw.renderersLive>0&&raw.rasterDrawsTotal==0&&raw.gpuSkiaDrawsTotal==0&&
        raw.lastDrawKind==WKE_RENDER_DIAGNOSTICS_DRAW_UNKNOWN&&raw.lastDrawQpc==0&&raw.lastDrawProcessId==0&&raw.lastDrawThreadId==0&&
        (raw.flags&(WKE_RENDER_DIAGNOSTICS_OBSERVED_RASTER|WKE_RENDER_DIAGNOSTICS_OBSERVED_GPU_SKIA|WKE_RENDER_DIAGNOSTICS_ENGINE_FINALIZED))==0,
        "actual-first-draw-history-not-self-consistent");
      return false; // A valid live renderer still owes an actual completed CPU draw.
    }
    require(raw.lastDrawQpc>=raw.initializedQpc&&raw.lastDrawQpc<=static_cast<std::uint64_t>(captured.QuadPart)&&
      raw.lastDrawProcessId==GetCurrentProcessId()&&raw.lastDrawThreadId>0&&raw.lastDrawThreadId<=MAXDWORD,
      "actual-completed-draw-clock-identity-differs");
    require(
      raw.rasterDrawsTotal>0&&raw.gpuSkiaDrawsTotal==0&&raw.lastDrawKind==WKE_RENDER_DIAGNOSTICS_DRAW_BITMAP_RASTER&&
      (raw.flags&WKE_RENDER_DIAGNOSTICS_OBSERVED_RASTER)!=0&&(raw.flags&WKE_RENDER_DIAGNOSTICS_OBSERVED_GPU_SKIA)==0,
      "actual-completed-cpu-raster-and-model-not-observed");
    if(terminal) {
      require(scalar.terminal_snapshot&&raw.renderersLive==0&&(raw.flags&WKE_RENDER_DIAGNOSTICS_ENGINE_FINALIZED)&&
        raw.closedSnapshotQpc>=raw.initializedQpc&&raw.engineFinalizedQpc>=raw.closedSnapshotQpc&&
        raw.engineFinalizedQpc<=static_cast<std::uint64_t>(captured.QuadPart)&&
        binding_.retain_terminal(value,true),"actual-finalized-terminal-not-observed");
    }
    captured_qpc_=static_cast<std::uint64_t>(captured.QuadPart); output=value; return true;
  }
  json serialize(const wkeRendererModelSnapshot& raw,bool terminal=false) const {
    soui::sdk::diagnostics::renderer_model_observation value;
    value.native=raw;value.module_path=module_path_;value.module_matches=true;
    value.identity_bound=raw.status==1||raw.status==4;value.terminal_snapshot=terminal;
    value.state=raw.status==0?soui::sdk::diagnostics::renderer_status::not_started:
      raw.status==4?soui::sdk::diagnostics::renderer_status::closed:soui::sdk::diagnostics::renderer_status::ready;
    return soui::acceptance::metrics::serialize(value);
  }
  const wkeRendererModelSnapshot& preinit() const noexcept { return preinit_; }
  std::uint64_t captured_qpc() const noexcept { return captured_qpc_; }
  std::string capabilities() const { return std::to_string(table_->capabilities); }
 private:
  using get_api=const wkeRendererModelDiagnosticsApi* (__cdecl*)(unsigned);
  HMODULE module_{}; get_api get_{};
  const wkeRendererModelDiagnosticsApi* table_{}; const wkeRendererDiagnosticsApi* v1_{};
  std::string module_path_;wkeRendererModelSnapshot preinit_{};
  soui::sdk::diagnostics::detail::renderer_model_binding binding_;
  std::uint64_t captured_qpc_{};
};

class real_cache_probe final {
 public:
  explicit real_cache_probe(const settings& options) {
    module_=LoadLibraryW(options.kernel.c_str()); require(module_!=nullptr,"cache-actual-module-load");
    std::array<wchar_t,32768> path{};
    const auto count=GetModuleFileNameW(module_,path.data(),static_cast<DWORD>(path.size()));
    require(count>0&&count<path.size()&&fs::equivalent(fs::path(path.data()),options.kernel),"cache-actual-module-route");
    get_=reinterpret_cast<get_api>(GetProcAddress(module_,"wkeGetCacheDiagnosticsApi"));
    require(get_!=nullptr,"cache-required-export"); table_=get_(1);
    require(soui::sdk::diagnostics::detail::cache_api_supported(table_),"cache-actual-table");
    invalid_outputs(); preinit_=read();
    require(preinit_.status==WKE_CACHE_NOT_STARTED&&preinit_.processId==0&&preinit_.processCreationFileTime==0&&
      preinit_.recordingEpoch==0&&preinit_.initializedQpc==0&&preinit_.sampleQpc==0&&
      preinit_.blinkStatus==0&&preinit_.glyphStatus==0&&preinit_.resourceStatus==0,"cache-preinit-not-started");
  }
  ~real_cache_probe() { if(module_)require(FreeLibrary(module_)!=FALSE,"cache-probe-extra-module-release"); }
  real_cache_probe(const real_cache_probe&)=delete;
  real_cache_probe& operator=(const real_cache_probe&)=delete;
  void invalid_outputs() const {
    for(const auto version:std::array<unsigned,3>{0,2,(std::numeric_limits<unsigned>::max)()})
      require(get_(version)==nullptr,"cache-wrong-version-rejected");
    require(table_->sampleExistingCaches(nullptr)==WKE_CACHE_INVALID_ARGUMENT,"cache-null-output");
    struct guarded final { std::array<unsigned char,16> before; wkeCacheSnapshot value; std::array<unsigned char,16> after; };
    for(const auto size:std::array<unsigned,3>{0,215,217}) {
      guarded output;std::memset(&output,0xa5,sizeof(output));output.value.structSize=size;
      std::array<unsigned char,sizeof(output)> original{};std::memcpy(original.data(),&output,sizeof(output));
      require(table_->sampleExistingCaches(&output.value)==WKE_CACHE_INVALID_ARGUMENT&&
        std::memcmp(original.data(),&output,sizeof(output))==0,"cache-invalid-size-no-write");
    }
  }
  wkeCacheSnapshot read() const {
    require(get_(1)==table_,"cache-table-stable");
    wkeCacheSnapshot output{};output.structSize=sizeof(output);
    const auto state=table_->sampleExistingCaches(&output);
    require(output.structSize==216&&output.abiVersion==1&&output.status==state,"cache-return-envelope-match");return output;
  }
  const wkeCacheSnapshot& preinit() const noexcept { return preinit_; }
 private:
  using get_api=const wkeCacheDiagnosticsApi* (__cdecl*)(unsigned);
  HMODULE module_{};get_api get_{};const wkeCacheDiagnosticsApi* table_{};wkeCacheSnapshot preinit_{};
};

int execute(const settings& options,HINSTANCE instance) {
  const auto case_began=GetTickCount64();
  auto app=std::make_unique<application>(); // Catch while all lifecycle owners remain alive.
  auto hold=std::make_unique<engine_hold>(); // Also survives the catch with an executing Engine borrow.
  try {
    FILETIME created{},exited{},kernel_time{},user_time{};
    require(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel_time,&user_time)!=FALSE,"actual-process-identity");
    const auto creation=(std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;
    const auto held_kernel=CreateFileW(options.kernel.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    require(held_kernel!=INVALID_HANDLE_VALUE && hash_file(held_kernel)==options.kernel_sha,"actual-held-kernel-sha");
    std::unique_ptr<real_renderer_model_probe> model;
    if(options.selected=="renderer-model-abi")model=std::make_unique<real_renderer_model_probe>(options);
    std::unique_ptr<real_cache_probe> cache;
    if(options.selected=="renderer-cache-abi")cache=std::make_unique<real_cache_probe>(options);
    const std::wstring class_name=L"SouiEngineCapacity_"+std::to_wstring(GetCurrentProcessId());
    WNDCLASSW type{}; type.lpfnWndProc=procedure; type.hInstance=instance; type.lpszClassName=class_name.c_str();
    require(RegisterClassW(&type)!=0,"actual-owner-class");
    const auto owner=CreateWindowExW(0,class_name.c_str(),L"Engine capacity regression",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,0,0,640,480,nullptr,nullptr,instance,app.get());
    require(owner!=nullptr && !IsWindowVisible(owner),"actual-hidden-owner");
    require(app->bridge.real.register_method("capacity.business","capacity.business",[context=app.get()](std::string_view params) {
      if (json::parse(params,nullptr,false)!=json{{"value",7}}) return std::string{R"({"error":"E_INVALID_PARAMS","message":"Expected fixed params."})"};
      ++context->business; return std::string{R"({"observed":true})"};
    }),"real-business-registration");
    require(app->bridge.real.register_method("capacity.confirm","capacity.confirm",[context=app.get()](std::string_view params) {
      if (json::parse(params,nullptr,false)!=json{{"observed",true}}) return std::string{R"({"error":"E_INVALID_PARAMS","message":"Expected fixed confirmation."})"};
      ++context->confirmed; return std::string{R"({"confirmed":true})"};
    }),"real-confirm-registration");
    require(app->bridge.real.set_session_capabilities({"capacity.business","capacity.confirm"}),"actual-native-page-capabilities");
    auto renderer=std::make_unique<soui::sdk::dynamic_miniblink_adapter>(soui::sdk::diagnostics::mode::scalar);
    app->renderer=renderer.get(); app->host=std::make_unique<soui::sdk::webview_host>(std::move(renderer));
    require(engine_admission::host_engine(*app->renderer)==nullptr,"unmounted-private-borrow-refused");
    soui::sdk::host_config config; config.architecture=architecture(); config.miniblink_binary=options.kernel;
    config.resource_root=options.resources; config.owner_window=owner; config.bridge_adapter=&app->bridge;
    require(app->host->start(config).ok && app->host->session()!=nullptr,"actual-public-mount");
    const auto began=GetTickCount64();
    while (app->confirmed.load()!=1) { require(GetTickCount64()-began<10000,"actual-page-confirmation-deadline"); pump(*app);
      require(MsgWaitForMultipleObjects(0,nullptr,FALSE,1,QS_ALLINPUT)!=WAIT_FAILED,"actual-page-wait"); }
    require(app->business.load()==1,"actual-authorized-baseline-business-once");
    auto* engine=engine_admission::host_engine(*app->renderer);
    require(engine!=nullptr && engine->drain_for(2s),"actual-mounted-Engine-borrow-and-quiet");
    bool foreign_refused=false;
    std::thread reader([&]{ foreign_refused=engine_admission::host_engine(*app->renderer)==nullptr; }); reader.join();
    require(foreign_refused,"private-borrow-owner-check");
    const auto receipt=app->renderer->execution_context_receipt();
    require(receipt!=0 && app->renderer->accepts_execution_context_receipt(receipt),"actual-completed-document-receipt");
    const auto module_before=GetModuleHandleW(options.kernel.filename().c_str());
    require(module_before!=nullptr,"actual-loaded-kernel-module");
    std::array<wchar_t,32768> module_path{};
    const auto path_size=GetModuleFileNameW(module_before,module_path.data(),static_cast<DWORD>(module_path.size()));
    require(path_size!=0 && path_size<module_path.size() && fs::equivalent(fs::path(module_path.data()),options.kernel),
      "actual-loaded-kernel-path");
    if(cache) {
      using namespace soui::sdk::diagnostics;
      std::atomic<DWORD> engine_tid{0};
      require(engine->post([&]{engine_tid.store(GetCurrentThreadId(),std::memory_order_release);}),"cache-real-owner-query");
      while(!engine_tid.load(std::memory_order_acquire)) {within_case(case_began);pump(*app);}
      require(engine_tid.load()!=GetCurrentThreadId(),"cache-owner-distinct-from-ui");
      cache->invalid_outputs(); wkeCacheSnapshot wrong_owner{};
      for(;;) {
        within_case(case_began);wrong_owner=cache->read();
        if(wrong_owner.status!=WKE_CACHE_BUSY)break;
        pump(*app);require(MsgWaitForMultipleObjects(0,nullptr,FALSE,1,QS_ALLINPUT)!=WAIT_FAILED,"cache-wrong-owner-busy-poll");
      }
      require(wrong_owner.status==WKE_CACHE_WRONG_OWNER,"cache-wrong-ui-owner-refused");
      require(soui::sdk::request_miniblink_cache_sample()==cache_status::pending,"cache-real-request-admitted");
      cache_observation completed;
      for(;;) {
        within_case(case_began);const auto state=soui::sdk::read_miniblink_cache_metrics(completed);
        if(state==cache_status::sampled&&completed.has_completed_sample&&!completed.inflight)break;
        require(state==cache_status::pending||state==cache_status::busy,"cache-request-completion-invalid");
        pump(*app);require(MsgWaitForMultipleObjects(0,nullptr,FALSE,1,QS_ALLINPUT)!=WAIT_FAILED,"cache-completion-owner-service");
      }
      LARGE_INTEGER captured{},frequency{};
      require(QueryPerformanceCounter(&captured)&&captured.QuadPart>0&&QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0,"cache-actual-clock");
      const auto& raw=completed.native;
      soui::sdk::diagnostics::detail::cache_identity_binding identity;
      require(completed.module_matches&&completed.identity_bound&&fs::equivalent(fs::path(completed.module_path),options.kernel)&&
        identity.observe(raw,GetCurrentProcessId(),creation)&&raw.engineOwnerThreadId==engine_tid.load()&&
        raw.qpcFrequency==static_cast<std::uint64_t>(frequency.QuadPart)&&raw.sampleQpc<=static_cast<std::uint64_t>(captured.QuadPart),
        "cache-completed-actual-identity-clock-components");
      cache_observation repeated;
      require(soui::sdk::read_miniblink_cache_metrics(repeated)==cache_status::sampled&&
        std::memcmp(&raw,&repeated.native,sizeof(raw))==0,"cache-pure-getter-does-not-resample");
      close_retained(*app);app->withdrawal_allowed=true;
      require(DestroyWindow(owner)!=FALSE&&!IsWindow(owner),"cache-real-owner-withdrawal");app->owner=nullptr;app.reset();
      require(engine->drain_for(2s),"cache-actual-quiet-after-host-close");
      hold->begin(*engine); // Existing finite hold blocks actual Engine, not a fake sample provider.
      const auto before=engine_admission::observe(*engine);
      require(soui::sdk::request_miniblink_cache_sample()==cache_status::pending,"cache-after-last-host-request-admitted");
      require(soui::sdk::request_miniblink_cache_sample()==cache_status::pending,"cache-single-flight-duplicate");
      const auto queued=engine_admission::observe(*engine);
      require(queued.queued[0]==before.queued[0]+1,"cache-exactly-one-ordinary-lane-task");
      cache_observation pending;
      require(soui::sdk::read_miniblink_cache_metrics(pending)==cache_status::pending&&pending.inflight&&
        std::memcmp(&raw,&pending.native,sizeof(raw))==0,"cache-pending-keeps-old-completion");
      const auto shutdown=soui::sdk::shutdown_miniblink_runtime();
      require(!shutdown.ok&&shutdown.code=="E_RUNTIME_SHUTDOWN_PENDING","cache-real-close-starts-while-task-queued");
      cache_observation closing;
      require(soui::sdk::read_miniblink_cache_metrics(closing)==cache_status::unavailable&&!closing.inflight&&
        closing.has_completed_sample&&std::memcmp(&raw,&closing.native,sizeof(raw))==0&&
        soui::sdk::request_miniblink_cache_sample()==cache_status::unavailable,"cache-close-cancels-late-publication");
      hold->release();shutdown_within_case(case_began);
      cache_observation terminal;
      require(soui::sdk::read_miniblink_cache_metrics(terminal)==cache_status::unavailable&&terminal.runtime_retired&&
        !terminal.inflight&&terminal.has_completed_sample&&std::memcmp(&raw,&terminal.native,sizeof(raw))==0,
        "cache-late-real-task-cannot-replace-retained-snapshot");
      cache->invalid_outputs();require(cache->read().status==WKE_CACHE_UNAVAILABLE,"cache-finalized-provider-does-not-sample");
      shutdown_within_case(case_began);
      cache_observation final_copy;
      require(soui::sdk::read_miniblink_cache_metrics(final_copy)==cache_status::unavailable&&
        std::memcmp(&raw,&final_copy.native,sizeof(raw))==0,"cache-idempotent-close-keeps-original-qpc");
      require(UnregisterClassW(class_name.c_str(),instance)!=FALSE&&CloseHandle(held_kernel)!=FALSE,"cache-class-kernel-handle-close");
      const json result{{"schema","soui.renderer-cache-real-abi/v1"},{"case",options.selected},{"pid",GetCurrentProcessId()},
        {"creationFileTime",std::to_string(creation)},{"architecture",architecture()},{"kernelSha256",options.kernel_sha},
        {"snapshotBytes",216},{"tableBytes",sizeof(wkeCacheDiagnosticsApi)},{"version",1},{"capabilities","7"},
        {"preinitStatus",cache->preinit().status},{"wrongVersionsRejected",3},{"invalidOutputNoWrite",true},
        {"wrongUiOwnerRejected",true},{"completed",soui::acceptance::metrics::serialize(completed)},
        {"pending",soui::acceptance::metrics::serialize(pending)},{"terminal",soui::acceptance::metrics::serialize(terminal)},
        {"captureQpc",std::to_string(captured.QuadPart)},{"singleFlightOneOrdinaryTask",true},
        {"lateQueuedTaskRejectedAfterRealClose",true},{"retainedSnapshotUnchanged",true},
        {"hostDestroyed",true},{"ownerWithdrawn",true},{"bridgeJoined",true},{"runtimeShutdown",true},
        {"resourceQualification",false},{"fullQualification",false}};
      require(output_new(options.output,result.dump(2)+"\n"),"cache-actual-receipt-create-new");return 0;
    }
    if(model) {
      std::atomic<DWORD> engine_tid{0};
      require(engine->post([&]{engine_tid.store(GetCurrentThreadId(),std::memory_order_release);}),"actual-model-owner-query-admitted");
      while(engine_tid.load(std::memory_order_acquire)==0) { within_case(case_began);pump(*app); }
      LARGE_INTEGER frequency{};require(QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0,"actual-qpc-frequency");
      ShowWindow(owner,SW_SHOWNOACTIVATE);require(IsWindowVisible(owner),"actual-model-owner-visible");
      require(RedrawWindow(owner,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW)!=FALSE,"actual-owned-normal-paint");
      wkeRendererModelSnapshot rendered{};
      for(;;) {
        within_case(case_began);
        if(model->observe(WKE_RENDER_DIAGNOSTICS_READY,engine_tid.load(std::memory_order_acquire),creation,
          static_cast<ULONGLONG>(frequency.QuadPart),rendered,false))break;
        pump(*app);require(MsgWaitForMultipleObjects(0,nullptr,FALSE,1,QS_ALLINPUT)!=WAIT_FAILED,"actual-model-render-wait");
      }
      model->invalid_outputs();
      close_retained(*app);app->withdrawal_allowed=true;
      require(DestroyWindow(owner)!=FALSE&&!IsWindow(owner),"model-owner-withdrawal-after-real-close");app->owner=nullptr;
      app.reset(); // Actual Bridge task-service join before runtime finalization.
      shutdown_within_case(case_began);
      wkeRendererModelSnapshot terminal{};
      for(;;) { within_case(case_began);
        if(model->observe(WKE_RENDER_DIAGNOSTICS_CLOSED,engine_tid.load(std::memory_order_acquire),creation,
          static_cast<ULONGLONG>(frequency.QuadPart),terminal,true))break;
        service_retired_owner();
      }
      model->invalid_outputs();shutdown_within_case(case_began);
      wkeRendererModelSnapshot repeated{};
      for(;;) { within_case(case_began);
        if(model->observe(WKE_RENDER_DIAGNOSTICS_CLOSED,engine_tid.load(std::memory_order_acquire),creation,
          static_cast<ULONGLONG>(frequency.QuadPart),repeated,true))break;
        service_retired_owner();
      }
      require(std::memcmp(&terminal,&repeated,sizeof(terminal))==0,
        "actual-idempotent-shutdown-getter-terminal-changed");
      require(UnregisterClassW(class_name.c_str(),instance)!=FALSE&&CloseHandle(held_kernel)!=FALSE,"model-class-kernel-handle-close");
      const json result{{"schema","soui.renderer-model-real-abi/v1"},{"case",options.selected},{"pid",GetCurrentProcessId()},
        {"creationFileTime",std::to_string(creation)},{"architecture",architecture()},{"kernelSha256",options.kernel_sha},
        {"tableBytes",sizeof(wkeRendererModelDiagnosticsApi)},{"snapshotBytes",sizeof(wkeRendererModelSnapshot)},
        {"version",2},{"capabilities",model->capabilities()},{"wrongVersionsRejectedPerPhase",4},{"wrongVersionPhases",3},
        {"invalidOutputShapesRejectedPerPhase",4},
        {"invalidOutputPhases",3},{"rejectedOutputBytesUnchanged",true},{"preinit",model->serialize(model->preinit())},
        {"completedRaster",model->serialize(rendered)},{"terminal",model->serialize(terminal,true)},
        {"repeatedTerminalEqual",true},{"captureQpc",std::to_string(model->captured_qpc())},
        {"hostDestroyed",true},{"ownerWithdrawn",true},{"bridgeJoined",true},{"runtimeShutdown",true},
        {"resourceQualification",false},{"fullQualification",false}};
      require(output_new(options.output,result.dump(2)+"\n"),"model-actual-receipt-create-new");return 0;
    }
    hold->begin(*engine);
    for (unsigned i=0;i!=512;++i) require(engine->post([]{}),"actual-ordinary-fill");
    const auto refused=app->renderer->try_enqueue_bridge_message(app->bridge.request(),false,receipt);
    require(!refused.ok && refused.code=="E_BUSY" && app->business.load()==1,"full-reply-before-real-business-zero-delta");
    soui::sdk::diagnostics::software_host_runtime observed;
    (void)app->renderer->read_runtime_metrics(observed);
    require(observed.requests.observation.state==soui::sdk::diagnostics::status::ready && observed.requests.ordinary.outstanding==0,
      "actual-refused-native-request-lease-retired");
    bool retained=false,gate_closed=false,old_receipt_rejected=false,same_engine=false;
    if (options.selected=="cleanup-full-retains-owner") {
      for (unsigned i=0;i!=32;++i) require(engine_admission::post(*engine,engine_lane::cleanup,[]{})==engine_admission_result::accepted,"actual-cleanup-fill");
      const auto closing=app->host->destroy();
      DWORD owner_pid{}; const auto owner_thread=GetWindowThreadProcessId(owner,&owner_pid);
      const auto module=GetModuleHandleW(options.kernel.filename().c_str());
      retained=!closing.ok && closing.code=="E_CURSOR_CLOSE_PENDING" && app->host!=nullptr && app->renderer!=nullptr &&
        IsWindow(owner) && owner_pid==GetCurrentProcessId() && owner_thread==GetCurrentThreadId() && module==module_before;
      require(retained,"real-Host-Bridge-HWND-module-retained-on-cleanup-full");
      soui::sdk::diagnostics::software_host_runtime closing_observed;
      const auto closing_metrics=app->renderer->read_runtime_metrics(closing_observed);
      gate_closed=closing_metrics==soui::sdk::diagnostics::status::ready && !closing_observed.bridge_accepting;
      require(gate_closed,"actual-cleanup-full-bridge-gate-closed");
      const bool receipt_refused=!app->renderer->accepts_execution_context_receipt(receipt);
      const auto old_request=app->renderer->try_enqueue_bridge_message(app->bridge.request(),false,receipt);
      old_receipt_rejected=receipt_refused && !old_request.ok && old_request.code=="E_CONTEXT_PENDING" && app->business.load()==1;
      require(old_receipt_rejected,"actual-old-completed-receipt-and-real-request-refused-after-close");
      same_engine=engine_admission::host_engine(*app->renderer)==engine;
      require(same_engine,"actual-original-Engine-webview-cursor-owner-retained");
    }
    hold->release();
    require(engine->drain_for(5s),"actual-capacity-work-drained");
    require(app->business.load()==1,"refused-business-never-executed-later");
    close_retained(*app);
    app->withdrawal_allowed=true;
    require(DestroyWindow(owner)!=FALSE && !IsWindow(owner),"real-owner-withdrawal-after-destroy"); app->owner=nullptr;
    app.reset(); // Real Bridge task_service destructor/join precedes Runtime shutdown.
    shutdown_within_case(case_began);
    require(UnregisterClassW(class_name.c_str(),instance)!=FALSE && CloseHandle(held_kernel)!=FALSE,"real-class-kernel-handle-close");
    const json result{{"schema","soui.engine-host-capacity/1"},{"case",options.selected},{"pid",GetCurrentProcessId()},
      {"creationFileTime",std::to_string(creation)},{"kernelSha256",options.kernel_sha},{"baselineBusinessCalls",1},{"refusedBusinessDelta",0},
      {"requestRejectedBusy",true},{"cleanupRetainedObserved",retained},{"cleanupGateClosedObserved",gate_closed},
      {"oldReceiptRejectedObserved",old_receipt_rejected},{"retainedSameEngineObserved",same_engine},
      {"hostDestroyed",true},{"ownerWithdrawn",true},
      {"bridgeJoined",true},{"runtimeShutdown",true},{"fullQualification",false}};
    require(output_new(options.output,result.dump(2)+"\n"),"actual-receipt-create-new"); return 0;
  } catch (...) { fatal("exception-with-retained-owners"); }
}
} // namespace
int wmain() { try { const auto options=arguments(); return execute(options,GetModuleHandleW(nullptr)); }
  catch (...) { fatal("argument-or-entry-exception"); } }
