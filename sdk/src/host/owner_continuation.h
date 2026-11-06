#pragma once

#include <functional>
#include <memory>

namespace soui::sdk::detail {

// Private adapter contract. The adapter retains the registration object while
// a producer calls request(); reset revokes service without destroying it.
// Callbacks must capture the actual adapter weak state, never a raw adapter.
class owner_continuation_registration final {
 public:
  struct state;
  ~owner_continuation_registration();
  owner_continuation_registration(const owner_continuation_registration&) = delete;
  owner_continuation_registration& operator=(const owner_continuation_registration&) = delete;
  owner_continuation_registration(owner_continuation_registration&&) = delete;
  owner_continuation_registration& operator=(owner_continuation_registration&&) = delete;

  void request() noexcept;  // Worker-safe; loop wake or the original legacy wake.
  void reset() noexcept;   // Creating owner only; late requests retain legacy wake.
  // true includes an effective registration rejecting a reentrant attempt.
  // Only false permits the adapter to invoke its original pump directly.
  bool service_legacy();

 private:
  explicit owner_continuation_registration(std::shared_ptr<state> value);
  const std::shared_ptr<state> state_;
  friend std::unique_ptr<owner_continuation_registration> attach_current_owner_continuation(
      std::weak_ptr<void>, std::function<bool()>, std::function<void()>) noexcept;
};

std::unique_ptr<owner_continuation_registration> attach_current_owner_continuation(
    std::weak_ptr<void> real_lifetime, std::function<bool()> bounded_pump,
    std::function<void()> legacy_notify) noexcept;

}  // namespace soui::sdk::detail
