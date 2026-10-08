#include <soui/runtime/services/permissions/permission_store.h>
#include <soui/runtime/services/task/task_service.h>
#include <soui/runtime/core/task/task_store.h>
#include <json.hpp>
#include "../../sdk/src/runtime/services/permissions/scope_id_sequence.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace soui::runtime;
using namespace std::chrono_literals;
using json = nlohmann::json;
constexpr const char* session = "shared-document";
constexpr const char* origin = "app://scope-tests";
const std::vector<std::string> capabilities{
    "task.start", "task.query", "task.cancel", "task.subscribe", "bridge.session.renew"};

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void wait_until(const std::function<bool()>& condition, const char* message) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!condition()) {
    if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error(message);
    std::this_thread::yield();
  }
}

class worker_gate {
 public:
  void arrive_and_wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    ++arrivals_;
    changed_.notify_all();
    require(changed_.wait_for(lock, 5s, [this] { return released_; }), "worker barrier timed out");
  }
  void await(unsigned count) {
    std::unique_lock<std::mutex> lock(mutex_);
    require(changed_.wait_for(lock, 5s, [this, count] { return arrivals_ >= count; }),
            "worker did not reach barrier");
  }
  void release() {
    std::lock_guard<std::mutex> lock(mutex_);
    released_ = true;
    changed_.notify_all();
  }
 private:
  std::mutex mutex_;
  std::condition_variable changed_;
  unsigned arrivals_{0};
  bool released_{false};
};

// This guard is declared after the service, so failures release its handlers
// before the service joins workers during stack unwinding.
struct release_on_exit {
  worker_gate& gate;
  ~release_on_exit() { gate.release(); }
};

bridge_request request(const std::shared_ptr<const authorization_scope>& scope, json params) {
  bridge_request result;
  result.session_id = session;
  result.origin = origin;
  result.authorization = scope;
  result.params_json = params.dump();
  return result;
}

std::shared_ptr<const authorization_scope> issue(permission_store& store) {
  const auto token = store.issue(session, origin, capabilities);
  const auto scope = store.resolve(session, origin, token, "task.start").scope;
  require(scope != nullptr, "grant resolution failed");
  return scope;
}

json start(task_service& service, const std::shared_ptr<const authorization_scope>& scope,
           const std::string& operation, const std::string& kind = "instant", json input = json::object()) {
  return json::parse(service.start(request(scope,
      {{"kind", kind}, {"operationId", operation}, {"input", std::move(input)}})));
}

json query(task_service& service, const std::shared_ptr<const authorization_scope>& scope,
           const std::string& id) {
  return json::parse(service.query(request(scope, {{"taskId", id}})));
}

bool terminal(const json& value) {
  const auto state = value.value("state", "");
  return state == "succeeded" || state == "partial" || state == "failed" || state == "cancelled";
}

json await_terminal(task_service& service, const std::shared_ptr<const authorization_scope>& scope,
                    const std::string& id) {
  json result;
  wait_until([&] { result = query(service, scope, id); return terminal(result); }, "task did not complete");
  return result;
}

void test_multi_grant_operations() {
  permission_store first_store, second_store;
  const auto first = issue(first_store), second = issue(first_store), foreign = issue(second_store);
  task_service service;
  require(service.register_handler("instant", [](const auto&, const auto&) { return task_outcome{}; }),
          "handler registration failed");
  const auto initial = start(service, first, "same-operation");
  const auto parallel = start(service, second, "same-operation");
  const auto other_store = start(service, foreign, "same-operation");
  require(initial.contains("taskId") && parallel.contains("taskId") && other_store.contains("taskId"),
          "same document operationId collided across native grants/stores");
  require(initial.at("taskId") != parallel.at("taskId") && parallel.at("taskId") != other_store.at("taskId"),
          "distinct grants reused a task identity");
  const auto duplicate = start(service, first, "same-operation");
  require(duplicate.at("duplicate") == true && duplicate.at("taskId") == initial.at("taskId"),
          "same grant lost operation idempotency");
  require(start(service, first, "same-operation", "instant", {{"different", true}})
              .value("error", "") == "E_OPERATION_CONFLICT", "changed input reused an operation");
}

