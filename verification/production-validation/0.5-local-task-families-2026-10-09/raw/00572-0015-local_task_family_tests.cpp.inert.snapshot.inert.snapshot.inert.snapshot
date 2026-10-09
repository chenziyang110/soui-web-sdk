#include <soui/runtime/services/task/task_service.h>
#include <soui/runtime/core/task/task_store.h>
#include <soui/json_bridge.h>
#include <json.hpp>
#ifdef SOUI_TASK_FAMILY_TEST_ONLY
#include "../../sdk/src/runtime/services/task/task_family_test_hooks.h"
#endif

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace soui::runtime;
using namespace std::chrono_literals;
using json = nlohmann::json;
constexpr const char* session = "family-session";
constexpr const char* origin = "app://family-tests";

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

template<class Predicate> void await(Predicate predicate, const char* message) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!predicate()) {
    require(std::chrono::steady_clock::now() < deadline, message);
    std::this_thread::yield();
  }
}

class gate final {
 public:
  void arrive_and_wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    ++arrivals_;
    changed_.notify_all();
    require(changed_.wait_for(lock, 5s, [this] { return open_; }), "handler gate timed out");
  }
  void await_arrivals(unsigned count) {
    std::unique_lock<std::mutex> lock(mutex_);
    require(changed_.wait_for(lock, 5s, [this, count] { return arrivals_ >= count; }), "handler did not reach gate");
  }
  void release() {
    std::lock_guard<std::mutex> lock(mutex_);
    open_ = true;
    changed_.notify_all();
  }
 private:
  std::mutex mutex_;
  std::condition_variable changed_;
  unsigned arrivals_{0};
  bool open_{false};
};

struct release_guard { gate& value; ~release_guard() { value.release(); } };

local_task_family_definition definition(std::string name = "one") {
  return {std::move(name), "work", "local.start", "local.query", "local.cancel",
      R"({"type":"object","properties":{"value":{"type":"integer","minimum":0,"maximum":100}},"required":["value"],"additionalProperties":false})",
      R"({"type":"object","properties":{"safe":{"type":"integer","minimum":0,"maximum":100}},"required":["safe"],"additionalProperties":false})",
      [](const task_outcome& outcome) {
        const auto raw = json::parse(outcome.result_json);
        return json{{"safe", raw.value("value", 0)}}.dump();
      }};
}

const std::vector<std::string> all_capabilities{"local.start", "local.query", "local.cancel", "shared.cap",
    "task.start", "task.query", "task.cancel", "task.subscribe", "bridge.describe", "bridge.session.renew"};
const std::vector<std::string> all_methods{"one.start", "one.query", "one.cancel", "two.start", "two.query", "two.cancel",
    "three.start", "three.query", "three.cancel", "task.start", "task.query", "task.cancel", "task.subscribe",
    "bridge.describe", "bridge.session.renew"};

struct fixture {
  explicit fixture(permission_store::clock_source now = nullptr) : permissions{now} {
    binding = permissions.create_local_binding(session, origin);
    grant = permissions.issue_local(binding, all_capabilities, all_methods);
    scope = permissions.resolve_local(binding, grant.token, "one.start", "local.start").scope;
    require(scope == grant.scope, "local scope did not resolve to the actual issued owner");
  }
  void handler(task_service::handler callback = [](const auto& invocation, const task_control&) {
    const auto input = json::parse(invocation.parameters_json);
    return task_outcome{result_kind::success, json{{"value", input.at("value")}, {"secret", "private-result"}}.dump(),
                        "private-message"};
  }) { require(service.register_handler("work", std::move(callback)), "existing kind registration failed"); }
  std::shared_ptr<const local_task_family> family(local_task_family_definition value = definition()) {
    auto result = service.register_local_family(registry, std::move(value));
    require(result && result->active(), "family registration failed");
    return result;
  }
  bridge_request request(std::string method, json params,
                         std::shared_ptr<const authorization_scope> authorization = {}) const {
    bridge_request request;
    request.request_id = "request"; request.session_id = session; request.origin = origin;
    request.token = grant.token; request.method = std::move(method); request.params_json = params.dump();
    request.authorization = authorization ? std::move(authorization) : scope;
    return request;
  }
  json start(const std::shared_ptr<const local_task_family>& family, std::string operation,
             int value = 1, std::string name = "one") {
    return json::parse(service.start_local(family, request(name + ".start", {{"operationId", operation}, {"input", {{"value", value}}}})));
  }
  json query(const std::shared_ptr<const local_task_family>& family, const std::string& id, std::string name = "one") {
    return json::parse(service.query_local(family, request(name + ".query", {{"taskId", id}})));
  }
  json terminal(const std::shared_ptr<const local_task_family>& family, const std::string& id, std::string name = "one") {
    json result;
    await([&] {
      result = query(family, id, name);
      const auto state = result.value("state", "");
      return state == "succeeded" || state == "failed" || state == "partial" || state == "cancelled";
    }, "task did not reach a retained terminal state");
    return result;
  }
  permission_store permissions;
  capability_registry registry;
  std::shared_ptr<native_connection_binding> binding;
  issued_local_grant grant;
  std::shared_ptr<const authorization_scope> scope;
  task_service service; // Joins before permission, registry and borrowed scopes disappear.
};

