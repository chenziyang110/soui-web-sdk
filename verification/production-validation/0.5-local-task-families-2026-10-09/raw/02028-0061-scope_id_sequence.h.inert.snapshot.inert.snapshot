#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace soui::runtime::detail {

// Module-local correlation IDs, not credentials. Zero permanently represents
// exhaustion, including after the final uint64 value has been allocated.
class scope_id_sequence final {
 public:
  explicit scope_id_sequence(std::uint64_t first = 1) noexcept : next_{first} {}
  [[nodiscard]] std::uint64_t allocate() {
    auto current = next_.load(std::memory_order_relaxed);
    while (current != 0) {
      const auto next = current == std::numeric_limits<std::uint64_t>::max() ? 0 : current + 1;
      if (next_.compare_exchange_weak(current, next, std::memory_order_relaxed)) return current;
    }
    throw std::overflow_error("Authorization scope identity exhausted");
  }
 private:
  std::atomic<std::uint64_t> next_;
};

}  // namespace soui::runtime::detail