void test_honest_cancel_outcomes() {
  for (const auto kind : {result_kind::success, result_kind::partial, result_kind::failure,
                          result_kind::task_cancelled, result_kind::user_cancelled}) {
    permission_store permissions;
    const auto scope = issue(permissions);
    worker_gate gate;
    task_service service;
    release_on_exit cleanup{gate};
    require(service.register_handler("committed", [&](const auto&, const task_control& control) {
      gate.arrive_and_wait();
      require(control.cancellation_requested(), "worker did not observe cancellation request");
      return task_outcome{kind, R"({"committed":true})", "actual handler outcome"};
    }), "committed handler registration failed");
    const auto id = start(service, scope, "committed-operation", "committed").at("taskId").get<std::string>();
    gate.await(1);
    const auto cancelled = json::parse(service.cancel(request(scope, {{"taskId", id}})));
    require(cancelled.value("cancelRequested", false) && query(service, scope, id).at("state") == "cancel-pending",
            "requesting cancellation prematurely formed a terminal state");
    gate.release();
    const auto finished = await_terminal(service, scope, id);
    const char* expected = kind == result_kind::success ? "succeeded" :
        kind == result_kind::partial ? "partial" : kind == result_kind::failure ? "failed" : "cancelled";
    require(finished.at("state") == expected && finished.at("result").at("committed") == true &&
            finished.at("message") == "actual handler outcome", "cancel request overwrote actual handler outcome");
    require(json::parse(service.cancel(request(scope, {{"taskId", id}}))).value("error", "") == "E_TASK_TERMINAL" &&
            query(service, scope, id) == finished, "late cancellation mutated a terminal task");
  }
}

std::atomic<std::int64_t> clock_ms{0};
permission_store::clock_type::time_point controlled_now() {
  return permission_store::clock_type::time_point{std::chrono::milliseconds(clock_ms.load())};
}

void test_identity_allocation() {
  detail::scope_id_sequence sequence;
  std::mutex mutex;
  std::vector<std::uint64_t> ids;
  std::vector<std::thread> threads;
  for (unsigned worker = 0; worker < 8; ++worker) {
    threads.emplace_back([&] {
      for (unsigned index = 0; index < 64; ++index) {
        const auto id = sequence.allocate();
        std::lock_guard<std::mutex> lock(mutex);
        ids.push_back(id);
      }
    });
  }
  for (auto& thread : threads) thread.join();
  std::sort(ids.begin(), ids.end());
  require(ids.size() == 512 && ids.front() == 1 && ids.back() == 512 &&
          std::adjacent_find(ids.begin(), ids.end()) == ids.end(), "parallel allocation reused scope IDs");
  detail::scope_id_sequence exhausted(std::numeric_limits<std::uint64_t>::max());
  require(exhausted.allocate() == std::numeric_limits<std::uint64_t>::max(), "final uint64 identity was lost");
  for (unsigned attempt = 0; attempt < 2; ++attempt) {
    bool rejected = false;
    try { (void)exhausted.allocate(); } catch (const std::overflow_error&) { rejected = true; }
    require(rejected, "exhausted scope identities wrapped/restarted");
  }
}

void test_scope_renewal_and_exact_revocation() {
  clock_ms.store(0);
  permission_store first(&controlled_now), other(&controlled_now);
  const auto token = first.issue(session, origin, capabilities);
  const auto original = first.resolve(session, origin, token, "task.start").scope;
  const auto neighbor = issue(first), foreign = issue(other);
  const auto id = original->scope_id();
  require(id.rfind("soui-scope-", 0) == 0 && id.size() > 11 &&
          std::all_of(id.begin() + 11, id.end(), [](char value) { return value >= '0' && value <= '9'; }) &&
          id != token && id != neighbor->scope_id() && id != foreign->scope_id(), "scope ID is not independent");
  require(!first.revoke_scope(nullptr) && !first.revoke_scope(foreign) && foreign->active(),
          "foreign or empty scope changed authorization");
  clock_ms.store(std::chrono::duration_cast<std::chrono::milliseconds>(25min).count());
  const auto renewal = first.renew(session, origin, token, "renew-original");
  require(renewal.ok() && renewal.token != token &&
          first.resolve(session, origin, renewal.token, "task.start").scope == original &&
          first.resolve(session, origin, token, "task.start").scope == original && original->scope_id() == id,
          "bearer renewal changed stable identity or overlap");
  require(first.revoke_scope(original) && !original->active() && original->scope_id() == id &&
          !first.revoke_scope(original), "scope revocation was not exact/permanent");
  require(!first.authorize(session, origin, token, "task.start") &&
          !first.authorize(session, origin, renewal.token, "task.start") &&
          !first.renew(session, origin, renewal.token, "revive").ok(), "revocation left a bearer alias active");
  require(neighbor->active() && foreign->active() && issue(first)->scope_id() != id,
          "exact revocation affected another grant or reused its ID");
  std::shared_ptr<const authorization_scope> held;
  {
    permission_store temporary;
    held = issue(temporary);
    require(held->active(), "temporary grant was not active");
  }
  require(!held->active() && !held->scope_id().empty(), "scope outlived issuing store authority");
}