void test_atomic_registration() {
  fixture f; f.handler();
  require(f.registry.register_method("one.query", "other", [](auto) { return "{}"; }), "collision setup failed");
  require(!f.service.register_local_family(f.registry, definition()), "colliding transaction succeeded");
  require(!f.registry.contains("one.start") && !f.registry.contains("one.cancel") && f.registry.contains("one.query"),
          "collision partially published the family");
  auto bad = definition("two"); bad.result_schema_json = R"({"type":"object"})";
  require(!f.service.register_local_family(f.registry, std::move(bad)), "invalid schema was registered");
  require(!f.registry.contains("two.start") && !f.registry.contains("two.query") && !f.registry.contains("two.cancel"),
          "schema failure partially published actions");
  auto absent = definition("two"); absent.kind = "missing";
  require(!f.service.register_local_family(f.registry, std::move(absent)), "unregistered business kind was accepted");
  const auto family = f.family(definition("two"));
  require(!f.service.register_local_family(f.registry, definition("task")) && !f.registry.contains("task.start"),
          "family registration occupied the plain built-in task namespace");
  require(!f.service.register_local_family(f.registry, definition("two")), "duplicate family was accepted");
  require(f.terminal(family, f.start(family, "valid", 7, "two").at("taskId"), "two").at("result").at("safe") == 7,
          "failed registration changed the actual existing handler");
}

struct throwing_copy {
  std::shared_ptr<std::atomic<bool>> fail;
  std::shared_ptr<std::atomic<unsigned>> copies;
  throwing_copy(std::shared_ptr<std::atomic<bool>> value, std::shared_ptr<std::atomic<unsigned>> count)
      : fail{std::move(value)}, copies{std::move(count)} {}
  throwing_copy(const throwing_copy& other) : fail{other.fail}, copies{other.copies} {
    ++*copies;
    if (fail->load()) throw std::bad_alloc{};
  }
  std::string operator()(const bridge_request&) const { return "{}"; }
};

void test_registry_immutable_callback() {
  fixture f; f.handler();
  auto fail = std::make_shared<std::atomic<bool>>(false);
  auto copies = std::make_shared<std::atomic<unsigned>>(0);
  require(f.registry.register_contextual_method("unrelated", "other", throwing_copy{fail, copies}), "copy target setup failed");
  const auto before = copies->load();
  fail->store(true);
  const auto family = f.family();
  require(copies->load() == before && f.registry.contains("unrelated"), "candidate registry copied an application functor");
  require(f.terminal(family, f.start(family, "retry").at("taskId")).at("state") == "succeeded", "shared callback snapshot changed registration");
}

void test_family_and_plain_isolation() {
  fixture f; f.handler();
  const auto one = f.family(), two = f.family(definition("two"));
  const auto id = f.start(one, "operation").at("taskId").get<std::string>();
  require(f.start(one, "operation").at("duplicate") == true, "same retained operation executed twice");
  require(f.start(one, "operation", 2).value("error", "") == "E_OPERATION_CONFLICT", "changed input reused operation");
  require(f.start(two, "operation", 1, "two").value("error", "") == "E_OPERATION_CONFLICT", "cross-family operation selected a task");
  require(f.query(two, id, "two").value("error", "") == "E_TASK_NOT_FOUND", "taskId crossed family");
  require(json::parse(f.service.query_local(two, f.request("two.query", {{"operationId", "operation"}}))).value("error", "") ==
              "E_TASK_NOT_FOUND", "operationId crossed family");
  require(json::parse(f.service.cancel_local(two, f.request("two.cancel", {{"taskId", id}}))).value("error", "") ==
              "E_TASK_NOT_FOUND", "cancel crossed family");
  const auto plain = json::parse(f.service.start(f.request("task.start", {{"kind", "work"}, {"operationId", "plain"},
      {"input", {{"value", 3}}}}))).at("taskId").get<std::string>();
  require(f.query(one, plain).value("error", "") == "E_TASK_NOT_FOUND", "same scope/kind plain task acquired family identity");
  require(json::parse(f.service.query(f.request("task.query", {{"taskId", id}}))).value("error", "") == "E_TASK_NOT_FOUND",
          "plain query exposed family data");
  require(json::parse(f.service.cancel(f.request("task.cancel", {{"taskId", id}}))).value("error", "") == "E_TASK_NOT_FOUND",
          "plain cancel changed family task");
  require(json::parse(f.service.subscribe(f.request("task.subscribe", {{"taskId", id}, {"eventName", "task.progress"}})))
              .value("error", "") == "E_TASK_NOT_FOUND", "plain subscription exposed family events");
  const auto alternate_binding = f.permissions.create_local_binding(session, origin);
  const auto alternate = f.permissions.issue_local(alternate_binding, all_capabilities, all_methods);
  require(json::parse(f.service.query_local(one, f.request("one.query", {{"taskId", id}}, alternate.scope)))
              .value("error", "") == "E_TASK_NOT_FOUND", "same document alternate scope selected task");
  f.terminal(one, id);
}

