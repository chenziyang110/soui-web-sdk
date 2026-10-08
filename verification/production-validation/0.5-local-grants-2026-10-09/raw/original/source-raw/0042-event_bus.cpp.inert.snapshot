#include <soui/runtime/core/events/event_bus.h>

#include <utility>

namespace soui::runtime {

struct event_bus_state {
  std::mutex mutex;
  std::uint64_t next_subscription_id{1};
  std::uint64_t next_sequence{1};
  bool accepting_events{true};
  std::unordered_map<std::uint64_t, event_bus::callback> listeners;
};

event_bus::subscription::subscription(std::weak_ptr<event_bus_state> state, std::uint64_t id)
    : state_{std::move(state)}, id_{id} {}

event_bus::subscription::subscription(subscription&& other) noexcept
    : state_{std::move(other.state_)}, id_{other.id_} {
  other.id_ = 0;
}

event_bus::subscription& event_bus::subscription::operator=(subscription&& other) noexcept {
  if (this != &other) {
    unsubscribe();
    state_ = std::move(other.state_);
    id_ = other.id_;
    other.id_ = 0;
  }
  return *this;
}

event_bus::subscription::~subscription() {
  unsubscribe();
}

void event_bus::subscription::unsubscribe() noexcept {
  const auto state = state_.lock();
  if (!state || id_ == 0) {
    id_ = 0;
    return;
  }

  std::lock_guard<std::mutex> lock(state->mutex);
  state->listeners.erase(id_);
  id_ = 0;
}

bool event_bus::subscription::active() const noexcept {
  const auto state = state_.lock();
  if (!state || id_ == 0) {
    return false;
  }

  std::lock_guard<std::mutex> lock(state->mutex);
  return state->listeners.find(id_) != state->listeners.end();
}

event_bus::event_bus() : state_{std::make_shared<event_bus_state>()} {}

event_bus::~event_bus() {
  destroy();
}

event_bus::subscription event_bus::subscribe(callback listener) {
  if (!listener) {
    return {};
  }

  std::lock_guard<std::mutex> lock(state_->mutex);
  if (!state_->accepting_events) {
    return {};
  }

  const auto id = state_->next_subscription_id++;
  state_->listeners.emplace(id, std::move(listener));
  return subscription{state_, id};
}

lifecycle_event event_bus::publish(lifecycle_event event) {
  std::vector<callback> listeners;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    event.sequence = state_->next_sequence++;
    if (!state_->accepting_events) {
      return event;
    }

    listeners.reserve(state_->listeners.size());
    for (const auto& entry : state_->listeners) {
      listeners.push_back(entry.second);
    }
  }

  for (const auto& listener : listeners) {
    listener(event);
  }
  return event;
}

void event_bus::destroy() noexcept {
  std::lock_guard<std::mutex> lock(state_->mutex);
  state_->accepting_events = false;
  state_->listeners.clear();
}

}  // namespace soui::runtime
