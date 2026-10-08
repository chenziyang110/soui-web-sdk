#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>

namespace soui::sdk::detail {

// A lease spans both queues and the executing handler. Dropping a queued
// callback during shutdown releases its lease without touching the Host.
class bridge_request_budget {
  struct state {
    std::mutex mutex;
    std::unordered_set<std::string> identities;
    std::size_t ordinary{0};
    std::size_t control{0};
  };

 public:
  static constexpr std::size_t ordinary_limit = 256;
  static constexpr std::size_t control_limit = 128;
  enum class rejection { none, duplicate, full };

  struct lease {
    lease(std::shared_ptr<state> owner, std::string identity, bool is_control)
        : owner(std::move(owner)), identity(std::move(identity)), control(is_control) {}
    ~lease() { release(); }
    void release() {
      std::lock_guard<std::mutex> lock(owner->mutex);
      if (!active) return;
      active = false;
      owner->identities.erase(identity);
      --(control ? owner->control : owner->ordinary);
    }
    lease(const lease&) = delete;
    lease& operator=(const lease&) = delete;
    std::shared_ptr<state> owner;
    std::string identity;
    bool control;
    bool active{false};
  };

  std::shared_ptr<lease> acquire(const std::string& id, std::uint64_t generation,
                                 bool control, rejection& error) {
    auto result = std::make_shared<lease>(state_, std::to_string(generation) + ":" + id, control);
    std::lock_guard<std::mutex> lock(state_->mutex);
    error = rejection::none;
    if (state_->identities.count(result->identity)) { error = rejection::duplicate; return {}; }
    auto& count = control ? state_->control : state_->ordinary;
    if (count >= (control ? control_limit : ordinary_limit)) { error = rejection::full; return {}; }
    state_->identities.insert(result->identity);
    ++count;
    result->active = true;
    return result;
  }

 private:
  std::shared_ptr<state> state_ = std::make_shared<state>();
};

}  // namespace soui::sdk::detail
