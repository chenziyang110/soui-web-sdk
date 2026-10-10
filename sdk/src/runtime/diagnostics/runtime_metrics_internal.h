#pragma once
#include <soui/diagnostics/runtime_metrics.h>
#include <atomic>
#include <chrono>
#include <limits>
#include <string>

namespace soui::sdk::diagnostics::detail {
inline std::uint64_t epoch() noexcept {
  static std::atomic<std::uint64_t> next{1};
  auto value = next.load(std::memory_order_relaxed);
  while (value && value != (std::numeric_limits<std::uint64_t>::max)()) {
    if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) return value;
  }
  return 0; // Exhaustion is unavailable; never reuse an epoch.
}
inline std::uint64_t now_us() noexcept {
  const auto value = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  return value > 0 ? static_cast<std::uint64_t>(value) : 0;
}
inline void add(std::uint64_t& value, std::uint64_t amount, bool& saturated) noexcept {
  const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
  if (amount > maximum - value) { value = maximum; saturated = true; }
  else value += amount;
}
inline void increment(std::uint64_t& value, bool& saturated) noexcept { add(value, 1, saturated); }
inline void high_water(std::uint64_t& value, std::uint64_t current) noexcept {
  if (current > value) value = current;
}
inline sample begin(mode enabled, std::uint64_t owner, bool saturated = false) noexcept {
  sample result;
  if (enabled != mode::scalar) return result;
  result.state = owner ? status::ready : status::unavailable;
  result.owner_epoch = owner;
  result.begin_monotonic_us = now_us();
  result.counter_saturated = saturated;
  result.clock_invalid = result.begin_monotonic_us == 0;
  return result;
}
inline void end(sample& value) noexcept {
  if (value.state == status::disabled) return;
  value.end_monotonic_us = now_us();
  value.clock_invalid = value.clock_invalid || !value.end_monotonic_us ||
      value.end_monotonic_us < value.begin_monotonic_us;
}
inline std::uint64_t age(std::uint64_t started, std::uint64_t now, sample& value) noexcept {
  if (!started || !now || now < started) { value.clock_invalid = true; return 0; }
  return now - started;
}
inline void strings(const std::string& value, std::uint64_t& size,
                    std::uint64_t& capacity, bool& saturated) noexcept {
  add(size, value.size(), saturated); add(capacity, value.capacity(), saturated);
}
// A facade is ready only if all present required facets are ready. Absent GUI
// is optional; every other missing facet remains visible. Never infer zero.
inline status combine(status left, status right) noexcept {
  if (left == status::wrong_owner_thread || right == status::wrong_owner_thread) return status::wrong_owner_thread;
  if (left == status::unavailable || right == status::unavailable) return status::unavailable;
  if (left == status::busy || right == status::busy) return status::busy;
  if (left == status::disabled || right == status::disabled) return status::disabled;
  if (left == status::not_started || right == status::not_started) return status::not_started;
  return status::ready;
}
} // namespace soui::sdk::diagnostics::detail
