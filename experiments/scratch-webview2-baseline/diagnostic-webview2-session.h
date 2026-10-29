#pragma once
// One local, measurement-only capture. The owner supplies its real Controller
// close and keeps this session alive until pending()==0 after cancellation.
#include "diagnostic-capture-policy.h"
#include "diagnostic-inputs.h"
#include <WebView2.h>
#include <wrl.h>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>

namespace soui::scratch_diagnostic {
class webview2_capture final : public std::enable_shared_from_this<webview2_capture> {
  using callback = std::function<void(bool, const json&)>;
  using receiver = Microsoft::WRL::ComPtr<ICoreWebView2DevToolsProtocolEventReceiver>;
  struct registration final { receiver event; EventRegistrationToken token{}; bool registered{}; };
 public:
  webview2_capture(ICoreWebView2* core, std::filesystem::path output, std::function<json()> guard, int width, int height)
      : core_(core), output_(std::move(output)), guard_(std::move(guard)),
        recipe_(json::parse(diagnostic_recipe_json)), scripts_(json::parse(diagnostic_scripts_json)), width_(width), height_(height) {
    validate_recipe(recipe_);
    registrations_.reserve(3); // No allocation may strand an already registered native token.
    if (!core_ || !std::filesystem::create_directory(output_)) throw std::runtime_error("Diagnostic directory must be new");
    result_ = {{"schema", "soui.scratch-diagnostic-capture/v1"}, {"diagnosticOnly", true},
      {"formalPerformancePassed", nullptr}, {"started", clock()}, {"recipe", recipe_},
      {"commands", json::array()}, {"failures", json::array()}, {"nativeGuards", json::array()},
      {"collectionCompleted", false}, {"cpuProfileQualified", false}, {"traceQualified", false}};
  }
  unsigned pending() const noexcept { return pending_; }
  unsigned active_callbacks() const noexcept { return active_; }
  bool events_retired() const noexcept { for(const auto& value:registrations_)if(value.registered)return false;return true; }
  bool active() const noexcept { return phase_ != phase::idle && phase_ != phase::done && phase_ != phase::timed_out && phase_ != phase::cancelled; }
  bool done() const noexcept { return phase_ == phase::done || phase_ == phase::timed_out; }
  bool timed_out() const noexcept { return timed_out_; }
  const json& result() const noexcept { return result_; }
  void start() {
    if (phase_ != phase::idle) throw std::runtime_error("Diagnostic capture may start only once");
    phase_ = phase::setup; begun_ = GetTickCount64();
    subscribe(L"Tracing.dataCollected"); subscribe(L"Tracing.bufferUsage"); subscribe(L"Tracing.tracingComplete");
    script(page_script::setup, [self=shared_from_this()](bool ok, const json&) {
      if (!ok) return self->abort("Scene setup failed"); self->phase_ = phase::waiting;
    });
  }
  // Low-frequency owner timer, never a page/renderer hot-path poll.
  void tick() {
    if (!active()) return;
    if (GetTickCount64() - begun_ > 120000) {
      failure("capture", "Diagnostic overall deadline exceeded; owner must close Controller then drain real callbacks");
      timed_out_=true;result_["timedOut"]=true;phase_=phase::timed_out;return;
    }
    if (pending_ || active_) return;
    if (phase_ == phase::waiting) {
      script(page_script::query, [self=shared_from_this()](bool ok, const json& value) {
        if (!ok) return self->abort("Scene query failed");
        if (value.value("eligible", false)) {
          if(value.value("width",0)!=self->width_ || value.value("height",0)!=self->height_ ||
              value.value("devicePixelRatio",0.0)!=1.0 || value.value("visibility",std::string{})!="visible")
            return self->abort("Diagnostic page viewport/DPR differs");
          self->result_["configuration"] = value;
          if (!self->guard("warmup-start")) return self->abort("Native warmup guard failed");
          self->phase_ = phase::setup;
          self->script(page_script::install, [self](bool installed, const json& warmup) {
            if (!installed) return self->abort("Probe installation failed"); self->result_["pageWarmup"] = warmup;
            self->prepare_commands();
          });
        }
      });
    } else if (phase_ == phase::warmup && GetTickCount64() >= deadline_) begin_capture();
    else if (phase_ == phase::capturing && GetTickCount64() >= deadline_) end_capture();
    else if (phase_ == phase::trace_wait && (trace_complete_ || GetTickCount64() >= deadline_)) {
      if (!trace_complete_) failure("trace-stop", "Tracing.tracingComplete was not observed");
      disable_and_finish();
    }
    else if (phase_ == phase::export_pending && !active_) {
      unsubscribe();if(phase_==phase::cancelled)return;
      phase_=phase::done;flush();
    }
  }
  void cancel() noexcept {
    if (phase_ == phase::cancelled) { retire_cancelled(); return; }
    const bool incomplete = phase_ != phase::done;
    cancel_export_needed_=incomplete;
    phase_ = phase::cancelled;
    try { if (incomplete) failure("cancelled", "Owner closed before diagnostic completion"); }
    catch (...) {}
    retire_cancelled();
  }
  // The window owner calls this only after actual Controller/browser retirement,
  // outside native callback/callout stacks. Pure evidence I/O is a later phase.
  void export_cancelled_after_retirement() {
    if(phase_!=phase::cancelled||pending_||active_||!events_retired())throw std::runtime_error("Diagnostic native borrows are not retired");
    if(cancel_export_needed_&&!cancel_exported_) {
      result_["ownerNativeRetirementVerified"]=true;flush();cancel_exported_=true;
    }
  }
 private:
  enum class phase { idle, setup, waiting, warmup, starting, capturing, stopping, trace_wait, finishing, export_pending, done, timed_out, cancelled };
  Microsoft::WRL::ComPtr<ICoreWebView2> core_;
  std::filesystem::path output_;
  std::function<json()> guard_;
  json recipe_, scripts_, result_, page_, cpu_;
  bounded_trace_buffer trace_;
  std::vector<registration> registrations_;
  phase phase_{phase::idle};
  unsigned pending_{},active_{};
  int width_{},height_{};
  ULONGLONG begun_{}, deadline_{};
  bool profiler_enabled_{}, profiler_started_{}, trace_started_{}, trace_complete_{}, timed_out_{},retiring_events_{};
  bool native_fault_{},event_failure_{};
  bool cancel_export_needed_{},cancel_exported_{};
  unsigned successful_subscriptions_{};
  struct receipt final { bool settled{}; };
  bool settle(const std::shared_ptr<receipt>& value) noexcept {
    if(value->settled)return false;value->settled=true;
    if(pending_)--pending_;else native_fault_=true;
    return true;
  }
  struct admission final {
    std::shared_ptr<webview2_capture> owner;std::shared_ptr<receipt> value;bool accepted{};
    ~admission(){if(!accepted)owner->settle(value);}
  };
  struct active_borrow final {
    std::shared_ptr<webview2_capture> owner;
    explicit active_borrow(std::shared_ptr<webview2_capture> value):owner(std::move(value)){++owner->active_;}
    ~active_borrow(){--owner->active_;owner->retire_cancelled();}
  };
  bool current(ICoreWebView2* original) const noexcept {return phase_!=phase::cancelled&&core_&&core_.Get()==original;}
  void retire_cancelled() noexcept {
    if(phase_!=phase::cancelled||active_||retiring_events_)return;
    retiring_events_=true;
    try{unsubscribe();}catch(...){native_fault_=event_failure_=true;}
    retiring_events_=false;
    if(!pending_&&!active_&&events_retired())core_.Reset();
  }
  static json clock() {
    FILETIME utc{}; GetSystemTimeAsFileTime(&utc); LARGE_INTEGER qpc{}, frequency{};
    QueryPerformanceCounter(&qpc); QueryPerformanceFrequency(&frequency);
    return {{"utcFileTime", std::to_string((static_cast<unsigned long long>(utc.dwHighDateTime)<<32)|utc.dwLowDateTime)},
      {"qpc", std::to_string(qpc.QuadPart)}, {"qpcFrequency", std::to_string(frequency.QuadPart)}};
  }
  static std::string utf8(LPCWSTR value) {
    if (!value) return "null";
    const auto length = wcsnlen_s(value, 16777217);
    if (length > 16777216) throw std::runtime_error("Diagnostic UTF16 response exceeds bound");
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (bytes < 1 || bytes > 16777216) throw std::runtime_error("Diagnostic UTF8 response exceeds bound");
    std::string result(static_cast<std::size_t>(bytes), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, static_cast<int>(length), result.data(), bytes, nullptr, nullptr)) throw std::runtime_error("Diagnostic UTF8 conversion failed");
    return result;
  }
  static std::wstring wide(const std::string& value) {
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size < 1) throw std::runtime_error("Diagnostic script conversion failed");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size)) throw std::runtime_error("Diagnostic script conversion failed");
    return result;
  }
  void failure(const char* operation, const std::string& error) noexcept {
    try{if (result_["failures"].size() < 64) result_["failures"].push_back({{"phase", operation}, {"error", error}, {"clock", clock()}});
      else result_["failureOverflow"] = true;}catch(...){native_fault_=true;}
  }
  void abort(const char* reason) {
    failure("capture", reason);
    if (phase_ == phase::finishing || phase_==phase::export_pending || phase_ == phase::done || phase_ == phase::cancelled) return;
    phase_ = phase::stopping; stop_profiles();
  }
  bool guard(const char* stage) {
    const auto value = guard_(); result_["nativeGuards"].push_back({{"phase", stage}, {"clock", clock()}, {"value", value}});
    return value.value("focused", false) && value.value("visible", false) && !value.value("minimized", true) && value.value("geometryQualified", false);
  }
  void script(page_script operation, callback next) {
    const auto original=core_;if(!current(original.Get()))return;
    const auto source = wide(fixed_page_script(operation, scripts_, diagnostic_probe_source));
    const auto self = shared_from_this();const auto completion=std::make_shared<receipt>();
    const auto handler=Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
      [self, original, completion, next](HRESULT status, LPCWSTR value) noexcept -> HRESULT {
        active_borrow borrowed(self);
        if(!self->settle(completion)){self->native_fault_=true;return S_OK;}
        if(!self->current(original.Get()))return S_OK;
        json parsed; bool ok=false;
        try { parsed = json::parse(utf8(value)); ok = SUCCEEDED(status) && !parsed.is_null();
          if (!ok) self->failure("script", "Fixed script failed/null HRESULT=" + std::to_string(status)); }
        catch (const std::exception& error) { self->failure("script", error.what()); }
        try { if(self->current(original.Get()))next(ok, parsed); }
        catch (const std::exception& error) { self->native_fault_=true;try{self->failure("script-next", error.what());self->phase_=phase::export_pending;}catch(...){} }
        return S_OK;
      });
    if(!handler)throw std::bad_alloc(); // All potentially allocating captures precede the pending charge.
    ++pending_;admission admitted{self,completion};active_borrow native_call(self);
    const auto submitted = original->ExecuteScript(source.c_str(),handler.Get());
    if(SUCCEEDED(submitted))admitted.accepted=true;
    else{const bool rejected=settle(completion);failure("script-admission",std::to_string(submitted));
      if(rejected&&current(original.Get()))next(false,json());else if(!rejected)native_fault_=true;}
  }
  void command(const wchar_t* method, callback next) {
    const auto original=core_;if(!current(original.Get()))return;
    const auto params = command_parameters(method, recipe_); if (!fixed_command(method, params, recipe_)) throw std::runtime_error("Diagnostic command denied");
    const auto index = result_["commands"].size();
    result_["commands"].push_back({{"method", utf8(method)}, {"params", params}, {"began", clock()}});
    const auto wire = wide(params.dump());const auto self = shared_from_this();const auto completion=std::make_shared<receipt>();
    const auto handler=Microsoft::WRL::Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
      [self, original, completion, next, index](HRESULT status, LPCWSTR value) noexcept -> HRESULT {
        active_borrow borrowed(self);
        if(!self->settle(completion)){self->native_fault_=true;return S_OK;}
        if(!self->current(original.Get()))return S_OK;
        json parsed; bool ok=false;
        try { const auto raw = utf8(value); parsed = json::parse(raw);
          auto& row = self->result_["commands"][index]; row["ended"] = clock(); row["hresult"] = status; row["rawResult"] = raw;
          ok = SUCCEEDED(status) && !parsed.is_null() && !parsed.contains("error"); if (!ok) self->failure("cdp", "Command failed HRESULT=" + std::to_string(status)); }
        catch (const std::exception& error) { self->failure("cdp-response", error.what()); }
        try { if(self->current(original.Get()))next(ok, parsed); }
        catch (const std::exception& error) { self->native_fault_=true;try{self->failure("cdp-next", error.what());self->phase_=phase::export_pending;}catch(...){} }
        return S_OK;
      });
    if(!handler)throw std::bad_alloc();
    ++pending_;admission admitted{self,completion};active_borrow native_call(self);
    const auto submitted = original->CallDevToolsProtocolMethod(method,wire.c_str(),handler.Get());
    if(SUCCEEDED(submitted))admitted.accepted=true;
    else{const bool rejected=settle(completion);failure("cdp-admission",std::to_string(submitted));
      if(rejected&&current(original.Get()))next(false,json());else if(!rejected)native_fault_=true;}
  }
  void subscribe(const wchar_t* method) {
    const auto original=core_;if(!current(original.Get()))return;
    active_borrow native_call(shared_from_this());
    registration value; if (FAILED(original->GetDevToolsProtocolEventReceiver(method, &value.event))) { event_failure_=true;failure("event-capability", utf8(method)); return; }
    if(!current(original.Get()))return;
    const auto weak = weak_from_this(); const std::string kind = utf8(method);
    const auto handler=Microsoft::WRL::Callback<ICoreWebView2DevToolsProtocolEventReceivedEventHandler>(
      [weak, kind](ICoreWebView2* sender, ICoreWebView2DevToolsProtocolEventReceivedEventArgs* args) noexcept -> HRESULT {
        if (const auto self=weak.lock()) {
          active_borrow borrowed(self);const auto pinned=self->core_;
          if (!self->current(pinned.Get())) return S_OK;
          LPWSTR raw{};
          try {
            Microsoft::WRL::ComPtr<IUnknown> expected,incoming;
            if(!sender||!args||FAILED(pinned.As(&expected))||FAILED(sender->QueryInterface(IID_PPV_ARGS(&incoming)))||expected.Get()!=incoming.Get())
              throw std::runtime_error("Trace event target identity differs");
            if(!self->current(pinned.Get()))return S_OK;
            const auto status=args->get_ParameterObjectAsJson(&raw);
            if (FAILED(status)) {self->event_failure_=true;self->failure("trace-event", "Event JSON unavailable");}
            else if (self->current(pinned.Get()) && self->trace_.append(kind, utf8(raw)) && kind == "Tracing.tracingComplete") self->trace_complete_ = true;
          }
          catch (const std::exception& error) {self->event_failure_=true;self->failure("trace-event", error.what());}
          catch (...) {self->event_failure_=self->native_fault_=true;}
          CoTaskMemFree(raw);
        } return S_OK;
      });
    if(!handler)throw std::bad_alloc();
    const auto registered=value.event->add_DevToolsProtocolEventReceived(handler.Get(),&value.token);
    if (FAILED(registered)) {event_failure_=true;failure("event-registration", kind);}
    else { value.registered = true; registrations_.push_back(std::move(value));++successful_subscriptions_; }
  }
  void unsubscribe() {
    active_borrow native_call(shared_from_this());
    for (auto& item : registrations_) if (item.registered) {
      // Mark in flight before a potentially reentrant native removal. Active
      // borrowing prevents owner Close or a second removal during this call.
      item.registered=false;
      const auto status = item.event->remove_DevToolsProtocolEventReceived(item.token);
      if (FAILED(status)) {item.registered=true;event_failure_=true;failure("event-removal", std::to_string(status));}
    }
  }
  void prepare_commands() {
    command(L"Tracing.getCategories", [self=shared_from_this()](bool ok, const json& value) {
      if (ok) self->result_["availableCategories"] = value.value("categories", json::array());
      self->command(L"Profiler.enable", [self](bool enabled, const json&) {
        self->profiler_enabled_ = enabled;
        self->command(L"Profiler.setSamplingInterval", [self](bool, const json&) {
          self->phase_ = phase::warmup; self->deadline_ = GetTickCount64() + 10000;
        });
      });
    });
  }
  void begin_capture() {
    phase_ = phase::starting; if (!guard("capture-start")) return abort("Native capture-start guard failed");
    command(L"Tracing.start", [self=shared_from_this()](bool ok, const json&) {
      self->trace_started_ = ok;
      self->command(L"Profiler.start", [self](bool profiling, const json&) {
        self->profiler_started_ = profiling;
        self->script(page_script::start, [self](bool started, const json& value) {
          if (!started) return self->abort("Probe start failed"); self->result_["pageCaptureStart"] = value;
          self->result_["captureStarted"] = clock(); self->phase_ = phase::capturing; self->deadline_ = GetTickCount64() + 10000;
        });
      });
    });
  }
  void end_capture() {
    phase_ = phase::stopping;
    script(page_script::finish, [self=shared_from_this()](bool ok, const json& value) {
      self->page_ = value; self->result_["captureEnded"] = clock();
      self->result_["collectionCompleted"] = ok && self->guard("capture-end");
      self->stop_profiles();
    });
  }
  void stop_profiles() {
    if (profiler_started_) {
      command(L"Profiler.stop", [self=shared_from_this()](bool stopped, const json& response) {
        if (stopped && response.contains("profile")) self->cpu_ = response["profile"];
        self->profiler_started_ = false; self->stop_trace();
      });
    } else stop_trace();
  }
  void stop_trace() {
    if (trace_started_) command(L"Tracing.end", [self=shared_from_this()](bool, const json&) {
      self->trace_started_ = false; self->phase_ = phase::trace_wait; self->deadline_ = GetTickCount64() + 15000;
    });
    else disable_and_finish();
  }
  void disable_and_finish() {
    phase_ = phase::finishing;
    command(L"Profiler.disable", [self=shared_from_this()](bool, const json&) {
      self->profiler_enabled_ = false;
      self->script(page_script::abort, [self](bool, const json&) {
        self->script(page_script::stop_workload, [self](bool, const json&) { if(self->phase_!=phase::cancelled)self->phase_ = phase::export_pending; });
      });
    });
  }
  void write(const wchar_t* name, const std::string& value) {
    std::ofstream file(output_/name, std::ios::binary | std::ios::trunc); file << value;
    if (!file) throw std::runtime_error("Diagnostic evidence export failed");
  }
  void flush() {
    json events=json::array(); bool malformed=false, lost=false, full=false;
    std::string raw;
    for (const auto& chunk : trace_.chunks()) {
      raw += json{{"method",chunk.method},{"raw",chunk.raw}}.dump()+'\n';
      try { const auto params=json::parse(chunk.raw);
        if (chunk.method=="Tracing.dataCollected") {if (!params.contains("value") || !params["value"].is_array()) malformed=true; else for (const auto& event:params["value"]) events.push_back(event);}
        else if (chunk.method=="Tracing.tracingComplete") lost=lost||params.value("dataLossOccurred",false);
        else full=full||params.value("percentFull",0.0)>=1.0||params.value("value",0.0)>=1.0;
      } catch (...) { malformed=true; }
    }
    result_["trace"]={{"bytes",trace_.bytes()},{"overflow",trace_.overflow()},{"allocationFailed",trace_.allocation_failed()},
      {"complete",trace_complete_},{"events",events.size()},{"malformed",malformed},{"dataLossOccurred",lost},{"bufferFull",full}};
    json missing=json::array();const auto available=result_.value("availableCategories",json::array());
    std::string selected=recipe_.at("traceCategories").get<std::string>();std::size_t begin=0;
    while(begin<=selected.size()) {const auto end=selected.find(',',begin);const auto name=selected.substr(begin,end==std::string::npos?end:end-begin);
      bool found=false;for(const auto& category:available)if(category==name)found=true;if(!found)missing.push_back(name);
      if(end==std::string::npos)break;begin=end+1;}
    result_["missingCategories"]=missing;
    result_["nativeFault"]=native_fault_;result_["traceEventFailure"]=event_failure_;
    result_["successfulTraceSubscriptions"]=successful_subscriptions_;result_["traceEventsRetired"]=events_retired();
    result_["traceQualified"]=trace_complete_&&!events.empty()&&!trace_.overflow()&&!trace_.allocation_failed()&&!malformed&&!lost&&!full&&missing.empty()&&
      successful_subscriptions_==3&&!event_failure_&&!native_fault_&&events_retired();
    result_["cpuProfileQualified"]=cpu_.is_object()&&cpu_.contains("nodes")&&cpu_["nodes"].is_array()&&!cpu_["nodes"].empty()&&
      cpu_.value("endTime",0.0)>cpu_.value("startTime",0.0)&&!native_fault_;
    result_["pageGuardsQualified"]=page_.is_object()&&page_.value("pageGuardsQualified",false);
    result_["ended"]=clock(); result_["nativeCdpCallbacksPending"]=pending_;
    result_["nativeCdpCallbacksActive"]=active_;
    result_["limitations"]={"Instrumented diagnostic only, not FPS/parity evidence.","Native snapshots do not prove continuous foreground or absence of occlusion.","Missing symbols/categories remain unknown; preserve passive WindowGuard and OS trace separately."};
    if (!cpu_.is_null()) write(L"cpu-profile.cpuprofile", cpu_.dump());
    if (!page_.is_null()) write(L"page-diagnostic.json", page_.dump());
    write(L"trace-events.ndjson", raw); write(L"trace.json", json{{"traceEvents",events}}.dump()); write(L"capture.json",result_.dump(2));
  }
};
} // namespace soui::scratch_diagnostic
