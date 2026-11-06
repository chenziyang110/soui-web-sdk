#pragma once

#include <mutex>
#include <type_traits>
#include <utility>

namespace soui::sdk::detail {

// Construct after the outer owner_pump_guard. Call enter_locked() only while
// holding the output mutex, immediately before the first actual queue removal.
// Navigation/stop may clear the queue, but must not clear this scope's active bit.
template<class Queue, class Notify>
class owner_output_drain_scope final {
 public:
  owner_output_drain_scope(bool& active, std::mutex& mutex, Queue& outputs, Notify notify)
      noexcept(std::is_nothrow_move_constructible<Notify>::value)
      : active_(active), mutex_(mutex), outputs_(outputs), notify_(std::move(notify)) {
    static_assert(std::is_nothrow_invocable<Notify&>::value, "Owner notification must be noexcept");
    static_assert(noexcept(std::declval<const Queue&>().empty()), "Queue inspection must be noexcept");
  }
  owner_output_drain_scope(const owner_output_drain_scope&) = delete;
  owner_output_drain_scope& operator=(const owner_output_drain_scope&) = delete;
  owner_output_drain_scope(owner_output_drain_scope&&) = delete;
  owner_output_drain_scope& operator=(owner_output_drain_scope&&) = delete;

  void enter_locked() noexcept {
    if (!active_) {
      active_ = true;
      entered_ = true;
    }
  }

  // The caller keeps its original normal empty-pump tail check if never entered.
  [[nodiscard]] bool entered() const noexcept { return entered_; }

  ~owner_output_drain_scope() noexcept {
    if (!entered_) return; // An empty/rejected pump does not acquire a new lock.
    std::lock_guard<std::mutex> lock(mutex_);
    active_ = false;
    if (!outputs_.empty()) notify_();
  }

 private:
  bool& active_;
  std::mutex& mutex_;
  const Queue& outputs_;
  Notify notify_;
  bool entered_{};
};

} // namespace soui::sdk::detail
