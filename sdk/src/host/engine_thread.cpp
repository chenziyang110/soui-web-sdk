#include <soui/host/engine_thread.h>
#include "engine_admission.h"
#include "../runtime/diagnostics/runtime_metrics_internal.h"

#include <atomic>
#include <exception>
#include <future>
#include <limits>
#include <memory>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef SOUI_ENGINE_STARTUP_TEST_ONLY
#include "engine_startup_test_hooks.h"
#endif

namespace soui::sdk {
namespace detail {
struct engine_capacity_state final {
  engine_capacity_state() noexcept {
    for (auto& count : reserved) count.store(0, std::memory_order_relaxed);
  }
  std::array<std::atomic<std::size_t>, 4> reserved{};
  std::uint64_t generation{0};
};
namespace {
constexpr std::array<std::size_t,4> lane_limits{
  engine_admission::ordinary_limit, engine_admission::control_limit,
  engine_admission::renewal_limit, engine_admission::cleanup_limit};
std::size_t index(engine_lane lane) noexcept { return static_cast<std::size_t>(lane); }
}
void engine_reply_ticket::release() noexcept {
  if (active_) { owner_->reserved[index(lane_)].fetch_sub(1,std::memory_order_acq_rel); active_=false; }
  owner_.reset(); generation_=0;
}
engine_reply_ticket::~engine_reply_ticket() { release(); }
engine_reply_ticket::engine_reply_ticket(engine_reply_ticket&& other) noexcept
    : owner_(std::move(other.owner_)), generation_(other.generation_), lane_(other.lane_),
      active_(std::exchange(other.active_,false)) { other.generation_=0; }
engine_reply_ticket& engine_reply_ticket::operator=(engine_reply_ticket&& other) noexcept {
  if (this != &other) { release(); owner_=std::move(other.owner_); generation_=other.generation_;
    lane_=other.lane_; active_=std::exchange(other.active_,false); other.generation_=0; }
  return *this;
}
engine_admission_result engine_admission::post(engine_thread& engine,engine_lane lane,engine_thread::task work) {
  return engine.post_internal(std::move(work),lane,nullptr);
}
engine_admission_result engine_admission::reserve_reply(engine_thread& engine,engine_lane lane,engine_reply_ticket& ticket) {
  return engine.reserve_reply(lane,ticket);
}
engine_admission_result engine_admission::submit_reply(engine_thread& engine,engine_reply_ticket& ticket,engine_thread::task work) {
  return engine.post_internal(std::move(work),ticket.lane_,&ticket);
}
engine_capacity_snapshot engine_admission::observe(const engine_thread& engine) { return engine.capacity_snapshot(); }
} // namespace detail

#ifdef _WIN32
namespace {

HANDLE create_engine_wake_event() noexcept {
#ifdef SOUI_ENGINE_STARTUP_TEST_ONLY
  return detail::engine_startup_test::create_wake_event();
#else
  return CreateEventW(nullptr, FALSE, FALSE, nullptr);
#endif
}

void close_engine_wake_event(HANDLE event) noexcept {
#ifdef SOUI_ENGINE_STARTUP_TEST_ONLY
  detail::engine_startup_test::close_wake_event(event);
#else
  (void)CloseHandle(event);
#endif
}

}  // namespace
#endif

engine_thread::engine_thread() : engine_thread(diagnostics::mode::disabled) {}
engine_thread::engine_thread(diagnostics::mode mode) : metrics_mode_(mode),
    metrics_epoch_(mode == diagnostics::mode::scalar ? diagnostics::detail::epoch() : 0) {}
diagnostics::status engine_thread::read_runtime_metrics(diagnostics::queue& out) const noexcept {
  out = {};
  if (metrics_mode_ != diagnostics::mode::scalar) return diagnostics::status::disabled;
  try {
    auto observed=diagnostics::detail::begin(metrics_mode_,metrics_epoch_);
    std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
    if (!lock.owns_lock()) { observed.state=diagnostics::status::busy; diagnostics::detail::end(observed); out.observation=observed; return observed.state; }
    out=metrics_; out.observation=observed; out.observation.counter_saturated=metrics_saturated_;
    out.queued=queued_; out.executing=executing_; out.has_capacity_bound=true; out.capacity_bound=queue_capacity;
    const auto now=diagnostics::detail::now_us();
    if (queued_) out.oldest_queued_age_us=diagnostics::detail::age(work_[head_].enqueued_us,now,out.observation);
    if (executing_) out.current_callback_age_us=diagnostics::detail::age(callback_started_us_,now,out.observation);
    diagnostics::detail::end(out.observation); return out.observation.state;
  } catch (...) { out={}; out.observation.state=diagnostics::status::unavailable; return out.observation.state; }
}
// One reservation protects all worker_ access, including thread construction
// and failed-start cleanup. It is not a mutex held across a callback or join.
class engine_thread::join_claim final {
 public:
  join_claim(engine_thread& owner, bool wait) : owner_(owner) {
    std::unique_lock<std::mutex> lock(owner_.join_mutex_, std::defer_lock);
    if (wait) lock.lock();
    else if (!lock.try_lock()) return;
    const auto caller = std::this_thread::get_id();
    // A discarded capture may reenter on the outside claimant's own stack.
    if (owner_.join_claimed_ && owner_.join_claim_owner_ == caller) return;
    if (wait) {
      owner_.join_ready_.wait(lock, [&] { return !owner_.join_claimed_; });
    } else if (owner_.join_claimed_) {
      return;
    }
    owner_.join_claimed_ = true;
    owner_.join_claim_owner_ = caller;
    held_ = true;
  }
  join_claim(const join_claim&) = delete;
  join_claim& operator=(const join_claim&) = delete;
  ~join_claim() {
    if (!held_) return;
    {
      std::lock_guard<std::mutex> lock(owner_.join_mutex_);
      owner_.join_claim_owner_ = std::thread::id{};
      owner_.join_claimed_ = false;
    }
    owner_.join_ready_.notify_all();
  }
  explicit operator bool() const noexcept { return held_; }
 private:
  engine_thread& owner_;
  bool held_{false};
};

engine_thread::~engine_thread() { stop(); }

bool engine_thread::start(initializer initialize, task shutdown) {
  if (!initialize || is_owner_thread()) return false;
  join_claim claim(*this, true);
  if (!claim) return false;
  std::shared_ptr<detail::engine_capacity_state> next_capacity;
  try { next_capacity=std::make_shared<detail::engine_capacity_state>(); }
  catch (...) { return false; }
#ifdef _WIN32
  const HANDLE wake_event = create_engine_wake_event();
  if (wake_event == nullptr) return false;
#endif

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != engine_thread_state::stopped || worker_.joinable() ||
        capacity_generation_ == (std::numeric_limits<std::uint64_t>::max)()) {
#ifdef _WIN32
      close_engine_wake_event(wake_event);
#endif
      return false;
    }
#ifdef _WIN32
    wake_event_ = wake_event;
#endif
    next_capacity->generation=++capacity_generation_;
    capacity_=std::move(next_capacity);
    state_ = engine_thread_state::starting;
    startup_complete_ = false;
    stop_requested_ = false;
  }
  try {
#ifdef SOUI_ENGINE_STARTUP_TEST_ONLY
    detail::engine_startup_test::before_worker_launch();
#endif
    // The reservation gives unique worker_ access. Construct outside mutex_:
    // launch failure may destroy moved captures that reenter this engine.
    worker_ = std::thread(&engine_thread::run, this, std::move(initialize),
                          std::move(shutdown));
  } catch (...) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      // No worker exists to publish startup completion or own this event.
      state_ = engine_thread_state::stopped;
      startup_complete_ = true;
      owner_thread_ = std::thread::id{};
