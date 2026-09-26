#include <soui/runtime/bridge/bridge_transport.h>

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

#include "../../../../third_party/include/json/json.hpp"

namespace soui::runtime {
namespace {

using json = nlohmann::json;

bridge_response make_error(std::string request_id, std::string session_id, std::string operation_id,
                           std::string code, std::string message) {
  bridge_response response;
  response.request_id = std::move(request_id);
  response.correlation_id = response.request_id;
  response.session_id = std::move(session_id);
  response.operation_id = std::move(operation_id);
  response.error_code = std::move(code);
  response.error_message = std::move(message);
  return response;
}

std::size_t json_depth(const json& value) {
  if (!value.is_structured()) return 1;
  std::size_t depth = 1;
  if (value.is_array()) {
    for (const auto& item : value) depth = std::max(depth, 1 + json_depth(item));
  } else if (value.is_object()) {
    for (const auto& item : value.items()) depth = std::max(depth, 1 + json_depth(item.value()));
  }
  return depth;
}

bool valid_id(std::string_view value) {
  if (value.empty() || value.size() > 128U) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == ':';
  });
}

bool valid_origin(std::string_view value) {
  if (value.empty() || value.size() > 512U) return false;
  return value.rfind("app://", 0) == 0 || value.rfind("file://", 0) == 0;
}

}  // namespace

bool capability_registry::register_method(std::string method, std::string permission_name, handler callback) {
  if (!callback) return false;
  return register_contextual_method(std::move(method), std::move(permission_name),
      [callback = std::move(callback)](const bridge_request& request) {
        return callback(request.params_json);
      });
}

bool capability_registry::register_contextual_method(std::string method, std::string permission_name,
                                                       contextual_handler callback) {
  if (method.empty() || permission_name.empty() || !callback || methods_.find(method) != methods_.end()) {
    return false;
  }
  methods_.emplace(std::move(method), capability{std::move(permission_name), std::move(callback)});
  return true;
}

bool capability_registry::contains(const std::string& method) const {
  return methods_.find(method) != methods_.end();
}

const std::string* capability_registry::capability_for(const std::string& method) const {
  const auto found = methods_.find(method);
  return found == methods_.end() ? nullptr : &found->second.permission;
}

const capability_registry::contextual_handler* capability_registry::handler_for(const std::string& method) const {
  const auto found = methods_.find(method);
  return found == methods_.end() ? nullptr : &found->second.callback;
}

bridge_response bridge_transport::handle(const bridge_request& request) const {
  bridge_request authorized_request = request;
  authorized_request.authorization.reset();
  bridge_response response = validate(authorized_request);
  if (!response.error_code.empty()) {
    return response;
  }

  const auto* callback = registry_.handler_for(request.method);
  if (callback == nullptr) {
    response.error_code = "E_UNIMPLEMENTED";
    response.error_message = "Registered method has no implementation.";
    return response;
  }
  response.ok = true;
  response.payload_json = (*callback)(authorized_request);
  try {
    const auto payload = response.payload_json.empty() ? json::object() : json::parse(response.payload_json);
    const auto error = payload.find("error");
    if (payload.is_object() && error != payload.end() && error->is_string()) {
      response.ok = false;
      response.error_code = error->get<std::string>();
      const auto message = payload.find("message");
      response.error_message = message != payload.end() && message->is_string()
                                   ? message->get<std::string>()
                                   : "Native handler rejected the request.";
      response.payload_json.clear();
    }
  } catch (const json::parse_error&) {
    response.ok = false;
    response.error_code = "E_SCHEMA_INVALID";
    response.error_message = "Native handler returned invalid JSON.";
    response.payload_json.clear();
  }
  return response;
}

std::string bridge_transport::handle_json(std::string_view json_text,
                                          bool native_user_gesture) const {
  if (json_text.empty() || json_text.size() > max_message_bytes) {
    return serialize(make_error({}, {}, {}, "E_LIMIT_MESSAGE_SIZE", "Bridge message exceeds the allowed size."));
  }

  json envelope;
  try {
    envelope = json::parse(json_text.begin(), json_text.end());
  } catch (const json::parse_error&) {
    return serialize(make_error({}, {}, {}, "E_MALFORMED_JSON", "Bridge message is not valid JSON."));
  }

  if (!envelope.is_object() || json_depth(envelope) > max_json_depth) {
    return serialize(make_error({}, {}, {}, "E_SCHEMA_INVALID", "Bridge envelope shape is invalid."));
  }

  static const std::set<std::string> allowed = {
      "kind", "schemaVersion", "bridgeVersion", "requestId", "correlationId", "sessionId",
      "origin", "token", "method", "operationId", "params", "timeoutMs"};
  for (const auto& item : envelope.items()) {
    if (allowed.find(item.key()) == allowed.end()) {
      return serialize(make_error({}, {}, {}, "E_SCHEMA_INVALID", "Bridge envelope contains unknown fields."));
    }
  }

  const auto string_value = [&envelope](const char* key) -> std::string {
    const auto found = envelope.find(key);
    return found != envelope.end() && found->is_string() ? found->get<std::string>() : std::string{};
  };
  const auto kind = string_value("kind");
  const auto schema_version = string_value("schemaVersion");
  if ((!kind.empty() && kind != "request") ||
      (!schema_version.empty() && schema_version != "soui.bridge/1")) {
    return serialize(make_error({}, {}, {}, "E_SCHEMA_VERSION_UNSUPPORTED",
                                "Bridge request kind/schemaVersion is not supported."));
  }

  bridge_request request;
  request.request_id = string_value("requestId");
  if (request.request_id.empty()) request.request_id = string_value("correlationId");
  request.session_id = string_value("sessionId");
  request.origin = string_value("origin");
  request.token = string_value("token");
  request.method = string_value("method");
  request.operation_id = string_value("operationId");
  request.native_user_gesture = native_user_gesture;
  const auto timeout = envelope.find("timeoutMs");
  if (timeout != envelope.end()) {
    if (!timeout->is_number_unsigned() || timeout->get<std::uint64_t>() == 0U ||
        timeout->get<std::uint64_t>() > 60000U) {
      return serialize(make_error(request.request_id, request.session_id, request.operation_id,
                                  "E_INVALID_PARAMS", "timeoutMs must be between 1 and 60000."));
    }
    request.timeout_ms = static_cast<std::uint32_t>(timeout->get<std::uint64_t>());
  }
  const auto params = envelope.find("params");
  request.params_json = params == envelope.end() ? "{}" : params->dump();

  return serialize(handle(request));
}