void test_method_capability_and_page_isolation() {
  fixture f; f.handler(); const auto one = f.family();
  auto query_only = f.permissions.issue_local(f.binding, {"local.start", "local.query"}, {"one.query"});
  require(json::parse(f.service.start_local(one, f.request("one.start", {{"operationId", "x"}, {"input", {{"value", 1}}}}, query_only.scope)))
              .value("error", "") == "E_UNAUTHORIZED", "capability bypassed method policy");
  auto wrong_cap = f.permissions.issue_local(f.binding, {"local.query"}, {"one.start"});
  require(json::parse(f.service.start_local(one, f.request("one.start", {{"operationId", "x"}, {"input", {{"value", 1}}}}, wrong_cap.scope)))
              .value("error", "") == "E_UNAUTHORIZED", "method bypassed capability policy");
  const auto page_token = f.permissions.issue(session, origin, all_capabilities);
  const auto page = f.permissions.resolve(session, origin, page_token, "local.start").scope;
  auto request = f.request("one.start", {{"operationId", "x"}, {"input", {{"value", 1}}}}, page);
  require(json::parse(f.service.start_local(one, request)).value("error", "") == "E_UNAUTHORIZED", "page scope became a local family scope");
  request.token = page_token;
  bridge_transport transport{f.registry, f.permissions};
  require(!transport.handle(request).ok, "page transport reached local-only family despite explicit same capability");
  require(f.registry.register_contextual_method("bridge.describe", "bridge.describe", [](const auto&) { return "{}"; }), "discovery setup failed");
  auto discovery = f.request("bridge.describe", json::object(), page); discovery.token = page_token;
  require(json::parse(transport.describe(discovery)).at("methods").size() == 1U, "page discovery exposed local family");
  discovery.token = f.grant.token;
  require(json::parse(transport.describe_local(discovery, f.binding, f.grant.token)).at("methods").size() == 3U,
          "local discovery did not preserve the complete typed family");
  auto shared = definition("two"); shared.start_capability = shared.query_capability = shared.cancel_capability = "shared.cap";
  const auto two = f.family(std::move(shared));
  const auto limited = f.permissions.issue_local(f.binding, {"shared.cap"}, {"two.start"});
  const auto started = json::parse(f.service.start_local(two,
      f.request("two.start", {{"operationId", "shared"}, {"input", {{"value", 1}}}}, limited.scope)));
  require(started.contains("taskId"), "shared-cap start was rejected");
  require(json::parse(f.service.query_local(two, f.request("two.query", {{"taskId", started.at("taskId")}}, limited.scope)))
              .value("error", "") == "E_UNAUTHORIZED", "shared cap erased distinct action method policy");
}

void test_strict_direct_input() {
  std::atomic<unsigned> calls{0};
  fixture f; f.handler([&](const auto&, const auto&) { ++calls; return task_outcome{}; }); const auto one = f.family();
  for (const auto& params : std::vector<json>{json::object(), {{"operationId", "x"}, {"input", {{"value", 1}}}, {"kind", "work"}},
      {{"operationId", "x"}, {"input", {{"value", 1}, {"extra", "secret"}}}},
      {{"operationId", "not ascii \u00e9"}, {"input", {{"value", 1}}}},
      {{"operationId", "x"}, {"input", {{"value", 101}}}}}) {
    require(json::parse(f.service.start_local(one, f.request("one.start", params))).value("error", "") == "E_INVALID_PARAMS",
            "direct core bypassed fixed schema or ASCII selector");
  }
  auto request = f.request("one.start", json::object());
  for (const auto& raw : {std::string(32769, ' '), std::string{"{\"operationId\":\"x\",\"input\":{\"value\":1e400}}"},
      std::string(40, '[') + "0" + std::string(40, ']')}) {
    request.params_json = raw;
    require(json::parse(f.service.start_local(one, request)).value("error", "") == "E_INVALID_PARAMS", "direct parser ignored byte/depth/numeric budget");
  }
  for (const auto& params : std::vector<json>{json::object(), {{"taskId", "x"}, {"operationId", "y"}},
      {{"operationId", 1}}, {{"taskId", "x"}, {"extra", true}}}) {
    require(json::parse(f.service.query_local(one, f.request("one.query", params))).value("error", "") == "E_INVALID_PARAMS",
            "direct query did not enforce XOR/extras/types");
  }
  require(json::parse(f.service.cancel_local(one, f.request("one.cancel", {{"taskId", "x"}, {"extra", true}})))
              .value("error", "") == "E_INVALID_PARAMS", "direct cancel allowed extra fields");
  require(calls.load() == 0, "rejected input ran the handler");
}

