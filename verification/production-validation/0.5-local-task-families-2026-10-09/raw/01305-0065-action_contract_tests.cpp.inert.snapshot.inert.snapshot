#include <soui/json_bridge.h>
#include <soui/runtime/bridge/action_contract.h>
#include <json.hpp>

#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using json = nlohmann::json;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

json object_schema(json properties = json::object(), json required = json::array()) {
  return {{"type", "object"}, {"properties", std::move(properties)},
          {"required", std::move(required)}, {"additionalProperties", false}};
}

json descriptor(json input = object_schema(), json output = object_schema()) {
  return {{"protocol", "soui.action/1"}, {"title", "计算库存金额"},
          {"description", "由公共 C++ 动作校验并执行。"},
          {"inputSchema", std::move(input)}, {"outputSchema", std::move(output)}};
}

json bootstrap(soui::sdk::json_bridge& bridge) {
  const auto response = json::parse(bridge.handle_json(json{{"requestId", "actions:bootstrap"},
      {"sessionId", ""}, {"origin", ""}, {"token", ""}, {"method", "bridge.bootstrap"},
      {"params", {{"protocol", "soui.bridge/1"}}}}.dump()));
  require(response.at("ok").get<bool>(), "action bootstrap failed");
  return response.at("result");
}

json call(soui::sdk::json_bridge& bridge, const json& auth, const char* method,
          json params = json::object()) {
  const auto raw = bridge.handle_json(json{{"requestId", "actions:call"},
      {"sessionId", auth.at("sessionId")}, {"origin", auth.at("origin")}, {"token", auth.at("token")},
      {"method", method}, {"params", std::move(params)}}.dump());
  require(raw.size() <= soui::runtime::bridge_transport::max_message_bytes, "action wire response exceeded Bridge bound");
  return json::parse(raw);
}

void rejected(const json& response, const char* code, const char* message) {
  require(!response.at("ok").get<bool>() && response.at("error").at("code") == code, message);
}

