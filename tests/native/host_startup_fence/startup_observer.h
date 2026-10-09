#pragma once
#ifndef SOUI_STARTUP_FENCE_TEST_OBSERVER
#error This fixed read-only observation is available only in the private startup regression build.
#endif
#include <array>
#include <cstddef>
#include <cstdint>

namespace soui::sdk::detail::startup_fence_test {
inline constexpr std::size_t event_capacity = 256;
struct snapshot final {
  bool found{false}, capacity_exceeded{false}, clock_valid{true}, fence_confirmed{false};
  std::uint64_t fence_begin_qpc{0}, fence_confirmed_qpc{0}, engine_request_count{0};
  unsigned status{0}, closing{0}, ole_registered{0}, queued{0}, running{0}, scopes{0}, callbacks{0};
  std::array<std::uint64_t, event_capacity> engine_request_qpc{};
};
// Read a fixed snapshot while this actual adapter is still retained by its Host.
// No setter, callback, owner transfer, instruction, gate or JS operation is exposed.
[[nodiscard]] snapshot read(const void* retained_adapter) noexcept;
} // namespace soui::sdk::detail::startup_fence_test
