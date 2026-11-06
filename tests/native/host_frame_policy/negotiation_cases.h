#pragma once
#include "../../../sdk/src/host/frame_policy_negotiation.h"
#include "../../../sdk/include/soui/host/frame_rate_policy.h"
#include <cstring>

namespace negotiation_fixture {
using namespace soui::sdk::detail;
inline void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
inline int WKE_INPUT_UI_CALL query(void*, wkeInputUiFramePolicy*) { return WKE_INPUT_UI_OK; }
inline int WKE_INPUT_UI_CALL apply(void*, unsigned, wkeInputUiFramePolicy*) { return WKE_INPUT_UI_OK; }
inline wkeInputUiFramePolicy snapshot(unsigned rate = 0) {
  return {sizeof(wkeInputUiFramePolicy), WKE_INPUT_UI_FRAME_POLICY_ABI_VERSION, 1, 30, 240, rate, {0, 0}};
}
inline void run() {
  static_assert(sizeof(wkeInputUiFramePolicy) == 32, "frame policy must have fixed cross-architecture layout");
  static_assert(offsetof(wkeInputUiApi, queryFramePolicy)
      == offsetof(wkeInputUiApi, serviceOwnerOpportunityUntil) + sizeof(wkeInputUiApi::serviceOwnerOpportunityUntil));
  static_assert(offsetof(wkeInputUiApi, applyFramePolicy)
      == offsetof(wkeInputUiApi, queryFramePolicy) + sizeof(wkeInputUiApi::queryFramePolicy));
  wkeInputUiApi api{};
  api.structSize = sizeof(api); api.abiVersion = WKE_INPUT_UI_ABI_VERSION;
  api.capabilities = WKE_INPUT_UI_CAP_FRAME_POLICY_STATE;
  api.queryFramePolicy = query; api.applyFramePolicy = apply;
  require(frame_policy_api_available(&api), "complete provider accepted");
  require(!frame_policy_api_available(nullptr), "absent provider rejected");
  const unsigned short_sizes[] = {0, 16, static_cast<unsigned>(offsetof(wkeInputUiApi, queryFramePolicy)),
      static_cast<unsigned>(offsetof(wkeInputUiApi, applyFramePolicy))};
  for (auto size : short_sizes) { api.structSize = size; require(!frame_policy_api_available(&api), "legacy prefix is not new capability"); }
  api.structSize = sizeof(api); api.abiVersion = 2;
  require(!frame_policy_api_available(&api), "wrong provider ABI rejected");
  api.abiVersion = 1; api.capabilities = 0;
  require(!frame_policy_api_available(&api), "pointer without capability rejected");
  api.capabilities = WKE_INPUT_UI_CAP_FRAME_POLICY_STATE; api.applyFramePolicy = nullptr;
  require(!frame_policy_api_available(&api), "missing apply rejected");
  api.applyFramePolicy = apply; api.queryFramePolicy = nullptr;
  require(!frame_policy_api_available(&api), "missing query rejected");
  auto value = snapshot();
  require(valid_frame_policy_snapshot(value), "disabled actual policy valid");
  value.appliedRate = 144; require(valid_frame_policy_snapshot(value), "exact applied rate valid");
  value.appliedRate = 360; require(!valid_frame_policy_snapshot(value), "out-of-range claimed actual invalid");
  value = snapshot(); value.policyVersion = 2; require(!valid_frame_policy_snapshot(value), "unknown policy rejected");
  value = snapshot(); value.reserved[1] = 1; require(!valid_frame_policy_snapshot(value), "unknown reserved flags rejected");
  value = snapshot(); value.minRate = 241; require(!valid_frame_policy_snapshot(value), "inverted limits rejected");

  const int rates[] = {60, 120, 144, 240, 360, 1000};
  for (int rate : rates) {
    const auto result = select_frame_policy_rate(rate, 1000, rate, 30, 240);
    require(result.valid && result.effective == (std::min)(rate, 240), "backend caps accepted high refresh");
    require(result.clamped == (rate > 240), "clamping reported separately");
    require(select_frame_policy_rate(rate, 144, 0, 30, 240).effective == 0, "main zero never becomes active");
  }
  require(select_frame_policy_rate(360, 144, 360, 30, 240).effective == 144, "display participates in managed minimum");
  require(select_frame_policy_rate(360, 0, 360, 30, 240).effective == 60, "unknown display retains 60");
  require(!select_frame_policy_rate(29, 144, 29, 30, 240).valid, "invalid public request rejected");
  require(!select_frame_policy_rate(360, 60, 360, 120, 240).valid, "no supported intersection rejected");
  require(soui::sdk::clamp_frame_rate_to_limits(360, 360, 240) == 240, "consumer shares acknowledged backend limit");
  require(soui::sdk::clamp_frame_rate_to_limits(360, 144, 240) == 144, "consumer retains display limit");
  require(soui::sdk::clamp_frame_rate_to_limits(360, 0, 240) == 60, "consumer unknown display stays conservative");
  require(soui::sdk::clamp_frame_rate_to_limits(360, 360, 0) == 360, "legacy zero clears backend constraint");
  require(soui::sdk::clamp_frame_rate_to_limits(0, 144, 240) == 0
      && soui::sdk::clamp_frame_rate_to_limits(1001, 144, 240) == 0
      && soui::sdk::clamp_frame_rate_to_limits(360, 360, 29) == 0, "invalid consumer rates stay invalid");

  using attempts = frame_policy_attempts;
  const auto start = attempts::clock::time_point{};
  frame_policy_key key{1, 99, WKE_INPUT_UI_CAP_FRAME_POLICY_STATE, sizeof(wkeInputUiApi), 1, 1, 360, 360, 360};
  attempts state;
  auto receipt = state.begin(key, start);
  require(receipt && !state.begin(key, start), "one in-progress receipt");
  require(state.complete(receipt, WKE_INPUT_UI_OK, 240, 240, start), "acknowledged apply completes");
  require(state.state() == attempts::outcome::applied && state.observed_applied() == 240
      && !state.begin(key, start + std::chrono::hours(1)), "same successful policy never reapplied");

  // Model a provider operation that takes longer than the first 100 ms
  // backoff. The retry interval starts when the failed operation completes,
  // not when the owner pump began it.
  const int transient_statuses[] = {WKE_INPUT_UI_BUSY, WKE_INPUT_UI_UNAVAILABLE};
  for (const int transient_status : transient_statuses) {
    attempts recovered;
    const auto failed_ticket = recovered.begin(key, start);
    const auto provider_completed = start + std::chrono::milliseconds(150);
    require(failed_ticket && recovered.complete(failed_ticket, transient_status, 240, -1, provider_completed),
        "slow transient provider failure is recorded at completion");
    require(!recovered.begin(key, provider_completed + std::chrono::milliseconds(99)),
        "slow BUSY/UNAVAILABLE still observes the full post-completion backoff");
    const auto retry_ticket = recovered.begin(key, provider_completed + std::chrono::milliseconds(100));
    require(retry_ticket != 0, "transient provider failure retries at the completion-based deadline");
    const auto retry_completed = provider_completed + std::chrono::milliseconds(250);
    require(recovered.complete(retry_ticket, WKE_INPUT_UI_OK, 240, 240, retry_completed)
        && recovered.attempts() == 2 && recovered.state() == attempts::outcome::applied
        && recovered.observed_applied() == 240
        && !recovered.begin(key, retry_completed + std::chrono::hours(1)),
        "a successful transient retry becomes applied and is deduplicated");
  }

  key.display_limit = 144;
  const auto next = state.begin(key, start);
  require(next && next != receipt && state.observed_applied() == 240, "new target preserves old actual until confirmation");
  require(!state.complete(receipt, WKE_INPUT_UI_OK, 60, 60, start), "late old completion discarded");
  require(state.complete(next, WKE_INPUT_UI_REJECTED, 144, 144, start), "rejected apply may supply truthful readback");
  require(state.observed_applied() == 144 && state.state() == attempts::outcome::failed
      && !state.begin(key, start + std::chrono::hours(1)), "readback is not apply success or retry license");
  ++key.view_generation;
  const auto stale_generation_receipt = state.begin(key, start);
  require(stale_generation_receipt && state.observed_applied() == -1, "new view clears old actual");
  ++key.view_generation;
  receipt = state.begin(key, start + std::chrono::milliseconds(1));
  require(receipt && receipt != stale_generation_receipt && state.observed_applied() == -1,
      "a newer view generation replaces the pending ticket");
  require(!state.complete(stale_generation_receipt, WKE_INPUT_UI_OK, 144, 144, start + std::chrono::milliseconds(2))
      && state.state() == attempts::outcome::in_progress && state.observed_applied() == -1,
      "late completion from an older view generation cannot publish actual state");
  auto at = start;
  const unsigned backoff[] = {100, 200, 400};
  for (unsigned delay : backoff) {
    require(state.complete(receipt, WKE_INPUT_UI_BUSY, -1, -1, at), "transient query failure recorded");
    at += std::chrono::milliseconds(delay);
    require(!state.begin(key, at - std::chrono::milliseconds(1)), "no per-pulse retry before deadline");
    receipt = state.begin(key, at);
    require(receipt != 0, "existing owner opportunity performs bounded retry");
  }
  require(state.complete(receipt, WKE_INPUT_UI_UNAVAILABLE, -1, -1, at), "final transient failure recorded");
  require(state.attempts() == 4 && state.state() == attempts::outcome::failed
      && !state.begin(key, at + std::chrono::hours(1)), "retry exhaustion is permanent for identity");
  key.capabilities |= WKE_INPUT_UI_CAP_FRACTIONAL_FRAME_POLICY;
  receipt = state.begin(key, at);
  require(receipt && state.attempts() == 1, "capability change permits fresh negotiation");
  require(state.complete_legacy(receipt, 144) && state.observed_applied() == -1
      && !state.begin(key, at + std::chrono::hours(1)), "legacy attempts dedup without invented success");
  ++key.mode;
  receipt = state.begin(key, at);
  require(receipt && state.complete(receipt, WKE_INPUT_UI_OK, 144, 120, at)
      && state.state() == attempts::outcome::failed, "OK with mismatched actual cannot claim configured");
  ++key.requested;
  receipt = state.begin(key, at);
  state.invalidate();
  require(!state.complete(receipt, WKE_INPUT_UI_OK, 144, 144, at) && state.observed_applied() == -1,
      "close invalidates pending receipt and state");
}
} // namespace negotiation_fixture
