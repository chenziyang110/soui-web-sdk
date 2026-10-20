#include <soui/runtime/bridge/action_contract.h>
#include <soui/runtime/bridge/bridge_transport.h>
#include <soui/runtime/core/events/event_bus.h>
#include <soui/runtime/core/task/task_store.h>
#include <soui/runtime/host/session/webview_session.h>
#include <soui/runtime/services/fileinsight-service/fileinsight-service.h>
#include <soui/runtime/services/filesystem-readonly/directory_scanner.h>
#include <soui/runtime/services/permissions/permission_store.h>
#include <soui/runtime/services/task/task_service.h>

#include <json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace soui::runtime;
using namespace std::chrono_literals;
using json = nlohmann::json;
namespace fs = std::filesystem;
namespace dm = soui::sdk::diagnostics;

// Do not use assert: the same behavioral checks must run in Release/NDEBUG.
std::atomic<std::size_t> checks{0};
std::size_t skipped_checks{0};
void require(bool value, const char* message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}

template <class Predicate>
void wait_until(Predicate predicate, const char* message) {
  const auto deadline = std::chrono::steady_clock::now() + 10s;
  while (!predicate()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      require(false, message);
    }
    std::this_thread::sleep_for(1ms);
  }
  require(true, message);
}

bridge_request request_for(permission_store& permissions, const std::string& token,
                           const std::string& capability, json params = json::object(),
                           const std::string& session = "portable-session") {
  bridge_request result;
  result.request_id = "portable:request";
  result.session_id = session;
  result.origin = "app://portable";
  result.token = token;
  result.method = capability;
  result.params_json = params.dump();
  result.authorization = permissions.resolve(result.session_id, result.origin, token, capability).scope;
  return result;
}

void require_error(const std::string& response, const char* code, const char* message) {
  require(json::parse(response).value("error", "") == code, message);
}

void test_task_store() {
  task_store tasks{"portable-"};
  bool duplicate = false, conflict = false, capacity = false;
  const auto first = tasks.start_scoped("operation:one", "scope:one", &duplicate, &conflict, &capacity);
  require(!first.task_id.empty() && first.state == task_state::queued &&
              !first.result.terminal() && !duplicate && !conflict && !capacity,
          "TaskStore did not admit a queued, nonterminal task");
  const auto retry = tasks.start_scoped("operation:one", "scope:one", &duplicate, &conflict, &capacity);
  require(duplicate && !conflict && !capacity && retry.task_id == first.task_id &&
              retry.sequence == first.sequence, "TaskStore duplicate changed task identity");
  const auto rejected = tasks.start_scoped("operation:one", "scope:two", &duplicate, &conflict);
  require(conflict && !duplicate && rejected.task_id.empty(), "TaskStore accepted a conflicting scope");
  require(!tasks.erase(first.task_id), "TaskStore erased active work");
  require(tasks.mark_running(first.task_id), "TaskStore rejected running transition");
  require(tasks.update_progress(first.task_id, 25, "quarter"), "TaskStore rejected progress");
  const auto progress = tasks.query(first.task_id).value();
  require(progress.state == task_state::running && progress.progress == 25 &&
              progress.message == "quarter" && progress.sequence > first.sequence,
          "TaskStore lost progress, message or sequence");
  require(tasks.observe_timeout(first.task_id, "observer elapsed"), "TaskStore rejected observed timeout");
  require(!tasks.query(first.task_id)->result.terminal(), "Observed timeout incorrectly terminated native work");
  require(tasks.request_cancel(first.task_id), "TaskStore rejected cancellation request");
  const auto pending = tasks.query(first.task_id).value();
  require(pending.state == task_state::cancel_requested && !pending.result.terminal(),
          "Cancellation request incorrectly confirmed native cancellation");
  require(!tasks.mark_running(first.task_id) && !tasks.update_progress(first.task_id, 50),
          "Progress restarted a cancel-pending task");
  require(tasks.confirm_cancelled(first.task_id, "worker confirmed"), "TaskStore rejected native confirmation");
  const auto cancelled = tasks.query(first.task_id).value();
  require(cancelled.state == task_state::cancelled && cancelled.result.terminal() &&
              cancelled.sequence > pending.sequence, "TaskStore did not publish cancelled terminal state");
  require(!tasks.complete(first.task_id, result_kind::success) && !tasks.request_cancel(first.task_id),
          "TaskStore changed a terminal task");
  require(tasks.erase(first.task_id) && !tasks.query(first.task_id), "TaskStore did not erase terminal task");
  for (const auto& entry : {std::make_pair(result_kind::success, task_state::succeeded),
                           std::make_pair(result_kind::failure, task_state::failed),
                           std::make_pair(result_kind::partial, task_state::partial)}) {
    const auto task = tasks.start("result:" + std::to_string(static_cast<int>(entry.first)));
    require(tasks.complete(task.task_id, entry.first, "native result") &&
                tasks.query(task.task_id)->state == entry.second &&
                tasks.query(task.task_id)->result.terminal(), "TaskStore result mapping changed");
  }
  require(!tasks.query("missing") && !tasks.request_cancel("missing"), "Unknown task became actionable");

  // Real retained tasks reach the public capacity; no implementation seam.
  task_store bounded{"bounded-"};
  std::vector<std::string> ids;
  for (std::size_t i = 0; i < task_store::max_tasks; ++i) {
    const auto task = bounded.start_scoped("work:" + std::to_string(i), "owner",
                                           nullptr, nullptr, &capacity);
    require(!capacity && !task.task_id.empty(), "TaskStore refused work below capacity");
    ids.push_back(task.task_id);
  }
  const auto full = bounded.start_scoped("extra", "owner", nullptr, nullptr, &capacity);
  require(capacity && full.task_id.empty(), "TaskStore evicted active work at capacity");
  require(bounded.start_scoped("work:0", "owner", &duplicate, nullptr, &capacity).task_id == ids.front() &&
              duplicate && !capacity, "TaskStore lost idempotency at capacity");
  require(bounded.complete(ids.front(), result_kind::success), "Could not make an eviction candidate");
  require(!bounded.start_scoped("replacement", "owner", nullptr, nullptr, &capacity).task_id.empty() &&
              !capacity && !bounded.query(ids.front()) && bounded.query(ids.back()).has_value(),
          "TaskStore did not evict only terminal history");
}

