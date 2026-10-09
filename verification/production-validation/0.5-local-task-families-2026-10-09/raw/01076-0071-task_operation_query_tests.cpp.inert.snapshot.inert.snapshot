#include <soui/json_bridge.h>
#include <soui/runtime/core/task/task_store.h>
#include <soui/runtime/services/permissions/permission_store.h>
#include <soui/runtime/services/task/task_service.h>
#include <json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace soui::runtime;
using namespace std::chrono_literals;
using json = nlohmann::json;
constexpr const char* session = "operation-document";
constexpr const char* origin = "app://operation-query";
const std::vector<std::string> capabilities{"task.start", "task.query", "task.cancel", "bridge.session.renew"};

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void wait_until(const std::function<bool()>& condition) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!condition()) {
    require(std::chrono::steady_clock::now() < deadline, "task wait timed out");
    std::this_thread::yield();
  }
}
bridge_request request(const std::shared_ptr<const authorization_scope>& scope, json params) {
  bridge_request value;
  value.session_id = session; value.origin = origin; value.authorization = scope;
  value.params_json = params.dump();
  return value;
}
std::shared_ptr<const authorization_scope> grant(permission_store& store) {
  return store.resolve(session, origin, store.issue(session, origin, capabilities), "task.start").scope;
}
json start(task_service& service, const std::shared_ptr<const authorization_scope>& scope,
           const std::string& operation, const std::string& kind = "instant", json input = json::object()) {
  return json::parse(service.start(request(scope, {{"kind", kind}, {"operationId", operation}, {"input", input}})));
}
json query(task_service& service, const std::shared_ptr<const authorization_scope>& scope, json params) {
  return json::parse(service.query(request(scope, std::move(params))));
}
json await_result(task_service& service, const std::shared_ptr<const authorization_scope>& scope,
                  const std::string& id) {
  json result;
  wait_until([&] {
    result = query(service, scope, {{"taskId", id}});
    const auto state = result.value("state", "");
    return state == "succeeded" || state == "partial" || state == "failed" || state == "cancelled";
  });
  return result;
}

class gate {
 public:
  void hold() { std::unique_lock<std::mutex> lock(mutex_); ++arrivals_; changed_.notify_all();
    require(changed_.wait_for(lock, 5s, [&] { return released_; }), "handler gate timed out"); }
  void await(unsigned count) { std::unique_lock<std::mutex> lock(mutex_);
    require(changed_.wait_for(lock, 5s, [&] { return arrivals_ >= count; }), "handler did not start"); }
  void release() { std::lock_guard<std::mutex> lock(mutex_); released_ = true; changed_.notify_all(); }
 private:
  std::mutex mutex_; std::condition_variable changed_; unsigned arrivals_{0}; bool released_{false};
};
struct release_on_exit { gate& value; ~release_on_exit() { value.release(); } };

void exact_input_and_compatibility() {
  permission_store permissions; const auto scope = grant(permissions);
  std::atomic<unsigned> calls{0}; task_service service;
  require(service.register_handler("instant", [&](const auto&, const auto&) { ++calls; return task_outcome{}; }), "register");
  const auto id = start(service, scope, "existing").at("taskId").get<std::string>();
  const auto before = await_result(service, scope, id);
  const std::vector<json> invalid{nullptr, json::array(), json::array({{{"taskId", id}}}), json::object(),
    {{"taskId", id}, {"operationId", "existing"}}, {{"taskId", id}, {"extra", true}},
    {{"operationId", "existing"}, {"extra", true}}, {{"unknown", "existing"}},
    {{"taskId", nullptr}}, {{"taskId", 1}}, {{"taskId", true}}, {{"taskId", json::object()}},
    {{"operationId", nullptr}}, {{"operationId", 1}}, {{"operationId", json::array()}},
    {{"taskId", ""}}, {{"operationId", ""}}, {{"taskId", std::string(129, 'a')}},
    {{"operationId", std::string(129, 'a')}}, {{"operationId", "contains space"}},
    {{"taskId", "tail\n"}}, {{"operationId", "tail\n"}}, {{"operationId", "中文"}},
    {{"operationId", std::string("nul\0byte", 8)}}};
  for (const auto& params : invalid)
    require(query(service, scope, params).value("error", "") == "E_INVALID_PARAMS", "invalid query input accepted");
  auto malformed = request(scope, json::object()); malformed.params_json = "{";
  require(json::parse(service.query(malformed)).value("error", "") == "E_INVALID_PARAMS", "malformed params accepted");
  require(query(service, scope, {{"taskId", id}}) == before && calls == 1, "taskId compatibility or read-only boundary changed");
  const auto maximum = std::string(128, 'A');
  const auto max_id = start(service, scope, maximum).at("taskId").get<std::string>();
  (void)await_result(service, scope, max_id);
  require(query(service, scope, {{"operationId", maximum}}).at("taskId") == max_id, "128-byte operation rejected");
}

