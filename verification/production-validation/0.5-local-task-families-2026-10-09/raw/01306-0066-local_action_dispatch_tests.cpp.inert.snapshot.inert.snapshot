#include <soui/json_bridge.h>
#include <soui/runtime/bridge/bridge_transport.h>
#include <json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace soui::runtime {
// TEST_ONLY: force bearer reuse after precise revocation. No wire entropy control.
struct permission_store_test_access {
  static void entropy(permission_store& store, bool (*source)(unsigned char*, std::size_t)) {
    store.entropy_ = source;
  }
};
}

namespace {
using namespace soui::runtime;
using json = nlohmann::json;
constexpr const char* session = "local-dispatch-session";
constexpr const char* origin = "app://local-dispatch";
constexpr const char* shared_cap = "work.shared";
constexpr const char* renewal = "bridge.session.renew";
constexpr std::int64_t renew_at = 25 * 60 * 1000;
constexpr std::int64_t lifetime = 30 * 60 * 1000;
std::int64_t clock_ms = 0;
std::size_t clock_reads = 0, close_on_read = 0;
std::shared_ptr<native_connection_binding> close_on_clock;
bool clock_closed = false;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
permission_store::clock_type::time_point now() {
  ++clock_reads;
  // Closing the binding does not acquire the store/scope locks held by a read.
  if (close_on_clock && close_on_read == clock_reads) {
    close_on_clock->close();
    clock_closed = true;
  }
  return permission_store::clock_type::time_point{} + std::chrono::milliseconds(clock_ms);
}
void reset_clock() {
  clock_ms = 0;
  clock_reads = close_on_read = 0;
  close_on_clock.reset();
  clock_closed = false;
}
bool repeated_entropy(unsigned char* bytes, std::size_t count) {
  std::fill(bytes, bytes + count, static_cast<unsigned char>(0x5A));
  return true;
}
json object_schema(json properties = json::object(), json required = json::array()) {
  return {{"type", "object"}, {"properties", std::move(properties)},
          {"required", std::move(required)}, {"additionalProperties", false}};
}
std::string contract() {
  return json{{"protocol", "soui.action/1"}, {"title", "Bounded native calculation"},
      {"description", "An application-approved output projection."},
      {"inputSchema", object_schema({{"n", {{"type", "integer"}, {"minimum", 1}, {"maximum", 10}}}}, {"n"})},
      {"outputSchema", object_schema({{"value", {{"type", "integer"}, {"minimum", 0}, {"maximum", 100}}}}, {"value"})}}.dump();
}
bridge_request request(std::string method, std::string params = R"({"n":2})") {
  bridge_request result;
  result.request_id = "local:request";
  result.session_id = "wire session must be discarded";
  result.origin = "wire origin must be discarded";
  result.token = "wire-token-must-be-discarded";
  result.method = std::move(method);
  result.params_json = std::move(params);
  result.operation_id = "operation:one";
  result.timeout_ms = 1000;
  result.native_user_gesture = true;
  return result;
}
void rejected(const bridge_response& result, const char* code) {
  require(!result.ok && result.error_code == code && result.payload_json.empty(),
          "Rejected local call accepted/disclosed a payload or used the wrong error category");
}
void successful(const bridge_response& result) {
  require(result.ok && result.error_code.empty() && json::parse(result.payload_json) == json{{"value", 2}},
          "Authorized bounded action did not return its validated result");
}
void discovery_rejected(const std::string& raw, const char* code = "E_UNAUTHORIZED") {
  const auto result = json::parse(raw);
  require(result.is_object() && result.at("error") == code && !result.contains("methods"),
          "Rejected discovery exposed a method list or used the wrong error category");
}
struct fixture {
  permission_store store{&now};
  capability_registry registry;
  bridge_transport transport{registry, store};
  std::shared_ptr<native_connection_binding> binding = store.create_local_binding(session, origin);
  std::size_t canonical_calls = 0;

