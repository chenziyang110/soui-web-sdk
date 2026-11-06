#pragma once

#include "../../../third_party/include/miniblink/input_ui_api.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace soui::sdk::detail {

[[nodiscard]] inline bool frame_policy_api_available(const wkeInputUiApi* api) noexcept {
  return api && api->abiVersion == WKE_INPUT_UI_ABI_VERSION
      && api->structSize >= offsetof(wkeInputUiApi, applyFramePolicy) + sizeof(api->applyFramePolicy)
      && (api->capabilities & WKE_INPUT_UI_CAP_FRAME_POLICY_STATE) != 0
      && api->queryFramePolicy && api->applyFramePolicy;
}

[[nodiscard]] inline bool valid_frame_policy_snapshot(const wkeInputUiFramePolicy& value) noexcept {
  return value.structSize == sizeof(value) && value.abiVersion == WKE_INPUT_UI_FRAME_POLICY_ABI_VERSION
      && value.policyVersion == 1 && value.minRate >= 30 && value.maxRate <= 1000
      && value.minRate <= value.maxRate && !value.reserved[0] && !value.reserved[1]
      && (!value.appliedRate || (value.appliedRate >= value.minRate && value.appliedRate <= value.maxRate));
}

struct frame_policy_selection final {
  int requested{}, display_limit{}, backend_min{}, backend_limit{}, effective{};
  bool valid{}, clamped{};
};

// selected_main is the result of the existing main_rate precedence policy.
// Zero disables that controller; it must never become a nonzero fallback.
[[nodiscard]] inline frame_policy_selection select_frame_policy_rate(int requested, int display_limit,
    int selected_main, int backend_min, int backend_limit) noexcept {
  frame_policy_selection value{requested,
      display_limit >= 30 && display_limit <= 1000 ? display_limit : 60,
      backend_min, backend_limit};
  if ((requested != 0 && (requested < 30 || requested > 1000))
      || (selected_main != 0 && (selected_main < 30 || selected_main > 1000))
      || backend_min < 30 || backend_limit > 1000 || backend_min > backend_limit)
    return value;
  value.effective = selected_main ? (std::min)({selected_main, value.display_limit, backend_limit}) : 0;
  value.valid = !selected_main || value.effective >= backend_min;
  value.clamped = value.valid && selected_main && value.effective != selected_main;
  return value;
}

struct frame_policy_key final {
  std::uint64_t view_generation{};
  std::uintptr_t provider_identity{};
  std::uint64_t capabilities{};
  unsigned api_size{}, api_version{}, mode{};
  int requested{}, display_limit{}, selected_main{};
  friend bool operator==(const frame_policy_key& a, const frame_policy_key& b) noexcept {
    return a.view_generation == b.view_generation && a.provider_identity == b.provider_identity
        && a.capabilities == b.capabilities && a.api_size == b.api_size && a.api_version == b.api_version
        && a.mode == b.mode && a.requested == b.requested && a.display_limit == b.display_limit
        && a.selected_main == b.selected_main;
  }
  friend bool operator!=(const frame_policy_key& a, const frame_policy_key& b) noexcept { return !(a == b); }
};

// Engine-owner-only bookkeeping. It owns no timer, callbacks, view, or module.
// A begin receipt is required for completion, including legacy setter calls.
class frame_policy_attempts final {
 public:
  using clock = std::chrono::steady_clock;
  enum class outcome { unattempted, in_progress, applied, legacy_unverified, retry_wait, failed };

  [[nodiscard]] std::uint64_t begin(const frame_policy_key& key, clock::time_point now) noexcept {
    if (!key_initialized_ || key != key_) {
      if (!key_initialized_ || key.view_generation != key_.view_generation
          || key.provider_identity != key_.provider_identity || key.capabilities != key_.capabilities
          || key.api_size != key_.api_size || key.api_version != key_.api_version)
        observed_applied_ = -1;
      key_ = key;
      key_initialized_ = true;
      attempts_ = 0;
      attempted_rate_ = -1;
      last_result_ = WKE_INPUT_UI_UNAVAILABLE;
      state_ = outcome::unattempted;
      current_ticket_ = 0;
    }
    if (state_ != outcome::unattempted && (state_ != outcome::retry_wait || now < retry_at_)) return 0;
    if (serial_ == (std::numeric_limits<std::uint64_t>::max)()) {
      state_ = outcome::failed;
      last_result_ = WKE_INPUT_UI_REJECTED;
      return 0;
    }
    ++attempts_;
    current_ticket_ = ++serial_;
    state_ = outcome::in_progress;
    return current_ticket_;
  }

  // observed_applied comes only from a validated query/apply snapshot. -1 means
  // no observation, not controller-disabled. REJECTED may follow partial work;
  // a successful readback can update actual state without claiming apply OK.
  bool complete(std::uint64_t ticket, int result, int attempted_rate, int observed_applied,
      clock::time_point now) noexcept {
    if (!current(ticket)) return false;
    current_ticket_ = 0;
    attempted_rate_ = attempted_rate;
    if (observed_applied >= 0) observed_applied_ = observed_applied;
    if (result == WKE_INPUT_UI_OK && (attempted_rate < 0 || observed_applied != attempted_rate))
      result = WKE_INPUT_UI_REJECTED;
    last_result_ = result;
    if (result == WKE_INPUT_UI_OK) state_ = outcome::applied;
    else if ((result == WKE_INPUT_UI_BUSY || result == WKE_INPUT_UI_UNAVAILABLE) && attempts_ <= 3) {
      retry_at_ = now + std::chrono::milliseconds(100U << (attempts_ - 1));
      state_ = outcome::retry_wait;
    } else state_ = outcome::failed;
    return true;
  }

  bool complete_legacy(std::uint64_t ticket, int attempted_rate) noexcept {
    if (!current(ticket)) return false;
    current_ticket_ = 0;
    attempted_rate_ = attempted_rate;
    observed_applied_ = -1;
    last_result_ = WKE_INPUT_UI_UNAVAILABLE;
    state_ = outcome::legacy_unverified;
    return true;
  }

  // Call when invalidating the mounted view. Outstanding receipts stay stale.
  void invalidate() noexcept {
    key_initialized_ = false;
    current_ticket_ = 0;
    attempts_ = 0;
    attempted_rate_ = observed_applied_ = -1;
    last_result_ = WKE_INPUT_UI_UNAVAILABLE;
    state_ = outcome::unattempted;
  }

  [[nodiscard]] outcome state() const noexcept { return state_; }
  [[nodiscard]] unsigned attempts() const noexcept { return attempts_; }
  [[nodiscard]] int attempted_rate() const noexcept { return attempted_rate_; }
  [[nodiscard]] int observed_applied() const noexcept { return observed_applied_; }
  [[nodiscard]] int last_result() const noexcept { return last_result_; }

 private:
  [[nodiscard]] bool current(std::uint64_t ticket) const noexcept {
    return ticket && ticket == current_ticket_ && state_ == outcome::in_progress;
  }
  frame_policy_key key_{};
  bool key_initialized_{};
  std::uint64_t serial_{}, current_ticket_{};
  unsigned attempts_{};
  int attempted_rate_{-1}, observed_applied_{-1}, last_result_{WKE_INPUT_UI_UNAVAILABLE};
  outcome state_{outcome::unattempted};
  clock::time_point retry_at_{};
};

} // namespace soui::sdk::detail
