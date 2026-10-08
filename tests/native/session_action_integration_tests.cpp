#include <soui/json_bridge.h>
#include <soui/runtime/bridge/bridge_transport.h>
#include <json.hpp>

#include <chrono>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using json = nlohmann::json;
using namespace soui::runtime;
constexpr const char* action = "inventory.inspect";
constexpr const char* read_capability = "inventory.read";
constexpr const char* renewal = "bridge.session.renew";

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

std::string contract() {
  const json schema{{"type", "object"}, {"properties", json::object()},
                    {"required", json::array()}, {"additionalProperties", false}};
  return json{{"protocol", "soui.action/1"}, {"title", "Inspect inventory"},
              {"description", "Inspect the granted native authorization scope."},
              {"inputSchema", schema}, {"outputSchema", schema}}.dump();
}

json bootstrap(soui::sdk::json_bridge& bridge) {
  const auto response = json::parse(bridge.handle_json(json{{"requestId", "integration:bootstrap"},
      {"sessionId", ""}, {"origin", ""}, {"token", ""}, {"method", "bridge.bootstrap"},
      {"params", {{"protocol", "soui.bridge/1"}}}}.dump()));
  require(response.at("ok").get<bool>(), "Restricted bootstrap failed");
  return response.at("result");
}

json call(soui::sdk::json_bridge& bridge, const json& auth, const char* method,
          json params = json::object()) {
  return json::parse(bridge.handle_json(json{{"requestId", "integration:call"},
      {"sessionId", auth.at("sessionId")}, {"origin", auth.at("origin")},
      {"token", auth.at("token")}, {"method", method}, {"params", std::move(params)}}.dump()));
}

void denied(const json& response) {
  require(!response.at("ok").get<bool>() && response.at("error").at("code") == "E_UNAUTHORIZED",
          "An ungranted capability reached the handler");
}

void restricted_bootstrap_and_discovery() {
  for (const bool grants_action : {false, true}) {
    soui::sdk::json_bridge bridge("app://session-action-integration");
    unsigned calls = 0;
    require(bridge.register_action(action, read_capability, contract(), [&](const bridge_request& request) {
      require(request.authorization && request.authorization->allows(read_capability) &&
                  !request.authorization->allows(renewal),
              "Restricted action received a broader native scope");
      ++calls;
      return std::string{"{}"};
    }), "Action registration failed");
    require(bridge.enable_session_renewal(), "Renewal registration failed");
    require(bridge.set_session_capabilities({grants_action ? read_capability : "bridge.describe"}),
            "Restricted grant failed");
    const auto auth = bootstrap(bridge);
    require(!auth.contains("lease"), "An ungranted renewal advertised a lease");
    denied(call(bridge, auth, renewal, {{"operationId", "denied-renewal"}}));
    if (grants_action) {
      require(call(bridge, auth, action).at("ok").get<bool>() && calls == 1U,
              "Granted action failed after restricted bootstrap");
      denied(call(bridge, auth, "bridge.describe"));
    } else {
      const auto methods = call(bridge, auth, "bridge.describe").at("result").at("methods");
      require(methods.size() == 1U && methods.at(0).at("name") == "bridge.describe",
              "Discovery disclosed an ungranted registered method");
      denied(call(bridge, auth, action));
      require(calls == 0U, "Discovery-only session executed an action");
    }
  }

  soui::sdk::json_bridge bridge("app://session-action-renewable");
  require(bridge.enable_session_renewal() &&
              bridge.set_session_capabilities({"bridge.describe", renewal}),
          "Renewable whitelist failed");
  const auto auth = bootstrap(bridge);
  require(auth.contains("lease") && auth.at("lease").at("generation") == 1U,
          "Granted renewal omitted its bootstrap lease");
  std::set<std::string> names;
  const auto discovery = call(bridge, auth, "bridge.describe");
  for (const auto& method : discovery.at("result").at("methods"))
    names.insert(method.at("name").get<std::string>());
  require(names == std::set<std::string>{"bridge.describe", renewal},
          "Renewable discovery escaped the native whitelist");
  const auto early = call(bridge, auth, renewal, {{"operationId", "too-early"}});
  require(!early.at("ok").get<bool>() && early.at("error").at("code") == "E_RENEW_TOO_EARLY",
          "Granted renewal did not reach the native lease timing check");
}

std::chrono::milliseconds elapsed{0};
permission_store::clock_type::time_point now() {
  return permission_store::clock_type::time_point{} + elapsed;
}

void action_scope_survives_renewal_without_trusting_forgery() {
  elapsed = std::chrono::milliseconds{0};
  permission_store permissions{&now}, foreign{&now};
  constexpr const char* session = "integration-session";
  constexpr const char* origin = "app://integration-action";
  const auto token = permissions.issue(session, origin, {read_capability, renewal});
  const auto native = permissions.resolve(session, origin, token, read_capability).scope;
  const auto foreign_token = foreign.issue(session, origin, {read_capability, "inventory.write"});
  const auto forged = foreign.resolve(session, origin, foreign_token, read_capability).scope;
  capability_registry registry;
  bridge_transport transport{registry, permissions};
  unsigned calls = 0;
  require(registry.register_action(action, read_capability, contract(), [&](const bridge_request& request) {
    require(request.authorization == native && request.authorization != forged &&
                request.authorization->allows(read_capability) &&
                !request.authorization->allows("inventory.write"),
            "Action trusted a forged or broadened native scope");
    ++calls;
    return std::string{"{}"};
  }) && transport.enable_session_renewal(), "Transport action/renewal registration failed");
  bridge_request request;
  request.request_id = "integration:action";
  request.session_id = session;
  request.origin = origin;
  request.token = token;
  request.method = action;
  request.params_json = "{}";
  request.authorization = forged;
  require(transport.handle(request).ok && calls == 1U && request.authorization == forged,
          "Action authorization did not independently resolve without mutating its caller");
  elapsed = std::chrono::minutes{25};
  auto renew_request = request;
  renew_request.method = renewal;
  renew_request.params_json = R"({"operationId":"integration:renew"})";
  const auto response = transport.handle(renew_request);
  require(response.ok, "Valid simulated-clock renewal failed");
  const auto renewed = json::parse(response.payload_json);
  request.token = renewed.at("token").get<std::string>();
  require(request.token != token && renewed.at("lease").at("generation") == 2U &&
              transport.handle(request).ok && calls == 2U,
          "Token rotation changed action ownership or bypassed its scope");
  request.token = foreign_token;
  request.authorization = native;
  const auto rejected = transport.handle(request);
  require(!rejected.ok && rejected.error_code == "E_UNAUTHORIZED" && calls == 2U,
          "A borrowed valid scope authorized foreign credentials");
}
} // namespace

int main() {
  try {
    restricted_bootstrap_and_discovery();
    action_scope_survives_renewal_without_trusting_forgery();
    std::cout << "Session/action integration passed (simulated renewal; no desktop or real-duration claim).\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
