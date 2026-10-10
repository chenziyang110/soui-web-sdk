#pragma once

// Private UI-thread policy. No platform calls or standard-library dependencies.
// begin() latches the first actual exit; retries can never renew its budget.
class resident_shutdown_policy final {
 public:
  using tick = unsigned long long;
  static constexpr tick budget_ms = 10000;
  static constexpr tick retry_ms = 50;

  void begin(tick now) noexcept {
    if (started_) return;
    started_ = true;
    first_ = now;
  }
  bool started() const noexcept { return started_; }
  tick elapsed(tick now) const noexcept { return now - first_; }
  bool expired(tick now) const noexcept {
    return started_ && elapsed(now) >= budget_ms;
  }
  tick remaining(tick now) const noexcept {
    if (!started_) return budget_ms;
    const auto used = elapsed(now);
    return used >= budget_ms ? 0 : budget_ms - used;
  }
  tick wait_ms(tick now) const noexcept {
    const auto left = remaining(now);
    return left < retry_ms ? left : retry_ms;
  }

 private:
  tick first_{0};
  bool started_{false};
};

enum class resident_host_close { completed, retry, fail_stop };

// The controller's actual std::string result is compared as a whole value.
// No prefix, unknown failure or other pending code authorizes a retry.
template <class Code>
resident_host_close resident_classify_host_close(bool ok, const Code& code) noexcept {
  if (ok) return resident_host_close::completed;
  return code == "E_BUSY" || code == "E_CURSOR_CLOSE_PENDING"
      ? resident_host_close::retry : resident_host_close::fail_stop;
}
