#include "app/generated/actions.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using json = nlohmann::json;
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
json bootstrap(soui::sdk::json_bridge& bridge) {
  const auto result = json::parse(bridge.handle_json(json{{"requestId", "consumer:bootstrap"},
    {"sessionId", ""}, {"origin", ""}, {"token", ""}, {"method", "bridge.bootstrap"},
    {"params", {{"protocol", "soui.bridge/1"}}}}.dump()));
  require(result.at("ok").get<bool>(), "Bootstrap failed");
  return result.at("result");
}
json call(soui::sdk::json_bridge& bridge, const json& auth, const char* method, json input) {
  return json::parse(bridge.handle_json(json{{"requestId", "consumer:call"},
    {"sessionId", auth.at("sessionId")}, {"origin", auth.at("origin")}, {"token", auth.at("token")},
    {"method", method}, {"params", std::move(input)}}.dump()));
}
void error(const json& result, const char* code) {
  require(!result.at("ok").get<bool>() && result.at("error").at("code") == code, "Unexpected error code");
}
} // namespace

int main() {
  try {
    soui::sdk::json_bridge bridge("app://generated-consumer");
    unsigned calls = 0;
    require(consumer_actions::register_Greet(bridge,
      [&](const consumer_actions::GreetInput& input, const soui::runtime::bridge_request& trusted) {
        require(trusted.authorization && trusted.authorization->allows("app.greet"), "Missing native authorization");
        ++calls;
        if (input.name == "throw") throw std::runtime_error("Expected handler exception");
        if (input.name == "invalid-output") return consumer_actions::GreetOutput{std::string(201, 'x')};
        if (input.attempts) require(*input.attempts == 9007199254740991LL, "Safe integer codec changed value");
        if (input.nothing) require(*input.nothing == nullptr, "Optional explicit null changed");
        if (input.meta) require(input.meta->tags == std::vector<std::string>{"one", "two"}, "Nested array codec failed");
        return consumer_actions::GreetOutput{"Hello, " + input.name};
      }), "Generated public registration failed");
    require(bridge.set_session_capabilities({"app.greet", "bridge.describe"}), "Grant setup failed");
    const auto auth = bootstrap(bridge);
    const json input{{"name", "开发者😀"}, {"GreetInputObject1", "shadow"}, {"attempts", 9007199254740991LL}, {"nothing", nullptr},
      {"enabled", true}, {"ratio", 0.5}, {"meta", {{"tags", json::array({"one", "two"})}}}};
    const auto typed = input.get<consumer_actions::GreetInput>();
    require(json(typed) == input, "Generated JSON roundtrip failed");
    const auto absent = json{{"name", "minimal"}}.get<consumer_actions::GreetInput>();
    require(!absent.nothing && !absent.meta && json(absent).size() == 1, "Absence became explicit null");
    const auto good = call(bridge, auth, "app.greet", input);
    require(good.at("ok").get<bool>() && good.at("result").at("message") == "Hello, 开发者😀" && calls == 1,
            "Generated action did not execute");
    const auto catalog = call(bridge, auth, "bridge.describe", json::object()).at("result");
    bool found = false;
    for (const auto& method : catalog.at("methods")) if (method.at("name") == "app.greet") {
      require(method.at("action") == json::parse(consumer_actions::contract_Greet), "Discovered schema differs"); found = true;
    }
    require(found, "Generated action not discoverable");
    for (const auto& bad : std::vector<json>{json{{"name", ""}}, json{{"name", "ok"}, {"unexpected", true}},
      json{{"name", "ok"}, {"attempts", 1.5}}, json{{"name", "ok"}, {"attempts", 9007199254740992LL}},
      json{{"name", "ok"}, {"meta", {{"tags", json::array({"bad"})}}}}}) {
      error(call(bridge, auth, "app.greet", bad), "E_INVALID_PARAMS");
      require(calls == 1, "Invalid raw input reached typed callback");
    }
    error(call(bridge, auth, "app.greet", {{"name", "invalid-output"}}), "E_SCHEMA_INVALID");
    error(call(bridge, auth, "app.greet", {{"name", "throw"}}), "E_NATIVE_FAILURE");
    require(calls == 3, "Failure callbacks ran an unexpected number of times");
    soui::sdk::json_bridge denied("app://generated-denied");
    require(consumer_actions::register_Greet(denied, [&](const auto&, const auto&) {
      ++calls; return consumer_actions::GreetOutput{"Denied"};
    }) && denied.set_session_capabilities({"bridge.describe"}), "Restricted registration failed");
    const auto restricted = bootstrap(denied);
    error(call(denied, restricted, "app.greet", {{"name", "ok"}}), "E_UNAUTHORIZED");
    require(calls == 3, "Generated binding bypassed native whitelist");
    const auto restricted_catalog = call(denied, restricted, "bridge.describe", json::object()).at("result");
    for (const auto& method : restricted_catalog.at("methods"))
      require(method.at("name") != "app.greet", "Discovery leaked an ungranted action");
    std::cout << "Installed generated action consumer passed. No GUI or target-system claim.\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