  explicit fixture(bool canonical = true) {
    if (canonical) require(registry.register_contextual_method("bridge.describe", "bridge.describe",
        [this](const bridge_request&) { ++canonical_calls; return R"({"private":"must not run"})"; }),
        "Canonical discovery registration failed");
  }
  void action(const char* method, const char* capability, capability_registry::contextual_handler callback,
              bool local = true) {
    require(registry.register_action(method, capability, contract(), std::move(callback)), "Action registration failed");
    if (local) require(registry.allow_local_action(method), "Typed local action opt-in failed");
  }
  issued_local_grant grant(const std::vector<std::string>& caps, const std::vector<std::string>& methods) {
    return store.issue_local(binding, caps, methods);
  }
};

void exposure_is_explicit_and_typed() {
  fixture f;
  std::size_t hidden_calls = 0, allowed_calls = 0, plain_calls = 0;
  f.action("work.hidden", shared_cap, [&](const bridge_request&) { ++hidden_calls; return R"({"value":2})"; }, false);
  f.action("work.allowed", shared_cap, [&](const bridge_request&) { ++allowed_calls; return R"({"value":2})"; });
  require(f.registry.register_method("work.plain", shared_cap,
      [&](std::string_view) { ++plain_calls; return R"({"value":2})"; }), "Plain registration failed");
  require(!f.registry.allow_local_action("work.plain") && !f.registry.allow_local_action("work.missing"),
          "Plain/missing registration acquired local exposure");
  const auto grant = f.grant({shared_cap}, {"work.hidden", "work.allowed", "work.plain"});
  rejected(f.transport.handle_local(request("work.hidden"), f.binding, grant.token), "E_UNAUTHORIZED");
  rejected(f.transport.handle_local(request("work.plain"), f.binding, grant.token), "E_UNAUTHORIZED");
  successful(f.transport.handle_local(request("work.allowed"), f.binding, grant.token));
  require(hidden_calls == 0 && plain_calls == 0 && allowed_calls == 1,
          "Exposure rejection entered a callback or prevented the explicitly allowed action");
}

void native_credentials_replace_all_supplied_authority() {
  fixture f;
  issued_local_grant grant;
  bool received_native_context = false;
  f.action("work.allowed", shared_cap, [&](const bridge_request& actual) {
    received_native_context = actual.session_id == session && actual.origin == origin && actual.token == grant.token &&
        actual.authorization == grant.scope && !actual.native_user_gesture && actual.operation_id == "operation:one";
    return R"({"value":2})";
  });
  grant = f.grant({shared_cap}, {"work.allowed"});
  const auto page = f.store.issue(session, origin, {shared_cap});
  auto forged = request("work.allowed");
  forged.authorization = f.store.resolve(session, origin, page, shared_cap).scope;
  require(forged.authorization && forged.authorization != grant.scope, "Forged-context fixture lacks a distinct real scope");
  const auto response = f.transport.handle_local(forged, f.binding, grant.token);
  successful(response);
  require(received_native_context && response.session_id == session && response.request_id == forged.request_id &&
          response.correlation_id == forged.request_id && response.operation_id == forged.operation_id,
          "Local call trusted wire credentials/gesture or lost bounded response correlation");
}

void method_and_capability_both_authorize_and_denials_match() {
  fixture f;
  std::size_t calls = 0;
  const auto callback = [&](const bridge_request&) { ++calls; return R"({"value":2})"; };
  f.action("work.first", shared_cap, callback);
  f.action("work.second", shared_cap, callback);
  f.action("work.other", "work.other-cap", callback);
  f.action("work.hidden", shared_cap, callback, false);
  const auto grant = f.grant({shared_cap}, {"work.first", "work.other", "work.hidden", "work.unknown"});
  successful(f.transport.handle_local(request("work.first"), f.binding, grant.token));
  std::string denial_message;
  for (const auto* method : {"work.second", "work.other", "work.hidden", "work.unknown"}) {
    const auto response = f.transport.handle_local(request(method), f.binding, grant.token);
    rejected(response, "E_UNAUTHORIZED");
    if (denial_message.empty()) denial_message = response.error_message;
    require(response.error_message == denial_message, "Unknown/hidden/method/capability rejection disclosed registry state");
  }
  require(calls == 1, "A shared capability bypassed the method policy or denial entered a callback");
}

void channel_store_and_connection_objects_cannot_be_copied() {
  fixture f;
  std::size_t calls = 0;
  std::shared_ptr<const authorization_scope> seen;
  f.action("work.allowed", shared_cap, [&](const bridge_request& actual) {
    ++calls; seen = actual.authorization; return R"({"value":2})";
  });
  const auto grant = f.grant({shared_cap}, {"work.allowed"});
  const auto page = f.store.issue(session, origin, {shared_cap});
  permission_store foreign{&now};
  const auto foreign_binding = foreign.create_local_binding(session, origin);
  const auto reconnect = f.store.create_local_binding(session, origin);
  rejected(f.transport.handle_local(request("work.allowed"), f.binding, page), "E_UNAUTHORIZED");
  rejected(f.transport.handle_local(request("work.allowed"), foreign_binding, grant.token), "E_UNAUTHORIZED");
  rejected(f.transport.handle_local(request("work.allowed"), reconnect, grant.token), "E_UNAUTHORIZED");
  rejected(f.transport.handle_local(request("work.allowed"), {}, grant.token), "E_UNAUTHORIZED");
  rejected(f.transport.handle_local(request("work.allowed"), f.binding, ""), "E_UNAUTHORIZED");
  rejected(f.transport.handle_local(request("work.allowed"), f.binding,
      std::string(bridge_transport::max_string_bytes + 1U, 'x')), "E_UNAUTHORIZED");
  auto page_request = request("work.allowed");
  page_request.session_id = session; page_request.origin = origin;
  page_request.token = grant.token; page_request.authorization = grant.scope;
  rejected(f.transport.handle(page_request), "E_UNAUTHORIZED");
  require(calls == 0, "Copied bearer/binding/channel authority entered the business callback");
  page_request.token = page;
  successful(f.transport.handle(page_request));
  require(seen && seen != grant.scope && seen == f.store.resolve(session, origin, page, shared_cap).scope,
          "Page entry trusted a supplied local scope");
  successful(f.transport.handle_local(request("work.allowed"), f.binding, grant.token));
  require(calls == 2 && seen == grant.scope, "Isolation rejection damaged independent legitimate authority");
}

void static_and_action_input_errors_do_not_enter_callback() {
  fixture f;
  std::size_t calls = 0;
  f.action("work.allowed", shared_cap, [&](const bridge_request&) { ++calls; return R"({"value":2})"; });
  const auto grant = f.grant({shared_cap}, {"work.allowed"});
  for (const auto* params : {R"({"n":0})", R"({"n":11})", "{}", R"({"n":2,"extra":true})"})
    rejected(f.transport.handle_local(request("work.allowed", params), f.binding, grant.token), "E_INVALID_PARAMS");
  rejected(f.transport.handle_local(request("work.allowed", "[]"), f.binding, grant.token), "E_SCHEMA_INVALID");
  rejected(f.transport.handle_local(request("work.allowed", "{"), f.binding, grant.token), "E_MALFORMED_JSON");
  rejected(f.transport.handle_local(request("work.allowed", std::string(bridge_transport::max_params_bytes + 1U, ' ')),
      f.binding, grant.token), "E_LIMIT_MESSAGE_SIZE");
  auto invalid = request("work.allowed");
  invalid.timeout_ms = 0;
  rejected(f.transport.handle_local(invalid, f.binding, grant.token), "E_INVALID_PARAMS");
  invalid = request("work.allowed");
  invalid.operation_id = "unsafe operation";
  const auto invalid_operation = f.transport.handle_local(invalid, f.binding, grant.token);
  rejected(invalid_operation, "E_INVALID_PARAMS");
  require(invalid_operation.operation_id.empty(), "Invalid operation identity was reflected to local output");
  require(calls == 0, "Invalid input entered the business callback");
  successful(f.transport.handle_local(request("work.allowed", R"({"n":1})"), f.binding, grant.token));
  successful(f.transport.handle_local(request("work.allowed", R"({"n":10})"), f.binding, grant.token));
  require(calls == 2, "Valid action boundary inputs were rejected");
}

void results_and_business_errors_have_bounded_local_projection() {
  fixture f;
  f.action("work.extra", shared_cap, [](const bridge_request&) { return R"({"value":2,"private":"secret-output-marker"})"; });
  f.action("work.malformed", shared_cap, [](const bridge_request&) { return std::string{"{"}; });
  f.action("work.large", shared_cap, [](const bridge_request&) { return std::string(bridge_transport::max_message_bytes + 1U, 'x'); });
  f.action("work.errorA", shared_cap, [](const bridge_request&) { return R"({"error":"PRIVATE_A","message":"secret-error-A"})"; });
  f.action("work.errorB", shared_cap, [](const bridge_request&) { return R"({"error":"PRIVATE_B","message":"secret-error-B"})"; });
  f.action("work.throwA", shared_cap, [](const bridge_request&) -> std::string { throw std::runtime_error("secret-exception-A"); });
  f.action("work.throwB", shared_cap, [](const bridge_request&) -> std::string { throw 42; });
  f.action("work.valid", shared_cap, [](const bridge_request&) { return R"({"value":2})"; });
  const auto grant = f.grant({shared_cap}, {"work.extra", "work.malformed", "work.large", "work.errorA", "work.errorB",
      "work.throwA", "work.throwB", "work.valid"});
  rejected(f.transport.handle_local(request("work.extra"), f.binding, grant.token), "E_SCHEMA_INVALID");
  rejected(f.transport.handle_local(request("work.malformed"), f.binding, grant.token), "E_SCHEMA_INVALID");
  rejected(f.transport.handle_local(request("work.large"), f.binding, grant.token), "E_LIMIT_MESSAGE_SIZE");
  const auto error_a = f.transport.handle_local(request("work.errorA"), f.binding, grant.token);
  const auto error_b = f.transport.handle_local(request("work.errorB"), f.binding, grant.token);
  rejected(error_a, "E_ACTION_REJECTED"); rejected(error_b, "E_ACTION_REJECTED");
  require(!error_a.error_message.empty() && error_a.error_message == error_b.error_message &&
          error_a.error_message.find("secret-") == std::string::npos &&
          error_a.error_message.find("PRIVATE_") == std::string::npos,
          "Local business error forwarded handler-controlled code/text");
  const auto throw_a = f.transport.handle_local(request("work.throwA"), f.binding, grant.token);
  const auto throw_b = f.transport.handle_local(request("work.throwB"), f.binding, grant.token);
  rejected(throw_a, "E_NATIVE_FAILURE"); rejected(throw_b, "E_NATIVE_FAILURE");
  require(!throw_a.error_message.empty() && throw_a.error_message == throw_b.error_message &&
          throw_a.error_message.find("secret-") == std::string::npos, "Local exception text escaped into the response");
  successful(f.transport.handle_local(request("work.valid"), f.binding, grant.token));
  // Schema validity is not a sensitivity policy. Applications approve the projection.
}

void invalidation_before_admission_prevents_callback() {
  for (int invalidation = 0; invalidation < 3; ++invalidation) {
    reset_clock();
    fixture f;
    std::size_t calls = 0;
    f.action("work.allowed", shared_cap, [&](const bridge_request&) { ++calls; return R"({"value":2})"; });
    const auto grant = f.grant({shared_cap}, {"work.allowed"});
    if (invalidation == 0) f.binding->close();
    else if (invalidation == 1) require(f.store.revoke_scope(grant.scope), "Precise revoke fixture failed");
    else clock_ms = lifetime;
    rejected(f.transport.handle_local(request("work.allowed"), f.binding, grant.token), "E_UNAUTHORIZED");
    require(calls == 0, "Closed/revoked/expired authority entered a callback");
  }
}

void numeric_overflow_and_deep_json_stay_inside_the_boundary() {
  fixture f;
  std::size_t calls = 0;
  f.action("work.allowed", shared_cap, [&](const bridge_request&) { ++calls; return R"({"value":2})"; });
  f.action("work.overflow", shared_cap, [&](const bridge_request&) { ++calls; return R"({"value":1e400})"; });
  const auto grant = f.grant({shared_cap}, {"work.allowed", "work.overflow"});
  rejected(f.transport.handle_local(request("work.allowed", R"({"n":1e400})"), f.binding, grant.token), "E_SCHEMA_INVALID");
  std::string deep;
  for (int i = 0; i < 4000; ++i) deep += R"({"x":)";
  deep += "0";
  deep.append(4000, '}');
  require(deep.size() < bridge_transport::max_params_bytes, "Deep fixture exceeded the byte bound");
  rejected(f.transport.handle_local(request("work.allowed", deep), f.binding, grant.token), "E_SCHEMA_INVALID");
  require(calls == 0, "Unrepresentable/deep params entered a callback");
  rejected(f.transport.handle_local(request("work.overflow"), f.binding, grant.token), "E_SCHEMA_INVALID");
  require(calls == 1, "Numeric output fixture did not execute its actual callback");

  const auto page = f.store.issue(session, origin, {shared_cap});
  const auto packet = [&](const std::string& params) {
    return std::string{R"({"requestId":"page:overflow","sessionId":")"} + session +
        R"(","origin":")" + origin + R"(","token":")" + page +
        R"(","method":"work.allowed","params":)" + params + "}";
  };
  for (const auto& params : {std::string{R"({"n":1e400})"}, deep}) {
    const auto result = json::parse(f.transport.handle_json(packet(params)));
    require(!result.at("ok").get<bool>() && result.at("error").at("code") == "E_SCHEMA_INVALID",
            "Page numeric/depth parser exception escaped instead of a bounded schema error");
  }
  require(calls == 1, "Page overflow/deep envelope entered a callback");
  successful(f.transport.handle_local(request("work.allowed"), f.binding, grant.token));
}

