#include <soui/host/soui_miniblink_host.h>
#include <soui/json_bridge.h>
#include "engine_admission.h"
#include <windows.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <json.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <memory>
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
  require(out.selected=="reply-full-zero-business" || out.selected=="cleanup-full-retains-owner","fixed-case");
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
int execute(const settings& options,HINSTANCE instance) {
  auto app=std::make_unique<application>(); // Catch while all lifecycle owners remain alive.
  auto hold=std::make_unique<engine_hold>(); // Also survives the catch with an executing Engine borrow.
  try {
    FILETIME created{},exited{},kernel_time{},user_time{};
    require(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel_time,&user_time)!=FALSE,"actual-process-identity");
    const auto creation=(std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;
    const auto held_kernel=CreateFileW(options.kernel.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    require(held_kernel!=INVALID_HANDLE_VALUE && hash_file(held_kernel)==options.kernel_sha,"actual-held-kernel-sha");
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
    require(soui::sdk::shutdown_miniblink_runtime().ok,"actual-runtime-shutdown");
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