void test_projection_once_and_actual_cancel() {
  gate running; std::atomic<unsigned> projected{0}; std::atomic<bool> projection_reentered{false};
  std::shared_ptr<const local_task_family> one; std::string id;
  capability_registry::contextual_handler query_callback;
  bridge_request reentrant_request;
  fixture f; release_guard release{running};
  f.handler([&](const auto&, const task_control& control) {
    require(control.report_progress(50, "private-progress"), "valid progress rejected");
    running.arrive_and_wait();
    require(control.cancellation_requested(), "handler did not observe cooperative cancellation");
    return task_outcome{result_kind::success, R"({"value":9,"secret":"private-result"})", "private-message"};
  });
  auto value = definition(); value.project_result = [&](const task_outcome& outcome) {
    ++projected;
    projection_reentered = json::parse(query_callback(reentrant_request)).at("state") == "cancel-pending";
    require(outcome.kind == result_kind::success, "cancel request rewrote the actual handler outcome");
    return R"({"safe":9})";
  };
  one = f.family(std::move(value));
  query_callback = *f.registry.handler_for("one.query");
  id = f.start(one, "cancel").at("taskId").get<std::string>(); running.await_arrivals(1);
  reentrant_request = f.request("one.query", {{"taskId", id}});
  const auto progress = f.query(one, id);
  require(progress.at("progress") == 50 && progress.at("sequence").is_string() && !progress.contains("message"), "progress leaked raw text or unsafe sequence");
  require(json::parse(f.service.cancel_local(one, f.request("one.cancel", {{"taskId", id}}))).at("cancelRequested") == true,
          "cooperative cancel did not request cancellation");
  running.release(); const auto result = f.terminal(one, id);
  require(result.at("state") == "succeeded" && result.at("result") == json{{"safe", 9}} && projected == 1 && projection_reentered,
          "projection was repeated, held the core lock, or changed terminal outcome");
  for (unsigned index = 0; index < 4; ++index) require(f.query(one, id) == result, "query mutated the retained result");
  require(projected == 1 && result.dump().find("private-") == std::string::npos, "query reran projection or exposed sensitive text");
}

void test_projection_and_normalization_failures() {
  for (unsigned mode = 0; mode < 7; ++mode) {
    std::atomic<unsigned> projected{0};
    fixture f;
    f.handler([mode](const auto& invocation, const auto&) {
      if (invocation.operation_id == "recovery") return task_outcome{result_kind::success, R"({"value":1})", {}};
      if (mode == 4) return task_outcome{result_kind::success, R"({"value":1e400})", "private-numeric"};
      if (mode == 5) return task_outcome{result_kind::success, std::string{"{\"bad\":\""} + char(0xff) + "\"}", "private-utf8"};
      if (mode == 6) return task_outcome{result_kind::success,
          std::string{"{\"value\":"} + std::string(40, '[') + "0" + std::string(40, ']') + "}", "private-depth"};
      return task_outcome{result_kind::success, R"({"value":1})", "private-message"};
    });
    auto value = definition(); value.project_result = [&, mode](const task_outcome& outcome) -> std::string {
      ++projected;
      if (mode == 0) throw std::runtime_error("private-exception");
      if (mode == 1) return R"({"secret":"private-result"})";
      if (mode == 2) return R"({"safe":1e400})";
      if (mode == 3) return std::string(40, '[') + "0" + std::string(40, ']');
      if (outcome.kind == result_kind::success) return R"({"safe":1})";
      require(outcome.kind == result_kind::failure, "unsupported raw JSON did not fail inside the worker normalizer");
      return R"({"safe":0})";
    };
    const auto one = f.family(std::move(value)); const auto result = f.terminal(one, f.start(one, "fault").at("taskId"));
    require(projected == 1 && result.dump().find("private-") == std::string::npos, "fault returned arbitrary business text");
    require(result.at("state") == (mode < 4 ? "succeeded" : "failed"), "projection error changed actual terminal kind or normalizer did not fail");
    if (mode < 4) require(result.at("projectionError") == "E_RESULT_PROJECTION" && !result.contains("result"), "projection failed open");
    else require(result.at("result") == json{{"safe", 0}}, "worker failed to recover from invalid raw numeric/UTF8 result");
    if (mode >= 4) {
      const auto next = f.terminal(one, f.start(one, "recovery").at("taskId"));
      require(next.at("state") == "succeeded" && next.at("result") == json{{"safe", 1}} && projected == 2,
              "same worker pool could not execute valid work after normalization failure");
    }
  }
}