void schema_checks() {
  const auto schema = object_schema({
      {"name", {{"type", "string"}, {"minLength", 1}, {"maxLength", 2}}},
      {"mode", {{"type", "string"}, {"maxLength", 6}, {"enum", {"sum", "count"}}}},
      {"units", {{"type", "integer"}, {"minimum", 0}, {"maximum", 20}}},
      {"price", {{"type", "number"}, {"minimum", 0}, {"maximum", 100}}},
      {"enabled", {{"type", "boolean"}}}, {"empty", {{"type", "null"}}},
      {"rows", {{"type", "array"}, {"items", {{"type", "integer"}}}, {"minItems", 1}, {"maxItems", 3}}}
  }, {"name", "mode", "units", "price", "enabled", "empty", "rows"});
  const auto contract = soui::runtime::action_contract::parse(descriptor(schema, schema).dump());
  require(contract != nullptr, "supported schema was rejected");
  json value{{"name", "😀原"}, {"mode", "sum"}, {"units", 1.0}, {"price", 1.25},
             {"enabled", true}, {"empty", nullptr}, {"rows", {0, 1, 2}}};
  require(contract->validates_input(value.dump()) && contract->validates_output(value.dump()),
          "supported values or Unicode codepoint length were rejected");
  for (const auto& replacement : std::vector<json>{
      {{"name", ""}}, {{"name", "😀原文"}}, {{"mode", "other"}}, {{"units", 1.5}}, {{"units", -1}},
      {{"price", 101}}, {{"enabled", 1}}, {{"empty", false}}, {{"rows", json::array()}}, {{"rows", {1, 2, 3, 4}}},
      {{"rows", {1, "2"}}}, {{"extra", true}}}) {
    auto invalid = value;
    invalid.update(replacement);
    require(!contract->validates_input(invalid.dump()), "invalid value matched action input schema");
  }
  auto missing = value;
  missing.erase("name");
  require(!contract->validates_input(missing.dump()), "missing required action field accepted");
  require(!contract->validates_input("[]") && !contract->validates_input("not-json"), "nonobject action input accepted");
  const auto integer = soui::runtime::action_contract::parse(descriptor(object_schema(
      {{"n", {{"type", "integer"}}}}, {"n"})).dump());
  require(integer && integer->validates_input(R"({"n":9007199254740991})") &&
      !integer->validates_input(R"({"n":9007199254740992})") &&
      !integer->validates_input(R"({"n":-9007199254740992})"), "integer bound diverged from JavaScript safe numbers");
  const auto one_character = soui::runtime::action_contract::parse(descriptor(object_schema(
      {{"text", {{"type", "string"}, {"minLength", 1}, {"maxLength", 1}}}}, {"text"})).dump());
  require(one_character && one_character->validates_input(R"({"text":"\ud83d\ude00"})") &&
      !one_character->validates_input(R"({"text":"e\u0301"})"), "string length must count codepoints, not bytes or graphemes");
  std::string invalid_utf8 = "{\"text\":\"";
  invalid_utf8 += static_cast<char>(0xff);
  invalid_utf8 += "\"}";
  require(!one_character->validates_input(invalid_utf8), "malformed UTF-8 action value accepted");

  for (const auto& malformed : std::vector<json>{
      {{"type", "object"}}, {{"type", "object"}, {"properties", json::object()}, {"additionalProperties", true}},
      {{"type", "object"}, {"properties", json::object()}, {"additionalProperties", false}, {"required", {"missing"}}},
      object_schema({{"x", {{"type", "string"}}}}),
      object_schema({{"x", {{"type", "string"}, {"maxLength", 4}, {"pattern", ".*"}}}}),
      object_schema({{"x", {{"type", "string"}, {"maxLength", 4}, {"enum", {"a", "a"}}}}}),
      object_schema({{"x", {{"type", "string"}, {"minLength", 5}, {"maxLength", 4}}}}),
      object_schema({{"x", {{"type", "string"}, {"maxLength", 32769}}}}),
      object_schema({{"x", {{"type", "array"}, {"maxItems", 4}}}}),
      object_schema({{"x", {{"type", "array"}, {"items", {{"type", "integer"}}}, {"maxItems", 1025}}}}),
      object_schema({{"x", {{"type", "array"}, {"items", {{"type", "integer"}}}, {"maxItems", 3}, {"minItems", 4}}}}),
      object_schema({{"x", {{"type", "boolean"}, {"enum", {true}}}}}),
      object_schema({{"x", {{"type", "integer"}, {"minimum", 4}, {"maximum", 3}}}}),
      object_schema({{"x", {{"type", "number"}, {"maximum", 9007199254740992ULL}}}}),
      object_schema({{"x", {{"type", {"integer", "null"}}}}}),
      object_schema({{"x", {{"type", "unknown"}}}}),
      object_schema({{"x", {{"type", "integer"}}}}, {"x", "x"}),
  }) require(!soui::runtime::action_contract::parse(descriptor(malformed).dump()), "malformed or unsupported schema accepted");

  auto unknown = descriptor();
  unknown["actor"] = "agent";
  require(!soui::runtime::action_contract::parse(unknown.dump()), "unknown descriptor key accepted");
  auto no_description = descriptor();
  no_description.erase("description");
  require(!soui::runtime::action_contract::parse(no_description.dump()), "missing descriptor field accepted");
  auto oversized_title = descriptor();
  oversized_title["title"] = std::string(257, 'x');
  require(!soui::runtime::action_contract::parse(oversized_title.dump()), "oversized title accepted");
  auto many_properties = object_schema();
  for (unsigned i = 0; i < 65; ++i) many_properties["properties"]["f" + std::to_string(i)] = {{"type", "boolean"}};
  require(!soui::runtime::action_contract::parse(descriptor(many_properties).dump()), "unbounded property count accepted");
  auto deep = object_schema();
  for (unsigned i = 0; i < 5; ++i) deep = object_schema({{"child", deep}});
  require(!soui::runtime::action_contract::parse(descriptor(deep).dump()), "deep schema accepted");
  const auto exact_depth = object_schema({{"rows", {{"type", "array"}, {"maxItems", 2},
      {"items", {{"type", "array"}, {"maxItems", 2}, {"items", object_schema(
          {{"value", {{"type", "integer"}}}}, {"value"})}}}}}});
  const auto exact_contract = soui::runtime::action_contract::parse(descriptor(exact_depth).dump());
  require(exact_contract && exact_contract->validates_input(R"({"rows":[[{"value":1}]]})"),
          "schema at the exact raw depth-eight boundary was rejected");
  auto large = object_schema({{"choice", {{"type", "string"}, {"maxLength", 8192},
                                          {"enum", {std::string(8192, 'x')}}}}});
  require(!soui::runtime::action_contract::parse(descriptor(large).dump()), "schema byte limit not enforced");
  require(!soui::runtime::action_contract::parse(std::string(16385, ' ')), "contract byte limit not enforced");
}

