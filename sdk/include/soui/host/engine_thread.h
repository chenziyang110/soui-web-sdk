#pragma once

#include <soui/diagnostics/runtime_metrics.h>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <array>
#include <cstdint>
#include <memory>
#include <functional>
#include <mutex>
#include <thread>

namespace soui::sdk {
namespace detail {
enum class engine_lane : std::uint8_t;
enum class engine_admission_result : std::uint8_t;
struct engine_capacity_state;
struct engine_capacity_snapshot;
class engine_reply_ticket;
class engine_admission;
}

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

  engine_thread();
  explicit engine_thread(diagnostics::mode);
  [[nodiscard]] diagnostics::status read_runtime_metrics(diagnostics::queue&) const noexcept;
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
  friend class detail::engine_admission;
  static constexpr std::size_t queue_capacity = 580;
  detail::engine_admission_result post_internal(task, detail::engine_lane, detail::engine_reply_ticket*);
  detail::engine_admission_result reserve_reply(detail::engine_lane, detail::engine_reply_ticket&);
  detail::engine_capacity_snapshot capacity_snapshot() const;
  void run(initializer initialize, task shutdown) noexcept;

  mutable std::mutex mutex_;
  std::condition_variable work_ready_;
  std::condition_variable state_changed_;
  std::condition_variable idle_;
  struct queued_work { task callback; std::uint64_t enqueued_us{0}; std::size_t lane{0}; };
  std::array<queued_work, queue_capacity> work_{};
  std::size_t head_{0}, tail_{0}, queued_{0};
  std::array<std::size_t, 4> lane_queued_{};
  std::shared_ptr<detail::engine_capacity_state> capacity_;
  std::uint64_t capacity_generation_{0};
  const diagnostics::mode metrics_mode_;
  const std::uint64_t metrics_epoch_;
  diagnostics::queue metrics_;
  bool metrics_saturated_{false};
  std::uint64_t callback_started_us_{0};
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