void test_task_store_terminal_truth() {
  task_store store;
  for (const auto kind : {result_kind::success, result_kind::partial, result_kind::failure,
                          result_kind::task_cancelled, result_kind::user_cancelled}) {
    const auto id = store.start("outcome-" + std::to_string(static_cast<int>(kind))).task_id;
    require(store.mark_running(id) && store.request_cancel(id) && store.complete(id, kind, "actual"),
            "task store rejected the actual handler result");
    const auto snapshot = store.query(id);
    const auto expected = kind == result_kind::success ? task_state::succeeded :
        kind == result_kind::partial ? task_state::partial : kind == result_kind::failure ? task_state::failed :
        task_state::cancelled;
    require(snapshot && snapshot->state == expected && snapshot->result.kind == kind &&
            !store.complete(id, result_kind::task_cancelled, "late") && !store.request_cancel(id),
            "task store cancellation falsified or changed terminal outcome");
  }
}

void test_queued_expiry() {
  clock_ms.store(0);
  permission_store permissions(&controlled_now), blockers;
  const auto stale = issue(permissions), live = issue(blockers);
  std::atomic<unsigned> executed{0};
  worker_gate gate;
  {
    task_service service;
    release_on_exit cleanup{gate};
    require(service.register_handler("block", [&](const auto&, const auto&) {
      gate.arrive_and_wait();
      return task_outcome{};
    }), "block handler registration failed");
    require(service.register_handler("probe", [&](const auto&, const auto&) { ++executed; return task_outcome{}; }),
            "probe handler registration failed");
    require(service.register_handler("instant", [](const auto&, const auto&) { return task_outcome{}; }),
            "instant handler registration failed");
    for (unsigned index = 0; index < task_service::max_workers; ++index)
      require(start(service, live, "block-" + std::to_string(index), "block").contains("taskId"), "block start failed");
    gate.await(static_cast<unsigned>(task_service::max_workers));
    const auto id = start(service, stale, "queued-stale", "probe").at("taskId").get<std::string>();
    require(query(service, stale, id).at("state") == "queued", "expiry test did not hold a queued task");
    clock_ms.store(std::chrono::duration_cast<std::chrono::milliseconds>(30min).count());
    gate.release();
    const auto sentinel = start(service, live, "after-expiry").at("taskId").get<std::string>();
    (void)await_terminal(service, live, sentinel);
  }
  require(executed.load() == 0, "expired queued grant entered native business callback");
}

void test_execution_recheck_after_event() {
  permission_store permissions, blockers;
  const auto stale = issue(permissions), live = issue(blockers);
  std::atomic<unsigned> executed{0};
  worker_gate queue_gate, event_gate;
  {
    task_service service;
    release_on_exit queue_cleanup{queue_gate}, event_cleanup{event_gate};
    require(service.register_handler("block", [&](const auto&, const auto&) {
      queue_gate.arrive_and_wait(); return task_outcome{};
    }), "event blocker registration failed");
    require(service.register_handler("probe", [&](const auto&, const auto&) { ++executed; return task_outcome{}; }),
            "event probe registration failed");
    for (unsigned index = 0; index < task_service::max_workers; ++index)
      require(start(service, live, "event-block-" + std::to_string(index), "block").contains("taskId"),
              "event blocker start failed");
    queue_gate.await(static_cast<unsigned>(task_service::max_workers));
    const auto id = start(service, stale, "pre-callback", "probe").at("taskId").get<std::string>();
    require(json::parse(service.subscribe(request(stale, {{"taskId", id}, {"eventName", "task.progress"}})))
                .contains("subscriptionId"), "event gate subscription failed");
    service.set_event_sink([&, id](std::string_view text) {
      const auto event = json::parse(text);
      if (event.at("taskId") == id && event.at("payload").at("state") == "running") event_gate.arrive_and_wait();
    });
    queue_gate.release();
    event_gate.await(1);
    // No shutdown flag is involved: the worker must recheck the actual grant.
    require(permissions.revoke_scope(stale), "event-gated grant revocation failed");
    event_gate.release();
  }
  require(executed.load() == 0, "worker entered callback using authorization cached before an event gate");
}

