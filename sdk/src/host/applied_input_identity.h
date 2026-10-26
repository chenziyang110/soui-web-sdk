#pragma once
#include <soui/host/interaction_protocol.h>
#include <mutex>
namespace soui::sdk::detail {
// Engine publishes only three scalars under this short lock. UI never waits:
// contention or unknown identity selects the original Engine mailbox path.
struct applied_input_identity final {
  static void publish(std::mutex& mutex, interaction_generation& target, interaction_generation value) noexcept {
    std::lock_guard<std::mutex> lock(mutex); target = value;
  }
  static bool matches(std::mutex& mutex, const interaction_generation& source, interaction_generation requested) noexcept {
    std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
    return lock.owns_lock() && source.view && source.document && source.geometry && source == requested;
  }
};
} // namespace soui::sdk::detail
