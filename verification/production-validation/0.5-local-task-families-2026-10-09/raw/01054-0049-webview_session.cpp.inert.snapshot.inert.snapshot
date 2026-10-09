#include <soui/runtime/host/session/webview_session.h>

#include <atomic>
#include <iomanip>
#include <sstream>
#include <utility>

namespace soui::runtime {
namespace {

std::atomic<std::uint64_t> next_session_number{1};

}  // namespace

webview_session::webview_session(std::shared_ptr<event_bus> bus)
    : bus_{std::move(bus)}, session_id_{make_session_id(next_session_number.fetch_add(1))} {
  if (!bus_) {
    bus_ = std::make_shared<event_bus>();
  }
}

webview_session::~webview_session() {
  if (!terminal()) {
    (void)destroy();
  }
}

const std::string& webview_session::session_id() const noexcept {
  return session_id_;
}

lifecycle_state webview_session::state() const noexcept {
  return state_;
}

bool webview_session::terminal() const noexcept {
  return is_terminal(state_);
}

webview_session_snapshot webview_session::snapshot() const {
  return webview_session_snapshot{session_id_, state_, terminal()};
}

event_bus& webview_session::events() const noexcept {
  return *bus_;
}

std::optional<lifecycle_event> webview_session::mark_ready() {
  return transition_to(lifecycle_state::ready);
}

std::optional<lifecycle_event> webview_session::hide() {
  return transition_to(lifecycle_state::hidden, "hidden without cancellation");
}

std::optional<lifecycle_event> webview_session::reload() {
  if (terminal()) return std::nullopt;
  bus_->destroy();
  bus_ = std::make_shared<event_bus>();
  session_id_ = make_session_id(next_session_number.fetch_add(1));
  state_ = lifecycle_state::reloaded;
  return bus_->publish(lifecycle_event{0, session_id_, state_, lifecycle_event_kind::state_changed,
                                       {}, "new session created after reload"});
}

std::optional<lifecycle_event> webview_session::close() {
  return transition_to(lifecycle_state::closed);
}

std::optional<lifecycle_event> webview_session::destroy() {
  const auto event = transition_to(lifecycle_state::destroyed);
  bus_->destroy();
  return event;
}

bool webview_session::accept_host_state(std::string observed_session_id, lifecycle_state next_state) {
  if (observed_session_id != session_id_) {
    (void)reject_old_session(std::move(observed_session_id), next_state);
    return false;
  }

  if (next_state == lifecycle_state::reloaded) return reload().has_value();
  return transition_to(next_state).has_value();
}

std::string webview_session::make_session_id(std::uint64_t number) {
  std::ostringstream stream;
  stream << "session-" << std::setw(6) << std::setfill('0') << number;
  return stream.str();
}

std::optional<lifecycle_event> webview_session::transition_to(lifecycle_state next_state, std::string message) {
  if (terminal()) {
    return std::nullopt;
  }

  state_ = next_state;
  return bus_->publish(lifecycle_event{0, session_id_, state_, lifecycle_event_kind::state_changed, {}, std::move(message)});
}

lifecycle_event webview_session::reject_old_session(std::string observed_session_id, lifecycle_state rejected_state) {
  return bus_->publish(lifecycle_event{0,
                                       session_id_,
                                       rejected_state,
                                       lifecycle_event_kind::rejected,
                                       std::move(observed_session_id),
                                       "event rejected for stale session"});
}

bool webview_session::is_terminal(lifecycle_state state) noexcept {
  return state == lifecycle_state::destroyed || state == lifecycle_state::closed;
}

}  // namespace soui::runtime
