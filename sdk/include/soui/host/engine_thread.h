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
#include <atomic>

namespace soui::sdk {
namespace detail {
enum class engine_lane : std::uint8_t;
enum class engine_admission_result : std::uint8_t;
struct engine_capacity_state;
struct engine_capacity_snapshot;
class engine_reply_ticket;
class engine_admission;
class engine_input_service;
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
  // Cold owner/startup capability hook. Clear on owner before provider unload.
  [[nodiscard]] bool set_owner_opportunity(task callback);
  // On the engine owner this only requests stopping; an outside owner must join.
  void stop(bool drain_pending = true);
  enum class stop_result { stopped, pending, failed };
  // One nonblocking held-native-handle poll; owner-thread calls request stop
  // and return Pending. Pending always keeps the engine object alive.
  [[nodiscard]] stop_result try_stop();

  [[nodiscard]] engine_thread_state state() const noexcept;
  [[nodiscard]] bool is_owner_thread(std::thread::id candidate) const noexcept;
  [[nodiscard]] bool is_owner_thread() const noexcept;

 private:
  friend class detail::engine_admission;
  static constexpr std::size_t queue_capacity = 580;
  detail::engine_admission_result post_internal(task, detail::engine_lane, detail::engine_reply_ticket*);
  detail::engine_admission_result reserve_reply(detail::engine_lane, detail::engine_reply_ticket&);
  detail::engine_capacity_snapshot capacity_snapshot() const;
  bool register_input_service(const std::shared_ptr<detail::engine_input_service>&);
  void unregister_input_service(const std::shared_ptr<detail::engine_input_service>&);
  void notify_input_service() noexcept;
  bool has_pending_input_locked() const noexcept;
  void service_one_input();
  std::uint64_t commit_input_order(std::atomic<std::uint64_t>&, std::atomic<bool>&, bool, bool);
  std::uint64_t next_admission_order_locked() noexcept;
  bool older_input_locked(std::uint64_t) const noexcept;
  void run(initializer initialize, task shutdown) noexcept;
  class join_claim;
  void request_stop(bool drain_pending);
  void retire_joined_worker();

  mutable std::mutex mutex_;
  std::mutex join_mutex_;
  std::condition_variable join_ready_;
  bool join_claimed_{false};
  std::thread::id join_claim_owner_{};
  std::condition_variable work_ready_;
  std::condition_variable state_changed_;
  std::condition_variable idle_;
  struct queued_work { task callback; std::uint64_t enqueued_us{0}; std::size_t lane{0}; std::uint64_t order{0}; };
  std::array<queued_work, queue_capacity> work_{};
  std::size_t head_{0}, tail_{0}, queued_{0};
  std::array<std::size_t, 4> lane_queued_{};
  // Separate bounded Host capacity: reply, renewal and cleanup lanes are intact.
  std::array<std::shared_ptr<detail::engine_input_service>, 32> input_services_{};
  std::size_t next_input_service_{0};
  std::uint64_t admission_order_{0}; // Serialized with FIFO/input head publication, never wraps.
  task owner_opportunity_;
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
  bool stop_requested_{false};
  std::size_t executing_{0};
#ifdef _WIN32
  // Auto-reset event used together with MsgWaitForMultipleObjectsEx so the
  // owner can service both native messages (Miniblink/COM) and FIFO work
  // without a polling timer.
  void* wake_event_{nullptr};
#endif
};

}  // namespace soui::sdk
