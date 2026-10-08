#include <soui/runtime/bridge/bridge_transport.h>
#include <soui/version.h>

#include <algorithm>
#include <set>
#include <stdexcept>
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
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == ':';
  });
}

bool valid_origin(std::string_view value) {
  if (value.empty() || value.size() > 512U) return false;
  return value.rfind("app://", 0) == 0 || value.rfind("file://", 0) == 0;
}

json response_metadata(const bridge_response& response) {
  json envelope{{"kind", "response"}, {"schemaVersion", "soui.bridge/1"},
                {"requestId", response.request_id},
                {"correlationId", response.correlation_id.empty() ? response.request_id : response.correlation_id},
                {"sessionId", response.session_id}, {"ok", response.ok}};
  if (!response.operation_id.empty()) envelope["operationId"] = response.operation_id;
  return envelope;
}

std::string discovery_error(const char* code, const char* message) {
  return json{{"error", code}, {"message", message}}.dump();
}

bool nonnegative_integer(const json& value, std::uint64_t& result) {
  if (!value.is_number_integer()) return false;
  if (!value.is_number_unsigned() && value.get<std::int64_t>() < 0) return false;
  result = value.get<std::uint64_t>();
  return true;
}

bool local_reserved_method(const std::string& method) {
  return method == "bridge.describe" || method == "bridge.session.renew" ||
      method == "task.subscribe" || method == "task.unsubscribe";
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
  if (!valid_id(method) || permission_name.empty() || permission_name.size() > 512U ||
      !callback || methods_.find(method) != methods_.end()) {
    return false;
  }
  // Capabilities are opaque UTF-8 names, but discovery must encode every accepted
  // registration into a bounded JSON entry without replacing invalid bytes.
  try {
    (void)json(permission_name).dump();
  } catch (const json::type_error&) {
    return false;
  }
  methods_.emplace(std::move(method), capability{std::move(permission_name), std::move(callback), {}});
  return true;
}

bool capability_registry::register_action(std::string method, std::string permission_name,
                                           std::string contract_json, contextual_handler callback) {
  const auto contract = action_contract::parse(contract_json);
  if (!contract || !register_contextual_method(method, std::move(permission_name), std::move(callback))) return false;
  methods_.at(method).action = contract;
  return true;
}

