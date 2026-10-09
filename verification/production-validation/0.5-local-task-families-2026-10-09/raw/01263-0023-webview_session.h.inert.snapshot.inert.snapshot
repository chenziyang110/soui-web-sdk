#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <soui/runtime/core/events/event_bus.h>

namespace soui::runtime {

struct webview_session_snapshot {
  std::string session_id;
  lifecycle_state state{lifecycle_state::ready};
  bool terminal{false};
};

class webview_session {
 public:
  explicit webview_session(std::shared_ptr<event_bus> bus = std::make_shared<event_bus>());
  webview_session(const webview_session&) = delete;
  webview_session& operator=(const webview_session&) = delete;
  webview_session(webview_session&&) noexcept = default;
  webview_session& operator=(webview_session&&) noexcept = default;
  ~webview_session();

  [[nodiscard]] const std::string& session_id() const noexcept;
  [[nodiscard]] lifecycle_state state() const noexcept;
  [[nodiscard]] bool terminal() const noexcept;
  [[nodiscard]] webview_session_snapshot snapshot() const;
  [[nodiscard]] event_bus& events() const noexcept;

  [[nodiscard]] std::optional<lifecycle_event> mark_ready();
  [[nodiscard]] std::optional<lifecycle_event> hide();
  [[nodiscard]] std::optional<lifecycle_event> reload();
  [[nodiscard]] std::optional<lifecycle_event> close();
  [[nodiscard]] std::optional<lifecycle_event> destroy();
  [[nodiscard]] bool accept_host_state(std::string observed_session_id, lifecycle_state state);

 private:
  [[nodiscard]] static std::string make_session_id(std::uint64_t number);
  [[nodiscard]] std::optional<lifecycle_event> transition_to(lifecycle_state next_state, std::string message = {});
  [[nodiscard]] lifecycle_event reject_old_session(std::string observed_session_id, lifecycle_state rejected_state);
  [[nodiscard]] static bool is_terminal(lifecycle_state state) noexcept;

  std::shared_ptr<event_bus> bus_;
  std::string session_id_;
  lifecycle_state state_{lifecycle_state::ready};
};

}  // namespace soui::runtime
