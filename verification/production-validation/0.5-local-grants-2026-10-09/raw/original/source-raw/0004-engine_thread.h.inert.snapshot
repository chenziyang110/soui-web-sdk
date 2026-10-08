#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace soui::sdk {

enum class engine_thread_state {
  stopped,
  starting,
  running,
  stopping,
  failed,
};

// Owns every operation that touches the process-wide Miniblink/V8 runtime.
// Work is FIFO and startup/shutdown execute on the same stable owner thread.
class engine_thread final {
 public:
  using task = std::function<void()>;
  using initializer = std::function<bool()>;

  engine_thread() = default;
  engine_thread(const engine_thread&) = delete;
  engine_thread& operator=(const engine_thread&) = delete;
  ~engine_thread();

  [[nodiscard]] bool start(initializer initialize, task shutdown = {});
  [[nodiscard]] bool post(task work);
  [[nodiscard]] bool invoke(task work,
                            std::chrono::milliseconds timeout = std::chrono::seconds(10));
  [[nodiscard]] bool drain_for(std::chrono::milliseconds timeout);
  void stop(bool drain_pending = true);

  [[nodiscard]] engine_thread_state state() const noexcept;
  [[nodiscard]] bool is_owner_thread(std::thread::id candidate) const noexcept;
  [[nodiscard]] bool is_owner_thread() const noexcept;

 private:
  void run(initializer initialize, task shutdown) noexcept;

  mutable std::mutex mutex_;
  std::condition_variable work_ready_;
  std::condition_variable state_changed_;
  std::condition_variable idle_;
  std::deque<task> work_;
  std::thread worker_;
  std::thread::id owner_thread_{};
  engine_thread_state state_{engine_thread_state::stopped};
  bool startup_complete_{false};
  std::size_t executing_{0};
#ifdef _WIN32
  // Auto-reset event used together with MsgWaitForMultipleObjectsEx so the
  // owner can service both native messages (Miniblink/COM) and FIFO work
  // without a polling timer.
  void* wake_event_{nullptr};
#endif
};

}  // namespace soui::sdk