bool capability_registry::allow_local_action(const std::string& method) {
  const auto found = methods_.find(method);
  if (local_reserved_method(method) || found == methods_.end() || !found->second.action) return false;
  found->second.local_enabled = true;
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

std::string bridge_transport::describe(const bridge_request& request) const {
  return describe_impl(request, {}, false);
}

bridge_request bridge_transport::local_request(const bridge_request& request,
    const std::shared_ptr<const native_connection_binding>& binding, const std::string& bearer) {
  auto trusted = request;
  trusted.session_id = binding ? binding->session_id_ : std::string{};
  trusted.origin = binding ? binding->origin_ : std::string{};
  trusted.token = bearer;
  trusted.authorization.reset();
  trusted.native_user_gesture = false;
  return trusted;
}

std::string bridge_transport::describe_local(const bridge_request& request,
    const std::shared_ptr<const native_connection_binding>& binding, const std::string& bearer) const {
  return describe_impl(local_request(request, binding, bearer), binding, true);
}

std::string bridge_transport::describe_impl(const bridge_request& request,
    const std::shared_ptr<const native_connection_binding>& binding, bool local) const {
  if (request.method != "bridge.describe")
    return discovery_error("E_INVALID_PARAMS", "Discovery requires the bridge.describe method.");
  bridge_request authorized_request = request;
  authorized_request.authorization.reset();
  auto response = validate(authorized_request, binding, local);
  if (!response.error_code.empty())
    return json{{"error", response.error_code}, {"message", response.error_message}}.dump();
  const auto params = json::parse(request.params_json);
  std::uint64_t cursor = 0;
  std::uint64_t limit = 32;
  for (const auto& field : params.items()) {
    if (field.key() != "cursor" && field.key() != "limit")
      return discovery_error("E_INVALID_PARAMS", "Discovery accepts only cursor and limit.");
  }
  if ((params.contains("cursor") && !nonnegative_integer(params["cursor"], cursor)) ||
      (params.contains("limit") && !nonnegative_integer(params["limit"], limit)) ||
      limit == 0 || limit > 64)
    return discovery_error("E_INVALID_PARAMS", "Expected a nonnegative integer cursor and an integer limit from 1 to 64.");

  response.ok = true;
  auto envelope = response_metadata(response);
  envelope["result"] = json::object();
  const auto envelope_bytes = envelope.dump().size() - 2U; // Empty result is replaced by the payload.
  if (envelope_bytes >= max_message_bytes)
    return discovery_error("E_LIMIT_MESSAGE_SIZE", "Response metadata exceeds the allowed size.");
  const auto payload_limit = max_message_bytes - envelope_bytes;
  json page{{"protocol", "soui.capabilities/1"}, {"sdkVersion", SOUI_SDK_VERSION_STRING},
            {"methods", json::array()}, {"nextCursor", nullptr}};
  std::uint64_t authorized_index = 0;
  for (const auto& method : registry_.methods_) {
    if (local) {
      if (!method.second.local_enabled || !method.second.action || local_reserved_method(method.first)) continue;
      const auto checked = permissions_.resolve_local(binding, request.token, method.first, method.second.permission);
      if (checked.result != authorization_result::authorized || checked.scope != authorized_request.authorization) continue;
    } else if (permissions_.check(request.session_id, request.origin, request.token,
                                  method.second.permission) != authorization_result::authorized) continue;
    if (authorized_index++ < cursor) continue;
    auto& entries = page["methods"];
    if (entries.size() == limit) {
      page["nextCursor"] = cursor + entries.size();
      break;
    }
    json entry{{"name", method.first}, {"capability", method.second.permission}};
    if (method.second.action) entry["action"] = json::parse(method.second.action->encoded());
    entries.push_back(std::move(entry));
    // Reserve the continuation cursor even on a potentially final page. This
    // keeps shrinking deterministic and guarantees the next page advances.
    page["nextCursor"] = cursor + entries.size();
    const auto cursor_bytes = std::to_string(cursor + entries.size()).size();
    const auto terminal_padding = cursor_bytes < 4U ? 4U - cursor_bytes : 0U; // JSON null may be longer.
    if (page.dump().size() + terminal_padding > payload_limit) {
      entries.erase(entries.end() - 1);
      if (entries.empty())
        return discovery_error("E_LIMIT_MESSAGE_SIZE", "Response metadata leaves no room for a discovery entry.");
      page["nextCursor"] = cursor + entries.size();
      break;
    }
    page["nextCursor"] = nullptr;
  }
  if (cursor > authorized_index)
    return discovery_error("E_INVALID_PARAMS", "Discovery cursor is outside the authorized registry.");
  const auto payload = page.dump();
  if (payload.size() > payload_limit)
    return discovery_error("E_LIMIT_MESSAGE_SIZE", "Response metadata leaves no room for discovery.");
  if (local && !local_authorized(authorized_request, binding))
    return discovery_error("E_UNAUTHORIZED", "Local request is not authorized.");
  return payload;
}

bridge_response bridge_transport::handle(const bridge_request& request) const {
  return handle_impl(request, {}, false);
}

bridge_response bridge_transport::handle_local(const bridge_request& request,
    const std::shared_ptr<const native_connection_binding>& binding, const std::string& bearer) const {
  return handle_impl(local_request(request, binding, bearer), binding, true);
}

bridge_response bridge_transport::handle_impl(const bridge_request& request,
    const std::shared_ptr<const native_connection_binding>& binding, bool local) const {
  bridge_request authorized_request = request;
  authorized_request.authorization.reset();
  bridge_response response = validate(authorized_request, binding, local);
  if (!response.error_code.empty()) {
    return response;
  }

  const auto finish = [&] {
    if (local && !local_authorized(authorized_request, binding)) {
      response.ok = false;
      response.error_code = "E_UNAUTHORIZED";
      response.error_message = "Local request is not authorized.";
      response.payload_json.clear();
    }
  };

  const auto* callback = registry_.handler_for(request.method);
  if (callback == nullptr) {
    response.error_code = "E_UNIMPLEMENTED";
    response.error_message = "Registered method has no implementation.";
    return response;
  }
  // Canonical local discovery has its own fixed pagination shape. A page-only
  // action registered under this reserved name cannot redefine that shape.
  const bool local_discovery = local && request.method == "bridge.describe";
  const auto action = local_discovery
      ? std::shared_ptr<const action_contract>{} : registry_.methods_.at(request.method).action;
  if (action && !action->validates_input(request.params_json)) {
    response.error_code = "E_INVALID_PARAMS";
    response.error_message = "Action parameters do not match the registered input schema.";
    return response;
  }
  response.ok = true;
  if (local_discovery) {
    response.payload_json = describe_impl(authorized_request, binding, true);
  } else if (action) {
    try { response.payload_json = (*callback)(authorized_request); }
    catch (...) {
      response.ok = false;
      response.error_code = "E_NATIVE_FAILURE";
      response.error_message = "Native action handler failed.";
      finish();
      return response;
    }
  } else response.payload_json = (*callback)(authorized_request);
  if (action && response.payload_json.size() > max_message_bytes) {
    response.ok = false;
    response.error_code = "E_LIMIT_MESSAGE_SIZE";
    response.error_message = "Native action result exceeds the Bridge message bound.";
    response.payload_json.clear();
    finish();
    return response;
  }
  try {
    const auto payload = response.payload_json.empty() ? json::object() : json::parse(response.payload_json,
        [&action](int depth, json::parse_event_t, json&) {
          if (action && static_cast<std::size_t>(depth) > max_json_depth)
            throw std::invalid_argument("Native action result is too deep");
          return true;
        });
    const auto error = payload.find("error");
    if (payload.is_object() && error != payload.end() && error->is_string()) {
      response.ok = false;
      response.error_code = local && !local_discovery ? "E_ACTION_REJECTED" : error->get<std::string>();
      const auto message = payload.find("message");
      response.error_message = local && !local_discovery ? "Local action rejected." : message != payload.end() && message->is_string()
                                   ? message->get<std::string>()
                                   : "Native handler rejected the request.";
      response.payload_json.clear();
    } else if (action && !action->validates_output(response.payload_json.empty() ? "{}" : response.payload_json)) {
      response.ok = false;
      response.error_code = "E_SCHEMA_INVALID";
      response.error_message = "Native action result does not match the registered output schema.";
      response.payload_json.clear();
    }
  } catch (const json::exception&) {
    response.ok = false;
    response.error_code = "E_SCHEMA_INVALID";
    response.error_message = "Native handler returned invalid JSON.";
    response.payload_json.clear();
  } catch (const std::invalid_argument&) {
    response.ok = false;
    response.error_code = "E_SCHEMA_INVALID";
    response.error_message = "Native action result exceeds the JSON depth bound.";
    response.payload_json.clear();
  }
  finish();
  return response;
}

std::string bridge_transport::handle_json(std::string_view json_text,
                                          bool native_user_gesture) const {
  if (json_text.empty() || json_text.size() > max_message_bytes) {
    return serialize(make_error({}, {}, {}, "E_LIMIT_MESSAGE_SIZE", "Bridge message exceeds the allowed size."));
  }

  json envelope;
  try {
    envelope = json::parse(json_text.begin(), json_text.end(), [](int depth, json::parse_event_t, json&) {
      if (static_cast<std::size_t>(depth) > max_json_depth) throw std::invalid_argument("Bridge envelope is too deep");
      return true;
    });
  } catch (const json::parse_error&) {
    return serialize(make_error({}, {}, {}, "E_MALFORMED_JSON", "Bridge message is not valid JSON."));
  } catch (const json::exception&) {
    return serialize(make_error({}, {}, {}, "E_SCHEMA_INVALID", "Bridge envelope contains an unsupported JSON value."));
  } catch (const std::invalid_argument&) {
    return serialize(make_error({}, {}, {}, "E_SCHEMA_INVALID", "Bridge envelope exceeds the JSON depth bound."));
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

bool bridge_transport::local_authorized(const bridge_request& request,
    const std::shared_ptr<const native_connection_binding>& binding) const {
  const auto* capability = registry_.capability_for(request.method);
  if (!capability || !request.authorization) return false;
  const auto checked = permissions_.resolve_local(binding, request.token, request.method, *capability);
  return checked.result == authorization_result::authorized && checked.scope == request.authorization;
}

bridge_response bridge_transport::validate(bridge_request& request,
    const std::shared_ptr<const native_connection_binding>& binding, bool local) const {
  bridge_response response;
  response.request_id = local && !valid_id(request.request_id) ? std::string{} : request.request_id;
  response.correlation_id = response.request_id;
  response.session_id = request.session_id;
  response.operation_id = local && !request.operation_id.empty() && !valid_id(request.operation_id)
      ? std::string{} : request.operation_id;

  if (local && (!binding || request.token.empty() || request.token.size() > max_string_bytes)) {
    response.error_code = "E_UNAUTHORIZED";
    response.error_message = "Local request is not authorized.";
    return response;
  }

  if (!valid_id(request.request_id) || !valid_id(request.session_id) || !valid_origin(request.origin) ||
      request.token.empty() || request.token.size() > max_string_bytes || !valid_id(request.method) ||
      request.params_json.empty() || request.timeout_ms == 0U || request.timeout_ms > 60000U ||
      (local && !request.operation_id.empty() && !valid_id(request.operation_id))) {
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
    params = json::parse(request.params_json, [](int depth, json::parse_event_t, json&) {
      if (static_cast<std::size_t>(depth) > max_json_depth) throw std::invalid_argument("Bridge params are too deep");
      return true;
    });
  } catch (const json::parse_error&) {
    response.error_code = "E_MALFORMED_JSON";
    response.error_message = "Request params are not valid JSON.";
    return response;
  } catch (const json::exception&) {
    response.error_code = "E_SCHEMA_INVALID";
    response.error_message = "Request params contain an unsupported JSON value.";
    return response;
  } catch (const std::invalid_argument&) {
    response.error_code = "E_SCHEMA_INVALID";
    response.error_message = "Request params exceed the JSON depth bound.";
    return response;
  }
  if (!params.is_object() || json_depth(params) > max_json_depth) {
    response.error_code = "E_SCHEMA_INVALID";
    response.error_message = "Request params must be a bounded JSON object.";
    return response;
  }

  const std::string* capability = registry_.capability_for(request.method);
  const auto registered = registry_.methods_.find(request.method);
  const bool local_discovery = local && request.method == "bridge.describe" &&
      capability && *capability == "bridge.describe";
  if (local && (!capability || (!local_discovery &&
      (local_reserved_method(request.method) || !registered->second.action || !registered->second.local_enabled)))) {
    response.error_code = "E_UNAUTHORIZED";
    response.error_message = "Local request is not authorized.";
    return response;
  }
  if (capability == nullptr) {
    response.error_code = "E_UNKNOWN_METHOD";
    response.error_message = "Requested method is not registered.";
    return response;
  }
  const auto authorization = local ? permissions_.resolve_local(binding, request.token, request.method, *capability)
      : permissions_.resolve(request.session_id, request.origin, request.token, *capability);
  if (authorization.result != authorization_result::authorized) {
    response.error_code = !local && authorization.result == authorization_result::expired ? "E_SESSION_EXPIRED" : "E_UNAUTHORIZED";
    response.error_message = local ? "Local request is not authorized." : authorization.result == authorization_result::expired
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
  auto envelope = response_metadata(response);
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
  auto encoded = envelope.dump();
  if (encoded.size() <= max_message_bytes) return encoded;
  // An oversized handler result or caller metadata must not escape the same
  // response limit enforced by the Web client. Drop optional operation metadata.
  const auto bounded = make_error(valid_id(response.request_id) ? response.request_id : std::string{},
      valid_id(response.session_id) ? response.session_id : std::string{}, {},
      "E_LIMIT_MESSAGE_SIZE", "Native response exceeds the allowed size.");
  auto fallback = response_metadata(bounded);
  fallback["error"] = {{"code", bounded.error_code}, {"message", bounded.error_message}};
  return fallback.dump();
}

}  // namespace soui::runtime