void invalidation_in_callback_drops_every_result_path() {
  for (int invalidation = 0; invalidation < 3; ++invalidation) {
    for (int result_kind = 0; result_kind < 4; ++result_kind) {
      reset_clock();
      fixture f;
      issued_local_grant grant;
      std::size_t calls = 0;
      bool side_effect = false;
      f.action("work.allowed", shared_cap, [&](const bridge_request&) -> std::string {
        ++calls; side_effect = true;
        if (invalidation == 0) f.binding->close();
        else if (invalidation == 1) require(f.store.revoke_scope(grant.scope), "Callback revoke fixture failed");
        else clock_ms = lifetime;
        if (result_kind == 1) throw std::runtime_error("secret-late-exception");
        if (result_kind == 2) return std::string(bridge_transport::max_message_bytes + 1U, 'x');
        if (result_kind == 3) return R"({"error":"PRIVATE_LATE","message":"secret-late-rejection"})";
        return R"({"value":2})";
      });
      grant = f.grant({shared_cap}, {"work.allowed"});
      rejected(f.transport.handle_local(request("work.allowed"), f.binding, grant.token), "E_UNAUTHORIZED");
      require(calls == 1 && side_effect, "Late invalidation fixture did not execute; close is not a rollback/barrier");
    }
  }
}