void test_request_quota() {
  constexpr std::uint64_t limit = 65536;
  task_store tasks{"quota-", dm::mode::disabled, task_store_request_quota_policy{limit}};
  const auto task = tasks.start("quota:operation");
  require(tasks.mark_running(task.task_id), "Quota fixture could not begin work");
  const auto before_task = tasks.query(task.task_id).value();
  task_store_request_quota_snapshot before, after;
  require(tasks.read_request_quota(before) == task_store_request_quota_status::available &&
              before.credit_valid && before.max_live_requested_bytes == limit &&
              before.charged_live_requested_bytes > 0, "Configured quota was unavailable with diagnostics disabled");
  bool refused = false;
  try {
    (void)tasks.update_progress(task.task_id, 90, std::string(static_cast<std::size_t>(limit) + 1, 'q'));
  } catch (const task_store_request_quota_exceeded&) {
    refused = true;
  }
  require(refused, "TaskStore accepted a private allocation larger than its quota");
  const auto after_task = tasks.query(task.task_id).value();
  require(after_task.sequence == before_task.sequence && after_task.state == before_task.state &&
              after_task.message == before_task.message && after_task.progress == before_task.progress,
          "Quota refusal partially published task state");
  require(tasks.read_request_quota(after) == task_store_request_quota_status::available &&
              after.credit_valid && after.denied_requests_exact &&
              after.denied_requests == before.denied_requests + 1 &&
              after.charged_live_requested_bytes == before.charged_live_requested_bytes &&
              after.successful_live_requested_bytes == before.successful_live_requested_bytes,
          "Quota refusal leaked credit or lost denial accounting");
  require(tasks.complete(task.task_id, result_kind::success, "fits") && tasks.erase(task.task_id),
          "TaskStore did not recover after quota refusal");
  task_store unconfigured{"free-", dm::mode::disabled, std::nullopt};
  require(unconfigured.read_request_quota(after) == task_store_request_quota_status::available &&
              !after.max_live_requested_bytes, "Explicit unconfigured quota became a finite default");
  task_store zero{"", dm::mode::disabled, task_store_request_quota_policy{0}};
  refused = false;
  try { (void)zero.start("zero"); }
  catch (const task_store_request_quota_exceeded&) { refused = true; }
  require(refused, "Zero quota admitted private allocations");
}

std::atomic<std::int64_t> clock_ms{0};
permission_store::clock_type::time_point permission_now() {
  return permission_store::clock_type::time_point{std::chrono::milliseconds{clock_ms.load()}};
}