#ifdef _WIN32
      wake_event_ = nullptr;
#endif
    }
#ifdef _WIN32
    close_engine_wake_event(wake_event);
#endif
    state_changed_.notify_all();
    return false;
  }

  std::unique_lock<std::mutex> lock(mutex_);
  state_changed_.wait(lock, [this] { return startup_complete_; });
  const bool running = state_ == engine_thread_state::running;
  lock.unlock();
  if (!running) {
    // The same reservation is still held from launch through failed/cancelled
    // initialization, finalizer/capture retirement and this actual native join.
    if (worker_.joinable()) worker_.join();
    retire_joined_worker();
  }
  return running;
}

bool engine_thread::post(task work) {
  return post_internal(std::move(work),detail::engine_lane::ordinary,nullptr) == detail::engine_admission_result::accepted;
}

detail::engine_admission_result engine_thread::post_internal(task work,detail::engine_lane lane,
                                                            detail::engine_reply_ticket* ticket) {
  using result=detail::engine_admission_result;
  const auto category=static_cast<std::size_t>(lane);
  if (!work || category >= lane_queued_.size()) return result::invalid;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != engine_thread_state::running) {
      if (metrics_mode_ == diagnostics::mode::scalar) diagnostics::detail::increment(metrics_.stopped_rejections_total,metrics_saturated_);
      return result::stopped;
    }
    if (ticket) {
      if (!ticket->active_ || ticket->owner_ != capacity_ || ticket->generation_ != capacity_generation_ ||
          ticket->lane_ != lane) return result::invalid;
    } else if (lane_queued_[category]+capacity_->reserved[category].load(std::memory_order_acquire) >= detail::lane_limits[category]) {
      if (metrics_mode_ == diagnostics::mode::scalar) diagnostics::detail::increment(metrics_.full_rejections_total,metrics_saturated_);
      return result::full;
    }
    // Credits guarantee a free physical slot. No callable allocation/destruction under this lock.
    if (queued_ >= queue_capacity || work_[tail_].callback) std::terminate();
    work_[tail_].callback.swap(work);
    work_[tail_].enqueued_us=metrics_mode_ == diagnostics::mode::scalar ? diagnostics::detail::now_us() : 0;
    work_[tail_].lane=category;
    tail_=(tail_+1)%queue_capacity; ++queued_; ++lane_queued_[category];
    if (ticket) { ticket->active_=false; capacity_->reserved[category].fetch_sub(1,std::memory_order_acq_rel); }
    if (metrics_mode_ == diagnostics::mode::scalar) {
      diagnostics::detail::increment(metrics_.posted_total,metrics_saturated_);
      diagnostics::detail::high_water(metrics_.high_water,queued_);
    }
