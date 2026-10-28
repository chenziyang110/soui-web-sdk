#pragma once

#include <cstddef>
#include <memory>
#include <utility>

namespace soui::desktop::detail {

// UI-thread-only terminal cleanup, independent of a full native message queue.
// Embedded nodes allocate nothing and never keep their owner alive while idle.
template <class Owner>
class deferred_failure final {
 public:
  deferred_failure() = default;
  ~deferred_failure() { cancel(); }
  deferred_failure(const deferred_failure&) = delete;
  deferred_failure& operator=(const deferred_failure&) = delete;

  void schedule(std::weak_ptr<Owner> owner) noexcept {
    owner_ = std::move(owner);
    if (queued_) return;
    next_ = nullptr;
    if (last_) last_->next_ = this;
    else first_ = this;
    last_ = this;
    queued_ = true;
  }

  void cancel() noexcept {
    if (queued_) {
      deferred_failure* previous = nullptr;
      for (auto** link = &first_; *link; link = &(*link)->next_) {
        if (*link == this) {
          *link = next_;
          if (last_ == this) last_ = previous;
          break;
        }
        previous = *link;
      }
    }
    queued_ = false;
    next_ = nullptr;
    owner_.reset();
  }

  static bool pending() noexcept { return first_ != nullptr; }

  template <class Callback>
  static void drain(Callback&& callback) {
    if (draining_) return;
    draining_ = true;
    struct guard {
      ~guard() { draining_ = false; }
    } reset;
    // A callback may cancel another owner or schedule more work. Limit this
    // invocation to the entry count, without retaining pointers across calls.
    std::size_t remaining = 0;
    for (auto* node = first_; node; node = node->next_) ++remaining;
    while (remaining && first_) {
      --remaining;
      auto* node = first_;
      auto owner = node->owner_.lock();
      node->cancel();
      if (owner) callback(owner);
    }
  }

 private:
  inline static thread_local deferred_failure* first_ = nullptr;
  inline static thread_local deferred_failure* last_ = nullptr;
  inline static thread_local bool draining_ = false;
  deferred_failure* next_ = nullptr;
  std::weak_ptr<Owner> owner_;
  bool queued_ = false;
};

}  // namespace soui::desktop::detail