void reused_bearer_cannot_authorize_a_previous_scope_result() {
  fixture f;
  permission_store_test_access::entropy(f.store, &repeated_entropy);
  issued_local_grant first, replacement;
  std::size_t calls = 0;
  f.action("work.allowed", shared_cap, [&](const bridge_request& actual) {
    ++calls;
    if (calls == 1) {
      require(actual.authorization == first.scope && f.store.revoke_scope(first.scope), "Original scope was not admitted/revoked");
      replacement = f.grant({shared_cap}, {"work.allowed"});
    }
    return R"({"value":2})";
  });
  first = f.grant({shared_cap}, {"work.allowed"});
  rejected(f.transport.handle_local(request("work.allowed"), f.binding, first.token), "E_UNAUTHORIZED");
  require(replacement.scope && replacement.scope != first.scope && replacement.token == first.token &&
          !first.scope->active() && replacement.scope->active(), "TEST_ONLY fixture did not actually replace the bearer owner");
  successful(f.transport.handle_local(request("work.allowed"), f.binding, replacement.token));
  require(calls == 2, "New scope cannot independently admit its own later call");
}

void private_renewal_preserves_scope_but_never_becomes_an_action() {
  fixture f;
  std::shared_ptr<const authorization_scope> seen;
  std::string seen_bearer;
  f.action("work.allowed", shared_cap, [&](const bridge_request& actual) {
    seen = actual.authorization; seen_bearer = actual.token; return R"({"value":2})";
  });
  require(f.transport.enable_session_renewal(), "Renewal registration failed");
  const auto grant = f.grant({shared_cap, renewal}, {"work.allowed", renewal});
  clock_ms = renew_at;
  const auto lease = f.store.renew_local(f.binding, grant.token, "renew:one");
  require(lease.ok() && !lease.token.empty() && lease.token != grant.token, "Native-only renewal fixture failed");
  successful(f.transport.handle_local(request("work.allowed"), f.binding, lease.token));
  require(seen == grant.scope && seen_bearer == lease.token, "Renewed bearer changed the native scope or trusted wire token");
  successful(f.transport.handle_local(request("work.allowed"), f.binding, grant.token));
  clock_ms = renew_at + 30000;
  rejected(f.transport.handle_local(request("work.allowed"), f.binding, grant.token), "E_UNAUTHORIZED");
  successful(f.transport.handle_local(request("work.allowed"), f.binding, lease.token));
  require(grant.scope->active() && seen == grant.scope, "Old alias expiry revoked renewed authority");
  rejected(f.transport.handle_local(request(renewal, R"({"operationId":"renew:wire"})"), f.binding, lease.token), "E_UNAUTHORIZED");
}