#ifdef _WIN32
    SetEvent(static_cast<HANDLE>(wake_event_));
#endif
  }
  work_ready_.notify_one();
  return result::accepted;
}

detail::engine_admission_result engine_thread::reserve_reply(detail::engine_lane lane,detail::engine_reply_ticket& ticket) {
  using result=detail::engine_admission_result;
  const auto category=static_cast<std::size_t>(lane);
  if (category >= 3 || ticket.active_) return result::invalid; // cleanup is never a wire reply lane
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != engine_thread_state::running) return result::stopped;
  if (lane_queued_[category]+capacity_->reserved[category].load(std::memory_order_acquire) >= detail::lane_limits[category]) {
    if (metrics_mode_ == diagnostics::mode::scalar) diagnostics::detail::increment(metrics_.full_rejections_total,metrics_saturated_);
    return result::full;
  }
  ticket.owner_=capacity_; ticket.generation_=capacity_generation_; ticket.lane_=lane;
  capacity_->reserved[category].fetch_add(1,std::memory_order_acq_rel); ticket.active_=true;
  return result::accepted;
}

detail::engine_capacity_snapshot engine_thread::capacity_snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  detail::engine_capacity_snapshot out; out.queued=lane_queued_; out.executing=executing_;
  out.generation=capacity_generation_; out.state=state_;
  if (capacity_) for (std::size_t i=0;i<out.reserved.size();++i) out.reserved[i]=capacity_->reserved[i].load(std::memory_order_acquire);
  return out;
}
bool engine_thread::invoke(task work, std::chrono::milliseconds timeout) {
  if (!work || timeout.count() < 0 || is_owner_thread()) return false;
  auto completion = std::make_shared<std::promise<void>>();
  auto completed = completion->get_future();
  if (!post([work = std::move(work), completion] {
        try {
          work();
          completion->set_value();
        } catch (...) {
          completion->set_exception(std::current_exception());
        }
      })) {
    return false;
  }
  if (completed.wait_for(timeout) != std::future_status::ready) return false;
  try {
    completed.get();
  } catch (...) {
    return false;
  }
  return true;
}

