#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

namespace soui::sdk::detail {

// Actual owner-STA scalar history. Disabled observers do not record or recover.
// The atomic fault latch alone may be written by an unexpected foreign thread.
class webview2_native_counters final {
 public:
  struct snapshot final {
    std::uint64_t acquired{}, released{}, closed{}, policy_active{};
    bool owns_controller{}, current_closed{}, fault{};
  };
  explicit webview2_native_counters(bool enabled = false) noexcept : enabled_(enabled) {}
  [[nodiscard]] bool enabled() const noexcept { return enabled_; }
  void mark_fault() noexcept { if (enabled_) fault_.store(true, std::memory_order_relaxed); }
  void acquire(bool on_owner = true) noexcept {
    if (!enabled_) return;
    if (!on_owner) { mark_fault(); return; }
    if (owns_controller_ || !increment(acquired_)) { mark_fault(); return; }
    owns_controller_ = true;
    current_closed_ = false;
  }
  void close_result(bool succeeded, bool on_owner = true) noexcept {
    if (!enabled_) return;
    if (!on_owner) { mark_fault(); return; }
    if (!owns_controller_ || !succeeded) { mark_fault(); return; }
    if (!current_closed_) {
      if (!increment(closed_)) { mark_fault(); return; }
      current_closed_ = true;
    }
  }
  void release(bool on_owner = true) noexcept {
    if (!enabled_) return;
    if (!on_owner) { mark_fault(); return; }
    if (!owns_controller_ || !current_closed_ || !increment(released_)) { mark_fault(); return; }
    owns_controller_ = false;
  }
  [[nodiscard]] bool enter_policy() noexcept {
    if (!enabled_) return false;
    if (!increment(policy_active_)) { mark_fault(); return false; }
    return true;
  }
  void leave_policy() noexcept {
    if (!enabled_) return;
    if (!policy_active_) { mark_fault(); return; }
    --policy_active_;
  }
  [[nodiscard]] snapshot observe() const noexcept {
    return {acquired_, released_, closed_, policy_active_, owns_controller_, current_closed_,
            fault_.load(std::memory_order_relaxed)};
  }
  [[nodiscard]] static bool increment(std::uint64_t& value) noexcept {
    if (value == (std::numeric_limits<std::uint64_t>::max)()) return false;
    ++value;
    return true;
  }

 private:
  const bool enabled_;
  std::atomic<bool> fault_{false};
  std::uint64_t acquired_{}, released_{}, closed_{}, policy_active_{};
  bool owns_controller_{}, current_closed_{};
};

}  // namespace soui::sdk::detail