void discovery_filters_before_pagination_without_callbacks() {
  fixture f;
  std::size_t action_calls = 0;
  const auto callback = [&](const bridge_request&) { ++action_calls; return R"({"value":2})"; };
  f.action("action.a_hidden", shared_cap, callback, false);
  f.action("action.b_first", shared_cap, callback);
  f.action("action.c_denied", shared_cap, callback);
  f.action("action.d_second", shared_cap, callback);
  f.action("action.e_othercap", "work.other-cap", callback);
  require(f.registry.register_method("plain.f", shared_cap,
      [&](std::string_view) { ++action_calls; return R"({"value":2})"; }), "Plain discovery fixture failed");
  const auto grant = f.grant({"bridge.describe", shared_cap}, {"bridge.describe", "action.a_hidden", "action.b_first",
      "action.d_second", "action.e_othercap", "plain.f"});
  const auto discover = [&](int cursor) {
    return json::parse(f.transport.describe_local(request("bridge.describe", json{{"cursor", cursor}, {"limit", 1}}.dump()),
        f.binding, grant.token));
  };
  const auto first = discover(0), second = discover(1), terminal = discover(2);
  require(first.at("protocol") == "soui.capabilities/1" && first.at("methods").size() == 1 &&
          first.at("methods").at(0).at("name") == "action.b_first" && first.at("nextCursor") == 1 &&
          second.at("methods").size() == 1 && second.at("methods").at(0).at("name") == "action.d_second" &&
          second.at("nextCursor").is_null() && terminal.at("methods").empty() && terminal.at("nextCursor").is_null(),
          "Hidden/plain/method-denied/capability-denied registrations affected local pagination");
  require(first.at("methods").at(0).at("capability") == shared_cap &&
          first.at("methods").at(0).at("action") == json::parse(contract()), "Discovery lost the existing action contract");
  discovery_rejected(f.transport.describe_local(request("bridge.describe", R"({"cursor":3,"limit":1})"),
      f.binding, grant.token), "E_INVALID_PARAMS");
  for (const auto* params : {R"({"cursor":-1})", R"({"limit":0})", R"({"limit":65})", R"({"unknown":1})"}) {
    discovery_rejected(f.transport.describe_local(request("bridge.describe", params), f.binding, grant.token), "E_INVALID_PARAMS");
    rejected(f.transport.handle_local(request("bridge.describe", params), f.binding, grant.token), "E_INVALID_PARAMS");
  }
  const auto no_discovery_method = f.grant({"bridge.describe", shared_cap}, {"action.b_first"});
  const auto no_discovery_cap = f.grant({shared_cap}, {"bridge.describe", "action.b_first"});
  discovery_rejected(f.transport.describe_local(request("bridge.describe", "{}"), f.binding, no_discovery_method.token));
  discovery_rejected(f.transport.describe_local(request("bridge.describe", "{}"), f.binding, no_discovery_cap.token));
  const auto via_handle = f.transport.handle_local(request("bridge.describe", R"({"cursor":0,"limit":1})"), f.binding, grant.token);
  require(via_handle.ok && json::parse(via_handle.payload_json) == first && action_calls == 0 && f.canonical_calls == 0,
          "Canonical local handle used a callback or diverged from direct discovery");
}