bool engine_thread::drain_for(std::chrono::milliseconds timeout) {
  if (timeout.count() < 0) return false;
  std::unique_lock<std::mutex> lock(mutex_);
  return idle_.wait_for(lock, timeout,
                        [this] { return queued_ == 0 && executing_ == 0; });
}

void engine_thread::request_stop(bool drain_pending) {
  std::array<task,queue_capacity> retired;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // Initializer reentry must survive run()'s startup publication.
    stop_requested_ = true;
    if (state_ == engine_thread_state::running)
      state_ = engine_thread_state::stopping;
    if (state_ == engine_thread_state::stopping && !drain_pending) {
      if (metrics_mode_ == diagnostics::mode::scalar) diagnostics::detail::add(metrics_.discarded_on_stop_total,queued_,metrics_saturated_);
      for (std::size_t i=0;i<queue_capacity;++i) retired[i].swap(work_[i].callback);
      head_=tail_=queued_=0; lane_queued_={};
    }
#ifdef _WIN32
    if (wake_event_ != nullptr) SetEvent(static_cast<HANDLE>(wake_event_));
#endif
  }
  work_ready_.notify_all();
  // Arbitrary capture destruction may reenter stop; no state mutex is held.
  for (auto& callback:retired) { task release; release.swap(callback); }
}

void engine_thread::retire_joined_worker() {
#ifdef _WIN32
  HANDLE event = nullptr;
#endif
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // Clear owner identity only after the thread and all its capture destructors
    // have exited. A logical stopped state alone is not a native join receipt.
    owner_thread_ = std::thread::id{};
#ifdef _WIN32
    event = static_cast<HANDLE>(wake_event_); wake_event_ = nullptr;
#endif
  }
#ifdef _WIN32
  if (event) close_engine_wake_event(event);
#endif
}

void engine_thread::stop(bool drain_pending) {
  // Never wait for a reservation held by a thread joining this owner.
  if (is_owner_thread()) {
    request_stop(drain_pending);
    return;
  }
  join_claim claim(*this, true);
  if (!claim) {
    // Same outside claimant's capture destructor: request only, never recurse
    // into its own join reservation. The original outside stop still joins.
    request_stop(drain_pending);
    return;
  }
  request_stop(drain_pending);
  if (worker_.joinable()) worker_.join();
  retire_joined_worker();
}

engine_thread::stop_result engine_thread::try_stop() {
  if (is_owner_thread()) {
    request_stop(true);
    return stop_result::pending;
  }
  join_claim claim(*this, false);
  if (!claim) return stop_result::pending;
#ifdef _WIN32
  HANDLE native = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ == engine_thread_state::starting) return stop_result::pending;
    stop_requested_ = true;
    if (state_ == engine_thread_state::running) state_ = engine_thread_state::stopping;
    if (!worker_.joinable())
      return state_ == engine_thread_state::stopped ? stop_result::stopped : stop_result::failed;
    native = static_cast<HANDLE>(worker_.native_handle());
    if (wake_event_) SetEvent(static_cast<HANDLE>(wake_event_));
  }
  work_ready_.notify_all();
  const DWORD observed = WaitForSingleObject(native, 0);
  if (observed == WAIT_TIMEOUT) return stop_result::pending;
  DWORD exit_code = STILL_ACTIVE;
  if (observed != WAIT_OBJECT_0 || !GetExitCodeThread(native, &exit_code) || exit_code != 0)
    return stop_result::failed;
  // The original std::thread handle is held by worker_ until this real exit.
  worker_.join();
  retire_joined_worker();
  engine_thread_state final_state;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    final_state = state_;
  }
  return final_state == engine_thread_state::stopped ? stop_result::stopped : stop_result::failed;
#else
  return stop_result::failed; // This finite HANDLE profile is explicitly Windows-only.
#endif
}

engine_thread_state engine_thread::state() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

bool engine_thread::is_owner_thread(std::thread::id candidate) const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return owner_thread_ != std::thread::id{} && owner_thread_ == candidate;
}

bool engine_thread::is_owner_thread() const noexcept {
  return is_owner_thread(std::this_thread::get_id());
}