void scope_and_store_isolation() {
  permission_store first_store, other_store;
  const auto a = grant(first_store), b = grant(first_store), c = grant(other_store), unknown = grant(first_store);
  task_service service;
  require(service.register_handler("instant", [](const task_invocation& task, const auto&) {
    return task_outcome{result_kind::success, task.parameters_json, "actual result"};
  }), "register");
  std::vector<std::string> ids;
  unsigned input = 0;
  for (const auto& scope : {a, b, c}) {
    const auto id = start(service, scope, "shared:op-._", "instant", {{"owner", ++input}}).at("taskId").get<std::string>();
    const auto snapshot = await_result(service, scope, id);
    require(query(service, scope, {{"operationId", "shared:op-._"}}) == snapshot, "operation snapshot changed owner/result");
    ids.push_back(id);
  }
  require(ids[0] != ids[1] && ids[1] != ids[2], "cross-store/grant task identity collision");
  require(query(service, unknown, {{"operationId", "shared:op-._"}}).value("error", "") == "E_TASK_NOT_FOUND", "unknown grant discovered another task");
  require(query(service, b, {{"taskId", ids[0]}}).value("error", "") == "E_TASK_NOT_FOUND", "taskId crossed scope");
}

std::atomic<std::int64_t> clock_ms{0};
permission_store::clock_type::time_point controlled_now() {
  return permission_store::clock_type::time_point{std::chrono::milliseconds(clock_ms.load())};
}
void renewal_expiry_and_revocation() {
  clock_ms.store(0); permission_store permissions(&controlled_now);
  auto token = permissions.issue(session, origin, capabilities);
  const auto scope = permissions.resolve(session, origin, token, "task.start").scope;
  std::atomic<unsigned> calls{0}; task_service service;
  require(service.register_handler("instant", [&](const auto&, const auto&) { ++calls; return task_outcome{}; }), "register");
  const auto id = start(service, scope, "renewable").at("taskId").get<std::string>();
  const auto snapshot = await_result(service, scope, id);
  clock_ms.store(std::chrono::duration_cast<std::chrono::milliseconds>(25min).count());
  const auto renewed = permissions.renew(session, origin, token, "renew-query"); require(renewed.ok(), "renew failed");
  const auto current = permissions.resolve(session, origin, renewed.token, "task.query").scope;
  require(current == scope && query(service, current, {{"operationId", "renewable"}}) == snapshot && calls == 1, "renewal lost operation identity");
  clock_ms.fetch_add(std::chrono::duration_cast<std::chrono::milliseconds>(31min).count());
  require(query(service, scope, {{"operationId", "renewable"}}).value("error", "") == "E_UNAUTHORIZED", "expired grant queried task");
  const auto fresh = grant(permissions);
  const auto next = start(service, fresh, "revocable").at("taskId").get<std::string>(); (void)await_result(service, fresh, next);
  require(permissions.revoke_scope(fresh), "revoke failed"); require(service.shutdown_scope(fresh), "cleanup failed");
  require(query(service, fresh, {{"operationId", "revocable"}}).value("error", "") == "E_UNAUTHORIZED", "revoked grant queried task");
  require(query(service, grant(permissions), {{"operationId", "revocable"}}).value("error", "") == "E_TASK_NOT_FOUND", "new grant revived old task");
}