void canonical_discovery_cannot_be_redefined_by_a_typed_control() {
  fixture f(false);
  std::size_t callbacks = 0;
  for (const auto* method : {"bridge.describe", renewal, "task.subscribe", "task.unsubscribe"}) {
    f.action(method, method, [&](const bridge_request&) { ++callbacks; return R"({"value":2})"; }, false);
    require(!f.registry.allow_local_action(method), "Reserved typed control acquired local exposure");
  }
  const auto grant = f.grant({"bridge.describe", renewal, "task.subscribe", "task.unsubscribe"},
      {"bridge.describe", renewal, "task.subscribe", "task.unsubscribe"});
  const auto local = f.transport.handle_local(request("bridge.describe", R"({"cursor":0,"limit":1})"), f.binding, grant.token);
  require(local.ok && json::parse(local.payload_json).at("methods").empty() && callbacks == 0,
          "Typed canonical control redefined local input/output schema, called business code or advertised itself");
  for (const auto* method : {renewal, "task.subscribe", "task.unsubscribe"})
    rejected(f.transport.handle_local(request(method), f.binding, grant.token), "E_UNAUTHORIZED");
  auto page = request("bridge.describe", "{}");
  page.session_id = session; page.origin = origin; page.token = f.store.issue(session, origin, {"bridge.describe"});
  rejected(f.transport.handle(page), "E_INVALID_PARAMS");
  page.params_json = R"({"n":2})";
  successful(f.transport.handle(page));
  require(callbacks == 1, "Canonical local special case changed the existing typed page behavior");

  fixture missing(false);
  const auto absent_grant = missing.grant({"bridge.describe"}, {"bridge.describe"});
  discovery_rejected(missing.transport.describe_local(request("bridge.describe", "{}"), missing.binding, absent_grant.token));
  fixture wrong(false);
  wrong.action("bridge.describe", "wrong.describe", [&](const bridge_request&) { ++callbacks; return R"({"value":2})"; }, false);
  const auto wrong_grant = wrong.grant({"bridge.describe", "wrong.describe"}, {"bridge.describe"});
  discovery_rejected(wrong.transport.describe_local(request("bridge.describe", "{}"), wrong.binding, wrong_grant.token));
  rejected(wrong.transport.handle_local(request("bridge.describe", "{}"), wrong.binding, wrong_grant.token), "E_UNAUTHORIZED");
  require(callbacks == 1, "Missing/noncanonical registration executed a local discovery callback");
}