void engine_thread::run(initializer initialize, task shutdown) noexcept {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    owner_thread_ = std::this_thread::get_id();
  }
  bool initialized = false;
  try {
    initialized = initialize();
  } catch (...) {
    initialized = false;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = initialized
        ? (stop_requested_ ? engine_thread_state::stopping : engine_thread_state::running)
        : engine_thread_state::failed;
    startup_complete_ = true;
  }
  state_changed_.notify_all();
  idle_.notify_all();
  if (!initialized) {
    try {
      if (shutdown) shutdown();
    } catch (...) {
    }
    return;
  }

  bool stopping_complete = false;
  for (;;) {
#ifdef _WIN32
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (state_ != engine_thread_state::stopping) {
        const HANDLE wake_event = static_cast<HANDLE>(wake_event_);
        const bool wait_for_work = queued_ == 0;
        lock.unlock();
        if (wait_for_work) {
          (void)MsgWaitForMultipleObjectsEx(
              1, &wake_event, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        {
          MSG message{};
          // A native callback may repost itself indefinitely. Yield to the
          // FIFO and stopping state after a bounded batch, even if the
          // Windows queue never becomes empty.
          constexpr unsigned max_native_messages_per_turn = 32;
          const auto native_deadline = std::chrono::steady_clock::now()
              + std::chrono::milliseconds(2);
          for (unsigned count = 0; count < max_native_messages_per_turn
               && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE;
               ++count) {
            if (message.message == WM_QUIT) {
              std::lock_guard<std::mutex> stop_lock(mutex_);
              if (state_ == engine_thread_state::running)
                state_ = engine_thread_state::stopping;
              break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (std::chrono::steady_clock::now() >= native_deadline) break;
          }
        }
        lock.lock();
      }
    }
#endif
    // Service a bounded FIFO batch too. One task per native batch makes an
    // input burst accumulate behind expensive paint messages, even though
    // neither queue is strictly starved. Neither budget can preempt a callback.
    constexpr unsigned max_tasks_per_turn = 32;
    const auto task_deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(2);
    for (unsigned count = 0; count < max_tasks_per_turn; ++count) {
      task current;
      {
        std::unique_lock<std::mutex> lock(mutex_);
#ifndef _WIN32
        work_ready_.wait(lock, [this] {
          return state_ == engine_thread_state::stopping || queued_ != 0;
        });
#endif
        if (queued_ == 0 && state_ == engine_thread_state::stopping) {
          stopping_complete = true;
          break;
        }
        if (queued_ == 0) break;
        current.swap(work_[head_].callback);
        --lane_queued_[work_[head_].lane];
        head_=(head_+1)%queue_capacity; --queued_;
        ++executing_;
        if (metrics_mode_ == diagnostics::mode::scalar) callback_started_us_=diagnostics::detail::now_us();
      }
      try {
        current();
      } catch (...) {
        // An isolated host callback must not kill the process-wide engine owner.
      }
      // The executing slot covers capture retirement as well as the call.
      // A capture destructor can reenter this Engine only outside mutex_.
      { task retired; retired.swap(current); }
      {
        std::lock_guard<std::mutex> lock(mutex_);
        --executing_;
        callback_started_us_=0;
        if (queued_ == 0 && executing_ == 0) idle_.notify_all();
      }
      if (std::chrono::steady_clock::now() >= task_deadline) break;
    }
    if (stopping_complete) break;
  }

  bool shutdown_succeeded = true;
  try {
    if (shutdown) shutdown();
  } catch (...) {
    shutdown_succeeded = false; // Preserve Failed; stopped is not a shutdown receipt.
  }
  std::array<task,queue_capacity> retired;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (metrics_mode_ == diagnostics::mode::scalar) diagnostics::detail::add(metrics_.discarded_on_stop_total,queued_,metrics_saturated_);
    for (std::size_t i=0;i<queue_capacity;++i) retired[i].swap(work_[i].callback);
    head_=tail_=queued_=0; lane_queued_={};
    executing_ = 0;
    callback_started_us_=0;
    // Keep owner identity through local/parameter capture destruction at exit.
    state_ = shutdown_succeeded ? engine_thread_state::stopped : engine_thread_state::failed;
  }
  for (auto& callback:retired) { task release; release.swap(callback); }
  idle_.notify_all();
  state_changed_.notify_all();
}

}  // namespace soui::sdk