void bridge_checks() {
  soui::sdk::json_bridge bridge("app://actions");
  unsigned calls = 0;
  const auto input = object_schema({{"units", {{"type", "integer"}, {"minimum", 1}, {"maximum", 10}}}}, {"units"});
  const auto output = object_schema({{"totalCents", {{"type", "integer"}, {"minimum", 0}}}}, {"totalCents"});
  const auto action = descriptor(input, output);
  const auto calculate = [&](const soui::runtime::bridge_request& request) {
    ++calls;
    const auto units = json::parse(request.params_json).at("units").get<int>();
    return json{{"totalCents", units * 125}}.dump();
  };
  require(bridge.register_action("inventory.total", "inventory.read", action.dump(), calculate), "public action registration failed");
  require(!bridge.register_action("inventory.total", "inventory.read", action.dump(), calculate), "duplicate action registration accepted");
  require(!bridge.register_action("inventory.invalid", "inventory.read", "{}", calculate), "malformed contract registered");
  require(bridge.register_action("inventory.badOutput", "inventory.read", action.dump(), [](const auto&) { return "{}"; }),
          "bad output handler registration failed");
  require(bridge.register_action("inventory.failure", "inventory.read", action.dump(), [](const auto&) {
    return R"({"error":"E_INVENTORY_FAILURE","message":"Inventory unavailable."})";
  }), "business error handler registration failed");
  require(bridge.register_action("inventory.throw", "inventory.read", action.dump(), [](const auto&) -> std::string {
    throw std::runtime_error("Business detail should not escape the native boundary.");
  }), "throwing action registration failed");
  require(bridge.register_action("inventory.oversized", "inventory.read", action.dump(), [](const auto&) {
    return std::string(65537, ' ');
  }), "oversized action registration failed");
  require(bridge.register_action("inventory.malformed", "inventory.read", action.dump(), [](const auto&) { return "not-json"; }),
          "malformed output action registration failed");
  require(bridge.register_action("inventory.hidden", "inventory.write", action.dump(), calculate), "hidden action registration failed");
  require(bridge.register_method("legacy.call", "inventory.read", [](auto) { return "{}"; }), "legacy registration failed");
  require(!bridge.set_session_capabilities({"inventory.read", "inventory.read"}) &&
      !bridge.set_session_capabilities({"unregistered"}), "invalid native grant accepted");
  require(bridge.set_session_capabilities({"bridge.describe", "inventory.read"}), "native capability whitelist failed");
  const auto auth = bootstrap(bridge);
  require(!bridge.register_action("late.action", "inventory.read", action.dump(), calculate) &&
      !bridge.set_session_capabilities({"inventory.write"}), "bootstrap did not freeze actions/grants");
  const auto discovered = call(bridge, auth, "bridge.describe").at("result").at("methods");
  bool saw_action = false, saw_legacy = false;
  for (const auto& method : discovered) {
    if (method.at("name") == "inventory.total") {
      require(method.at("action") == action, "discovery changed public action contract");
      saw_action = true;
    }
    if (method.at("name") == "legacy.call") {
      require(method.size() == 2U && !method.contains("action"), "legacy discovery shape changed");
      saw_legacy = true;
    }
    require(method.at("name") != "inventory.hidden" && method.at("name") != "task.start", "discovery exposed an ungranted method");
  }
  require(saw_action && saw_legacy && calls == 0, "discovery invoked a handler or lost registered action");
  rejected(call(bridge, auth, "inventory.total", {{"units", 0}}), "E_INVALID_PARAMS", "action input schema not enforced");
  rejected(call(bridge, auth, "inventory.total", {{"units", 1}, {"actor", "agent"}}), "E_INVALID_PARAMS", "page actor field bypassed schema");
  rejected(call(bridge, auth, "inventory.hidden", {{"units", 1}}), "E_UNAUTHORIZED", "direct call bypassed native whitelist");
  rejected(call(bridge, auth, "task.start", {{"kind", "inventory.total"}, {"operationId", "bypass"}, {"input", {{"units", 1}}}}),
      "E_UNAUTHORIZED", "task.start bypassed action authorization");
  require(calls == 0, "denied or invalid action invoked a business handler");
  const auto success = call(bridge, auth, "inventory.total", {{"units", 3}});
  require(success.at("ok").get<bool>() && success.at("result").at("totalCents") == 375 && calls == 1,
          "valid synchronous action failed across public bridge");
  rejected(call(bridge, auth, "inventory.badOutput", {{"units", 1}}), "E_SCHEMA_INVALID", "action output schema not enforced");
  const auto failure = call(bridge, auth, "inventory.failure", {{"units", 1}});
  rejected(failure, "E_INVENTORY_FAILURE", "business error was replaced by output validation");
  require(failure.at("error").at("message") == "Inventory unavailable.", "business error message was lost");
  rejected(call(bridge, auth, "inventory.throw", {{"units", 1}}), "E_NATIVE_FAILURE", "action exception escaped its boundary");
  rejected(call(bridge, auth, "inventory.oversized", {{"units", 1}}), "E_LIMIT_MESSAGE_SIZE", "oversized action response was not bounded before parsing");
  rejected(call(bridge, auth, "inventory.malformed", {{"units", 1}}), "E_SCHEMA_INVALID", "malformed action output accepted");
  require(call(bridge, auth, "inventory.total", {{"units", 1}}).at("ok").get<bool>(), "action failure damaged subsequent calls");
  bridge.shutdown_session(auth.at("sessionId").get<std::string>());
  rejected(call(bridge, auth, "inventory.total", {{"units", 1}}), "E_UNAUTHORIZED", "old session retained action access");
  const auto replacement = bootstrap(bridge);
  rejected(call(bridge, replacement, "inventory.hidden", {{"units", 1}}), "E_UNAUTHORIZED", "navigation reset frozen native grant");
  require(call(bridge, replacement, "bridge.describe").at("result").at("methods") == discovered, "navigation changed frozen contract discovery");

  soui::sdk::json_bridge no_grants("app://actions");
  require(no_grants.set_session_capabilities({}), "empty whitelist must be configurable");
  rejected(call(no_grants, bootstrap(no_grants), "bridge.describe"), "E_UNAUTHORIZED", "empty whitelist granted discovery");
  soui::sdk::json_bridge legacy("app://actions");
  require(legacy.register_action("inventory.total", "inventory.read", action.dump(), calculate), "default grant action registration failed");
  const auto legacy_auth = bootstrap(legacy);
  require(call(legacy, legacy_auth, "inventory.total", {{"units", 1}}).at("ok").get<bool>(), "default legacy grants changed");
  const auto before_bypass = calls;
  rejected(call(legacy, legacy_auth, "task.start", {{"kind", "inventory.total"}, {"operationId", "bypass"},
      {"input", {{"units", 0}}}}), "E_UNKNOWN_METHOD", "ordinary task permission mapped an action without its contract");
  require(calls == before_bypass, "task.start bypassed synchronous action validation");
}