void test_page_terminal_message_and_worker_recovery() {
  gate poisoned, drained;
  std::mutex events_mutex;
  std::vector<json> events;
  std::atomic<unsigned> valid_calls{0};
  fixture f;
  release_guard release_poisoned{poisoned}, release_drained{drained};
  f.handler([&](const auto& invocation, const task_control& control) {
    const auto value = json::parse(invocation.parameters_json).at("value").template get<int>();
    if (value == 0) {
      require(!control.report_progress(10, std::string(1, char(0xff))), "invalid progress UTF-8 was accepted");
      poisoned.arrive_and_wait();
      return task_outcome{result_kind::success, "{}", std::string(1, char(0xff))};
    }
    ++valid_calls;
    drained.arrive_and_wait();
    return task_outcome{result_kind::success, R"({"value":1})", "recovered"};
  });
  f.service.set_event_sink([&](std::string_view text) {
    const auto event = json::parse(text.begin(), text.end());
    std::lock_guard<std::mutex> lock(events_mutex);
    events.push_back(event);
  });
  const auto token = f.permissions.issue(session, origin, all_capabilities);
  const auto page = f.permissions.resolve(session, origin, token, "task.start").scope;
  const auto start = [&](const std::string& operation, int value) {
    return json::parse(f.service.start(f.request("task.start", {{"kind", "work"}, {"operationId", operation},
        {"input", {{"value", value}}}}, page))).at("taskId").template get<std::string>();
  };
  const std::array<std::string, 2> poison_ids{start("poison0", 0), start("poison1", 0)};
  poisoned.await_arrivals(2);
  for (const auto& id : poison_ids)
    require(json::parse(f.service.subscribe(f.request("task.subscribe", {{"taskId", id}, {"eventName", "task.progress"}}, page)))
                .contains("subscriptionId"), "page terminal subscription setup failed");
  const std::array<std::string, 2> recovery_ids{start("recovery0", 1), start("recovery1", 1)};
  poisoned.release();
  drained.await_arrivals(2); // Both workers have finished normalization and subscribed terminal emit.
  require(valid_calls == 2, "one worker stopped before its subsequent FIFO work");
  for (const auto& id : poison_ids) {
    const auto result = json::parse(f.service.query(f.request("task.query", {{"taskId", id}}, page)));
    require(result.at("state") == "failed" && result.at("result") == json::object() &&
                result.at("message") == "A task handler returned an invalid result object.",
            "invalid terminal message was not normalized into a safe retained failure");
  }
  {
    std::lock_guard<std::mutex> lock(events_mutex);
    require(events.size() == 2, "safe subscribed terminal messages did not reach the page sink");
    for (const auto& event : events)
      require(event.at("payload").at("state") == "failed" &&
                  event.at("payload").at("message") == "A task handler returned an invalid result object.",
              "terminal event contained invalid or arbitrary handler text");
  }
  drained.release();
  for (const auto& id : recovery_ids) {
    json result;
    await([&] {
      result = json::parse(f.service.query(f.request("task.query", {{"taskId", id}}, page)));
      return result.value("state", "") == "succeeded";
    }, "the same worker pool did not complete valid recovery work");
    require(result.at("result") == json{{"value", 1}} && result.at("message") == "recovered",
            "normalization failure corrupted subsequent work");
  }
}

void test_brand_lifetime_and_foreign_service() {
  fixture f; f.handler(); const auto one = f.family();
  task_service foreign;
  require(foreign.register_handler("work", [](const auto&, const auto&) { return task_outcome{}; }), "foreign kind setup failed");
  require(json::parse(foreign.start_local(one, f.request("one.start", {{"operationId", "x"}, {"input", {{"value", 1}}}})))
              .value("error", "") == "E_UNAUTHORIZED", "foreign service accepted an actual but foreign family");
  capability_registry registry;
  std::optional<task_service> reused; reused.emplace();
  require(reused->register_handler("work", [](const auto&, const auto&) { return task_outcome{}; }), "first service setup failed");
  const auto old = reused->register_local_family(registry, definition()); require(old && old->active(), "old family setup failed");
  reused.reset(); require(!old->active(), "destroyed service left family issuer active");
  bridge_transport transport{registry, f.permissions};
  require(!transport.handle_local(f.request("one.start", {{"operationId", "x"}, {"input", {{"value", 1}}}}), f.binding, f.grant.token).ok,
          "registry callback kept destroyed service usable");
  reused.emplace(); // The same optional storage cannot resurrect a retired issuer brand.
  require(reused->register_handler("work", [](const auto&, const auto&) { return task_outcome{}; }), "replacement service setup failed");
  require(json::parse(reused->query_local(old, f.request("one.query", {{"taskId", "unknown"}}))).value("error", "") == "E_UNAUTHORIZED",
          "service address reuse resurrected old family authority");
}

std::atomic<std::int64_t> clock_ms{0};
permission_store::clock_type::time_point controlled_now() {
  return permission_store::clock_type::time_point{std::chrono::milliseconds(clock_ms.load())};
}

void test_renew_revoke_and_expiry() {
  clock_ms = 0;
  fixture f{controlled_now}; f.handler(); const auto one = f.family();
  const auto id = f.start(one, "retained").at("taskId").get<std::string>(); const auto result = f.terminal(one, id);
  clock_ms = 25 * 60 * 1000;
  const auto renewed = f.permissions.renew_local(f.binding, f.grant.token, "renew"); require(renewed.ok(), "actual local renewal failed");
  const auto alias = f.permissions.resolve_local(f.binding, renewed.token, "one.query", "local.query");
  require(alias.scope == f.scope && f.query(one, id) == result, "renewal changed retained actual scope identity");
  require(f.permissions.revoke_scope(f.scope) && f.service.shutdown_scope(f.scope), "precise cleanup failed");
  require(f.query(one, id).value("error", "") == "E_UNAUTHORIZED", "revoked scope still read family result");
  clock_ms = 0; fixture expired{controlled_now}; expired.handler(); const auto family = expired.family();
  clock_ms = 30 * 60 * 1000;
  require(expired.start(family, "expired").value("error", "") == "E_UNAUTHORIZED", "expired scope admitted work");
}

