#pragma once

#include <chrono>
#include <cstdint>

namespace soui::sdk::detail {

// Controller-owned state. Only a successful, non-reentered submission is known.
// Never use this cache for source, session, permission or document identity.
class controller_updates final {
 public:
  template<class Submit>
  auto bounds(int width, int height, Submit submit) -> decltype(submit(width, height)) {
    using result = decltype(submit(width, height));
    if (bounds_known_ && width_ == width && height_ == height) return result{0};
    bounds_known_ = false;
    const auto ticket = ++bounds_ticket_;
    const auto status = submit(width, height);
    if (status >= 0 && ticket == bounds_ticket_) {
      width_ = width; height_ = height; bounds_known_ = true;
    } else bounds_known_ = false;
    return status;
  }
  template<class Submit>
  auto visibility(bool visible, Submit submit) -> decltype(submit(visible)) {
    using result = decltype(submit(visible));
    if (visibility_known_ && visible_ == visible) return result{0};
    visibility_known_ = false;
    const auto ticket = ++visibility_ticket_;
    const auto status = submit(visible);
    if (status >= 0 && ticket == visibility_ticket_) {
      visible_ = visible; visibility_known_ = true;
    } else visibility_known_ = false;
    return status;
  }

 private:
  std::uint64_t bounds_ticket_{}, visibility_ticket_{};
  int width_{}, height_{};
  bool bounds_known_{}, visibility_known_{}, visible_{};
};

class owner_work_budget final {
 public:
  using clock = std::chrono::steady_clock;
  explicit owner_work_budget(clock::time_point started) noexcept : started_(started) {}
  [[nodiscard]] bool allows(unsigned completed, clock::time_point now) const noexcept {
    return completed < 16 && (completed == 0 || now - started_ < std::chrono::milliseconds(2));
  }

 private:
  clock::time_point started_;
};

}  // namespace soui::sdk::detail
