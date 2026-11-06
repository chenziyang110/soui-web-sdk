#pragma once
// Dedicated external witness. No second trace implementation or global JS bind.
#include <soui/bridge_adapter.h>
#include <soui/host/soui_miniblink_host.h>
#include <soui/host/interaction_protocol.h>
#include <soui/diagnostics/input_frame_trace.h>
#include "engine_admission.h" // Explicit source header matching the installed SDK4.
#include <json.hpp>
#include <windows.h>
#include <array>
#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace soui::benchmark {
class sync_input_witness final : public sdk::bridge_adapter {
 public:
  static constexpr std::size_t row_capacity=256;
  // Each fixed row has <2KiB of bounded JSON; top-level/control metadata <16KiB.
  static constexpr std::size_t maximum_json_bytes=row_capacity*2048+16384;
 private:
  using json=nlohmann::json;
  using exec=void*;using view=void*;using value=std::int64_t;
  struct js_data;
  using get_property=value(__cdecl*)(exec,value,const char*);
  using set_property=bool(__cdecl*)(exec,value,const char*,value);
  using finalize_fn=void(__cdecl*)(js_data*);
  using call_fn=value(__cdecl*)(exec,value,value*,int);
  struct js_data {char type_name[100]{};get_property get{};set_property set{};finalize_fn finalize{};call_fn call{};};
  static_assert(offsetof(js_data,get)==(sizeof(void*)==8?104:100)&&sizeof(js_data)==(sizeof(void*)==8?136:116),"jsData ABI differs from frozen wkedefine.h");
  struct api {
    view(__cdecl* current_view)(){};exec(__cdecl* global_exec)(view){};
    int(__cdecl* view_id)(view){};bool(__cdecl* alive)(int){};HWND(__cdecl* hwnd)(view){};
    value(__cdecl* function)(exec,js_data*){};void(__cdecl* set_global)(exec,const char*,value){};
    js_data*(__cdecl* get_data)(exec,value){};value(__cdecl* integer)(int){};
    value(__cdecl* run_js)(view,const char*){};const char*(__cdecl* string)(exec,value){};
    bool(__cdecl* boolean)(exec,value){};
  };
  struct state;
  struct cookie {js_data data{};state* owner{};std::uint64_t magic{0x53594e4349443335ULL};};
  struct row {
    std::uint64_t ordinal{},begin{},end{},bound_receipt{},current_receipt{},es{};
    DWORD actual_thread{};unsigned call_kind{},fault_bits{},eligibility_reason{};int returned{};
    bool armed{},authorized{},cookie_match{},context_present{},context_stable{};
    sdk::diagnostics::input_frame_trace::event context{};
  };
  struct state {
    explicit state(std::string r,std::filesystem::path b):run(std::move(r)),binary(std::move(b)),ui(GetCurrentThreadId()){}
    std::string run;std::filesystem::path binary;DWORD ui{};HWND owner{};
    sdk::dynamic_miniblink_adapter* adapter{};std::atomic<sdk::engine_thread*> engine{nullptr};
    api functions;HMODULE module{};cookie binding{};
    view native_view{};int native_view_id{};DWORD engine_tid{};std::uint64_t receipt{},listener_receipt{},capture_qpc{},ready_qpc{};
    std::atomic<bool> attached{false},listener_ready{false},started{false},installed{false},armed{true},cold_seen{false};
    std::atomic<bool> ready_posted{false},page_ready{false},audit_posted{false},audit_done{false},audit_running{false};
    std::atomic<bool> failed{false},overflow{false},runtime_barrier{false};
    std::atomic<unsigned> setup_phase{0};std::atomic<bool> bootstrap_accepted{false},engine_owner_verified{false};
    std::atomic_flag ledger_busy=ATOMIC_FLAG_INIT;std::atomic<bool> concurrent_call{false};
    std::atomic<unsigned> error_code{0},finalize_count{0};std::atomic<bool> finalized_while_armed{false};
    std::array<row,row_capacity> rows{};std::size_t used{};
    std::atomic<std::uint64_t> attempted{0},stored{0},positive_calls{0};
    bool tls_module_self{},recorder_module_self{},audit_context_present{};
    sdk::diagnostics::input_frame_trace::event audit_context{};
    std::uint64_t audit_begin{},audit_end{},audit_page_call{};int audit_returned{-1};
  };
  inline static std::atomic<state*> active_{nullptr}; // One witness per isolated process.
  sdk::bridge_adapter& delegate_;std::unique_ptr<state> state_;
  static void need(bool v,const char* m){if(!v)throw std::runtime_error(m);}
  static std::uint64_t clock() noexcept {LARGE_INTEGER q{};return QueryPerformanceCounter(&q)&&q.QuadPart>0?static_cast<std::uint64_t>(q.QuadPart):0;}
  static void fail(state& s,unsigned code) noexcept {unsigned zero=0;(void)s.error_code.compare_exchange_strong(zero,code);s.failed.store(true,std::memory_order_release);}
  template<class F> static F symbol(HMODULE m,const char* n){const auto f=GetProcAddress(m,n);need(f!=nullptr,"Sync witness export missing");return reinterpret_cast<F>(f);}
  static void load_api(state& s){
    s.module=GetModuleHandleW(s.binary.filename().c_str());need(s.module!=nullptr,"Sync witness DLL not loaded");
    wchar_t path[32768]{};const auto count=GetModuleFileNameW(s.module,path,32768);
    need(count&&count<32768&&std::filesystem::equivalent(std::filesystem::path(path),s.binary),"Sync witness DLL path mismatch");
    auto& a=s.functions;
#define SOUI_SYNC_SYMBOL(field,name) a.field=symbol<decltype(a.field)>(s.module,name)
    SOUI_SYNC_SYMBOL(current_view,"wkeGetWebViewForCurrentContext");SOUI_SYNC_SYMBOL(global_exec,"wkeGlobalExec");
    SOUI_SYNC_SYMBOL(view_id,"wkeGetWebviewId");SOUI_SYNC_SYMBOL(alive,"wkeIsWebviewAlive");SOUI_SYNC_SYMBOL(hwnd,"wkeGetHostHWND");
    SOUI_SYNC_SYMBOL(function,"jsFunction");SOUI_SYNC_SYMBOL(set_global,"jsSetGlobal");SOUI_SYNC_SYMBOL(get_data,"jsGetData");
    SOUI_SYNC_SYMBOL(integer,"jsInt");SOUI_SYNC_SYMBOL(run_js,"wkeRunJS");SOUI_SYNC_SYMBOL(string,"jsToTempString");SOUI_SYNC_SYMBOL(boolean,"jsToBoolean");
#undef SOUI_SYNC_SYMBOL
  }
  static void owner(state& s){
    const auto engine=s.engine.load(std::memory_order_acquire);
    need(s.armed.load(std::memory_order_acquire)&&engine&&engine->is_owner_thread()&&GetCurrentThreadId()==s.engine_tid,"Sync witness engine owner changed");
    need(s.adapter&&s.adapter->accepts_execution_context_receipt(s.receipt),"Sync witness receipt revoked");
  }
  static exec checked(state& s){
    owner(s);need(s.native_view&&s.native_view_id&&s.functions.alive(s.native_view_id)&&s.functions.view_id(s.native_view)==s.native_view_id,"Sync witness view retired");
    const auto es=s.functions.global_exec(s.native_view);need(es&&s.adapter->owns_execution_context(es,s.receipt),"Sync witness actual context not owned");owner(s);return es;
  }
  static void __cdecl finalize(js_data* data) noexcept {
    auto* s=active_.load(std::memory_order_acquire);if(!s||data!=&s->binding.data)return;
    if(s->finalize_count.fetch_add(1,std::memory_order_relaxed)!=0)fail(*s,7);
    if(s->armed.load(std::memory_order_acquire)){s->finalized_while_armed.store(true);fail(*s,7);}
    // GC/uninitialization only notes retirement. App owns storage until drain.
  }
  static value __cdecl read(exec es,value object,value*,int count) noexcept {
    auto* s=active_.load(std::memory_order_acquire);if(!s)return 0; // Unreachable after correct Runtime barrier.
    const auto ordinal=s->attempted.fetch_add(1,std::memory_order_relaxed)+1;
    if(s->ledger_busy.test_and_set(std::memory_order_acquire)){s->concurrent_call.store(true);fail(*s,10);return s->functions.integer(0);}
    struct unlock {state* s;~unlock(){s->ledger_busy.clear(std::memory_order_release);}} lock{s};
    row r;r.ordinal=ordinal;r.begin=clock();r.actual_thread=GetCurrentThreadId();r.es=reinterpret_cast<std::uintptr_t>(es);
    const bool installed=s->installed.load(std::memory_order_acquire);
    r.bound_receipt=installed?s->receipt:0;r.armed=s->armed.load(std::memory_order_acquire);r.call_kind=s->audit_running.load()?2U:(r.ordinal==1?0U:1U);
    namespace trace=sdk::diagnostics::input_frame_trace;
    r.context_present=trace::current_context(r.context);
    if(!r.begin)r.fault_bits|=1;
    if(!r.armed||!installed)r.fault_bits|=2;
    if(count!=0)r.fault_bits|=4;
    try {
      auto* engine=s->engine.load(std::memory_order_acquire);
      if(!r.fault_bits&&engine&&r.actual_thread==s->engine_tid&&engine->is_owner_thread()){
        // Extract the vendor wrapper's cookie before another opaque getter can
        // invalidate JS temporaries. Compare the address; never dereference it.
        r.cookie_match=s->functions.get_data(es,object)==&s->binding.data;
        r.current_receipt=s->adapter->execution_context_receipt();
        r.authorized=r.cookie_match&&s->binding.owner==s&&s->binding.magic==0x53594e4349443335ULL&&r.current_receipt==r.bound_receipt&&
          s->adapter->accepts_execution_context_receipt(r.bound_receipt)&&s->adapter->owns_execution_context(es,r.bound_receipt)&&s->adapter->accepts_execution_context_receipt(r.bound_receipt);
        if(!r.authorized)r.fault_bits|=8;
      }else if(!r.fault_bits)r.fault_bits|=16;
      trace::event after{};const bool present=trace::current_context(after);
      r.context_stable=present==r.context_present&&(!present||std::memcmp(&r.context,&after,sizeof(after))==0);
      if(!r.context_stable)r.fault_bits|=32;
      if(r.authorized&&r.context_stable&&!r.fault_bits){
        if(!r.context_present)r.eligibility_reason=1;
        else if(r.context.point!=trace::stage::engine_begin)r.eligibility_reason=2;
        else if(r.context.value!=static_cast<std::uint64_t>(sdk::input_kind::pointer_move))r.eligibility_reason=3;
        else if(!r.context.input_first||r.context.input_first!=r.context.input_last||r.context.input_first>INT_MAX)r.eligibility_reason=4;
        else if(!r.context.view_id||!r.context.view_generation||!r.context.document_generation||!r.context.geometry_generation)r.eligibility_reason=5;
        else r.returned=static_cast<int>(r.context.input_first);
        if(!s->adapter->accepts_execution_context_receipt(r.bound_receipt)){r.returned=0;r.authorized=false;r.fault_bits|=8;}
      }else r.eligibility_reason=6;
    }catch(...){r.returned=0;r.authorized=false;r.fault_bits|=64;}
    if(s->used>=s->rows.size()){s->overflow.store(true);fail(*s,8);return s->functions.integer(0);}
    if(r.fault_bits)fail(*s,9);
    if(r.returned>0)s->positive_calls.fetch_add(1,std::memory_order_release);
    if(r.ordinal==1&&r.returned==0&&r.authorized&&!r.fault_bits)s->cold_seen.store(true,std::memory_order_release);
    const auto result=s->functions.integer(r.returned); // Last opaque JS API.
    r.end=clock();if(!r.end||r.end<r.begin){r.fault_bits|=1;fail(*s,9);}
    s->rows[s->used++]=r;s->stored.store(s->used,std::memory_order_release);return result;
  }
  static void capture(state& s){
    // Sole authenticated bootstrap runs in the real SDK native JS callback.
    // Its actual thread is captured now, then independently verified against
    // the UI-borrowed Engine before any function is installed or input sent.
    s.setup_phase.store(2);need(s.attached.load(std::memory_order_acquire)&&s.armed.load()&&!s.listener_ready.load(),"Capture is duplicate or detached");
    load_api(s);s.setup_phase.store(3);
    const auto receipt=s.adapter->execution_context_receipt();need(receipt&&s.adapter->accepts_execution_context_receipt(receipt),"Capture receipt not accepted");
    s.setup_phase.store(4);const auto v=s.functions.current_view();need(v!=nullptr,"No current authenticated broker view");
    s.setup_phase.store(5);const auto es=s.functions.global_exec(v);need(es&&s.adapter->owns_execution_context(es,receipt),"Actual bootstrap exec is not owned");
    s.setup_phase.store(6);const auto id=s.functions.view_id(v);need(id&&s.functions.alive(id),"Captured view is not alive");
    s.setup_phase.store(7);const auto hwnd=s.functions.hwnd(v);DWORD pid{};
    need(IsWindow(hwnd)&&GetWindowThreadProcessId(hwnd,&pid)==s.ui&&pid==GetCurrentProcessId()&&(hwnd==s.owner||IsChild(s.owner,hwnd)),"Captured HWND owner differs");
    s.setup_phase.store(8);need(s.adapter->accepts_execution_context_receipt(receipt),"Capture receipt revoked");
    s.native_view=v;s.native_view_id=id;s.receipt=receipt;s.listener_receipt=receipt;s.engine_tid=GetCurrentThreadId();s.capture_qpc=clock();need(s.capture_qpc,"Capture QPC unavailable");
    s.setup_phase.store(9);s.listener_ready.store(true,std::memory_order_release); // Publish all captured POD last; never retain es.
  }
  static void install(state& s){
    s.setup_phase.store(11);owner(s);need(!s.installed.load(),"Installation is not unique");
    s.setup_phase.store(12);const auto es=checked(s); // Fresh globalExec, owner/TID/alive/receipt/actual owns.
    s.engine_owner_verified.store(true,std::memory_order_release);
    s.setup_phase.store(13);const auto fn=s.functions.function(es,&s.binding.data);s.functions.set_global(es,"__souiSyncInputId",fn); // No intervening opaque getter.
    s.setup_phase.store(14);owner(s);need(s.adapter->owns_execution_context(es,s.receipt)&&s.adapter->accepts_execution_context_receipt(s.receipt),"Installation context revoked");
    s.installed.store(true,std::memory_order_release);
  }
  static json event_json(const sdk::diagnostics::input_frame_trace::event& e){
    return {{"struct_size",e.struct_size},{"version",e.version},{"point",static_cast<unsigned>(e.point)},{"why",static_cast<unsigned>(e.why)},{"outcome",static_cast<unsigned>(e.outcome)},{"flags",e.flags},{"thread_id",e.thread_id},{"producer_slot",e.producer_slot},
      {"qpc",std::to_string(e.qpc)},{"view_id",std::to_string(e.view_id)},{"view_generation",std::to_string(e.view_generation)},{"document_generation",std::to_string(e.document_generation)},{"geometry_generation",std::to_string(e.geometry_generation)},
      {"input_first",std::to_string(e.input_first)},{"input_last",std::to_string(e.input_last)},{"task_id",std::to_string(e.task_id)},{"frame_id",std::to_string(e.frame_id)},{"action_first",std::to_string(e.action_first)},{"action_last",std::to_string(e.action_last)},{"value",std::to_string(e.value)},{"related_id",std::to_string(e.related_id)}};
  }
 public:
  sync_input_witness(sdk::bridge_adapter& delegate,std::string run,std::filesystem::path binary):delegate_(delegate),state_(std::make_unique<state>(std::move(run),std::move(binary))){
    auto& s=*state_;need(s.run.size()==32&&s.run.find_first_not_of("0123456789abcdef")==std::string::npos&&s.binary.is_absolute(),"Sync witness configuration invalid");
    s.binding.owner=&s;std::memcpy(s.binding.data.type_name,"soui.sync-input-context.v1",27);s.binding.data.finalize=finalize;s.binding.data.call=read;
    HMODULE tls{},recorder{};const auto flags=GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    s.tls_module_self=GetModuleHandleExW(flags,reinterpret_cast<LPCWSTR>(&sdk::diagnostics::input_frame_trace::current_context),&tls)&&tls==GetModuleHandleW(nullptr);
    s.recorder_module_self=GetModuleHandleExW(flags,reinterpret_cast<LPCWSTR>(&sdk::diagnostics::input_frame_trace::process_recorder),&recorder)&&recorder==GetModuleHandleW(nullptr);
    need(s.tls_module_self&&s.recorder_module_self,"Trace symbols are not from the same EXE");
    state* zero=nullptr;need(active_.compare_exchange_strong(zero,&s),"Only one synchronous witness per process");
  }
  ~sync_input_witness() override {
    auto* s=state_.get();if(!s)return;disarm();
    if(!s->runtime_barrier.load(std::memory_order_acquire)){(void)state_.release();return;} // Bounded fail-path quarantine; never early-free jsData.
    auto* expected=s;(void)active_.compare_exchange_strong(expected,nullptr);
  }
  void attach_adapter(sdk::dynamic_miniblink_adapter& adapter,HWND owner_window){auto& s=*state_;need(GetCurrentThreadId()==s.ui&&!s.attached.load(),"Sync witness attach owner");DWORD pid{};need(IsWindow(owner_window)&&GetWindowThreadProcessId(owner_window,&pid)==s.ui&&pid==GetCurrentProcessId(),"Sync witness attach HWND");s.adapter=&adapter;s.owner=owner_window;s.attached.store(true,std::memory_order_release);}
  void start() noexcept {
    auto& s=*state_;if(s.started.load()||s.failed.load()||!s.listener_ready.load(std::memory_order_acquire))return;
    try {s.setup_phase.store(10);need(GetCurrentThreadId()==s.ui&&s.attached.load()&&s.armed.load()&&s.adapter->document_ready(),"Start owner/readiness differs");
      auto* engine=sdk::detail::engine_admission::host_engine(*s.adapter);need(engine,"Actual SDK Engine borrow unavailable");s.engine.store(engine,std::memory_order_release);s.started.store(true);
      need(s.adapter->accepts_execution_context_receipt(s.receipt),"Captured receipt revoked before install task");
      const auto event=json{{"type","soui.sync-input-witness.installed"},{"run_id",s.run},{"request_id","sync-input-witness-ready:"+s.run}}.dump();auto* raw=&s;
      need(engine->post([raw,event]{if(!raw->armed.load())return;try{install(*raw);raw->setup_phase.store(15);owner(*raw);raw->adapter->dispatch_event_now(event);raw->setup_phase.store(16);}catch(...){fail(*raw,100+raw->setup_phase.load());}}),"Install task admission failed");
    }catch(...){fail(s,100+s.setup_phase.load());}
  }
  void poll() noexcept {
    auto& s=*state_;if(!s.armed.load()||s.failed.load()||!s.installed.load(std::memory_order_acquire))return;
    if(GetCurrentThreadId()!=s.ui){fail(s,3);return;}auto* engine=s.engine.load();auto* raw=&s;
    if(s.cold_seen.load(std::memory_order_acquire)&&!s.ready_posted.exchange(true)){
      if(!engine->post([raw]{if(!raw->armed.load())return;try{const auto es=checked(*raw);const auto value=raw->functions.run_js(raw->native_view,"return typeof window.__souiSyncInputId==='function' && window.__souiSyncInputWitnessReady===true;");const bool ready=raw->functions.boolean(es,value);(void)checked(*raw);need(ready,"Page initial marker is not ready after cold call");raw->ready_qpc=clock();need(raw->ready_qpc,"Page ready clock unavailable");raw->page_ready.store(true,std::memory_order_release);}catch(...){fail(*raw,4);}}))fail(s,4);
    }
    if(s.page_ready.load(std::memory_order_acquire)&&s.positive_calls.load(std::memory_order_acquire)&&!s.audit_posted.exchange(true)){
      if(!engine->post([raw]{if(!raw->armed.load())return;try{namespace trace=sdk::diagnostics::input_frame_trace;raw->audit_begin=clock();raw->audit_context_present=trace::current_context(raw->audit_context);need(raw->audit_context_present&&raw->audit_context.point==trace::stage::task_begin,"Deferred negative control lacks a real Engine task");const auto es=checked(*raw);raw->audit_running.store(true);const auto result=raw->functions.run_js(raw->native_view,"return window.__souiSyncDeferredAudit();");const char* bytes=raw->functions.string(es,result);need(bytes,"Deferred control returned no JSON");std::size_t count=0;while(count<=1024&&bytes[count])++count;need(count&&count<=1024,"Deferred control JSON bound");const std::string copy(bytes,count);raw->audit_running.store(false);(void)checked(*raw);const auto j=json::parse(copy);need(j.is_object()&&j.value("done",false)&&j.contains("getter_call")&&j["getter_call"].is_number_unsigned()&&j.contains("returned_id")&&j["returned_id"].is_number_integer(),"Deferred control schema invalid");raw->audit_page_call=j["getter_call"].get<std::uint64_t>();raw->audit_returned=j["returned_id"].get<int>();need(raw->audit_returned==0,"Deferred task inherited stale input ID");raw->audit_end=clock();need(raw->audit_end>=raw->audit_begin,"Deferred task clock invalid");raw->audit_done.store(true,std::memory_order_release);}catch(...){raw->audit_running.store(false);fail(*raw,5);}}))fail(s,5);
    }
  }
  void disarm() noexcept {if(state_)state_->armed.store(false,std::memory_order_release);}
  bool page_ready()const noexcept{return state_->page_ready.load(std::memory_order_acquire);}
  bool failed()const noexcept{return state_->failed.load(std::memory_order_acquire);}
  bool audit_done()const noexcept{return state_->audit_done.load(std::memory_order_acquire);}
  void after_runtime_shutdown(bool quiescent){auto& s=*state_;need(GetCurrentThreadId()==s.ui&&quiescent,"Sync witness needs actual Runtime/Engine+trace quiescence");disarm();s.engine.store(nullptr,std::memory_order_release);s.runtime_barrier.store(true,std::memory_order_release);}
  json result() const {
    const auto& s=*state_;need(s.runtime_barrier.load(std::memory_order_acquire),"Do not inspect witness payload before Runtime barrier");
    const auto session=sdk::diagnostics::input_frame_trace::process_recorder().snapshot().session; // One cold identity read.
    need(session.version==1&&session.process_id==GetCurrentProcessId()&&session.process_incarnation&&session.qpc_frequency&&session.run_id_high==std::stoull(s.run.substr(0,16),nullptr,16)&&session.run_id_low==std::stoull(s.run.substr(16),nullptr,16),"Recorder session identity does not match witness run");
    auto rows=json::array();const auto count=s.stored.load(std::memory_order_acquire);need(count<=s.rows.size(),"Witness row count invalid");
    for(std::size_t i=0;i<count;++i){const auto& r=s.rows[i];rows.push_back({{"ordinal",r.ordinal},{"call_kind",r.call_kind},{"begin_qpc",std::to_string(r.begin)},{"end_qpc",std::to_string(r.end)},{"actual_thread_id",r.actual_thread},{"actual_es",std::to_string(r.es)},{"bound_receipt",std::to_string(r.bound_receipt)},{"current_receipt",std::to_string(r.current_receipt)},{"authorized",r.authorized},{"cookie_match",r.cookie_match},{"context_present",r.context_present},{"context_stable",r.context_stable},{"context",r.context_present?event_json(r.context):json(nullptr)},{"returned_id",r.returned},{"fault_bits",r.fault_bits},{"eligibility_reason",r.eligibility_reason}});}
    const bool complete=s.bootstrap_accepted.load()&&s.engine_owner_verified.load()&&s.installed.load()&&s.page_ready.load()&&s.audit_done.load()&&!s.failed.load()&&!s.overflow.load()&&s.attempted.load()==count&&s.runtime_barrier.load();
    return{{"schema","soui.sync-input-context-witness/v1"},{"run_id",s.run},{"identity",{{"run_id",s.run},{"process_id",session.process_id},{"process_incarnation",std::to_string(session.process_incarnation)},{"qpc_frequency",std::to_string(session.qpc_frequency)},{"run_id_high",std::to_string(session.run_id_high)},{"run_id_low",std::to_string(session.run_id_low)}}},{"installed",s.installed.load()},{"listener_ready",s.listener_ready.load()},{"page_ready",s.page_ready.load()},{"cold_seen",s.cold_seen.load()},{"deferred_done",s.audit_done.load()},{"failed",s.failed.load()},{"error_code",s.error_code.load()},{"setup_phase",s.setup_phase.load()},{"bootstrap_response_accepted",s.bootstrap_accepted.load()},{"engine_owner_verified",s.engine_owner_verified.load()},{"capacity",s.rows.size()},{"json_byte_budget",maximum_json_bytes},{"attempted",s.attempted.load()},{"stored",count},{"overflow",s.overflow.load()},{"concurrent_call",s.concurrent_call.load()},{"rows",rows},{"binding",{{"receipt",std::to_string(s.receipt)},{"listener_receipt",std::to_string(s.listener_receipt)},{"wke_view_id",s.native_view_id},{"engine_thread_id",s.engine_owner_verified.load()?s.engine_tid:0},{"capture_thread_id",s.engine_tid},{"capture_qpc",std::to_string(s.capture_qpc)},{"page_ready_qpc",std::to_string(s.ready_qpc)},{"per_context_jsFunction",true},{"process_global_binding_used",false}}},{"deferred_control",{{"begin_qpc",std::to_string(s.audit_begin)},{"end_qpc",std::to_string(s.audit_end)},{"page_getter_call",s.audit_page_call},{"returned_id",s.audit_returned},{"context",s.audit_context_present?event_json(s.audit_context):json(nullptr)}}},{"tls_module_is_exe",s.tls_module_self},{"recorder_module_is_exe",s.recorder_module_self},{"finalize_count",s.finalize_count.load()},{"finalized_while_armed",s.finalized_while_armed.load()},{"normal_runtime_barrier",s.runtime_barrier.load()},{"collector_complete",complete},{"physicalT8",nullptr},{"performanceGate",false},{"releaseReady",false}};
  }
  std::string handle_json(std::string_view request,bool gesture=false) override {
    auto response=delegate_.handle_json(request,gesture);auto& s=*state_;if(!s.armed.load()||request.size()>4096)return response;
    try{const auto r=json::parse(request,nullptr,false);if(!r.is_object()||r.value("method",std::string{})!="bridge.bootstrap"||r.value("requestId",std::string{})!="sync-input-witness-ready:"+s.run)return response;
      s.setup_phase.store(1);need(response.size()<=8192,"Bootstrap response bound");const auto granted=json::parse(response,nullptr,false);
      need(granted.is_object()&&granted.value("ok",false)&&granted.value("requestId",std::string{})=="sync-input-witness-ready:"+s.run,"Sole bootstrap rejected");
      s.bootstrap_accepted.store(true,std::memory_order_release);capture(s);
    }catch(...){fail(s,100+s.setup_phase.load());}return response; // Original grant and response remain untouched.
  }
  void shutdown_session(const std::string& s) override{delegate_.shutdown_session(s);}
  void set_owner_window(void* w) noexcept override{delegate_.set_owner_window(w);}
  void set_event_sink(event_sink f) override{delegate_.set_event_sink(std::move(f));}
  void record_paint() noexcept override{delegate_.record_paint();}
  void set_present_count(std::uint64_t n) noexcept override{delegate_.set_present_count(n);}
  void set_draw_interval(unsigned n) noexcept override{delegate_.set_draw_interval(n);}
  void set_gpu_active(bool b) noexcept override{delegate_.set_gpu_active(b);}
  unsigned active_frame_rate()const noexcept override{return delegate_.active_frame_rate();}
  bool set_backend_frame_rate_limit(unsigned n)noexcept override{return delegate_.set_backend_frame_rate_limit(n);}
  unsigned requested_frame_rate()const noexcept override{return delegate_.requested_frame_rate();}
  std::string register_root(const soui::runtime::scan_request_context& c,const std::filesystem::path& p)override{return delegate_.register_root(c,p);}
};
} // namespace soui::benchmark
