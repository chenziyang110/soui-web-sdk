#pragma once

#include <soui/devtools.h>
#include <limits>

namespace soui::sdk::detail {
// Only aggregates copied native counters. No callout, permission or drain action.
[[nodiscard]] inline bool publish_callback_observation(devtools::observation& output,
    const devtools::callback_observation& counts) noexcept {
  const auto pending = static_cast<std::uint64_t>(counts.interrupt_pending) +
      counts.protocol_queued + counts.resource_queued;
  const auto active = static_cast<std::uint64_t>(counts.dispatch_active) +
      counts.resource_active + counts.frontend_paint_active;
  constexpr auto limit = (std::numeric_limits<std::uint32_t>::max)();
  if (!counts.sampled || counts.domain != devtools::callback_domain::miniblink_session ||
      pending > limit || active > limit) return false;
  output.callbacks = counts;
  output.pending_callbacks = static_cast<std::uint32_t>(pending);
  output.active_callbacks = static_cast<std::uint32_t>(active);
  return true;
}
} // namespace soui::sdk::detail