void test_scope_shutdown_and_subscription_isolation() {
  permission_store permissions;
  const auto first = issue(permissions), second = issue(permissions);
  worker_gate first_gate, second_gate;
  std::atomic<bool> first_cancelled{false}, second_cancelled{true};
  std::mutex event_mutex;
  std::vector<json> events;
  task_service service;
  release_on_exit first_cleanup{first_gate}, second_cleanup{second_gate};
  require(service.register_handler("hold", [&](const task_invocation& invocation, const task_control& control) {
    const bool first_job = json::parse(invocation.parameters_json).at("first").get<bool>();
    (first_job ? first_gate : second_gate).arrive_and_wait();
    const bool cancelled = control.cancellation_requested();
    (first_job ? first_cancelled : second_cancelled).store(cancelled);
    (void)control.report_progress(50, "scope-local progress");
    return task_outcome{cancelled ? result_kind::task_cancelled : result_kind::success, "{}", "confirmed"};
  }), "scope handler registration failed");
  service.set_event_sink([&](std::string_view text) {
    std::lock_guard<std::mutex> lock(event_mutex);
    events.push_back(json::parse(text));
  });
  const auto first_id = start(service, first, "shared-op", "hold", {{"first", true}}).at("taskId").get<std::string>();
  const auto second_id = start(service, second, "shared-op", "hold", {{"first", false}}).at("taskId").get<std::string>();
  first_gate.await(1); second_gate.await(1);
  std::string first_subscription, second_subscription;
  const auto subscribe = [&](const auto& scope, const std::string& id) {
    return json::parse(service.subscribe(request(scope, {{"taskId", id}, {"eventName", "task.progress"}})));
  };
  for (std::size_t index = 0; index < task_service::max_subscriptions; ++index) {
    const bool is_first = index % 2 == 0;
    const auto value = subscribe(is_first ? first : second, is_first ? first_id : second_id);
    require(value.contains("subscriptionId"), "subscription slot was rejected early");
    if (index == 0) first_subscription = value.at("subscriptionId").get<std::string>();
    if (index == 1) second_subscription = value.at("subscriptionId").get<std::string>();
  }
  require(subscribe(second, second_id).value("error", "") == "E_BUSY", "subscription capacity was exceeded");
  require(query(service, second, first_id).value("error", "") == "E_TASK_NOT_FOUND" &&
          json::parse(service.cancel(request(second, {{"taskId", first_id}}))).value("error", "") == "E_TASK_NOT_FOUND" &&
          subscribe(second, first_id).value("error", "") == "E_TASK_NOT_FOUND" &&
          json::parse(service.unsubscribe(request(second, {{"subscriptionId", first_subscription}})))
              .value("error", "") == "E_SUBSCRIPTION_NOT_FOUND", "another grant accessed task resources");
  require(!service.shutdown_scope(nullptr) && !service.shutdown_scope(first) && first->active(),
          "scope shutdown accepted active authorization before revocation");
  require(permissions.revoke_scope(first) && service.shutdown_scope(first) && service.shutdown_scope(first),
          "exact scope shutdown did not accept revoked scope idempotently");
  require(start(service, first, "late", "hold", {{"first", true}}).value("error", "") == "E_UNAUTHORIZED" &&
          query(service, first, first_id).value("error", "") == "E_UNAUTHORIZED", "revoked native handle was reused");
  require(json::parse(service.unsubscribe(request(second, {{"subscriptionId", second_subscription}})))
              .value("removed", false), "closing one scope removed another scope's subscription");
  // One second-scope removal plus 64 first-scope removals restored 65 slots.
  for (unsigned index = 0; index < 65; ++index)
    require(subscribe(second, second_id).contains("subscriptionId"), "scope shutdown failed to reclaim subscriptions");
  require(subscribe(second, second_id).value("error", "") == "E_BUSY", "scope cleanup erased unrelated subscriptions");
  std::size_t first_events = 0;
  {
    std::lock_guard<std::mutex> lock(event_mutex);
    first_events = static_cast<std::size_t>(std::count_if(events.begin(), events.end(), [&](const auto& event) {
      return event.at("taskId") == first_id;
    }));
  }
  first_gate.release(); second_gate.release();
  require(await_terminal(service, second, second_id).at("state") == "succeeded", "neighbor task was cancelled");
  wait_until([&] { return first_cancelled.load(); }, "scope shutdown did not request cooperative cancellation");
  require(!second_cancelled.load(), "scope shutdown requested cancellation for a different grant");
  wait_until([&] {
    std::lock_guard<std::mutex> lock(event_mutex);
    return std::any_of(events.begin(), events.end(), [&](const auto& event) {
      return event.at("taskId") == second_id && event.at("payload").at("progress") == 50;
    });
  }, "surviving scope subscription lost progress");
  {
    std::lock_guard<std::mutex> lock(event_mutex);
    require(static_cast<std::size_t>(std::count_if(events.begin(), events.end(), [&](const auto& event) {
      return event.at("taskId") == first_id;
    })) == first_events, "closed scope dispatched new events");
  }
}

