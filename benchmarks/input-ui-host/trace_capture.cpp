#include "trace_capture.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <ostream>
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