void test_sessions_and_permissions() {
  std::vector<lifecycle_event> events;
  webview_session session;
  auto old_listener = session.events().subscribe([&](const auto& event) { events.push_back(event); });
  const auto old_id = session.session_id();
  require(session.hide().has_value() && session.mark_ready().has_value(), "Session lifecycle transitions failed");
  require(events.size() == 2 && events[1].sequence > events[0].sequence,
          "Lifecycle events were lost or reordered");
  clock_ms.store(0);
  permission_store permissions{&permission_now};
  const auto token = permissions.issue(old_id, "app://portable", {"read", "bridge.session.renew"});
  const auto original = permissions.resolve(old_id, "app://portable", token, "read").scope;
  require(original && original->active() && original->allows("read") && !original->allows("write"),
          "Capability scope was broadened or not active");
  require(!permissions.authorize(old_id, "app://other", token, "read"), "Origin mismatch was authorized");
  require(!permissions.renew(old_id, "app://portable", token, "early").ok(), "Early renewal was accepted");
  clock_ms.store(std::chrono::duration_cast<std::chrono::milliseconds>(25min).count());
  const auto lease = permissions.renew(old_id, "app://portable", token, "rotate");
  require(lease.ok() && lease.token != token && lease.generation == 2, "Permission renewal failed");
  require(permissions.resolve(old_id, "app://portable", lease.token, "read").scope == original,
          "Renewal changed stable authorization identity");
  require(permissions.authorize(old_id, "app://portable", token, "read"), "Renewal overlap was lost");
  clock_ms.fetch_add(31000);
  require(!permissions.authorize(old_id, "app://portable", token, "read") &&
              permissions.authorize(old_id, "app://portable", lease.token, "read"),
          "Expired overlap retained the old bearer or rejected the renewed one");

  // The embedding host owns revoke-before-cleanup; a session object is not a host.
  permissions.revoke_session(old_id);
  require(session.reload().has_value() && session.session_id() != old_id && !old_listener.active(),
          "Reload retained document identity or old listeners");
  require(!original->active() && !permissions.authorize(old_id, "app://portable", lease.token, "read"),
          "Revocation left the previous document scope active");
  auto current_listener = session.events().subscribe([&](const auto& event) { events.push_back(event); });
  require(!session.accept_host_state(old_id, lifecycle_state::closed) &&
              events.back().kind == lifecycle_event_kind::rejected &&
              events.back().rejected_session_id == old_id && !session.terminal(),
          "Stale host state closed the replacement document");
  const auto replacement = permissions.issue(session.session_id(), "app://portable", {"read"});
  require(permissions.authorize(session.session_id(), "app://portable", replacement, "read") &&
              !permissions.authorize(old_id, "app://portable", replacement, "read"),
          "Replacement grant escaped its document identity");
  require(session.close().has_value() && session.terminal() && !session.reload().has_value() &&
              !session.mark_ready().has_value(), "Terminal session was reopened");
  current_listener.unsubscribe();
  require(!current_listener.active(), "Lifecycle unsubscribe retained a listener");
  clock_ms.fetch_add(std::chrono::duration_cast<std::chrono::milliseconds>(30min).count());
  require(permissions.check(session.session_id(), "app://portable", replacement, "read") ==
              authorization_result::expired, "Expired grant did not fail closed");
}

std::string echo_contract() {
  const json shape{{"type", "object"}, {"properties", {{"value", {{"type", "integer"},
      {"minimum", 0}, {"maximum", 100}}}}}, {"required", {"value"}}, {"additionalProperties", false}};
  return json{{"protocol", "soui.action/1"}, {"title", "Echo bounded value"},
              {"description", "Return a bounded integer through the native action boundary."},
              {"inputSchema", shape}, {"outputSchema", shape}}.dump();
}

