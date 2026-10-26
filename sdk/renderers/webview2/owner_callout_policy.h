#pragma once
namespace soui::sdk::detail {
// Identity must be checked again after an opaque COM callout returns.
template<class Identity, class Callout>
bool owner_callout_current(Identity&& identity, Callout&& callout) {
  return identity() && callout() && identity();
}
class owner_pump_guard final {
 public:
  explicit owner_pump_guard(bool& running) noexcept : running_(running ? nullptr : &running) {
    if (running_) *running_ = true;
  }
  ~owner_pump_guard() { if (running_) *running_ = false; }
  explicit operator bool() const noexcept { return running_ != nullptr; }
  owner_pump_guard(const owner_pump_guard&) = delete;
  owner_pump_guard& operator=(const owner_pump_guard&) = delete;
 private:
  bool* running_;
};
} // namespace soui::sdk::detail
