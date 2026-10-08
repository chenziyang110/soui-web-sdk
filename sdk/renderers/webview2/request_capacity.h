#pragma once

#include <json.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>

namespace soui::sdk::detail::webview2_capacity {

enum class lane : std::size_t { ordinary, cleanup, renewal };
inline constexpr std::array<std::size_t, 3> limits{128, 128, 1};
inline constexpr std::size_t message_limit = 64U * 1024U;
constexpr std::size_t index(lane value) noexcept { return static_cast<std::size_t>(value); }

inline bool bounded_string(const nlohmann::json& packet, const char* key, std::size_t maximum) {
  const auto value = packet.find(key);
  if (value == packet.end() || !value->is_string()) return false;
  const auto& text = value->get_ref<const std::string&>();
  return !text.empty() && text.size() <= maximum;
}

// Admission priority is not authorization. Only these bounded actual methods
// qualify; the Bridge must still authenticate and authorize every invocation.
inline lane classify(const nlohmann::json& packet) {
  if (!packet.is_object() || !bounded_string(packet, "sessionId", 256) ||
      !bounded_string(packet, "origin", 1024) || !bounded_string(packet, "token", 512) ||
      !bounded_string(packet, "method", 128)) return lane::ordinary;
  const auto params = packet.find("params");
  if (params == packet.end() || !params->is_object() || params->size() != 1) return lane::ordinary;
  const auto& method = packet.at("method").get_ref<const std::string&>();
  if (method == "bridge.session.renew" && bounded_string(*params, "operationId", 128)) {
    const auto& operation = params->at("operationId").get_ref<const std::string&>();
    if (std::all_of(operation.begin(), operation.end(), [](unsigned char character) {
      return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
          (character >= '0' && character <= '9') || character == '-' || character == '_' ||
          character == '.' || character == ':';
    })) return lane::renewal;
  }
  if ((method == "task.cancel" && bounded_string(*params, "taskId", 256)) ||
      (method == "task.unsubscribe" && bounded_string(*params, "subscriptionId", 256))) return lane::cleanup;
  return lane::ordinary;
}

class request_budget final {
  struct state {
    std::mutex mutex;
    std::unordered_set<std::string> identities;
    std::array<std::size_t, 3> used{};
  };

 public:
  enum class rejection { none, duplicate, full };
  class lease final {
   public:
    lease(std::shared_ptr<state> owner, std::string identity, lane category)
        : category(category), owner_(std::move(owner)), identity_(std::move(identity)) {}
    ~lease() { release(); }
    lease(const lease&) = delete;
    lease& operator=(const lease&) = delete;
    void release() {
      std::lock_guard<std::mutex> lock(owner_->mutex);
      if (!active_) return;
      active_ = false;
      owner_->identities.erase(identity_);
      --owner_->used[index(category)];
    }
    const lane category;

   private:
    friend class request_budget;
    std::shared_ptr<state> owner_;
    std::string identity_;
    bool active_{false};
  };

  [[nodiscard]] std::shared_ptr<lease> acquire(const std::string& id, std::uint64_t generation,
                                             lane category, rejection& rejected) {
    auto slot = std::make_shared<lease>(state_, std::to_string(generation) + ":" + id, category);
    std::lock_guard<std::mutex> lock(state_->mutex);
    rejected = rejection::none;
    if (state_->identities.count(slot->identity_)) { rejected = rejection::duplicate; return {}; }
    auto& count = state_->used[index(category)];
    if (count >= limits[index(category)]) { rejected = rejection::full; return {}; }
    state_->identities.insert(slot->identity_);
    ++count;
    slot->active_ = true;
    return slot;
  }

 private:
  std::shared_ptr<state> state_ = std::make_shared<state>();
};

// Used under the Host mutex. Events and rejected requests use ordinary output
// capacity; only an admitted native control lease can use a reserved reply lane.
class output_budget final {
 public:
  [[nodiscard]] bool reserve(lane category, std::size_t bytes) noexcept {
    const auto item = index(category), maximum = limits[item] * message_limit;
    if (used_[item] >= limits[item] || bytes > message_limit || bytes > maximum - bytes_[item]) return false;
    ++used_[item]; bytes_[item] += bytes;
    return true;
  }
  void release(lane category, std::size_t bytes) noexcept {
    --used_[index(category)]; bytes_[index(category)] -= bytes;
  }

 private:
  std::array<std::size_t, 3> used_{}, bytes_{};
};

class priority_policy final {
 public:
  [[nodiscard]] lane choose(const std::array<bool, 3>& ready) noexcept {
    if (ready[index(lane::renewal)]) return lane::renewal;
    if (ready[index(lane::cleanup)] && (!ready[index(lane::ordinary)] || cleanup_burst_ < 4)) {
      cleanup_burst_ = std::min(4U, cleanup_burst_ + 1);
      return lane::cleanup;
    }
    cleanup_burst_ = 0;
    return lane::ordinary;
  }

 private:
  unsigned cleanup_burst_{};
};

template<class Queue, class Category>
auto next(Queue& queue, priority_policy& policy, Category category) {
  std::array<bool, 3> ready{};
  for (const auto& item : queue) ready[index(category(item))] = true;
  const auto chosen = policy.choose(ready);
  return std::find_if(queue.begin(), queue.end(), [&](const auto& item) { return category(item) == chosen; });
}

template<class Queue, class Category, class Barrier>
auto next_with_barrier(Queue& queue, priority_policy& policy, Category category, Barrier barrier) {
  if (!queue.empty() && barrier(queue.front())) return queue.begin();
  return next(queue, policy, category);
}

}  // namespace soui::sdk::detail::webview2_capacity
