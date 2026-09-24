#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <soui/runtime/services/permissions/permission_store.h>

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

  [[nodiscard]] bool register_method(std::string method, std::string permission_name, handler callback);
  [[nodiscard]] bool register_contextual_method(std::string method, std::string permission_name,
                                                contextual_handler callback);
  [[nodiscard]] bool contains(const std::string& method) const;
  [[nodiscard]] const std::string* capability_for(const std::string& method) const;
  [[nodiscard]] const contextual_handler* handler_for(const std::string& method) const;

 private:
  struct capability {
    std::string permission;
    contextual_handler callback;
  };
  std::unordered_map<std::string, capability> methods_;
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
  [[nodiscard]] std::string handle_json(std::string_view json_text) const;

 private:
  [[nodiscard]] bridge_response validate(const bridge_request& request) const;
  [[nodiscard]] static std::string serialize(const bridge_response& response);

  capability_registry& registry_;
  permission_store& permissions_;
};

}  // namespace soui::runtime