void authorization_is_native() {
  permission_store permissions; const auto scope = grant(permissions); task_service service;
  require(service.register_handler("instant", [](const auto&, const auto&) { return task_outcome{}; }), "register");
  const auto id = start(service, scope, "protected").at("taskId").get<std::string>(); (void)await_result(service, scope, id);
  auto forged = request(scope, {{"operationId", "protected"}}); forged.session_id = "foreign-session";
  require(json::parse(service.query(forged)).value("error", "") == "E_UNAUTHORIZED", "forged session accepted");
  forged = request(scope, {{"operationId", "protected"}}); forged.origin = "app://foreign";
  require(json::parse(service.query(forged)).value("error", "") == "E_UNAUTHORIZED", "forged origin accepted");
  forged = request(nullptr, {{"operationId", "protected"}});
  require(json::parse(service.query(forged)).value("error", "") == "E_UNAUTHORIZED", "missing scope accepted");
  const auto start_only = permissions.resolve(session, origin, permissions.issue(session, origin, {"task.start"}), "task.start").scope;
  require(query(service, start_only, {{"operationId", "protected"}}).value("error", "") == "E_UNAUTHORIZED", "missing task.query capability accepted");
}

void active_queued_and_honest_outcomes() {
  permission_store permissions; const auto scope = grant(permissions); gate barrier;
  std::atomic<unsigned> calls{0}, cancelled{0}; task_service service; release_on_exit cleanup{barrier};
  require(service.register_handler("hold", [&](const task_invocation& task, const task_control& control) {
    ++calls; (void)control.report_progress(25, "held"); barrier.hold();
    if (control.cancellation_requested()) ++cancelled;
    const auto index = json::parse(task.parameters_json).at("index").get<unsigned>();
    return task_outcome{index == 1 ? result_kind::partial : result_kind::failure, task.parameters_json, "real terminal"};
  }), "register");
  const auto a = start(service, scope, "active-1", "hold", {{"index", 1}}).at("taskId").get<std::string>();
  const auto b = start(service, scope, "active-2", "hold", {{"index", 2}}).at("taskId").get<std::string>(); barrier.await(2);
  const auto queued = start(service, scope, "queued", "hold", {{"index", 3}}).at("taskId").get<std::string>();
  require(query(service, scope, {{"operationId", "active-1"}}).at("state") == "running", "running operation not queryable");
  const auto before = query(service, scope, {{"taskId", queued}});
  require(before.at("state") == "queued" && query(service, scope, {{"operationId", "queued"}}) == before, "queued operation not retained");
  for (unsigned count = 0; count < 8; ++count)
    require(query(service, scope, {{"operationId", "never-accepted"}}).value("error", "") == "E_TASK_NOT_FOUND", "unknown operation was started");
  require(calls == 2 && cancelled == 0, "query started/cancelled a handler"); barrier.release();
  require(await_result(service, scope, a).at("state") == "partial", "partial outcome lost");
  require(await_result(service, scope, b).at("state") == "failed", "failure outcome lost");
  const auto finished = await_result(service, scope, queued);
  require(query(service, scope, {{"operationId", "queued"}}) == finished && calls == 3 && cancelled == 0, "query replayed/cancelled completed work");
}