void test_queued_close_with_real_drain() {
  for (unsigned mode = 0; mode < 3; ++mode) {
  clock_ms = 0;
  gate blocked, drained; std::atomic<unsigned> probe{0};
  std::atomic<unsigned> success{0}, cancelled{0};
  fixture f{controlled_now}; release_guard unblock{blocked}, release_sentinels{drained};
  f.handler([&](const auto& invocation, const auto&) {
    const auto value = json::parse(invocation.parameters_json).at("value").get<int>();
    if (value == 0) blocked.arrive_and_wait();
    else if (value == 1) ++probe;
    else drained.arrive_and_wait();
    return task_outcome{result_kind::success, R"({"value":0})", {}};
  });
  auto value = definition(); value.project_result = [&](const task_outcome& outcome) {
    if (outcome.kind == result_kind::success) ++success;
    if (outcome.kind == result_kind::task_cancelled) ++cancelled;
    return R"({"safe":0})";
  };
  const auto one = f.family(std::move(value));
  for (unsigned index = 0; index < 2; ++index) require(f.start(one, "block" + std::to_string(index), 0).contains("taskId"), "block admission failed");
  blocked.await_arrivals(2);
  for (unsigned index = 0; index < 2; ++index) require(f.start(one, "probe" + std::to_string(index), 1).contains("taskId"), "queued probe admission failed");
  if (mode == 0) f.binding->close();
  else if (mode == 1) require(f.permissions.revoke_scope(f.scope) && f.service.shutdown_scope(f.scope), "queued revoke failed");
  else clock_ms = 30 * 60 * 1000;
  const auto page_token = f.permissions.issue(session, origin, all_capabilities);
  const auto page = f.permissions.resolve(session, origin, page_token, "task.start").scope;
  for (unsigned index = 0; index < 2; ++index) {
    require(json::parse(f.service.start(f.request("task.start", {{"kind", "work"}, {"operationId", "sentinel" + std::to_string(index)},
        {"input", {{"value", 2}}}}, page))).contains("taskId"), "post-probe sentinel admission failed");
  }
  blocked.release(); drained.await_arrivals(2);
  require(probe == 0 && success == 2 && cancelled == 2,
          "close/revoke/expiry failed queued admission or changed already-started outcome before both FIFO sentinels");
  drained.release();
  }
}

void test_shared_capacity_and_retention() {
  gate blocked; fixture f; release_guard unblock{blocked};
  f.handler([&](const auto& invocation, const auto&) {
    if (json::parse(invocation.parameters_json).at("value") == 0) blocked.arrive_and_wait();
    return task_outcome{result_kind::success, R"({"value":1})", {}};
  });
  const auto one = f.family(), two = f.family(definition("two"));
  for (unsigned index = 0; index < task_service::max_inflight; ++index)
    require(f.start(index % 2 ? two : one, "full" + std::to_string(index), 0, index % 2 ? "two" : "one").contains("taskId"), "shared queue filled early");
  blocked.await_arrivals(2);
  require(f.start(one, "overflow").value("error", "") == "E_BUSY", "family created a second inflight budget");
  require(json::parse(f.service.start(f.request("task.start", {{"kind", "work"}, {"operationId", "plain-overflow"},
      {"input", {{"value", 1}}}}))).value("error", "") == "E_BUSY", "plain task used a second queue budget");
  blocked.release();
  // A separate service keeps capacity accounting independent of completion timing.
  fixture retained; retained.handler(); const auto family = retained.family();
  const auto old_plain = json::parse(retained.service.start(retained.request("task.start", {{"kind", "work"},
      {"operationId", "old-plain"}, {"input", {{"value", 1}}}}))).at("taskId").get<std::string>();
  await([&] {
    return json::parse(retained.service.query(retained.request("task.query", {{"taskId", old_plain}}))).value("state", "") == "succeeded";
  }, "plain retention setup did not complete");
  require(json::parse(retained.service.subscribe(retained.request("task.subscribe", {{"taskId", old_plain},
      {"eventName", "task.progress"}}))).contains("subscriptionId"), "old retained subscription setup failed");
  const auto first = retained.start(family, "first").at("taskId").get<std::string>(); retained.terminal(family, first);
  for (unsigned index = 1; index <= task_store::max_tasks; ++index)
    retained.terminal(family, retained.start(family, "retained" + std::to_string(index)).at("taskId"));
  require(retained.query(family, first).value("error", "") == "E_TASK_NOT_FOUND", "retention grew beyond the shared store limit");
  require(json::parse(retained.service.query_local(family, retained.request("one.query", {{"operationId", "first"}})))
              .value("error", "") == "E_TASK_NOT_FOUND", "eviction left operation index alive");
  require(json::parse(retained.service.query(retained.request("task.query", {{"operationId", "old-plain"}})))
              .value("error", "") == "E_TASK_NOT_FOUND", "family admission retained an evicted plain operation");
  const auto current_plain = json::parse(retained.service.start(retained.request("task.start", {{"kind", "work"},
      {"operationId", "current-plain"}, {"input", {{"value", 1}}}}))).at("taskId").get<std::string>();
  for (std::size_t index = 0; index < task_service::max_subscriptions; ++index)
    require(json::parse(retained.service.subscribe(retained.request("task.subscribe", {{"taskId", current_plain},
        {"eventName", "task.progress"}}))).contains("subscriptionId"), "eviction leaked a subscription slot");
  require(json::parse(retained.service.subscribe(retained.request("task.subscribe", {{"taskId", current_plain},
      {"eventName", "task.progress"}}))).value("error", "") == "E_BUSY", "subscription budget grew after retention eviction");
}