void test_exact_revoke_after_transport_validation() {
  for (const bool subscribing : {false, true}) {
    permission_store permissions;
    const auto token = permissions.issue(session, origin, capabilities);
    const auto scope = permissions.resolve(session, origin, token, "task.start").scope;
    const auto neighbor = issue(permissions);
    worker_gate admission_gate;
    task_service service;
    release_on_exit cleanup{admission_gate};
    require(service.register_handler("instant", [](const auto&, const auto&) { return task_outcome{}; }),
            "admission handler registration failed");
    const auto id = start(service, scope, "original").at("taskId").get<std::string>();
    capability_registry registry;
    require(registry.register_contextual_method("test.gated", subscribing ? "task.subscribe" : "task.start",
        [&](const bridge_request& checked) {
          require(checked.authorization == scope && checked.authorization->active(),
                  "transport did not supply the live scope before admission gate");
          admission_gate.arrive_and_wait();
          return subscribing ? service.subscribe(checked) : service.start(checked);
        }), "admission method registration failed");
    bridge_transport transport(registry, permissions);
    auto pending = request(neighbor, subscribing
        ? json{{"taskId", id}, {"eventName", "task.progress"}}
        : json{{"kind", "instant"}, {"operationId", "late"}, {"input", json::object()}});
    pending.request_id = "gated-request";
    pending.method = "test.gated";
    pending.token = token;
    // Even a direct bridge_request supplied with another native object must be
    // rebuilt from the bearer by the existing transport.
    auto response = std::async(std::launch::async, [&] { return transport.handle(pending); });
    admission_gate.await(1);
    require(permissions.revoke_scope(scope) && service.shutdown_scope(scope), "admission scope revocation failed");
    admission_gate.release();
    require(response.wait_for(5s) == std::future_status::ready, "late admission did not finish");
    const auto rejected = response.get();
    require(!rejected.ok && rejected.error_code == "E_UNAUTHORIZED", "validated request admitted after exact revoke");
    require(start(service, neighbor, "late").contains("taskId"), "exact revoke blocked another grant's admission");
  }
}

void test_shared_capacity_and_queued_scope_shutdown() {
  permission_store permissions;
  const auto first = issue(permissions), second = issue(permissions);
  worker_gate gate;
  std::atomic<unsigned> first_executed{0}, second_executed{0};
  task_service service;
  release_on_exit cleanup{gate};
  require(service.register_handler("capacity", [&](const task_invocation& invocation, const task_control& control) {
    const bool is_first = json::parse(invocation.parameters_json).at("first").get<bool>();
    (is_first ? first_executed : second_executed).fetch_add(1);
    gate.arrive_and_wait();
    return task_outcome{control.cancellation_requested() ? result_kind::task_cancelled : result_kind::success};
  }), "capacity handler registration failed");
  std::vector<std::string> survivors;
  for (std::size_t index = 0; index < task_service::max_inflight; ++index) {
    const bool is_first = index % 2 == 0;
    const auto value = start(service, is_first ? first : second, "capacity-" + std::to_string(index),
                             "capacity", {{"first", is_first}});
    require(value.contains("taskId"), "shared task capacity rejected an available slot");
    if (!is_first) survivors.push_back(value.at("taskId").get<std::string>());
  }
  gate.await(static_cast<unsigned>(task_service::max_workers));
  require(start(service, second, "capacity-extra", "capacity", {{"first", false}}).value("error", "") == "E_BUSY",
          "multiple scopes bypassed the shared inflight limit");
  require(permissions.revoke_scope(first) && service.shutdown_scope(first), "queued scope shutdown failed");
  require(start(service, second, "capacity-extra", "capacity", {{"first", false}}).value("error", "") == "E_BUSY",
          "shutdown declared running work finished before handlers exited");
  gate.release();
  for (const auto& id : survivors)
    require(await_terminal(service, second, id).at("state") == "succeeded", "scope shutdown damaged queued neighbor work");
  require(first_executed.load() == 1 && second_executed.load() == task_service::max_inflight / 2,
          "revoked queued work executed or surviving work was lost");
  const auto replacement = start(service, second, "capacity-extra", "capacity", {{"first", false}});
  require(replacement.contains("taskId"), "completed cleanup did not restore inflight capacity");
  (void)await_terminal(service, second, replacement.at("taskId").get<std::string>());
}