void retention_is_not_execution_history() {
  permission_store permissions; const auto scope = grant(permissions); std::atomic<unsigned> calls{0}; task_service service;
  require(service.register_handler("instant", [&](const auto&, const auto&) { ++calls; return task_outcome{}; }), "register");
  std::string first_id, last_id;
  for (std::size_t n = 0; n <= task_store::max_tasks; ++n) {
    last_id = start(service, scope, "retained-" + std::to_string(n)).at("taskId").get<std::string>();
    if (!n) first_id = last_id;
    (void)await_result(service, scope, last_id);
  }
  require(query(service, scope, {{"operationId", "retained-0"}}).value("error", "") == "E_TASK_NOT_FOUND" &&
          query(service, scope, {{"taskId", first_id}}).value("error", "") == "E_TASK_NOT_FOUND", "evicted operation remained queryable");
  const auto before = query(service, scope, {{"taskId", last_id}});
  const auto count = calls.load();
  for (unsigned n = 0; n < 4; ++n)
    require(query(service, scope, {{"operationId", "retained-1024"}}) == before, "query changed terminal snapshot");
  require(calls == count && count == task_store::max_tasks + 1, "retention query restarted evicted/completed work");
}

void public_bridge_lost_reply_lookup() {
  std::atomic<unsigned> calls{0}; soui::sdk::json_bridge bridge(origin);
  require(bridge.register_task_handler("instant", [&](const auto&, const auto&) { ++calls; return task_outcome{}; }), "register bridge handler");
  const auto bootstrap = json::parse(bridge.handle_json(json{{"requestId", "query-bootstrap"},
    {"sessionId", ""}, {"origin", ""}, {"token", ""}, {"method", "bridge.bootstrap"},
    {"params", {{"protocol", "soui.bridge/1"}}}}.dump()));
  require(bootstrap.value("ok", false), "public bootstrap failed");
  const auto auth = bootstrap.at("result"); unsigned next = 0;
  const auto call = [&](const char* method, json params) {
    return json::parse(bridge.handle_json(json{{"requestId", "public-query-" + std::to_string(++next)},
      {"sessionId", auth.at("sessionId")}, {"origin", auth.at("origin")}, {"token", auth.at("token")},
      {"method", method}, {"params", std::move(params)}, {"timeoutMs", 1000}}.dump()));
  };
  // Deliberately ignore the start acknowledgement; operation lookup is a new read.
  (void)call("task.start", {{"kind", "instant"}, {"operationId", "lost-start-reply"}, {"input", json::object()}});
  json response;
  wait_until([&] { response = call("task.query", {{"operationId", "lost-start-reply"}});
    if (!response.value("ok", false)) return true;
    return response.at("result").at("state") == "succeeded"; });
  require(response.value("ok", false) && response.at("result").contains("taskId") && calls == 1, "public operation query lost/restarted work");
  const auto snapshot = response.at("result");
  require(call("task.query", {{"taskId", snapshot.at("taskId")}}).at("result") == snapshot, "public taskId compatibility changed");
  require(call("task.query", {{"taskId", snapshot.at("taskId")}, {"operationId", "lost-start-reply"}})
    .at("error").at("code") == "E_INVALID_PARAMS", "public XOR validation bypassed");
}
}  // namespace

int main() {
  const std::vector<std::pair<const char*, void(*)()>> tests{
    {"exact-input-and-taskId", exact_input_and_compatibility}, {"scope-and-store-isolation", scope_and_store_isolation},
    {"renewal-expiry-revocation", renewal_expiry_and_revocation}, {"native-authorization", authorization_is_native},
    {"active-queued-real-outcomes", active_queued_and_honest_outcomes}, {"retention-read-only", retention_is_not_execution_history},
    {"public-bridge-lost-reply", public_bridge_lost_reply_lookup}};
  unsigned failed = 0;
  for (const auto& test : tests) { try { test.second(); std::cout << "PASS " << test.first << '\n'; }
    catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; } }
  std::cout << tests.size() - failed << '/' << tests.size() << " task operation query cases passed\n";
  return failed ? 1 : 0;
}
