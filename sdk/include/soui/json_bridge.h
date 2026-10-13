#pragma once
#include <soui/bridge_adapter.h>
#include <soui/runtime/bridge/bridge_transport.h>
#include <soui/runtime/services/desktop/desktop_services.h>
#include <soui/runtime/services/task/task_service.h>
#include <soui/runtime/services/gui/gui_commands.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace soui::runtime::agent { class windows_local_endpoint; }
namespace soui::runtime::detail { class gui_commands_state; }

namespace soui::sdk {
// Register methods before mounting the host. Grants are scoped to the trusted
// local document session; navigation revokes them through shutdown_session.
// bridge.describe lists only registered methods authorized for that session.
class json_bridge final : public bridge_adapter {
 public:
  explicit json_bridge(std::string origin);
  json_bridge(std::string origin, diagnostics::mode metrics);
  [[nodiscard]] diagnostics::status read_runtime_metrics(diagnostics::bridge_runtime&) const noexcept;
  [[nodiscard]] diagnostics::status read_task_store_storage_metrics(diagnostics::task_store_storage&) const noexcept;
  ~json_bridge() override;
  // Native opt-in after all registration, before the first page bootstrap.
  // Creates one stopped, bridge-owned endpoint using this bridge's services.
  // Freezes registration/set_session_capabilities only after successful creation;
  // returns nullptr if already frozen. Allocation failure leaves registration open.
  // Borrowed pointer: never delete it; valid until this bridge is destroyed.
  // Before destruction, request_stop and owner poll_stop must reach actual stopped.
  // Task-handler contexts still outlive bridge task_service destruction/join.
  [[nodiscard]] soui::runtime::agent::windows_local_endpoint* create_local_endpoint();
  bool register_method(std::string name, std::string capability,
                       soui::runtime::capability_registry::handler handler);
  bool register_contextual_method(std::string name, std::string capability,
                                  soui::runtime::capability_registry::contextual_handler handler);
  bool register_action(std::string name, std::string capability, std::string contract_json,
                       soui::runtime::capability_registry::contextual_handler handler);
  // Opt in before bootstrap. Only existing typed actions can be exposed;
  // ordinary built-in tasks remain unavailable to local callers.
  bool allow_local_action(const std::string& name);
  // Native-only document grant, configured before bootstrap and frozen at the
  // first bootstrap or endpoint creation. Every listed capability must already
  // be registered. An empty list denies ordinary calls; omission keeps legacy grants.
  bool set_session_capabilities(std::vector<std::string> capabilities);
  bool register_task_handler(std::string kind, soui::runtime::task_service::handler handler);
  bool register_local_task_family(soui::runtime::local_task_family_definition definition);
  // Explicit optional native table, before endpoint creation/first bootstrap.
  // Controls are page-only; rows and native effects need independent local grants.
  bool enable_gui_commands(std::vector<soui::runtime::gui_command_definition> rows);
  // Owner only; current must check actual Host ready + receipt/digest. One binding
  // per bridge; revocation never reopens it. No page boolean grants readiness.
  bool bind_gui_document(std::string native_generation, std::function<bool()> current);
  [[nodiscard]] soui::runtime::gui_owner_result poll_gui_commands();
  // Foreign-safe admission close. Owner must poll to closed before Host teardown.
  void close_gui_commands() noexcept;
  bool enable_directory_service();
  bool enable_clipboard_service();
  bool enable_window_service();
  // Opt in before bootstrap; the Web SDK renews without creating a new session.
  bool enable_session_renewal();
  std::string handle_json(std::string_view request, bool native_user_gesture = false) override;
  void shutdown_session(const std::string& session_id) override;
  void set_event_sink(event_sink sink) override;
  void set_owner_window(void* owner) noexcept override { owner_window_.store(owner); }

 private:
  std::string origin_;
  const diagnostics::mode metrics_mode_;
  soui::runtime::permission_store permissions_;
  soui::runtime::directory_picker directory_{permissions_};
  soui::runtime::clipboard_service clipboard_{permissions_};
  soui::runtime::window_service windows_{permissions_};
  soui::runtime::capability_registry registry_;
  soui::runtime::bridge_transport transport_{registry_, permissions_};
  // Outlives actual task_service destructor/join. Closed before either is destroyed.
  std::shared_ptr<soui::runtime::detail::gui_commands_state> gui_;
  std::shared_ptr<const event_sink> event_sink_;
  soui::runtime::task_service tasks_;
  std::vector<std::string> capabilities_;
  std::vector<std::string> session_capabilities_;
  mutable std::mutex mutex_;
  std::string active_session_;
  bool registration_closed_{false};
  bool session_capabilities_configured_{false};
  std::atomic<void*> owner_window_{nullptr};
  // Declared last: endpoint destruction/fail-stop precedes every borrowed service.
  std::unique_ptr<soui::runtime::agent::windows_local_endpoint> local_endpoint_;
};
}  // namespace soui::sdk
