#pragma once

#include <array>
#include <cstddef>

namespace soui::sdk::detail {

// STA-owned accounting. A cancellation result is separate from an outstanding
// native completion; even a completion currently invoking application code
// prevents successful teardown. This class never waits or pumps messages.
class preparation_lifetime final {
 public:
  enum class operation : std::size_t { environment, controller, script };

  class completion final {
   public:
    completion(const completion&) = delete;
    completion& operator=(const completion&) = delete;
    ~completion() { if (accepted_) --owner_.callbacks_; }
    [[nodiscard]] explicit operator bool() const noexcept { return accepted_; }

   private:
    friend class preparation_lifetime;
    completion(preparation_lifetime& owner, operation value) noexcept
        : owner_{owner}, accepted_{owner.settle(value)} {
      if (accepted_) ++owner_.callbacks_;
    }
    preparation_lifetime& owner_;
    bool accepted_;
  };

  [[nodiscard]] bool begin(operation value) noexcept {
    auto& pending = pending_[static_cast<std::size_t>(value)];
    if (cancelled_ || pending) return false;
    pending = true;
    return true;
  }
  // Used only for synchronous submission failure. Real callbacks use receive
  // so a reentrant unmount cannot tear resources down beneath that callback.
  [[nodiscard]] bool settle(operation value) noexcept {
    auto& pending = pending_[static_cast<std::size_t>(value)];
    const bool previous = pending;
    pending = false;
    return previous;
  }
  [[nodiscard]] completion receive(operation value) noexcept { return completion{*this, value}; }
  void cancel() noexcept { cancelled_ = true; }
  [[nodiscard]] bool cancelled() const noexcept { return cancelled_; }
  struct snapshot final { std::size_t pending{}, active{}; };
  [[nodiscard]] snapshot observe() const noexcept {
    snapshot result{0, callbacks_};
    for (const bool pending : pending_) if (pending) ++result.pending;
    return result;
  }
  [[nodiscard]] bool settled() const noexcept {
    if (callbacks_ != 0U) return false;
    for (const bool pending : pending_) if (pending) return false;
    return true;
  }

 private:
  std::array<bool, 3> pending_{};
  std::size_t callbacks_{0};
  bool cancelled_{false};
};

}  // namespace soui::sdk::detail
