#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <soui/runtime/services/permissions/permission_store.h>
#include <soui/runtime/bridge/action_contract.h>

namespace soui::runtime {

struct bridge_request {
  std::string request_id;
  std::string session_id;
  std::string origin;
  std::string token;
  std::string method;
  std::string params_json;
  std::string operation_id;
  std::uint32_t timeout_ms{10000};
  // Set only by the native host at the JS binding; never parsed from JSON.
  bool native_user_gesture{false};
  // Native-only credential result; transport always overwrites supplied values.
  std::shared_ptr<const authorization_scope> authorization;
};

struct bridge_response {
  bool ok{false};
  std::string request_id;
  std::string session_id;
  std::string correlation_id;
  std::string operation_id;
  std::string error_code;
  std::string error_message;
  std::string payload_json;
};

class capability_registry {
 public:
  using handler = std::function<std::string(std::string_view)>;
  using contextual_handler = std::function<std::string(const bridge_request&)>;

  // Method IDs use 1-128 ASCII letters/digits or .-_:; capability names are
  // nonempty valid UTF-8, bounded to 512 bytes for capability discovery.
  [[nodiscard]] bool register_method(std::string method, std::string permission_name, handler callback);
  [[nodiscard]] bool register_contextual_method(std::string method, std::string permission_name,
                                                contextual_handler callback);
  // Actions retain the same authorization boundary as ordinary methods. Their
  // bounded contract is advertised by discovery and enforced before/after call.
  [[nodiscard]] bool register_action(std::string method, std::string permission_name,
                                     std::string contract_json, contextual_handler callback);
  // Native registration-time opt-in for an existing typed action. This does
  // not certify business data as nonsensitive or grant any connection access.
  [[nodiscard]] bool allow_local_action(const std::string& method);
  [[nodiscard]] bool contains(const std::string& method) const;
  [[nodiscard]] const std::string* capability_for(const std::string& method) const;
  [[nodiscard]] const contextual_handler* handler_for(const std::string& method) const;

 private:
  friend class bridge_transport;
  friend class task_service;
  struct capability {
    std::string permission;
    std::shared_ptr<const contextual_handler> callback;
    std::shared_ptr<const action_contract> action;
    bool local_enabled{false};
    bool local_only{false};
  };
  struct local_action_registration {
    std::string method, permission, contract;
    contextual_handler callback;
  };
  struct prepared_local_actions {
    explicit prepared_local_actions(capability_registry& registry) : owner{registry} { owner.preparing_ = true; }
    ~prepared_local_actions() { methods.clear(); owner.preparing_ = false; }
    capability_registry& owner;
    std::map<std::string, capability> methods;
  };
  [[nodiscard]] std::unique_ptr<prepared_local_actions> prepare_local_actions(
      std::vector<local_action_registration> actions);
  void commit_local_actions(prepared_local_actions& prepared) noexcept;
  // Registration is frozen before bootstrap; ordered storage makes discovery
  // deterministic without copying the complete registry for every request.
  std::map<std::string, capability> methods_;
  bool preparing_{false};
};

class bridge_transport {
 public:
  static constexpr std::size_t max_message_bytes{64U * 1024U};
  static constexpr std::size_t max_params_bytes{32U * 1024U};
  static constexpr std::size_t max_string_bytes{4096U};
  static constexpr std::size_t max_json_depth{32U};

  bridge_transport(capability_registry& registry, permission_store& permissions)
      : registry_{registry}, permissions_{permissions} {}

  [[nodiscard]] bridge_response handle(const bridge_request& request) const;
  // Trusted native callers only. The bearer never belongs in local wire JSON.
  // Rewrites all credential fields and always clears native_user_gesture.
  [[nodiscard]] bridge_response handle_local(const bridge_request& request,
      const std::shared_ptr<const native_connection_binding>& binding, const std::string& bearer) const;
  [[nodiscard]] std::string handle_json(std::string_view json_text,
                                        bool native_user_gesture = false) const;
  // Returns the authorized discovery payload, without invoking listed methods.
  [[nodiscard]] std::string describe(const bridge_request& request) const;
  [[nodiscard]] std::string describe_local(const bridge_request& request,
      const std::shared_ptr<const native_connection_binding>& binding, const std::string& bearer) const;
  // Register before bootstrap; applications must also grant this capability.
  [[nodiscard]] bool enable_session_renewal();
  [[nodiscard]] static std::string serialize_lease(const permission_lease& lease);

 private:
  [[nodiscard]] static bridge_request local_request(const bridge_request& request,
      const std::shared_ptr<const native_connection_binding>& binding, const std::string& bearer);
  [[nodiscard]] bridge_response handle_impl(const bridge_request& request,
      const std::shared_ptr<const native_connection_binding>& binding, bool local) const;
  [[nodiscard]] std::string describe_impl(const bridge_request& request,
      const std::shared_ptr<const native_connection_binding>& binding, bool local) const;
  [[nodiscard]] bool local_authorized(const bridge_request& request,
      const std::shared_ptr<const native_connection_binding>& binding) const;
  [[nodiscard]] bridge_response validate(bridge_request& request,
      const std::shared_ptr<const native_connection_binding>& binding = {}, bool local = false) const;
  [[nodiscard]] static std::string serialize(const bridge_response& response);

  capability_registry& registry_;
  permission_store& permissions_;
};

}  // namespace soui::runtime
