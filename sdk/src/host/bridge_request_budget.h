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
    std::size_t renewal{0};
  };

 public:
  static constexpr std::size_t ordinary_limit = 256;
  static constexpr std::size_t control_limit = 128;
  static constexpr std::size_t renewal_limit = 1;
  enum class lane { ordinary, control, renewal };
  enum class rejection { none, duplicate, full };

  struct lease {
    lease(std::shared_ptr<state> owner, std::string identity, lane category)
        : owner(std::move(owner)), identity(std::move(identity)), category(category) {}
    ~lease() { release(); }
    void release() {
      std::lock_guard<std::mutex> lock(owner->mutex);
      if (!active) return;
      active = false;
      owner->identities.erase(identity);
      --(category == lane::renewal ? owner->renewal :
          category == lane::control ? owner->control : owner->ordinary);
    }
    lease(const lease&) = delete;
    lease& operator=(const lease&) = delete;
    std::shared_ptr<state> owner;
    std::string identity;
    lane category;
    bool active{false};
  };

  std::shared_ptr<lease> acquire(const std::string& id, std::uint64_t generation,
                                 bool control, rejection& error) {
    return acquire(id, generation, control ? lane::control : lane::ordinary, error);
  }
  std::shared_ptr<lease> acquire(const std::string& id, std::uint64_t generation,
                                 lane category, rejection& error) {
    auto result = std::make_shared<lease>(state_, std::to_string(generation) + ":" + id, category);
    std::lock_guard<std::mutex> lock(state_->mutex);
    error = rejection::none;
    if (state_->identities.count(result->identity)) { error = rejection::duplicate; return {}; }
    auto& count = category == lane::renewal ? state_->renewal :
        category == lane::control ? state_->control : state_->ordinary;
    const auto limit = category == lane::renewal ? renewal_limit :
        category == lane::control ? control_limit : ordinary_limit;
    if (count >= limit) { error = rejection::full; return {}; }
    state_->identities.insert(result->identity);
    ++count;
    result->active = true;
    return result;
  }

 private:
  std::shared_ptr<state> state_ = std::make_shared<state>();
};

}  // namespace soui::sdk::detail
