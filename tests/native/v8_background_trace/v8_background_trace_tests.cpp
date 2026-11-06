#include "content/common/V8BackgroundTraceScope.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace trace = content::input_frame_trace;

namespace {

class scoped_handle final {
 public:
  explicit scoped_handle(HANDLE value) noexcept : value_(value) {}
  ~scoped_handle() noexcept { if (value_) CloseHandle(value_); }
  scoped_handle(const scoped_handle&) = delete;
  scoped_handle& operator=(const scoped_handle&) = delete;
  HANDLE get() const noexcept { return value_; }
 private:
  HANDLE value_{};
};

struct callback_result final {
  HANDLE completed{};
  std::atomic<bool> registered{false};
  std::atomic<std::uint64_t> task_id{0};
  std::atomic<std::uint32_t> record_result{static_cast<std::uint32_t>(trace::record_result::stopped)};
  std::atomic<std::uint32_t> registered_after_scope{0};
  std::atomic<std::uint32_t> active_after_scope{(std::numeric_limits<std::uint32_t>::max)()};
  std::atomic<std::uint64_t> unregistered_after_scope{(std::numeric_limits<std::uint64_t>::max)()};
};

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

DWORD WINAPI queue_user_work_item_callback(void* raw) {
  auto& result = *static_cast<callback_result*>(raw);
  {
#ifndef SOUI_V8_BACKGROUND_TRACE_NEGATIVE_CONTROL
    trace::background_producer_scope producer;
#endif
    result.registered.store(trace::current_thread_registered(), std::memory_order_release);
    const auto task_id = trace::next_task_id();
    result.task_id.store(task_id, std::memory_order_release);
    trace::event value;
    value.point = trace::stage::task_posted;
    value.task_id = task_id;
    result.record_result.store(static_cast<std::uint32_t>(trace::record(value)), std::memory_order_release);
  }
  // Signal only after the scope has destructed; the callback has no further
  // trace work after this completion boundary.
  const auto after = trace::process_recorder().snapshot();
  result.registered_after_scope.store(after.registered_threads, std::memory_order_release);
  result.active_after_scope.store(after.active_producers, std::memory_order_release);
  result.unregistered_after_scope.store(after.unregistered_writes, std::memory_order_release);
  (void)SetEvent(result.completed);
  return 0;
}

void configure_process_recorder() {
  const auto identity = trace::current_identity(0x5648425453434f50ULL, 0x455f434f4e545241ULL);
  require(identity.process_id && identity.process_incarnation && identity.qpc_frequency,
    "Process trace identity unavailable");
  require(trace::process_recorder().configure(identity, trace::mode::scalar),
    "Process trace recorder cold configure failed");
}

void queue_one_callback(callback_result& result) {
  scoped_handle completed(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  require(completed.get() != nullptr, "QueueUserWorkItem completion event creation failed");
  result.completed = completed.get();
  require(QueueUserWorkItem(&queue_user_work_item_callback, &result, WT_EXECUTEONLYONCE) != FALSE,
    "QueueUserWorkItem submission failed");
  const auto waited = WaitForSingleObject(completed.get(), 10000);
  if (waited != WAIT_OBJECT_0) {
    // QueueUserWorkItem cannot cancel/reclaim this callback. Do not unwind
    // its stack-owned result or close its event while it may still use them.
    std::cerr << "QueueUserWorkItem completion failed: " << waited << '\n';
    TerminateProcess(GetCurrentProcess(), 24);
    std::_Exit(24);
  }
}

void unscoped_counterexample() {
  const auto before = trace::process_recorder().snapshot();
  const auto task_id = trace::next_task_id();
  trace::event value;
  value.point = trace::stage::task_posted;
  const auto result = trace::record(value);
  const auto after = trace::process_recorder().snapshot();
  require(task_id == 0 && result == trace::record_result::unregistered,
    "Missing-scope negative control did not reject causal producer operations");
  require(after.unregistered_writes == before.unregistered_writes + 2,
    "Missing-scope negative control did not retain both unregistered writes");
  require(after.registered_threads == before.registered_threads && after.active_producers == 0,
    "Missing-scope negative control fabricated a producer registration");
}

void queue_callback_contract() {
  const auto before = trace::process_recorder().snapshot();
  callback_result result;
  queue_one_callback(result);
  const auto after = trace::process_recorder().snapshot();
  require(result.registered.load(std::memory_order_acquire),
    "QueueUserWorkItem callback did not own an active producer inside scope");
  require(result.task_id.load(std::memory_order_acquire) != 0,
    "QueueUserWorkItem callback failed to acquire a causal task id");
  require(result.record_result.load(std::memory_order_acquire) ==
      static_cast<std::uint32_t>(trace::record_result::scalar_only),
    "QueueUserWorkItem callback trace write was not accepted by the scalar recorder");
  require(result.active_after_scope.load(std::memory_order_acquire) == 0 && after.active_producers == 0,
    "QueueUserWorkItem producer stayed active after its scope ended");
  require(result.registered_after_scope.load(std::memory_order_acquire) >= 1,
    "QueueUserWorkItem scope did not reserve a producer slot");
  require(after.unregistered_writes == before.unregistered_writes,
    "Scoped QueueUserWorkItem writes were still marked unregistered");
  require(after.stages[static_cast<std::size_t>(trace::stage::task_posted)] ==
      before.stages[static_cast<std::size_t>(trace::stage::task_posted)] + 1,
    "QueueUserWorkItem scoped event did not reach the process recorder");
}

void same_thread_resume_contract() {
  std::exception_ptr failure;
  bool resumed_same_slot = true;
  std::thread worker([&] {
    try {
      std::uint32_t first_slot_count = 0;
      for (unsigned iteration = 0; iteration != 6; ++iteration) {
        {
          trace::background_producer_scope producer;
          require(trace::current_thread_registered(), "Resumed worker scope was not registered");
          const auto task_id = trace::next_task_id();
          require(task_id != 0, "Resumed worker did not receive a task id");
          trace::event value;
          value.point = trace::stage::task_posted;
          value.task_id = task_id;
          require(trace::record(value) == trace::record_result::scalar_only,
            "Resumed worker scoped trace write failed");
          const auto live = trace::process_recorder().snapshot();
          if (!iteration) first_slot_count = live.registered_threads;
          else if (live.registered_threads != first_slot_count) resumed_same_slot = false;
        }
        const auto detached = trace::process_recorder().snapshot();
        require(!trace::current_thread_registered() && detached.active_producers == 0,
          "Repeated worker scope did not detach its temporary producer");
        if (detached.registered_threads != first_slot_count) resumed_same_slot = false;
      }
    } catch (...) {
      failure = std::current_exception();
    }
  });
  worker.join();
  if (failure) std::rethrow_exception(failure);
  require(resumed_same_slot, "Repeated scopes on the same OS thread consumed additional producer slots");
}

void nested_context_and_exception_contracts() {
  require(trace::register_current_thread(), "Existing main-thread producer registration failed");
  trace::event context;
  context.point = trace::stage::task_posted;
  context.task_id = 0x1234;
  require(trace::set_current_context(context), "Existing main-thread context setup failed");
  const auto slots_before = trace::process_recorder().snapshot().registered_threads;
  {
    trace::background_producer_scope outer;
    {
      trace::background_producer_scope inner;
      require(trace::current_thread_registered(), "Nested scope detached the borrowed producer");
      trace::event observed;
      require(trace::current_context(observed) && observed.task_id == context.task_id,
        "Nested scope replaced an existing causal context");
    }
    trace::event observed;
    require(trace::current_thread_registered() && trace::current_context(observed) &&
        observed.task_id == context.task_id,
      "Nested scope teardown did not preserve the borrowed producer and context");
  }
  trace::event preserved;
  require(trace::current_thread_registered() && trace::current_context(preserved) &&
      preserved.task_id == context.task_id,
    "Borrowing scopes detached the caller's existing producer or context");
  require(trace::process_recorder().snapshot().registered_threads == slots_before,
    "Nested scopes allocated an additional producer slot");
  trace::detach_current_thread();
  require(trace::process_recorder().snapshot().active_producers == 0,
    "Manual borrowed producer cleanup left an active slot");

  struct expected_exception final {};
  bool was_registered_inside = false;
  bool caught = false;
  try {
    trace::background_producer_scope producer;
    was_registered_inside = trace::current_thread_registered();
    throw expected_exception{};
  } catch (const expected_exception&) {
    caught = true;
  }
  require(caught && was_registered_inside, "Exception-unwind fixture did not enter an owned scope");
  require(!trace::current_thread_registered() && trace::process_recorder().snapshot().active_producers == 0,
    "Exception unwinding leaked the scope-owned producer");
}

void stopped_recorder_stays_lossy() {
  auto& recorder = trace::process_recorder();
  recorder.stop();
  const auto before = recorder.snapshot();
  std::uint64_t task_id = 1;
  trace::record_result write = trace::record_result::recorded;
  {
    trace::background_producer_scope producer;
    require(!trace::current_thread_registered(), "Stopped recorder falsely registered a producer");
    task_id = trace::next_task_id();
    trace::event value;
    value.point = trace::stage::task_posted;
    write = trace::record(value);
  }
  const auto after = recorder.snapshot();
  require(task_id == 0 && write == trace::record_result::unregistered,
    "Stopped recorder falsely accepted scoped producer work");
  require(after.stopped && after.active_producers == 0 && after.quiescent,
    "Stopped recorder rejection left a producer active");
  require(after.registration_rejections > before.registration_rejections,
    "Stopped registration rejection was not preserved");
  require(after.causal_lost == before.causal_lost + 1,
    "Stopped scope rejection did not record causal loss");
  require(after.unregistered_writes == before.unregistered_writes + 2,
    "Rejected scope operations did not remain source loss");
}

int run_positive_contracts() {
  configure_process_recorder();
  unscoped_counterexample();
  queue_callback_contract();
  same_thread_resume_contract();
  nested_context_and_exception_contracts();
  const auto before_stop = trace::process_recorder().snapshot();
  require(before_stop.active_producers == 0 && before_stop.causal_lost == 0 &&
      before_stop.unregistered_writes == 2,
    "Scoped paths changed the intentionally retained missing-scope loss counters");
  stopped_recorder_stays_lossy();
  std::cout << "V8 background trace contracts: QueueUserWorkItem scoped writes accepted; "
    "same-thread slot reused; borrowed context preserved; exception detached; stopped writes remain loss\n";
  return 0;
}

int run_negative_control() {
  configure_process_recorder();
  const auto before = trace::process_recorder().snapshot();
  callback_result result;
  queue_one_callback(result);
  const auto after = trace::process_recorder().snapshot();
  const bool reproduced = !result.registered.load(std::memory_order_acquire) &&
    result.task_id.load(std::memory_order_acquire) == 0 &&
    result.record_result.load(std::memory_order_acquire) ==
      static_cast<std::uint32_t>(trace::record_result::unregistered) &&
    after.unregistered_writes == before.unregistered_writes + 2 && after.registered_threads == 0 &&
    after.active_producers == 0;
  if (reproduced) {
    std::cerr << "expected negative control: raw QueueUserWorkItem callback produced two unregistered writes\n";
    return 23;
  }
  std::cerr << "negative control unexpectedly did not reproduce missing producer registration\n";
  return 0;
}

} // namespace

int main() {
  try {
#ifdef SOUI_V8_BACKGROUND_TRACE_NEGATIVE_CONTROL
    return run_negative_control();
#else
    return run_positive_contracts();
#endif
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