void discovery_rechecks_authority_after_a_mid_read_close() {
  fixture f;
  std::size_t calls = 0;
  f.action("action.first", shared_cap, [&](const bridge_request&) { ++calls; return R"({"value":2})"; });
  f.action("action.second", shared_cap, [&](const bridge_request&) { ++calls; return R"({"value":2})"; });
  const auto grant = f.grant({"bridge.describe", shared_cap}, {"bridge.describe", "action.first", "action.second"});
  clock_reads = 0; close_on_read = 2; close_on_clock = f.binding;
  const auto result = f.transport.describe_local(request("bridge.describe", "{}"), f.binding, grant.token);
  require(clock_closed && !f.binding->active(), "TEST_ONLY mid-read close hook was not reached");
  discovery_rejected(result);
  require(calls == 0 && f.canonical_calls == 0, "Discovery executed business code while checking authorization");
  close_on_clock.reset(); close_on_read = 0;
}

json bootstrap(soui::sdk::json_bridge& bridge) {
  const auto result = json::parse(bridge.handle_json(json{{"requestId", "local:bootstrap"}, {"sessionId", ""},
      {"origin", ""}, {"token", ""}, {"method", "bridge.bootstrap"}, {"params", {{"protocol", "soui.bridge/1"}}}}.dump()));
  require(result.at("ok").get<bool>(), "Page bootstrap failed");
  return result.at("result");
}
json page_call(soui::sdk::json_bridge& bridge, const json& auth, const char* method, json params = json{{"n", 2}}) {
  return json::parse(bridge.handle_json(json{{"requestId", "page:request"}, {"sessionId", auth.at("sessionId")},
      {"origin", auth.at("origin")}, {"token", auth.at("token")}, {"method", method}, {"params", std::move(params)}}.dump()));
}
void facade_freezes_flags_and_preserves_page_grants_and_legacy_errors() {
  soui::sdk::json_bridge bridge{origin};
  std::size_t action_calls = 0;
  const auto callback = [&](const bridge_request&) { ++action_calls; return std::string{R"({"value":2})"}; };
  require(bridge.register_action("work.marked", shared_cap, contract(), callback) &&
          bridge.register_action("work.unmarked", shared_cap, contract(), callback), "Facade action registration failed");
  require(bridge.register_method("work.legacy", shared_cap,
      [](std::string_view) { return R"({"error":"LEGACY_CODE","message":"legacy application message"})"; }),
      "Facade legacy registration failed");
  require(bridge.allow_local_action("work.marked") && !bridge.allow_local_action("work.legacy") &&
          !bridge.allow_local_action("work.missing"), "Facade exposure did not require an existing typed action");
  require(bridge.enable_session_renewal() && !bridge.allow_local_action(renewal), "Facade exposed the private renewal control");
  for (const auto* method : {"bridge.describe", "task.start", "task.query", "task.cancel", "task.subscribe", "task.unsubscribe"}) {
    require(!bridge.allow_local_action(method), "Facade converted a canonical/plain built-in into a local action");
    require(!bridge.register_action(method, shared_cap, contract(), callback), "Facade replaced an already registered plain built-in");
  }
  const auto auth = bootstrap(bridge);
  require(!bridge.allow_local_action("work.unmarked") && !bridge.allow_local_action("work.marked") &&
          !bridge.register_action("work.late", shared_cap, contract(), callback), "Bootstrap did not freeze native registration/exposure");
  for (const auto* method : {"work.marked", "work.unmarked"}) {
    const auto result = page_call(bridge, auth, method);
    require(result.at("ok").get<bool>() && result.at("result") == json{{"value", 2}}, "Local exposure changed default page authorization");
  }
  const auto legacy = page_call(bridge, auth, "work.legacy");
  require(!legacy.at("ok").get<bool>() && legacy.at("error").at("code") == "LEGACY_CODE" &&
          legacy.at("error").at("message") == "legacy application message", "Local sanitization changed legacy page business errors");
  for (const auto* field : {"localBearer", "nativeConnectionBinding", "authorization", "nativeUserGesture"}) {
    auto packet = json{{"requestId", "page:forged"}, {"sessionId", auth.at("sessionId")},
        {"origin", auth.at("origin")}, {"token", auth.at("token")}, {"method", "work.marked"}, {"params", {{"n", 2}}}};
    packet[field] = "untrusted-wire-authority";
    const auto result = json::parse(bridge.handle_json(packet.dump()));
    require(!result.at("ok").get<bool>() && result.at("error").at("code") == "E_SCHEMA_INVALID",
            "Page JSON acquired a native/local credential or gesture field");
  }
  require(action_calls == 2, "Facade registration rejection invoked an action");

  soui::sdk::json_bridge restricted{origin};
  std::size_t restricted_calls = 0;
  require(restricted.register_action("work.local", "work.local-cap", contract(),
      [&](const bridge_request&) { ++restricted_calls; return R"({"value":2})"; }) &&
      restricted.set_session_capabilities({"bridge.describe"}) && restricted.allow_local_action("work.local"),
      "Restricted page/local registration fixture failed");
  const auto restricted_auth = bootstrap(restricted);
  const auto denied = page_call(restricted, restricted_auth, "work.local");
  require(!denied.at("ok").get<bool>() && denied.at("error").at("code") == "E_UNAUTHORIZED" && restricted_calls == 0,
          "Local exposure silently added a page grant capability");
  const auto discovered = page_call(restricted, restricted_auth, "bridge.describe", json::object());
  require(discovered.at("ok").get<bool>(), "Restricted page discovery failed");
  for (const auto& item : discovered.at("result").at("methods"))
    require(item.at("name") != "work.local", "Local marking made an ungranted page capability discoverable");
}
}