void test_bridge() {
  permission_store permissions, foreign;
  const auto token = permissions.issue("portable-session", "app://portable", {"echo.read"});
  const auto other_token = foreign.issue("portable-session", "app://portable", {"echo.read", "write"});
  const auto real_scope = permissions.resolve("portable-session", "app://portable", token, "echo.read").scope;
  const auto forged_scope = foreign.resolve("portable-session", "app://portable", other_token, "echo.read").scope;
  capability_registry registry;
  unsigned calls = 0;
  bool invalid_output = false, throw_handler = false, last_gesture = false;
  require(registry.register_action("echo.run", "echo.read", echo_contract(), [&](const bridge_request& request) {
    ++calls;
    require(request.authorization && request.authorization->allows("echo.read") &&
                !request.authorization->allows("write"), "Bridge trusted a broadened authorization");
    last_gesture = request.native_user_gesture;
    if (throw_handler) throw std::runtime_error("native failure");
    return invalid_output ? std::string{R"({"value":"wrong"})"} : request.params_json;
  }), "Typed action registration failed");
  require(!registry.register_action("echo.run", "echo.read", echo_contract(), [](const auto&) { return "{}"; }),
          "Duplicate registration replaced an action");
  require(!action_contract::parse("{}"), "Malformed action contract was accepted");
  require(!registry.register_method("invalid method", "read", [](auto) { return "{}"; }),
          "Invalid method identifier was registered");
  require(!registry.register_method("invalid.utf8", std::string("\xC0\xAF", 2), [](auto) { return "{}"; }),
          "Overlong UTF-8 capability was registered");
  bridge_transport transport{registry, permissions};
  auto request = request_for(permissions, token, "echo.read", {{"value", 7}});
  request.method = "echo.run";
  request.authorization = forged_scope;
  const auto response = transport.handle(request);
  require(response.ok && json::parse(response.payload_json).at("value") == 7 &&
              calls == 1 && request.authorization == forged_scope,
          "Bridge failed to independently resolve authorization without mutating its caller");
  const auto reject = [&](bridge_request bad, const char* code) {
    const auto old_calls = calls;
    const auto result = transport.handle(bad);
    require(!result.ok && result.error_code == code && calls == old_calls,
            "Invalid or unauthorized Bridge request reached native handler");
  };
  auto bad = request;
  bad.token = other_token; bad.authorization = real_scope;
  reject(bad, "E_UNAUTHORIZED");
  bad = request; bad.origin = "app://other"; reject(bad, "E_UNAUTHORIZED");
  bad = request; bad.params_json = R"({"value":"7"})"; reject(bad, "E_INVALID_PARAMS");
  bad.params_json = R"({"value":101})"; reject(bad, "E_INVALID_PARAMS");
  bad.params_json = R"({"value":7,"extra":true})"; reject(bad, "E_INVALID_PARAMS");
  bad.params_json = "{"; reject(bad, "E_MALFORMED_JSON");
  bad.params_json = "[]"; reject(bad, "E_SCHEMA_INVALID");
  bad.params_json = "{\"nested\":" + std::string(40, '[') + "0" + std::string(40, ']') + "}";
  reject(bad, "E_SCHEMA_INVALID");
  bad.params_json = std::string(bridge_transport::max_params_bytes + 1, ' ');
  reject(bad, "E_LIMIT_MESSAGE_SIZE");
  bad = request; bad.timeout_ms = 0; reject(bad, "E_INVALID_PARAMS");
  bad.timeout_ms = 60001; reject(bad, "E_INVALID_PARAMS");
  const auto wire_error = [&](const std::string& wire, const char* code) {
    const auto old_calls = calls;
    const auto result = json::parse(transport.handle_json(wire));
    require(!result.at("ok").get<bool>() && result.at("error").at("code") == code &&
                old_calls == calls, "Invalid wire envelope reached handler");
  };
  wire_error("{", "E_MALFORMED_JSON");
  wire_error(R"({"unexpected":true})", "E_SCHEMA_INVALID");
  wire_error(std::string(bridge_transport::max_message_bytes + 1, ' '), "E_LIMIT_MESSAGE_SIZE");
  invalid_output = true;
  require(transport.handle(request).error_code == "E_SCHEMA_INVALID",
          "Typed action published a schema-invalid native result");
  invalid_output = false; throw_handler = true;
  require(transport.handle(request).error_code == "E_NATIVE_FAILURE", "Native action exception escaped transport");
  throw_handler = false;

  require(registry.allow_local_action("echo.run"), "Typed action could not opt into local transport");
  const auto binding = permissions.create_local_binding("local-session", "app://local");
  const auto grant = permissions.issue_local(binding, {"echo.read"}, {"echo.run"});
  request.native_user_gesture = true;
  const auto local = transport.handle_local(request, binding, grant.token);
  require(local.ok && !last_gesture && grant.scope->allows_local_method("echo.run", "echo.read"),
          "Local transport failed or invented a user gesture");
  require(!transport.handle_local(request, binding, token).ok, "Page bearer crossed into a local connection");
  binding->close();
  require(!grant.scope->active() && !transport.handle_local(request, binding, grant.token).ok,
          "Closed native connection retained authorization");
  permissions.revoke_session("portable-session");
  reject(request, "E_UNAUTHORIZED");
}