#ifndef SOUI_TASK_FAMILY_TEST_ONLY
struct reentrant_copy_state {
  bool armed{false};
  unsigned copies{0}, reentries{0};
  std::function<void()> reenter;
};
struct reentrant_copy {
  std::shared_ptr<reentrant_copy_state> state;
  explicit reentrant_copy(std::shared_ptr<reentrant_copy_state> value) : state{std::move(value)} {}
  reentrant_copy(const reentrant_copy& other) : state{other.state} {
    ++state->copies;
    if (state->armed) { ++state->reentries; state->reenter(); }
  }
  std::string operator()(const bridge_request&) const { return "{}"; }
};

void test_json_bridge_default_page_contract() {
  const auto state = std::make_shared<reentrant_copy_state>();
  soui::sdk::json_bridge bridge{origin};
  require(bridge.register_task_handler("work", [](const auto&, const auto&) { return task_outcome{}; }), "bridge kind setup failed");
  require(bridge.register_contextual_method("unrelated", "other", reentrant_copy{state}), "reentrant callable setup failed");
  state->reenter = [&] {
    (void)bridge.register_method("copy.reentry", "copy.cap", [](auto) { return "{}"; });
  };
  state->armed = true;
  const auto copies = state->copies;
  require(bridge.register_local_task_family(definition()), "bridge family registration failed");
  require(state->copies == copies && state->reentries == 0 && !bridge.set_session_capabilities({"copy.cap"}),
          "bridge-locked family snapshot copied/reentered an application callable");
  require(!bridge.set_session_capabilities({"local.start"}), "local family appended a page capability");
  const auto bootstrap = json::parse(bridge.handle_json(R"({"requestId":"bootstrap","sessionId":"","origin":"","token":"","method":"bridge.bootstrap","params":{"protocol":"soui.bridge/1"}})"));
  require(bootstrap.at("ok") == true && !bridge.register_local_task_family(definition("two")), "bootstrap failed to freeze family registration");
}
#endif

#ifdef SOUI_TASK_FAMILY_TEST_ONLY
std::optional<detail::task_family_test::prepare_point> failure_point;
std::function<void(detail::task_family_test::prepare_point)> preparation_probe;

void test_registration_mutation_guard() {
  using point = detail::task_family_test::prepare_point;
  fixture f; f.handler();
  const auto action = R"({"protocol":"soui.action/1","title":"unrelated","description":"guard probe","inputSchema":{"type":"object","properties":{},"additionalProperties":false},"outputSchema":{"type":"object","properties":{},"additionalProperties":false}})";
  require(f.registry.register_action("unrelated", "other", action, [](const auto&) { return "{}"; }), "guard action setup failed");
  unsigned probes = 0;
  struct clear_probe { ~clear_probe() { preparation_probe = {}; } } cleanup;
  preparation_probe = [&](point current) {
    if (current != point::registration_registry) return;
    ++probes;
    require(!f.registry.register_method("nested.method", "other", [](auto) { return "{}"; }) &&
                !f.registry.register_contextual_method("nested.context", "other", [](const auto&) { return "{}"; }) &&
                !f.registry.register_action("nested.action", "other", action, [](const auto&) { return "{}"; }) &&
                !f.registry.allow_local_action("unrelated") &&
                !f.service.register_local_family(f.registry, definition("two")),
            "prepare guard permitted a registry mutation or nested family preparation");
  };
  const auto one = f.family();
  preparation_probe = {};
  require(probes == 1 && !f.registry.contains("nested.method") && !f.registry.contains("nested.context") &&
              !f.registry.contains("nested.action") && !f.registry.contains("two.start"),
          "nested preparation partially published registry/core state");
  require(f.registry.allow_local_action("unrelated") && f.family(definition("two")),
          "prepare guard did not release after the outer transaction");
  f.terminal(one, f.start(one, "outer").at("taskId"));
}

