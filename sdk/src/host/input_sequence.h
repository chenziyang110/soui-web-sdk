#pragma once
#include <atomic>
#include <cstdint>
#include <limits>
namespace soui::sdk::detail {
// Zero means explicit sequence failure. Neither exhaustion nor contention can
// wrap/reuse an identity; the caller seals input until a new document/view.
inline std::uint64_t claim_input_sequence(std::atomic<std::uint64_t>& sequence) noexcept {
  auto value = sequence.load(std::memory_order_relaxed);
  for (unsigned attempt = 0; attempt < 8; ++attempt) {
    if (value == (std::numeric_limits<std::uint64_t>::max)()) return 0;
    if (sequence.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) return value + 1;
  }
  return 0;
}
} // namespace soui::sdk::detail
