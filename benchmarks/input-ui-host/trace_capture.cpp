#include "trace_capture.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <ostream>
#include <sstream>
#include <iomanip>
#include <locale>
#include <vector>
#include <stdexcept>
#include <streambuf>
#include <thread>

namespace soui::benchmark {
namespace trace = sdk::diagnostics::input_frame_trace;
namespace {
class new_file_buffer final : public std::streambuf {
 public:
  explicit new_file_buffer(const std::filesystem::path& path) {
    file_=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file_==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create a new trace output");
    setp(bytes_.data(),bytes_.data()+bytes_.size());
  }
  ~new_file_buffer() override { (void)sync();CloseHandle(file_); }
  bool persist() { return sync()==0 && FlushFileBuffers(file_)!=FALSE; }
 protected:
  int sync() override {
    const auto count=static_cast<DWORD>(pptr()-pbase());
    if(!count)return 0;
    DWORD written{};
    if(!WriteFile(file_,pbase(),count,&written,nullptr)||written!=count)return -1;
    setp(bytes_.data(),bytes_.data()+bytes_.size());return 0;
  }
  int_type overflow(int_type value) override {
    if(sync()!=0)return traits_type::eof();
    if(!traits_type::eq_int_type(value,traits_type::eof())){*pptr()=traits_type::to_char_type(value);pbump(1);}
    return traits_type::not_eof(value);
  }
 private:
  HANDLE file_{INVALID_HANDLE_VALUE};
  std::array<char,65536> bytes_{};
};
const char* boolean(bool value)noexcept{return value?"true":"false";}
bool same_identity(const trace::identity& a,const trace::identity& b)noexcept {
  return a.version==b.version&&a.process_id==b.process_id&&a.run_id_high==b.run_id_high&&
    a.run_id_low==b.run_id_low&&a.process_incarnation==b.process_incarnation&&a.qpc_frequency==b.qpc_frequency;
}
void identity_json(std::ostream& out,const trace::identity& value) {
  std::ostringstream run;run.imbue(std::locale::classic());run<<std::hex<<std::setfill('0')<<std::setw(16)<<value.run_id_high<<std::setw(16)<<value.run_id_low;
  out<<"{\"version\":"<<value.version<<",\"run_id\":\""<<run.str()<<"\",\"process_id\":"<<value.process_id
    <<",\"process_incarnation\":\""<<value.process_incarnation<<"\",\"qpc_frequency\":\""<<value.qpc_frequency<<"\"}";
}
template<class Values> void wide_array(std::ostream& out,const Values& values) {
  out<<'[';bool first=true;for(const auto value:values){if(!first)out<<',';first=false;out<<'"'<<value<<'"';}out<<']';
}
void snapshot_json(std::ostream& out,const trace::scalar_snapshot& value) {
  out<<"{\"session\":";identity_json(out,value.session);
  out<<",\"mode\":\""<<(value.recording_mode==trace::mode::scalar?"scalar":value.recording_mode==trace::mode::detailed?"detailed":"invalid")
    <<"\",\"registered_threads\":"<<value.registered_threads<<",\"active_producers\":"<<value.active_producers
    <<",\"pending_registrations\":"<<value.pending_registrations;
#define WIDE_FIELD(field) out<<",\"" #field "\":\""<<value.field<<'"'
  WIDE_FIELD(attempted);WIDE_FIELD(recorded);WIDE_FIELD(dropped);WIDE_FIELD(queued);
  WIDE_FIELD(unregistered_writes);WIDE_FIELD(registration_rejections);WIDE_FIELD(causal_lost);
#undef WIDE_FIELD
#define BOOL_FIELD(field) out<<",\"" #field "\":"<<boolean(value.field)
  BOOL_FIELD(stopped);BOOL_FIELD(quiescent);BOOL_FIELD(available);BOOL_FIELD(counter_saturated);
  BOOL_FIELD(owner_violation);BOOL_FIELD(invalid_event);BOOL_FIELD(clock_invalid);
#undef BOOL_FIELD
  out<<",\"stages\":";wide_array(out,value.stages);out<<",\"counters\":";wide_array(out,value.counters);
  out<<",\"durations\":[";for(std::size_t n=0;n<value.durations.size();++n){if(n)out<<',';wide_array(out,value.durations[n]);}out<<"]}";
}
void write_scalar_json(const std::filesystem::path& file,const std::string& body) {
  if(body.size()>65536)throw std::runtime_error("Scalar metadata exceeds bound");
  new_file_buffer buffer(file);std::ostream out(&buffer);out.write(body.data(),static_cast<std::streamsize>(body.size()));
  if(!out.good()||!buffer.persist())throw std::runtime_error("Scalar metadata persistence failed");
}
std::vector<std::string> source_reasons(const trace::scalar_snapshot& value,const trace::identity& expected,bool final) {
  std::vector<std::string> reasons;
  if(!value.available)reasons.push_back("source_unavailable");
  if(!same_identity(value.session,expected))reasons.push_back("identity_mismatch");
  if(value.recording_mode!=trace::mode::scalar)reasons.push_back("mode_mismatch");
  if(value.dropped||value.unregistered_writes||value.registration_rejections||value.causal_lost)reasons.push_back("source_loss");
  if(value.counter_saturated)reasons.push_back("counter_saturated");
  if(value.owner_violation)reasons.push_back("owner_violation");
  if(value.invalid_event)reasons.push_back("invalid_event");
  if(value.clock_invalid)reasons.push_back("clock_invalid");
  if(value.recorded||value.queued||value.attempted<value.recorded)reasons.push_back("scalar_event_accounting");
  if(!final&&value.stopped)reasons.push_back("initial_source_stopped");
  if(final&&(!value.stopped||!value.quiescent||value.active_producers||value.pending_registrations))reasons.push_back("not_quiescent");
  return reasons;
}
bool counters_monotonic(const trace::scalar_snapshot& a,const trace::scalar_snapshot& b)noexcept {
  if(b.attempted<a.attempted||b.recorded<a.recorded||b.dropped<a.dropped||b.unregistered_writes<a.unregistered_writes||
    b.registration_rejections<a.registration_rejections||b.causal_lost<a.causal_lost)return false;
  for(std::size_t n=0;n<a.stages.size();++n)if(b.stages[n]<a.stages[n])return false;
  for(std::size_t n=0;n<a.counters.size();++n)if(b.counters[n]<a.counters[n])return false;
  for(std::size_t n=0;n<a.durations.size();++n)for(std::size_t k=0;k<a.durations[n].size();++k)
    if(b.durations[n][k]<a.durations[n][k])return false;
  return true;
}
bool same_stopped_snapshot(const trace::scalar_snapshot& a,const trace::scalar_snapshot& b)noexcept {
  return same_identity(a.session,b.session)&&a.recording_mode==b.recording_mode&&
    a.registered_threads==b.registered_threads&&a.active_producers==b.active_producers&&
    a.pending_registrations==b.pending_registrations&&a.attempted==b.attempted&&a.recorded==b.recorded&&
    a.dropped==b.dropped&&a.queued==b.queued&&a.unregistered_writes==b.unregistered_writes&&
    a.registration_rejections==b.registration_rejections&&a.causal_lost==b.causal_lost&&
    a.stopped==b.stopped&&a.quiescent==b.quiescent&&a.available==b.available&&
    a.counter_saturated==b.counter_saturated&&a.owner_violation==b.owner_violation&&
    a.invalid_event==b.invalid_event&&a.clock_invalid==b.clock_invalid&&
    a.stages==b.stages&&a.counters==b.counters&&a.durations==b.durations;
}
void reasons_json(std::ostream& out,const std::vector<std::string>& values) {
  out<<'[';for(std::size_t n=0;n<values.size();++n){if(n)out<<',';out<<'"'<<values[n]<<'"';}out<<']';
}
}
scalar_boundary capture_scalar_boundary(trace::identity expected,trace::trace_source& sdk_source,trace::trace_source* kernel_source) {
  scalar_boundary value;value.expected=expected;value.capture_begin_qpc=trace::now_qpc();
  value.sdk=sdk_source.snapshot();value.kernel_present=kernel_source!=nullptr;
  if(kernel_source)value.kernel=kernel_source->snapshot();value.capture_end_qpc=trace::now_qpc();return value;
}
void write_scalar_initial(const std::filesystem::path& output,const scalar_boundary& value,const std::string& cold) {
  std::ostringstream out;out.imbue(std::locale::classic());
  out<<"{\"schema\":\"soui.input-ui-scalar-boundary/v1\",\"phase\":\"initial\",\"scope\":\"whole-process-lifetime\",\"identity\":";
  identity_json(out,value.expected);out<<",\"capture_begin_qpc\":\""<<value.capture_begin_qpc<<"\",\"capture_end_qpc\":\""<<value.capture_end_qpc
    <<"\",\"cold_profile\":"<<cold<<",\"sources\":{\"sdk\":{\"available\":"<<boolean(value.sdk.available)<<",\"snapshot\":";
  snapshot_json(out,value.sdk);out<<"},\"kernel\":{\"available\":"<<boolean(value.kernel_present&&value.kernel.available)
    <<",\"backend_status\":\""<<(value.kernel_present?"loaded":"api_unavailable")<<"\",\"snapshot\":";
  if(value.kernel_present)snapshot_json(out,value.kernel);else out<<"null";
  out<<"}}}\n";write_scalar_json(output/L"scalar-initial.json",out.str());
}
scalar_export_result finish_scalar_capture(const std::filesystem::path& output,const scalar_boundary& initial,
    trace::trace_source& sdk_source,trace::trace_source* kernel_source,std::chrono::milliseconds remaining) {
  const auto deadline=std::chrono::steady_clock::now()+(std::max)(std::chrono::milliseconds::zero(),remaining);
  const auto final=capture_scalar_boundary(initial.expected,sdk_source,kernel_source);
  auto sdk_reasons=source_reasons(initial.sdk,initial.expected,false),kernel_reasons=std::vector<std::string>{};
  const auto final_sdk_reasons=source_reasons(final.sdk,initial.expected,true);
  sdk_reasons.insert(sdk_reasons.end(),final_sdk_reasons.begin(),final_sdk_reasons.end());
  if(!counters_monotonic(initial.sdk,final.sdk))sdk_reasons.push_back("counter_reversed");
  if(!initial.kernel_present||!final.kernel_present)kernel_reasons.push_back("api_unavailable");
  else {
    kernel_reasons=source_reasons(initial.kernel,initial.expected,false);
    const auto after=source_reasons(final.kernel,initial.expected,true);
    kernel_reasons.insert(kernel_reasons.end(),after.begin(),after.end());
    if(!counters_monotonic(initial.kernel,final.kernel))kernel_reasons.push_back("counter_reversed");
  }
  const bool clock_ok=initial.capture_begin_qpc&&initial.capture_end_qpc>=initial.capture_begin_qpc&&
    final.capture_begin_qpc>=initial.capture_end_qpc&&final.capture_end_qpc>=final.capture_begin_qpc;
  if(!clock_ok){sdk_reasons.push_back("boundary_clock_invalid");kernel_reasons.push_back("boundary_clock_invalid");}
  const auto export_source=[&](trace::trace_source& source,const wchar_t* name) {
    if(std::chrono::steady_clock::now()>=deadline)return false;
    try {
      if(source.snapshot().recording_mode!=trace::mode::scalar)return false;
      new_file_buffer file(output/name);std::ostream stream(&file);trace::exporter exporter(stream,source);
      if(!exporter.begin())return false;
      const auto batch=exporter.drain(1);
      if(batch.busy||batch.count||exporter.limit_reached()||!exporter.finish()||!stream.good()||!file.persist())return false;
      return std::chrono::steady_clock::now()<deadline;
    }catch(...){return false;}
  };
  const bool sdk_saved=export_source(sdk_source,L"scalar-sdk-final.ndjson");
  const bool kernel_saved=kernel_source&&export_source(*kernel_source,L"scalar-kernel-final.ndjson");
  if(!same_stopped_snapshot(final.sdk,sdk_source.snapshot()))sdk_reasons.push_back("source_changed_during_export");
  if(kernel_source&&!same_stopped_snapshot(final.kernel,kernel_source->snapshot()))kernel_reasons.push_back("source_changed_during_export");
  if(!sdk_saved)sdk_reasons.push_back("export_or_persistence_failed");
  if(kernel_source&&!kernel_saved)kernel_reasons.push_back("export_or_persistence_failed");
  if(std::chrono::steady_clock::now()>=deadline){sdk_reasons.push_back("close_deadline");kernel_reasons.push_back("close_deadline");}
  scalar_export_result result;result.sdk_usable=sdk_reasons.empty();result.kernel_usable=kernel_reasons.empty();
  result.usable=result.sdk_usable&&result.kernel_usable;
  const bool kernel_requested=initial.kernel_present||kernel_source;
  result.io_complete=sdk_saved&&(!kernel_requested||kernel_saved);
  // Legacy e2e without the optional kernel API can still close normally, but
  // never acquires a two-source measurement qualification.
  result.ok=result.io_complete&&result.sdk_usable&&(!kernel_requested||result.kernel_usable);
  std::ostringstream out;out.imbue(std::locale::classic());
  out<<"{\"schema\":\"soui.input-ui-scalar-status/v1\",\"mode\":\"scalar\",\"scope\":\"whole-process-lifetime\",\"identity\":";
  identity_json(out,initial.expected);out<<",\"initial_file\":\"scalar-initial.json\",\"capture_begin_qpc\":\""<<final.capture_begin_qpc
    <<"\",\"capture_end_qpc\":\""<<final.capture_end_qpc<<"\",\"sources\":{\"sdk\":{\"available\":"<<boolean(final.sdk.available)
    <<",\"persisted\":"<<boolean(sdk_saved)<<",\"usable\":"<<boolean(result.sdk_usable)<<",\"reasons\":";
  reasons_json(out,sdk_reasons);out<<",\"final_file\":\"scalar-sdk-final.ndjson\",\"final_snapshot\":";snapshot_json(out,final.sdk);
  out<<"},\"kernel\":{\"available\":"<<boolean(final.kernel_present&&final.kernel.available)<<",\"persisted\":"<<boolean(kernel_saved)
    <<",\"usable\":"<<boolean(result.kernel_usable)<<",\"reasons\":";reasons_json(out,kernel_reasons);
  out<<",\"final_file\":"<<(kernel_source?"\"scalar-kernel-final.ndjson\"":"null")<<",\"final_snapshot\":";
  if(final.kernel_present)snapshot_json(out,final.kernel);else out<<"null";
  out<<"}},\"io_complete\":"<<boolean(result.io_complete)<<",\"all_requested_sources_usable\":"<<boolean(result.usable)
    <<",\"full_causal_eligible\":false,\"physical_latency_measured\":false}\n";
  write_scalar_json(output/L"scalar-final-status.json",out.str());
  if(std::chrono::steady_clock::now()>=deadline){result.io_complete=false;result.ok=false;result.usable=false;}
  return result;
}
struct trace_capture::state final {
  state(const std::filesystem::path& directory,trace::trace_source& sdk,trace::trace_source& kernel)
      :output(directory),sdk_source(sdk),kernel_source(kernel) {
    ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);failed_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!ready||!failed_event){if(ready)CloseHandle(ready);if(failed_event)CloseHandle(failed_event);
      throw std::runtime_error("Trace consumer events unavailable");}
  }
  ~state(){if(worker.joinable())worker.join();CloseHandle(ready);CloseHandle(failed_event);}
  void fail(const char* message) noexcept {
    failed.store(true,std::memory_order_release);
    try{std::lock_guard<std::mutex> lock(mutex);error=message;}catch(...){ }
    SetEvent(failed_event);SetEvent(ready);
  }
  void run() noexcept {
    try {
      new_file_buffer sdk_file(output/L"trace-sdk.ndjson"),kernel_file(output/L"trace-vendor.ndjson");
      std::ostream sdk_output(&sdk_file),kernel_output(&kernel_file);
      trace::exporter sdk_export(sdk_output,sdk_source),kernel_export(kernel_output,kernel_source);
      if(!sdk_export.begin()||!kernel_export.begin())throw std::runtime_error("Trace sources are not cold-configured for this run");
      SetEvent(ready);
      auto closing_started=std::chrono::steady_clock::time_point{};
      for(;;) {
        const auto sdk_batch=sdk_export.drain(1024),kernel_batch=kernel_export.drain(1024);
        if(sdk_batch.busy||kernel_batch.busy)throw std::runtime_error("Another consumer is draining this trace");
        if(!sdk_output.good()||!kernel_output.good()||sdk_export.limit_reached()||kernel_export.limit_reached())
          throw std::runtime_error("Trace I/O or bounded event limit failed");
        if(closing.load(std::memory_order_acquire)) {
          if(closing_started==std::chrono::steady_clock::time_point{})closing_started=std::chrono::steady_clock::now();
          const bool sdk_done=sdk_export.finish(),kernel_done=kernel_export.finish();
          if(sdk_done&&kernel_done) {
            if(!sdk_file.persist()||!kernel_file.persist())throw std::runtime_error("Trace persistence failed");
            complete.store(true,std::memory_order_release);return;
          }
          if(std::chrono::steady_clock::now()-closing_started>std::chrono::milliseconds(close_budget.load(std::memory_order_acquire)))
            throw std::runtime_error("Trace producers did not quiesce after normal runtime shutdown");
        }
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait_for(lock,std::chrono::milliseconds(4));
      }
    }catch(const std::exception& exception){fail(exception.what());}
    catch(...){fail("Trace consumer failed");}
  }
  std::filesystem::path output;
  trace::trace_source& sdk_source;
  trace::trace_source& kernel_source;
  HANDLE ready{nullptr},failed_event{nullptr};
  std::thread worker;mutable std::mutex mutex;std::condition_variable changed;
  std::string error;
  std::atomic<bool> closing{false},failed{false},complete{false};
  std::atomic<unsigned> close_budget{3000};
};
trace_capture::trace_capture(const std::filesystem::path& output,trace::trace_source& sdk,trace::trace_source& kernel)
    :state_(std::make_unique<state>(output,sdk,kernel)) {
  state_->worker=std::thread([this]{state_->run();});
  if(WaitForSingleObject(state_->ready,5000)!=WAIT_OBJECT_0||state_->failed.load(std::memory_order_acquire)) {
    (void)finish();throw std::runtime_error("Trace consumer could not start");
  }
}
trace_capture::~trace_capture(){(void)finish();}
HANDLE trace_capture::failure_event() const noexcept{return state_->failed_event;}
std::string trace_capture::failure_message() const {
  std::lock_guard<std::mutex> lock(state_->mutex);return state_->error;
}
bool trace_capture::finish(std::chrono::milliseconds budget) noexcept {
  const auto bounded=(std::max)(std::int64_t{0},(std::min)(std::int64_t{3000},static_cast<std::int64_t>(budget.count())));
  state_->close_budget.store(static_cast<unsigned>(bounded),std::memory_order_release);
  state_->closing.store(true,std::memory_order_release);state_->changed.notify_one();
  if(state_->worker.joinable())state_->worker.join();
  return state_->complete.load(std::memory_order_acquire)&&!state_->failed.load(std::memory_order_acquire);
}
}