void discovery_size_checks() {
  soui::sdk::json_bridge bridge("app://many-actions");
  json choices = json::array();
  for (unsigned i = 0; i < 12; ++i) choices.push_back(std::to_string(i) + std::string(480, '\x01'));
  const auto contract = descriptor(object_schema({{"choice", {{"type", "string"}, {"maxLength", 512}, {"enum", choices}}}}));
  // Control characters expand to six bytes each; keep one schema below 8 KiB.
  auto small = contract;
  small["inputSchema"]["properties"]["choice"]["enum"] = json::array({std::string(1000, '\x01')});
  for (unsigned i = 0; i < 18; ++i)
    require(bridge.register_action("action." + std::to_string(i), "actions", small.dump(), [](const auto&) { return "{}"; }),
            "size-bound action registration failed");
  const auto auth = bootstrap(bridge);
  std::set<std::string> methods;
  std::size_t cursor = 0;
  bool shortened = false;
  for (;;) {
    const auto page = call(bridge, auth, "bridge.describe", {{"cursor", cursor}, {"limit", 64}}).at("result");
    for (const auto& entry : page.at("methods")) {
      require(methods.insert(entry.at("name").get<std::string>()).second, "action pagination repeated a method");
      if (entry.contains("action")) require(entry.at("action") == small, "size-limited page changed action contract");
    }
    if (page.at("nextCursor").is_null()) break;
    const auto next = page.at("nextCursor").get<std::size_t>();
    require(next > cursor && next == methods.size(), "action page did not advance");
    shortened = shortened || page.at("methods").size() < 64U;
    cursor = next;
  }
  require(shortened && methods.size() == 25U, "action contracts did not shrink wire-bound pages or lost methods");
}
}  // namespace

int main() {
  try {
    schema_checks();
    bridge_checks();
    discovery_size_checks();
    std::cout << "Public action contracts: schema, Unicode, grants, errors and discovery bounds passed\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
