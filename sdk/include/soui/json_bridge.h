#pragma once
#include <soui/bridge_adapter.h>
#include <soui/runtime/bridge/bridge_transport.h>
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
  std::string handle_json(std::string_view request, bool native_user_gesture = false) override;
  void shutdown_session(const std::string& session_id) override;

 private:
  std::string origin_;
  soui::runtime::permission_store permissions_;
  soui::runtime::capability_registry registry_;
  soui::runtime::bridge_transport transport_{registry_, permissions_};
  std::vector<std::string> capabilities_;
  std::mutex mutex_;
  std::string active_session_;
  bool registration_closed_{false};
};
}  // namespace soui::sdk