int main() {
  const std::pair<const char*, void (*)()> cases[] = {
      {"explicit typed exposure", exposure_is_explicit_and_typed},
      {"native credential rewrite", native_credentials_replace_all_supplied_authority},
      {"capability and method authorization", method_and_capability_both_authorize_and_denials_match},
      {"channel and connection object isolation", channel_store_and_connection_objects_cannot_be_copied},
      {"input rejection before callback", static_and_action_input_errors_do_not_enter_callback},
      {"bounded output and sanitized business failure", results_and_business_errors_have_bounded_local_projection},
      {"numeric overflow and bounded parser depth", numeric_overflow_and_deep_json_stay_inside_the_boundary},
      {"pre-admission invalidation", invalidation_before_admission_prevents_callback},
      {"late invalidation on all result paths", invalidation_in_callback_drops_every_result_path},
      {"same bearer new scope result rejection", reused_bearer_cannot_authorize_a_previous_scope_result},
      {"private renewal preserves scope", private_renewal_preserves_scope_but_never_becomes_an_action},
      {"filtered discovery pagination", discovery_filters_before_pagination_without_callbacks},
      {"canonical discovery and reserved controls", canonical_discovery_cannot_be_redefined_by_a_typed_control},
      {"discovery mid-read close", discovery_rechecks_authority_after_a_mid_read_close},
      {"facade freeze and page compatibility", facade_freezes_flags_and_preserves_page_grants_and_legacy_errors},
  };
  for (const auto& test : cases) {
    reset_clock();
    try {
      test.second();
      std::cout << "PASS " << test.first << '\n';
    } catch (const std::exception& error) {
      std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
      return 1;
    } catch (...) {
      std::cerr << "FAIL " << test.first << ": unexpected exception\n";
      return 1;
    }
  }
  return 0;
}
