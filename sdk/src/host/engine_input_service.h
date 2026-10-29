#pragma once
#include <chrono>
#include <cstdint>
#include <limits>

namespace soui::sdk::detail {

// Normal ordinals are unique 1..MAX-1. At exhaustion, recovery is served
// before cleanup's reserved MAX ordinal; zero cutoff means no FIFO head.
constexpr bool input_order_precedes(std::uint64_t head, std::uint64_t cutoff) noexcept {
  return !cutoff || head < cutoff || (head == cutoff && head == (std::numeric_limits<std::uint64_t>::max)());
}

// Engine retains shared ownership until owner-thread unregister or shutdown.
// Implementations publish pending before notifying the existing Engine event.
class engine_input_service {
 public:
  virtual ~engine_input_service() = default;
  [[nodiscard]] virtual bool pending() const noexcept = 0;
  virtual void service() noexcept = 0;
  // Atomic published head only: called under Engine mutex, never take a channel lock.
  [[nodiscard]] virtual std::uint64_t oldest_order() const noexcept { return 0; }
  virtual void service_before(std::uint64_t exclusive_order) noexcept { (void)exclusive_order; service(); }
  // Private, cooperative deadline adoption. Existing providers keep their
  // service contract until they explicitly participate in the shared budget.
  virtual void service_before_until(std::uint64_t exclusive_order,
      std::chrono::steady_clock::time_point deadline) noexcept {
    (void)deadline;
    service_before(exclusive_order);
  }
  virtual void quiesce() noexcept = 0;
  virtual void close() noexcept = 0;
};

}  // namespace soui::sdk::detail
