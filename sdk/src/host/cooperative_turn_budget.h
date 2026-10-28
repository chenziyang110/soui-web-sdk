#pragma once
#include <algorithm>
#include <chrono>

namespace soui::sdk::detail {
class cooperative_turn_budget final {
 public:
  using clock = std::chrono::steady_clock;
  explicit cooperative_turn_budget(unsigned microseconds, clock::time_point start = clock::now()) noexcept
      : deadline_(microseconds >= 100 && microseconds <= 5000
          ? start + std::chrono::microseconds(microseconds) : clock::time_point::max()) {}
  [[nodiscard]] clock::time_point limit(clock::time_point component) const noexcept { return (std::min)(component, deadline_); }
 private:
  clock::time_point deadline_;
};
} // namespace soui::sdk::detail