void test_preparation_faults() {
  using point = detail::task_family_test::prepare_point;
  for (const auto fault : {point::registration_registry, point::registration_families}) {
    fixture f; f.handler(); failure_point = fault;
    require(!f.service.register_local_family(f.registry, definition()), "controlled registry preparation fault succeeded");
    failure_point.reset();
    require(!f.registry.contains("one.start") && !f.registry.contains("one.query") && !f.registry.contains("one.cancel"), "prepare fault published partial methods");
    f.family();
  }
  for (const auto fault : {point::store_tasks, point::store_operations, point::store_scopes, point::store_receipt,
      point::admission_owners, point::admission_operations, point::admission_queue, point::admission_subscriptions, point::admission_receipt}) {
    std::atomic<unsigned> calls{0}; gate drained;
    fixture f; release_guard release{drained};
    f.handler([&](const auto& invocation, const auto&) {
      if (invocation.operation_id.rfind("drain", 0) == 0) drained.arrive_and_wait();
      else ++calls;
      return task_outcome{result_kind::success, R"({"value":1})", {}};
    });
    const auto one = f.family(); const auto first = f.start(one, "before").at("taskId").get<std::string>(); const auto saved = f.terminal(one, first);
    const auto calls_before = calls.load(); failure_point = fault;
    require(f.start(one, "fault").value("error", "") == "E_NATIVE_FAILURE", "controlled preparation fault was admitted");
    failure_point.reset();
    require(f.query(one, first) == saved && calls == calls_before, "preparation fault mutated retained data or ran handler");
    require(json::parse(f.service.query_local(one, f.request("one.query", {{"operationId", "fault"}}))).value("error", "") == "E_TASK_NOT_FOUND",
            "preparation fault left an operation receipt");
    const auto drain1 = f.start(one, "drain1").at("taskId").get<std::string>();
    const auto drain2 = f.start(one, "drain2").at("taskId").get<std::string>();
    drained.await_arrivals(2);
    require(calls == calls_before && drain1 == "sdk-task-000002" && drain2 == "sdk-task-000003", "failed preparation ran queued work or consumed identity");
    drained.release();
    const auto drained1 = f.terminal(one, drain1), drained2 = f.terminal(one, drain2);
    const auto final_sequence = std::max(std::stoull(drained1.at("sequence").get<std::string>()), std::stoull(drained2.at("sequence").get<std::string>()));
    const auto retry = f.start(one, "fault"); require(retry.at("duplicate") == false && retry.at("taskId") == "sdk-task-000004", "failed preparation consumed ID or left queue/index");
    const auto result = f.terminal(one, retry.at("taskId"));
    require(std::stoull(result.at("sequence").get<std::string>()) == final_sequence + 3U && calls == calls_before + 1,
            "failed preparation consumed sequence or executed more than once");
  }
  failure_point.reset();
  task_store store;
  std::string first;
  for (unsigned index = 0; index < task_store::max_tasks; ++index) {
    const auto task = store.start("retained" + std::to_string(index));
    if (index == 0) first = task.task_id;
    require(store.complete(task.task_id, result_kind::success), "store retention setup failed");
  }
  const auto saved = store.query(first); require(saved.has_value(), "oldest retained task missing before fault");
  failure_point = point::store_receipt;
  bool threw = false;
  try { (void)store.start("new"); } catch (const std::bad_alloc&) { threw = true; }
  failure_point.reset();
  const auto after = store.query(first);
  require(threw && after && after->sequence == saved->sequence, "last prepare fault evicted retained evidence");
  bool duplicate = false;
  require(store.start_scoped("retained0", "retained0", &duplicate).task_id == first && duplicate,
          "failed eviction changed the operation index");
  require(store.start("new").task_id == "task-001025" && !store.query(first), "successful retry did not commit exact next identity/eviction");
}
#endif
}  // namespace

#ifdef SOUI_TASK_FAMILY_TEST_ONLY
namespace soui::runtime::detail::task_family_test {
void before_prepare(prepare_point point) {
  if (failure_point && *failure_point == point) throw std::bad_alloc{};
  if (preparation_probe) preparation_probe(point);
}
}  // namespace soui::runtime::detail::task_family_test
#endif

int main() {
  try {
    const std::vector<std::pair<const char*, void(*)()>> tests{
      {"atomic registration", test_atomic_registration}, {"immutable registry callback", test_registry_immutable_callback},
      {"family/plain/scope isolation", test_family_and_plain_isolation}, {"method/cap/page isolation", test_method_capability_and_page_isolation},
      {"strict direct parsing", test_strict_direct_input}, {"once projection and honest cancel", test_projection_once_and_actual_cancel},
      {"projection/normalizer failures", test_projection_and_normalization_failures},
      {"page message UTF-8 and worker recovery", test_page_terminal_message_and_worker_recovery},
      {"actual brand lifetime", test_brand_lifetime_and_foreign_service},
      {"renew/revoke/expiry", test_renew_revoke_and_expiry}, {"queued close FIFO drain", test_queued_close_with_real_drain},
      {"shared capacity/retention", test_shared_capacity_and_retention},
#ifndef SOUI_TASK_FAMILY_TEST_ONLY
      {"json bridge page policy", test_json_bridge_default_page_contract},
#else
      {"registration mutation guard", test_registration_mutation_guard},
      {"controlled prepare failures", test_preparation_faults},
#endif
    };
    for (const auto& test : tests) { test.second(); std::cout << "PASS " << test.first << '\n'; }
    std::cout << "Local task family: " << tests.size() << '/' << tests.size() << " passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