void test_async_tasks() {
  permission_store permissions;
  const std::vector<std::string> capabilities{"task.start", "task.query", "task.cancel", "task.subscribe"};
  const auto token = permissions.issue("portable-session", "app://portable", capabilities);
  const auto stranger = permissions.issue("portable-session", "app://portable", capabilities);
  std::atomic<bool> progress_allowed{false}, finish_allowed{false}, release_blockers{false};
  std::atomic<unsigned> interactive_calls{0}, blocked_calls{0}, cancelled_calls{0};
  std::mutex events_mutex;
  std::vector<json> events;
  const auto caller_thread = std::this_thread::get_id();
  task_service service{dm::mode::scalar};
  require(service.register_handler("interactive", [&](const task_invocation& invocation, const task_control& control) {
    ++interactive_calls;
    require(std::this_thread::get_id() != caller_thread && invocation.parameters_json == R"({"value":7})",
            "Task did not execute its normalized input on an actual worker");
    while (!progress_allowed.load() && !control.cancellation_requested()) std::this_thread::sleep_for(1ms);
    if (control.cancellation_requested()) return task_outcome{result_kind::task_cancelled, "{}", "cleanup"};
    require(!control.report_progress(25, std::string(4097, 'x')), "Unbounded task progress was accepted");
    require(control.report_progress(25, "quarter"), "Task progress publication failed");
    while (!finish_allowed.load() && !control.cancellation_requested()) std::this_thread::sleep_for(1ms);
    return task_outcome{control.cancellation_requested() ? result_kind::task_cancelled : result_kind::success,
                        R"({"value":7})", "worker finished"};
  }), "Interactive task registration failed");
  require(service.register_handler("block", [&](const task_invocation&, const task_control& control) {
    ++blocked_calls;
    while (!release_blockers.load() && !control.cancellation_requested()) std::this_thread::sleep_for(1ms);
    if (control.cancellation_requested()) {
      ++cancelled_calls;
      return task_outcome{result_kind::task_cancelled, "{}", "cancel confirmed"};
    }
    return task_outcome{result_kind::success, R"({"released":true})", "done"};
  }), "Blocking task registration failed");
  require(service.register_handler("throw", [](const auto&, const auto&) -> task_outcome {
    throw std::runtime_error("native handler failure");
  }), "Throwing handler registration failed");
  require(service.register_handler("invalid-result", [](const auto&, const auto&) {
    return task_outcome{result_kind::success, "[]", "invalid object"};
  }), "Invalid-result handler registration failed");
  require(service.register_handler("oversize-result", [](const auto&, const auto&) {
    return task_outcome{result_kind::success,
        json{{"text", std::string(bridge_transport::max_params_bytes, 'x')}}.dump(), "oversized"};
  }), "Oversized-result handler registration failed");
  service.set_event_sink([&](std::string_view text) {
    std::lock_guard<std::mutex> lock(events_mutex);
    events.push_back(json::parse(text));
  });
  const auto start = [&](const char* kind, const std::string& operation, json input = json::object()) {
    return request_for(permissions, token, "task.start",
                       {{"kind", kind}, {"operationId", operation}, {"input", std::move(input)}});
  };
  const auto query = [&](const std::string& id) {
    return json::parse(service.query(request_for(permissions, token, "task.query", {{"taskId", id}})));
  };
  auto work = start("interactive", "progress", {{"value", 7}});
  auto unauthenticated = work; unauthenticated.authorization.reset();
  require_error(service.start(unauthenticated), "E_UNAUTHORIZED", "Direct task call accepted an empty scope");
  auto malformed = work; malformed.params_json = std::string(bridge_transport::max_params_bytes + 1, ' ');
  require_error(service.start(malformed), "E_INVALID_PARAMS", "Direct task input bypassed size bound");
  malformed.params_json = "{\"kind\":\"interactive\",\"operationId\":\"bad\",\"input\":{\"nested\":" +
      std::string(40, '[') + "0" + std::string(40, ']') + "}}";
  require_error(service.start(malformed), "E_INVALID_PARAMS", "Direct task input bypassed depth bound");
  const auto id = json::parse(service.start(work)).at("taskId").get<std::string>();
  const auto duplicate = json::parse(service.start(work));
  require(duplicate.at("taskId") == id && duplicate.value("duplicate", false), "Async task retry ran duplicate work");
  require_error(service.start(start("interactive", "progress", {{"value", 8}})),
                "E_OPERATION_CONFLICT", "Different task input reused an operation identity");
  const auto subscription = json::parse(service.subscribe(request_for(permissions, token, "task.subscribe",
      {{"taskId", id}, {"eventName", "task.progress"}}))).at("subscriptionId").get<std::string>();
  progress_allowed.store(true);
  wait_until([&] { return query(id).value("progress", 0) == 25; }, "Async progress was not queryable");
  wait_until([&] {
    std::lock_guard<std::mutex> lock(events_mutex);
    return std::any_of(events.begin(), events.end(), [&](const json& event) {
      return event.at("taskId") == id && event.at("sessionId") == "portable-session" &&
             event.at("eventName") == "task.progress" && event.at("payload").at("progress") == 25;
    });
  }, "Subscribed async progress event was not delivered");
  require_error(service.query(request_for(permissions, stranger, "task.query", {{"taskId", id}})),
                "E_TASK_NOT_FOUND", "Another grant read a task in the same session");
  require_error(service.cancel(request_for(permissions, stranger, "task.cancel", {{"taskId", id}})),
                "E_TASK_NOT_FOUND", "Another grant cancelled a task");
  require_error(service.query(request_for(permissions, token, "task.query",
      {{"taskId", id}, {"operationId", "progress"}})), "E_INVALID_PARAMS", "Task query accepted ambiguous selectors");
  finish_allowed.store(true);
  wait_until([&] { return query(id).value("state", "") == "succeeded"; }, "Native task did not complete");
  require(query(id).at("result").at("value") == 7 && interactive_calls.load() == 1,
          "Native result was lost or idempotent handler ran twice");
  const auto by_operation = json::parse(service.query(request_for(permissions, token, "task.query",
      {{"operationId", "progress"}})));
  require(by_operation.at("taskId") == id, "Operation lookup did not resolve retained native task");
  require(json::parse(service.unsubscribe(request_for(permissions, token, "task.subscribe",
      {{"subscriptionId", subscription}}))).value("removed", false), "Task unsubscribe failed");

  const auto cancel_id = json::parse(service.start(start("block", "cancel"))).at("taskId").get<std::string>();
  wait_until([&] { return blocked_calls.load() == 1; }, "Cancellation target did not enter native worker");
  require(json::parse(service.cancel(request_for(permissions, token, "task.cancel",
      {{"taskId", cancel_id}}))).value("cancelRequested", false), "Async cancellation was not acknowledged");
  wait_until([&] { return query(cancel_id).value("state", "") == "cancelled"; }, "Native cancellation was not confirmed");
  require(cancelled_calls.load() == 1, "Cancellation did not reach the actual task control");

  std::vector<std::string> queued;
  for (std::size_t i = 0; i < task_service::max_inflight; ++i) {
    queued.push_back(json::parse(service.start(start("block", "full:" + std::to_string(i))))
                         .at("taskId").get<std::string>());
  }
  wait_until([&] { return blocked_calls.load() == 1 + task_service::max_workers; },
             "Fixed native worker pool did not fill");
  dm::tasks full_metrics;
  wait_until([&] { return service.read_runtime_metrics(full_metrics) == dm::status::ready; },
             "TaskService diagnostics never became available");
  require(full_metrics.worker_slots == task_service::max_workers &&
              full_metrics.active_handlers == task_service::max_workers &&
              full_metrics.inflight == task_service::max_inflight &&
              full_metrics.queued == task_service::max_inflight - task_service::max_workers,
          "TaskService diagnostics disagree with real bounded workers and queue");
  require_error(service.start(start("block", "over")), "E_BUSY", "Async service exceeded its inflight bound");
  const auto at_capacity = json::parse(service.start(start("block", "full:0")));
  require(at_capacity.value("duplicate", false) && at_capacity.at("taskId") == queued.front(),
          "Task retry was rejected or duplicated at capacity");
  // Both workers are blocked, so the last entry is known to be queued.
  require(json::parse(service.cancel(request_for(permissions, token, "task.cancel",
      {{"taskId", queued.back()}}))).value("cancelRequested", false), "Queued cancellation was rejected");
  release_blockers.store(true);
  wait_until([&] { return query(queued.back()).value("state", "") == "cancelled"; },
             "Queued cancellation did not terminate");
  wait_until([&] {
    return std::all_of(queued.begin(), queued.end() - 1, [&](const std::string& item) {
      return query(item).value("state", "") == "succeeded";
    });
  }, "Bounded native queue did not drain");
  require(blocked_calls.load() == task_service::max_inflight, "Cancelled queued work entered a native handler");
  require_error(service.cancel(request_for(permissions, token, "task.cancel", {{"taskId", queued.front()}})),
                "E_TASK_TERMINAL", "Completed work accepted cancellation");
  const auto failed_id = json::parse(service.start(start("throw", "throws"))).at("taskId").get<std::string>();
  wait_until([&] { return query(failed_id).value("state", "") == "failed"; },
             "Throwing native handler did not become a failed task");
  require(query(failed_id).value("handlerError", "") == "E_NATIVE_HANDLER",
          "Failed task lost the native handler error");
  for (const auto* kind : {"invalid-result", "oversize-result"}) {
    const auto invalid_id = json::parse(service.start(start(kind, kind))).at("taskId").get<std::string>();
    wait_until([&] { return query(invalid_id).value("state", "") == "failed"; },
               "Invalid native result did not become a failed task");
    require(query(invalid_id).value("outcomeError", "") == "E_HANDLER_OUTCOME" &&
                query(invalid_id).at("result") == json::object(),
            "Invalid native task result escaped validation");
  }

  release_blockers.store(false);
  const auto entered_before = blocked_calls.load();
  const auto revoked_id = json::parse(service.start(start("block", "revoke"))).at("taskId").get<std::string>();
  wait_until([&] { return blocked_calls.load() > entered_before; }, "Revoke target did not start");
  auto cached = start("block", "late");
  require(!service.shutdown_scope(cached.authorization), "Active grant was treated as revoked");
  permissions.revoke_session(cached.session_id);
  require(service.shutdown_scope(cached.authorization), "Revoked scope cleanup was rejected");
  require_error(service.start(cached), "E_UNAUTHORIZED", "Cached old scope admitted work after revocation");
  require_error(service.query(request_for(permissions, token, "task.query", {{"taskId", revoked_id}})),
                "E_UNAUTHORIZED", "Revoked grant could still query tasks");
  wait_until([&] { return cancelled_calls.load() == 2; }, "Scope shutdown did not cancel its native worker");

  const auto fresh = permissions.issue("portable-session", "app://portable", capabilities);
  require_error(service.query(request_for(permissions, fresh, "task.query", {{"taskId", id}})),
                "E_TASK_NOT_FOUND", "Replacement scope adopted old work");
  std::atomic<unsigned> zero_calls{0};
  task_service zero{dm::mode::disabled, task_store_request_quota_policy{0}};
  require(zero.register_handler("zero", [&](const auto&, const auto&) { ++zero_calls; return task_outcome{}; }),
          "Zero-quota handler registration failed");
  require_error(zero.start(request_for(permissions, fresh, "task.start",
      {{"kind", "zero"}, {"operationId", "zero"}, {"input", json::object()}})),
                "E_TASK_STORE_QUOTA", "TaskService hid a real admission quota refusal");
  require(zero_calls.load() == 0, "Quota-rejected work reached a handler");
}