void test_prune_reclaims_operations_and_subscriptions() {
  permission_store permissions;
  const auto scope = issue(permissions);
  task_service service;
  require(service.register_handler("instant", [](const auto&, const auto&) { return task_outcome{}; }),
          "prune handler registration failed");
  const auto first = start(service, scope, "old-operation").at("taskId").get<std::string>();
  (void)await_terminal(service, scope, first);
  for (std::size_t index = 0; index < task_service::max_subscriptions; ++index)
    require(json::parse(service.subscribe(request(scope, {{"taskId", first}, {"eventName", "task.progress"}})))
                .contains("subscriptionId"), "prune setup failed to fill subscriptions");
  for (std::size_t index = 0; index < task_store::max_tasks; ++index) {
    const auto id = start(service, scope, "retained-" + std::to_string(index)).at("taskId").get<std::string>();
    (void)await_terminal(service, scope, id);
  }
  require(query(service, scope, first).value("error", "") == "E_TASK_NOT_FOUND", "oldest terminal task was not pruned");
  const auto replacement = start(service, scope, "old-operation", "instant", {{"new", true}});
  require(replacement.contains("taskId") && replacement.at("duplicate") == false && replacement.at("taskId") != first,
          "pruned operation identity leaked or conflicted with a new request");
  const auto id = replacement.at("taskId").get<std::string>();
  for (std::size_t index = 0; index < task_service::max_subscriptions; ++index)
    require(json::parse(service.subscribe(request(scope, {{"taskId", id}, {"eventName", "task.progress"}})))
                .contains("subscriptionId"), "pruned tasks leaked bounded subscription capacity");
  require(json::parse(service.subscribe(request(scope, {{"taskId", id}, {"eventName", "task.progress"}})))
              .value("error", "") == "E_BUSY", "prune removed the bounded subscription limit");
}

}  // namespace

int main(int count, char** args) {
  try {
    const std::string selected = count > 1 ? args[1] : "all";
    const std::vector<std::string> cases{"all", "multi-grant", "honest-cancel", "queued-expiry", "identity",
        "revocation", "store-truth", "execution-recheck", "shutdown", "admission", "capacity", "prune"};
    require(std::find(cases.begin(), cases.end(), selected) != cases.end(), "unknown test case");
    if (selected == "all" || selected == "multi-grant") test_multi_grant_operations();
    if (selected == "all" || selected == "honest-cancel") test_honest_cancel_outcomes();
    if (selected == "all" || selected == "queued-expiry") test_queued_expiry();
    if (selected == "all" || selected == "identity") test_identity_allocation();
    if (selected == "all" || selected == "revocation") test_scope_renewal_and_exact_revocation();
    if (selected == "all" || selected == "store-truth") test_task_store_terminal_truth();
    if (selected == "all" || selected == "execution-recheck") test_execution_recheck_after_event();
    if (selected == "all" || selected == "shutdown") test_scope_shutdown_and_subscription_isolation();
    if (selected == "all" || selected == "admission") test_exact_revoke_after_transport_validation();
    if (selected == "all" || selected == "capacity") test_shared_capacity_and_queued_scope_shutdown();
    if (selected == "all" || selected == "prune") test_prune_reclaims_operations_and_subscriptions();
    std::cout << "native task scope tests passed: " << selected << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
