#pragma once
#include <soui/bridge_adapter.h>
#include <soui/runtime/bridge/bridge_transport.h>
#include <soui/runtime/services/desktop/desktop_services.h>
#include <soui/runtime/services/task/task_service.h>
#include <atomic>
#include <mutex>
#include <vector>

namespace soui::sdk {
// Register methods before mounting the host. Grants are scoped to the trusted
// local document session; navigation revokes them through shutdown_session.
class json_bridge final : public bridge_adapter {
 public:
  explicit json_bridge(std::string origin);
  bool register_method(std::string name, std::string capability,
                       soui::runtime::capability_registry::handler handler);
  bool register_contextual_method(std::string name, std::string capability,
                                  soui::runtime::capability_registry::contextual_handler handler);
  bool register_task_handler(std::string kind, soui::runtime::task_service::handler handler);
  bool enable_clipboard_service();
  bool enable_window_service();
  std::string handle_json(std::string_view request, bool native_user_gesture = false) override;
  void shutdown_session(const std::string& session_id) override;
  void set_event_sink(event_sink sink) override;
  void set_owner_window(void* owner) noexcept override { owner_window_.store(owner); }

 private:
  std::string origin_;
  soui::runtime::permission_store permissions_;
  soui::runtime::clipboard_service clipboard_{permissions_};
  soui::runtime::window_service windows_{permissions_};
  soui::runtime::capability_registry registry_;
  soui::runtime::bridge_transport transport_{registry_, permissions_};
  soui::runtime::task_service tasks_;
  std::vector<std::string> capabilities_;
  std::mutex mutex_;
  std::string active_session_;
  bool registration_closed_{false};
  std::atomic<void*> owner_window_{nullptr};
};
}  // namespace soui::sdk
