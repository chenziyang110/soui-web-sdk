#pragma once

#include "../../../third_party/include/miniblink/input_ui_api.h"
#include <chrono>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace soui::sdk::detail {
struct provider_owner_callbacks final {
  decltype(wkeInputUiApi::serviceOwnerOpportunity) legacy{};
  decltype(wkeInputUiApi::serviceOwnerOpportunityUntil) bounded{};
};

inline provider_owner_callbacks select_provider_owner_callbacks(const wkeInputUiApi* api) noexcept {
  provider_owner_callbacks callbacks;
  if (!api || api->structSize < offsetof(wkeInputUiApi, capabilities) + sizeof(api->capabilities)
      || api->abiVersion != WKE_INPUT_UI_ABI_VERSION) return callbacks;
  if (api->structSize < offsetof(wkeInputUiApi, serviceOwnerOpportunity) + sizeof(api->serviceOwnerOpportunity)
      || !(api->capabilities & WKE_INPUT_UI_CAP_OWNER_OPPORTUNITY) || !api->serviceOwnerOpportunity) return callbacks;
  callbacks.legacy = api->serviceOwnerOpportunity;
  if (api->structSize >= offsetof(wkeInputUiApi, serviceOwnerOpportunityUntil) + sizeof(api->serviceOwnerOpportunityUntil)
      && (api->capabilities & WKE_INPUT_UI_CAP_OWNER_DEADLINE)) callbacks.bounded = api->serviceOwnerOpportunityUntil;
  return callbacks;
}

// Samples must be QPC first, steady second. Only the remaining duration crosses
// clock domains; using the earlier QPC sample cannot extend the original budget.
inline bool map_provider_owner_deadline(std::chrono::steady_clock::time_point deadline,
    std::chrono::steady_clock::time_point steady_after, std::uint64_t qpc_before,
    std::uint64_t qpc_frequency, std::uint64_t& qpc_deadline) noexcept {
  using clock = std::chrono::steady_clock;
  using unsigned_rep = std::make_unsigned_t<clock::rep>;
  static_assert(std::is_integral_v<clock::rep> && sizeof(clock::rep) <= sizeof(std::uint64_t));
  if (!qpc_frequency) return false;
  if (deadline <= steady_after) { qpc_deadline = qpc_before; return true; }
  // Unsigned subtraction also handles a duration spanning the signed epoch.
  const auto remaining = static_cast<std::uint64_t>(static_cast<unsigned_rep>(deadline.time_since_epoch().count())
      - static_cast<unsigned_rep>(steady_after.time_since_epoch().count()));
  constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
  constexpr auto numerator = static_cast<std::uint64_t>(clock::period::num);
  constexpr auto denominator = static_cast<std::uint64_t>(clock::period::den);
  if (qpc_frequency > maximum / numerator) return false;
  const auto scaled_frequency = qpc_frequency * numerator;
  if (remaining > maximum / scaled_frequency) return false;
  const auto ticks = remaining * scaled_frequency / denominator; // floor, never ceil.
  if (qpc_before > maximum - ticks) return false;
  qpc_deadline = qpc_before + ticks;
  return true;
}

enum class provider_owner_service_status { bounded, bridge_fallback, provider_fallback, rejected };
struct provider_owner_service_result final {
  bool work_remaining{false};
  provider_owner_service_status status{provider_owner_service_status::rejected};
};

// Sampler is injected in regressions and is the real Win32 QPC/steady pair in
// the adapter. A failed status is never interpreted as a continuation bit.
template<class Sampler>
provider_owner_service_result service_provider_owner_until(const provider_owner_callbacks& callbacks,
    std::chrono::steady_clock::time_point deadline, Sampler&& sample) {
  std::uint64_t qpc_before = 0, qpc_frequency = 0, qpc_deadline = 0;
  std::chrono::steady_clock::time_point steady_after;
  if (!callbacks.bounded || !sample(qpc_before, qpc_frequency, steady_after)
      || !map_provider_owner_deadline(deadline, steady_after, qpc_before, qpc_frequency, qpc_deadline)) {
    if (callbacks.legacy) callbacks.legacy();
    return {false, provider_owner_service_status::bridge_fallback};
  }
  unsigned int more = 0;
  const auto status = callbacks.bounded(qpc_deadline, qpc_frequency, &more);
  if (status == WKE_INPUT_UI_OK) return {more != 0, provider_owner_service_status::bounded};
  if (status == WKE_INPUT_UI_UNAVAILABLE) {
    if (callbacks.legacy) callbacks.legacy();
    return {false, provider_owner_service_status::provider_fallback};
  }
  return {false, provider_owner_service_status::rejected};
}
} // namespace soui::sdk::detail