class temporary_directory {
 public:
  temporary_directory() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
      path = fs::temp_directory_path() /
          ("soui-portable-" + std::to_string(nonce) + "-" + std::to_string(attempt));
      std::error_code error;
      if (fs::create_directory(path, error)) return;
      if (error && error != std::errc::file_exists) throw fs::filesystem_error("temporary directory", path, error);
    }
    throw std::runtime_error("Could not create a unique scanner fixture");
  }
  ~temporary_directory() { std::error_code ignored; fs::remove_all(path, ignored); }
  fs::path path;
};

void write_file(const fs::path& path, const std::string& data) {
  std::ofstream stream(path, std::ios::binary);
  stream << data;
  stream.close();
  require(static_cast<bool>(stream), "Could not create real scanner input");
}

void test_directory_scan() {
  temporary_directory fixture;
  const auto root = fixture.path / fs::u8path(u8"中文目录");
  const auto nested = root / fs::u8path(u8"子目录");
  fs::create_directories(nested);
  write_file(root / fs::u8path(u8"报告.TXT"), "hello");
  write_file(nested / fs::u8path(u8"说明.txt"), "world!");
  write_file(root / "plain", "abc");
  const auto outside = fixture.path / "outside";
  fs::create_directory(outside);
  write_file(outside / "secret.txt", "must stay outside");
  std::error_code link_error;
  fs::create_directory_symlink(outside, root / "external-link", link_error);
  if (link_error) {
    // Some Windows test accounts lack symlink privileges; all authorization,
    // Unicode and opaque-root rejection checks still execute below.
    ++skipped_checks;
    std::cout << "SKIP scanner symlink check: fixture creation returned "
              << link_error.category().name() << ':' << link_error.value()
              << " (" << link_error.message() << ")\n";
  }
  permission_store permissions;
  task_store tasks{"scan-"};
  const std::vector<std::string> capabilities{
      "desktop.dialog.pickDirectory", "filesystem.scan", "task.query", "task.cancel"};
  const auto token = permissions.issue("portable-session", "app://portable", capabilities);
  const auto other_token = permissions.issue("portable-session", "app://portable", capabilities);
  scan_request_context context{"portable-session", "app://portable", token, {}};
  scan_request_context stranger{"portable-session", "app://portable", other_token, {}};
  directory_scanner scanner{permissions, tasks};
  const auto root_id = scanner.register_root(context, root);
  require(!root_id.empty() && root_id.find(root.u8string()) == std::string::npos,
          "Scanner failed to bind an opaque native root");
  require(scanner.register_root(context, root / "missing").empty(), "Scanner bound a nonexistent root");
  require(scanner.start(context, "traversal", "../outside").code == "E_ROOT_NOT_FOUND",
          "Scanner accepted path traversal as an opaque root");
  require(scanner.start(stranger, "stolen", root_id).code == "E_ROOT_NOT_FOUND",
          "Another grant used a registered root in the same session");
  auto forged = stranger;
  forged.authorization = permissions.resolve(context.session_id, context.origin, token, "filesystem.scan").scope;
  require(scanner.start(forged, "forged", root_id).code == "E_ROOT_NOT_FOUND",
          "Scanner trusted caller-supplied native identity");
  const auto started = scanner.start(context, "scan:unicode", root_id);
  require(started.ok() && !started.task_id.empty(), "Real Unicode directory scan did not start");
  require(scanner.start(context, "scan:unicode", root_id).task_id == started.task_id,
          "Scanner retry duplicated work");
  require(scanner.start(context, "scan:unicode", root_id, "*.txt").code == "E_OPERATION_CONFLICT",
          "Scanner accepted conflicting filters for an operation");
  scan_response page;
  wait_until([&] {
    page = scanner.read_page(context, started.task_id, "", 2);
    return page.kind != result_kind::timeout_observed;
  }, "Directory scan did not finish");
  require(page.ok() && page.entries.size() == 2 && page.has_more && !page.next_cursor.empty(),
          "Scanner did not paginate completed traversal");
  std::vector<scan_entry> entries = page.entries;
  const auto tail = scanner.read_page(context, started.task_id, page.next_cursor, 2);
  entries.insert(entries.end(), tail.entries.begin(), tail.entries.end());
  require(tail.ok() && !tail.has_more && entries.size() == 4 && tail.file_count == 3 &&
              tail.total_bytes == 14, "Scanner pagination or native byte summary was incorrect");
  std::set<std::string> names;
  for (const auto& entry : entries) {
    names.insert(entry.relative_path);
    require(!fs::u8path(entry.relative_path).is_absolute() && entry.relative_path.find("..") != 0 &&
                entry.relative_path.find("secret.txt") == std::string::npos,
            "Scanner exposed an absolute path or escaped its authorized root");
  }
  require(names.count(u8"报告.TXT") == 1 && names.count(u8"子目录/说明.txt") == 1 &&
              names.count(u8"子目录") == 1, "Scanner corrupted Chinese UTF-8 paths");
  const auto text_group = std::find_if(tail.groups.begin(), tail.groups.end(),
      [](const auto& group) { return group.extension == ".txt"; });
  require(text_group != tail.groups.end() && text_group->file_count == 2 &&
              text_group->total_bytes == 11, "Scanner lost case-normalized extension summary");
  if (!link_error) require(tail.skipped_count >= 1, "Scanner did not report skipping an external symlink");
  require(scanner.read_page(context, started.task_id, "invalid", 2).code == "E_INVALID_CURSOR" &&
              scanner.read_page(context, started.task_id, "", 0).code == "E_INVALID_ARGUMENT",
          "Scanner accepted invalid pagination");
  require(scanner.read_page(stranger, started.task_id, "", 2).code == "E_TASK_NOT_FOUND" &&
              !scanner.owns_task(stranger, started.task_id) && !scanner.cancel(stranger, started.task_id),
          "Scanner task escaped its authorization owner");
  require(tasks.query(started.task_id)->state == task_state::succeeded,
          "Scanner failed to commit its actual task terminal state");

  capability_registry registry;
  soui::modules::fileinsight::FileinsightService::register_methods(registry, scanner);
  bridge_transport transport{registry, permissions};
  auto request = request_for(permissions, token, "task.query", {
      {"sessionId", context.session_id}, {"origin", context.origin}, {"token", token},
      {"taskId", started.task_id}, {"pageSize", 100}});
  request.method = "fileinsight.scan.query";
  const auto response = transport.handle(request);
  require(response.ok && json::parse(response.payload_json).at("fileCount") == 3,
          "Fileinsight facade did not expose the actual bounded scan result");
  permissions.revoke_session(context.session_id);
  scanner.shutdown_session(context.session_id);
  require(scanner.register_root(context, root).empty() &&
              scanner.start(context, "late", root_id).code == "E_PERMISSION_DENIED" &&
              scanner.read_page(context, started.task_id, "", 2).code == "E_PERMISSION_DENIED",
          "Revoked scanner scope admitted or read work after shutdown");
}
}  // namespace

int main() {
  try {
    test_task_store();
    test_request_quota();
    test_sessions_and_permissions();
    test_bridge();
    test_async_tasks();
    test_directory_scan();
    std::cout << "Portable Runtime integration passed: " << checks.load()
              << " checks, " << skipped_checks
              << " skipped (real native workers and filesystem; no desktop/renderer claim).\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Portable Runtime integration failed after " << checks.load()
              << " checks: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "Portable Runtime integration failed after " << checks.load() << " checks: unknown exception\n";
    return 1;
  }
}
