#pragma once
#include <atomic>
namespace soui::sdk::detail {
// Latest native visual intent only. DOM input and JS visibility events never
// enter this state. A dirty observation survives hide/show coalescing.
struct host_visibility_policy final {
  static void request(std::atomic<bool>& intent, std::atomic<bool>& dirty, bool visible) noexcept {
    if (intent.exchange(visible, std::memory_order_acq_rel) != visible) dirty.store(true, std::memory_order_release);
  }
  static void invalidate(std::atomic<bool>& dirty, std::atomic<int>& applied) noexcept {
    applied.store(-1, std::memory_order_release); dirty.store(true, std::memory_order_release);
  }
  static bool pending(const std::atomic<bool>& intent, const std::atomic<bool>& dirty, const std::atomic<int>& applied) noexcept {
    return dirty.load(std::memory_order_acquire) || applied.load(std::memory_order_acquire) != (intent.load(std::memory_order_acquire) ? 1 : 0);
  }
  template<class Setter>
  static bool apply_one(const std::atomic<bool>& intent, std::atomic<bool>& dirty, std::atomic<int>& applied, Setter&& setter) noexcept {
    const bool target = intent.load(std::memory_order_acquire);
    const bool refresh = dirty.exchange(false, std::memory_order_acq_rel);
    if (!refresh && applied.load(std::memory_order_acquire) == (target ? 1 : 0)) return true;
    if (!setter(target)) { invalidate(dirty, applied); return false; }
    applied.store(target ? 1 : 0, std::memory_order_release); // Only an actual OK receipt acknowledges publication.
    if (intent.load(std::memory_order_acquire) != target) dirty.store(true, std::memory_order_release);
    return !dirty.load(std::memory_order_acquire);
  }
};
} // namespace soui::sdk::detail