bridge_response bridge_transport::validate(bridge_request& request) const {
  bridge_response response;
  response.request_id = request.request_id;
  response.correlation_id = request.request_id;
  response.session_id = request.session_id;
  response.operation_id = request.operation_id;

  if (!valid_id(request.request_id) || !valid_id(request.session_id) || !valid_origin(request.origin) ||
      request.token.empty() || request.token.size() > max_string_bytes || !valid_id(request.method) ||
      request.params_json.empty() || request.timeout_ms == 0U || request.timeout_ms > 60000U) {
    response.error_code = "E_INVALID_PARAMS";
    response.error_message = "Required bridge fields are missing or malformed.";
    return response;
  }
  if (request.params_json.size() > max_params_bytes) {
    response.error_code = "E_LIMIT_MESSAGE_SIZE";
    response.error_message = "Request params exceed the allowed size.";
    return response;
  }

  json params;
  try {
    params = json::parse(request.params_json);
  } catch (const json::parse_error&) {
    response.error_code = "E_MALFORMED_JSON";
    response.error_message = "Request params are not valid JSON.";
    return response;
  }
  if (!params.is_object() || json_depth(params) > max_json_depth) {
    response.error_code = "E_SCHEMA_INVALID";
    response.error_message = "Request params must be a bounded JSON object.";
    return response;
  }

  const std::string* capability = registry_.capability_for(request.method);
  if (capability == nullptr) {
    response.error_code = "E_UNKNOWN_METHOD";
    response.error_message = "Requested method is not registered.";
    return response;
  }
  const auto authorization = permissions_.resolve(request.session_id, request.origin, request.token, *capability);
  if (authorization.result != authorization_result::authorized) {
    response.error_code = authorization.result == authorization_result::expired ? "E_SESSION_EXPIRED" : "E_UNAUTHORIZED";
    response.error_message = authorization.result == authorization_result::expired
        ? "Session authorization has expired. A new authorized session is required."
        : "Session token is not authorized for this method.";
    return response;
  }
  request.authorization = authorization.scope;
  return response;
}

bool bridge_transport::enable_session_renewal() {
  return registry_.register_contextual_method("bridge.session.renew", "bridge.session.renew",
      [this](const bridge_request& request) {
        const auto params = json::parse(request.params_json, nullptr, false);
        if (!params.is_object() || params.size() != 1U || !params.contains("operationId") ||
            !params["operationId"].is_string())
          return std::string{R"({"error":"E_INVALID_PARAMS","message":"Expected only operationId."})"};
        return serialize_lease(permissions_.renew(request.session_id, request.origin, request.token,
                                                  params["operationId"].get<std::string>()));
      });
}

std::string bridge_transport::serialize_lease(const permission_lease& lease) {
  if (!lease.ok()) return json{{"error", lease.error_code}, {"message", "Session lease operation rejected."}}.dump();
  return json{{"sessionId", lease.session_id}, {"origin", lease.origin}, {"token", lease.token},
              {"lease", {{"generation", lease.generation}, {"expiresInMs", lease.expires_in_ms},
                         {"renewAfterMs", lease.renew_after_ms}}}}.dump();
}

std::string bridge_transport::serialize(const bridge_response& response) {
  json envelope;
  envelope["kind"] = "response";
  envelope["schemaVersion"] = "soui.bridge/1";
  envelope["requestId"] = response.request_id;
  envelope["correlationId"] = response.correlation_id.empty() ? response.request_id : response.correlation_id;
  envelope["sessionId"] = response.session_id;
  if (!response.operation_id.empty()) envelope["operationId"] = response.operation_id;
  envelope["ok"] = response.ok;
  if (response.ok) {
    try {
      envelope["result"] = response.payload_json.empty() ? json::object() : json::parse(response.payload_json);
    } catch (const json::parse_error&) {
      envelope["ok"] = false;
      envelope["error"] = {{"code", "E_SCHEMA_INVALID"}, {"message", "Native handler returned invalid JSON."}};
    }
  } else {
    envelope["error"] = {{"code", response.error_code.empty() ? "E_INTERNAL" : response.error_code},
                         {"message", response.error_message.empty() ? "Native bridge rejected the request."
                                                                     : response.error_message}};
  }
  return envelope.dump();
}

}  // namespace soui::runtime
