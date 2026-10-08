#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace soui::runtime {

enum class lifecycle_state {
  ready,
  hidden,
  reloaded,
  destroyed,
  closed,
};

enum class lifecycle_event_kind {
  state_changed,
  rejected,
};

struct lifecycle_event {
  std::uint64_t sequence{0};
  std::string session_id;
  lifecycle_state state{lifecycle_state::ready};
  lifecycle_event_kind kind{lifecycle_event_kind::state_changed};
  std::string rejected_session_id;
  std::string message;
};

class event_bus {
 public:
  using callback = std::function<void(const lifecycle_event&)>;

  class subscription {
   public:
    subscription() = default;
    subscription(const subscription&) = delete;
    subscription& operator=(const subscription&) = delete;
    subscription(subscription&& other) noexcept;
    subscription& operator=(subscription&& other) noexcept;
    ~subscription();

    void unsubscribe() noexcept;
    [[nodiscard]] bool active() const noexcept;

   private:
    friend class event_bus;

    subscription(std::weak_ptr<struct event_bus_state> state, std::uint64_t id);

    std::weak_ptr<struct event_bus_state> state_;
    std::uint64_t id_{0};
  };

  event_bus();
  event_bus(const event_bus&) = delete;
  event_bus& operator=(const event_bus&) = delete;
  ~event_bus();

  [[nodiscard]] subscription subscribe(callback listener);
  [[nodiscard]] lifecycle_event publish(lifecycle_event event);
  void destroy() noexcept;

 private:
  std::shared_ptr<struct event_bus_state> state_;
};

}  // namespace soui::runtime
