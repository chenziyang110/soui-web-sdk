#include <soui/json_bridge.h>
#include <json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using json = nlohmann::json;
using namespace std::chrono_literals;

void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

void wait_until(const std::function<bool()>& condition, const char* message) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!condition()) {
    if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error(message);
    std::this_thread::sleep_for(5ms);
  }
}

json call(soui::sdk::json_bridge& bridge, const json& auth, const std::string& method, json params) {
  static unsigned next_request = 1;
  return json::parse(bridge.handle_json(json{{"requestId", "task-test:" + std::to_string(next_request++)},
      {"sessionId", auth.at("sessionId")}, {"origin", auth.at("origin")}, {"token", auth.at("token")},
      {"method", method}, {"params", std::move(params)}, {"timeoutMs", 10000}}.dump()));
}

std::string task_id(const json& response) {
  require(response.at("ok").get<bool>(), "task.start was rejected");
  return response.at("result").at("taskId").get<std::string>();
}

std::atomic<std::int64_t> renewal_clock_ms{0};
soui::runtime::permission_store::clock_type::time_point renewal_now() {
  return soui::runtime::permission_store::clock_type::time_point{
      std::chrono::milliseconds(renewal_clock_ms.load())};
}

void test_stable_task_authorization() {
  using namespace soui::runtime;
  renewal_clock_ms.store(0);
  permission_store permissions(&renewal_now);
  const std::vector<std::string> capabilities{
      "task.start", "task.query", "task.cancel", "task.subscribe", "bridge.session.renew"};
  std::string token = permissions.issue("long-session", "app://renewal-test", capabilities);
  const auto identity = permissions.resolve("long-session", "app://renewal-test", token, "task.start").scope;
  std::atomic<unsigned> requested{0}, reported{0};
  std::atomic<bool> stop{false};
  std::mutex events_mutex;
  std::vector<json> events;
  task_service service;
  require(service.register_handler("hold", [&](const task_invocation&, const task_control& control) {
    unsigned seen = 0;
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!stop.load() && !control.cancellation_requested() &&
           std::chrono::steady_clock::now() < deadline) {
      const auto target = requested.load();
      if (target > seen) {
        (void)control.report_progress(target, "renewal progress");
        seen = target;
        reported.store(target);
      }
      std::this_thread::yield();
    }
    return task_outcome{result_kind::success, R"({"retained":true})", "done"};
  }), "renewal handler registration failed");
  service.set_event_sink([&](std::string_view event) {
    std::lock_guard<std::mutex> lock(events_mutex);
    events.push_back(json::parse(event));
  });
  const auto request = [&](const std::string& bearer, const char* capability, json params) {
    bridge_request result;
    result.session_id = "long-session";
    result.origin = "app://renewal-test";
    result.token = bearer;
    result.params_json = params.dump();
    result.authorization = permissions.resolve(result.session_id, result.origin, bearer, capability).scope;
    return result;
  };
  const json start_params{{"kind", "hold"}, {"operationId", "original-work"}, {"input", json::object()}};
  auto original = request(token, "task.start", start_params);
  auto no_scope = original;
  no_scope.authorization.reset();
  require(json::parse(service.start(no_scope)).value("error", "") == "E_UNAUTHORIZED",
          "direct native task caller bypassed required authorization scope");
  const auto started = json::parse(service.start(original));
  require(started.contains("taskId"), "authorized long-running task was rejected");
  const auto id = started.at("taskId").get<std::string>();
  auto subscribe = request(token, "task.subscribe", {{"taskId", id}, {"eventName", "task.progress"}});
  const auto subscription = json::parse(service.subscribe(subscribe)).at("subscriptionId").get<std::string>();
  std::uint64_t last_sequence = 0;
  for (unsigned round = 1; round <= 20; ++round) {
    renewal_clock_ms.fetch_add(std::chrono::duration_cast<std::chrono::milliseconds>(25min).count());
    const auto renewed = permissions.renew("long-session", "app://renewal-test", token,
                                           "renew-" + std::to_string(round));
    require(renewed.ok(), "task owner renewal failed");
    token = renewed.token;
    const auto next = request(token, "task.start", start_params);
    require(next.authorization == identity, "renewal replaced stable authorization identity");
    const auto duplicate = json::parse(service.start(next));
    require(duplicate.at("taskId") == id && duplicate.at("duplicate") == true,
            "renewal replaced the existing task or lost idempotency");
    requested.store(round);
    wait_until([&] { return reported.load() == round; }, "renewed task stopped reporting");
    const auto query = json::parse(service.query(request(token, "task.query", {{"taskId", id}})));
    require(query.at("taskId") == id && query.at("progress") == round,
            "renewed credentials lost old task state");
    {
      std::lock_guard<std::mutex> lock(events_mutex);
      require(!events.empty() && events.back().at("taskId") == id &&
              events.back().at("payload").at("progress") == round,
              "pre-renewal subscription stopped receiving native progress");
      const auto sequence = events.back().at("sequence").get<std::uint64_t>();
      require(sequence > last_sequence, "renewal reset task event sequence");
      last_sequence = sequence;
    }
  }
  const auto stranger = permissions.issue("long-session", "app://renewal-test", capabilities);
  require(json::parse(service.query(request(stranger, "task.query", {{"taskId", id}})))
              .value("error", "") == "E_TASK_NOT_FOUND", "same-session new grant stole task");
  require(json::parse(service.cancel(request(stranger, "task.cancel", {{"taskId", id}})))
              .value("error", "") == "E_TASK_NOT_FOUND", "same-session new grant cancelled task");
  require(json::parse(service.subscribe(request(stranger, "task.subscribe",
              {{"taskId", id}, {"eventName", "task.progress"}}))).value("error", "") == "E_TASK_NOT_FOUND",
          "same-session new grant subscribed to old task");
  require(json::parse(service.unsubscribe(request(stranger, "task.subscribe",
              {{"subscriptionId", subscription}}))).value("error", "") == "E_SUBSCRIPTION_NOT_FOUND",
          "same-session new grant removed old subscription");
  auto wrong_origin = request(token, "task.query", {{"taskId", id}});
  wrong_origin.origin = "app://different";
  require(json::parse(service.query(wrong_origin)).value("error", "") == "E_UNAUTHORIZED",
          "native scope escaped its origin binding");
  require(json::parse(service.unsubscribe(request(token, "task.subscribe",
              {{"subscriptionId", subscription}}))).value("removed", false),
          "current token could not unsubscribe original subscription");
  require(json::parse(service.subscribe(request(token, "task.subscribe",
              {{"taskId", id}, {"eventName", "task.progress"}}))).contains("subscriptionId"),
          "replacement subscription failed");
  auto cached = request(token, "task.start",
      {{"kind", "hold"}, {"operationId", "late-admission"}, {"input", json::object()}});
  auto cached_subscription = request(token, "task.subscribe",
      {{"taskId", id}, {"eventName", "task.progress"}});
  renewal_clock_ms.fetch_add(std::chrono::duration_cast<std::chrono::milliseconds>(30min).count());
  std::size_t event_count;
  {
    std::lock_guard<std::mutex> lock(events_mutex);
    event_count = events.size();
  }
  requested.store(21);
  wait_until([&] { return reported.load() == 21; }, "admitted task did not continue after grant expiry");
  {
    std::lock_guard<std::mutex> lock(events_mutex);
    require(events.size() == event_count, "expired subscription received newly dispatched progress");
  }
  permissions.revoke_session("long-session");
  service.shutdown_session("long-session");
  require(json::parse(service.start(cached)).value("error", "") == "E_UNAUTHORIZED",
          "already-authorized late request admitted a task after revoke");
  require(json::parse(service.subscribe(cached_subscription)).value("error", "") == "E_UNAUTHORIZED",
          "already-authorized late request admitted a subscription after revoke");
  stop.store(true);
}

void test_live_revoke_before_task_admission() {
  using namespace soui::runtime;
  for (const bool subscription : {false, true}) {
    renewal_clock_ms.store(0);
    permission_store permissions(&renewal_now);
    const auto token = permissions.issue("live-close", "app://live-close",
        {"task.start", "task.query", "task.subscribe"});
    task_service service;
    require(service.register_handler("instant", [](const task_invocation&, const task_control&) {
      return task_outcome{result_kind::success, "{}", "done"};
    }), "live-close handler registration failed");
    bridge_request request;
    request.request_id = "live-close";
    request.session_id = "live-close";
    request.origin = "app://live-close";
    request.token = token;
    request.authorization = permissions.resolve(request.session_id, request.origin, token, "task.start").scope;
    request.params_json = R"({"kind":"instant","operationId":"original","input":{}})";
    const auto task = json::parse(service.start(request)).at("taskId").get<std::string>();
    std::promise<void> entered;
    auto entry = entered.get_future();
    std::promise<void> released;
    const auto release = released.get_future().share();
    capability_registry registry;
    require(registry.register_contextual_method("test.gated", subscription ? "task.subscribe" : "task.start",
        [&](const bridge_request& authorized_request) {
          require(authorized_request.authorization && authorized_request.authorization->active(),
                  "transport did not resolve a still-live scope before the gate");
          entered.set_value();
          require(release.wait_for(5s) == std::future_status::ready, "task authorization gate timed out");
          return subscription ? service.subscribe(authorized_request) : service.start(authorized_request);
        }), "gated method registration failed");
    bridge_transport transport(registry, permissions);
    request.method = "test.gated";
    request.params_json = subscription
        ? json{{"taskId", task}, {"eventName", "task.progress"}}.dump()
        : R"({"kind":"instant","operationId":"late","input":{}})";
    auto result = std::async(std::launch::async, [&] { return transport.handle(request); });
    require(entry.wait_for(5s) == std::future_status::ready, "request did not reach live authorization barrier");
    require(request.authorization->active(), "revoke test accidentally used an expired scope");
    permissions.revoke_session(request.session_id);
    service.shutdown_session(request.session_id);
    released.set_value();
    require(result.wait_for(5s) == std::future_status::ready, "revoked task admission did not return");
    const auto response = result.get();
    require(!response.ok && response.error_code == "E_UNAUTHORIZED",
            "validated request admitted a task or subscription after live revocation");
  }
}

}  // namespace

int main() {
  try {
    test_stable_task_authorization();
    test_live_revoke_before_task_admission();
    soui::sdk::json_bridge bridge("app://task-test");
    std::atomic<bool> allow_progress{false}, allow_finish{false}, release_block{false};
    std::atomic<unsigned> block_started{0};
    std::mutex events_mutex;
    std::vector<json> events;
    bridge.set_event_sink([&](std::string_view event) {
      std::lock_guard<std::mutex> lock(events_mutex);
      events.push_back(json::parse(event));
    });
    require(bridge.register_task_handler("interactive", [&](const soui::runtime::task_invocation& invocation,
                                                            const soui::runtime::task_control& control) {
      require(invocation.parameters_json == R"({"value":7})", "task input changed across the worker boundary");
      while (!allow_progress.load() && !control.cancellation_requested()) std::this_thread::sleep_for(2ms);
      require(!control.report_progress(25, std::string(4097, 'x')),
              "unbounded progress message was accepted");
      require(control.report_progress(25, "quarter complete"), "task progress was rejected");
      while (!allow_finish.load() && !control.cancellation_requested()) std::this_thread::sleep_for(2ms);
      return soui::runtime::task_outcome{soui::runtime::result_kind::success, R"({"value":7})", "completed"};
    }), "interactive handler registration failed");
    require(bridge.register_task_handler("block", [&](const soui::runtime::task_invocation&,
                                                      const soui::runtime::task_control& control) {
      ++block_started;
      while (!release_block.load() && !control.cancellation_requested()) std::this_thread::sleep_for(2ms);
      return soui::runtime::task_outcome{soui::runtime::result_kind::success, R"({"released":true})", "done"};
    }), "blocking handler registration failed");
    require(bridge.register_task_handler("oversize", [](const auto&, const auto&) {
      return soui::runtime::task_outcome{soui::runtime::result_kind::success,
                                         std::string(32769, 'x'), "too large"};
    }), "oversized result handler registration failed");

    const auto bootstrap = json::parse(bridge.handle_json(json{{"requestId", "task-bootstrap"},
      {"sessionId", ""}, {"origin", ""}, {"token", ""}, {"method", "bridge.bootstrap"},
      {"params", {{"protocol", "soui.bridge/1"}}}}.dump()));
    require(bootstrap.at("ok").get<bool>(), "task bootstrap failed");
    const auto auth = bootstrap.at("result");
    require(!bridge.register_task_handler("late", [](const auto&, const auto&) {
      return soui::runtime::task_outcome{};
    }), "task handler registration must close at bootstrap");

    const auto progress_id = task_id(call(bridge, auth, "task.start",
        {{"kind", "interactive"}, {"operationId", "progress-1"}, {"input", {{"value", 7}}}}));
    require(progress_id.rfind("sdk-task-", 0) == 0, "public task IDs must not overlap app task IDs");
    const auto subscription = call(bridge, auth, "task.subscribe",
        {{"taskId", progress_id}, {"eventName", "task.progress"}});
    require(subscription.at("ok").get<bool>(), "task subscription failed");
    require(subscription.at("result").at("subscriptionId").get<std::string>().rfind("sdk-subscription-", 0) == 0,
            "public subscription IDs must not overlap app subscriptions");
    allow_progress.store(true);
    wait_until([&] {
      const auto query = call(bridge, auth, "task.query", {{"taskId", progress_id}});
      return query.at("ok").get<bool>() && query.at("result").at("progress") == 25;
    }, "task progress was not visible to the public query method");
    {
      const auto query = call(bridge, auth, "task.query", {{"taskId", progress_id}});
      require(query.at("result").at("message") == "quarter complete", "progress message was lost");
    }
    wait_until([&] {
      std::lock_guard<std::mutex> lock(events_mutex);
      return std::any_of(events.begin(), events.end(), [&](const json& event) {
        return event.at("sessionId") == auth.at("sessionId") && event.at("taskId") == progress_id &&
               event.at("eventName") == "task.progress" && event.at("payload").at("progress") == 25;
      });
    }, "task progress event was not delivered to the subscribed session");
    allow_finish.store(true);
    wait_until([&] {
      const auto query = call(bridge, auth, "task.query", {{"taskId", progress_id}});
      return query.at("ok").get<bool>() && query.at("result").at("state") == "succeeded";
    }, "task did not reach a successful terminal state");
    const auto completed = call(bridge, auth, "task.query", {{"taskId", progress_id}});
    require(completed.at("result").at("result").at("value") == 7, "task result was not retained");
    const auto removed = call(bridge, auth, "task.unsubscribe",
        {{"subscriptionId", subscription.at("result").at("subscriptionId")}});
    require(removed.at("ok").get<bool>() && removed.at("result").at("removed") == true,
            "task unsubscription was not acknowledged");

    const auto cancel_id = task_id(call(bridge, auth, "task.start",
        {{"kind", "block"}, {"operationId", "cancel-1"}, {"input", json::object()}}));
    wait_until([&] { return block_started.load() >= 1; }, "cancel target did not enter a native worker");
    const auto cancelled = call(bridge, auth, "task.cancel", {{"taskId", cancel_id}});
    require(cancelled.at("ok").get<bool>() && cancelled.at("result").at("cancelRequested") == true,
            "native cancellation was not acknowledged");
    wait_until([&] {
      const auto query = call(bridge, auth, "task.query", {{"taskId", cancel_id}});
      return query.at("ok").get<bool>() && query.at("result").at("state") == "cancelled";
    }, "native cancellation did not reach a terminal result");

    std::vector<std::string> queued;
    for (unsigned index = 0; index < soui::runtime::task_service::max_inflight; ++index) {
      queued.push_back(task_id(call(bridge, auth, "task.start",
          {{"kind", "block"}, {"operationId", "full-" + std::to_string(index)},
           {"input", {{"index", index}}}})));
    }
    wait_until([&] { return block_started.load() >= 3; }, "fixed worker pool did not begin queued work");
    const auto over = call(bridge, auth, "task.start",
        {{"kind", "block"}, {"operationId", "full-extra"}, {"input", json::object()}});
    require(!over.at("ok").get<bool>() && over.at("error").at("code") == "E_BUSY",
            "full native queue did not reject the next task");
    const auto duplicate = call(bridge, auth, "task.start",
        {{"kind", "block"}, {"operationId", "full-0"}, {"input", {{"index", 0}}}});
    require(duplicate.at("ok").get<bool>() && duplicate.at("result").at("taskId") == queued.front() &&
            duplicate.at("result").at("duplicate") == true, "same-scope retry lost idempotency at capacity");
    const auto conflict = call(bridge, auth, "task.start",
        {{"kind", "block"}, {"operationId", "full-0"}, {"input", {{"index", 999}}}});
    require(!conflict.at("ok").get<bool>() && conflict.at("error").at("code") == "E_OPERATION_CONFLICT",
            "different-scope retry did not preserve the conflict boundary");
    release_block.store(true);
    wait_until([&] {
      const auto last = call(bridge, auth, "task.query", {{"taskId", queued.back()}});
      return last.at("ok").get<bool>() && last.at("result").at("state") == "succeeded";
    }, "bounded task queue did not drain");
    const auto terminal_cancel = call(bridge, auth, "task.cancel", {{"taskId", queued.back()}});
    require(!terminal_cancel.at("ok").get<bool>() && terminal_cancel.at("error").at("code") == "E_TASK_TERMINAL",
            "terminal task accepted cancellation");
    const auto oversize_id = task_id(call(bridge, auth, "task.start",
        {{"kind", "oversize"}, {"operationId", "oversize-1"}, {"input", json::object()}}));
    wait_until([&] {
      const auto query = call(bridge, auth, "task.query", {{"taskId", oversize_id}});
      return query.at("ok").get<bool>() && query.at("result").at("state") == "failed";
    }, "oversized native result was not rejected");
    release_block.store(false);
    const auto started_before_navigation = block_started.load();
    const auto navigation_id = task_id(call(bridge, auth, "task.start",
        {{"kind", "block"}, {"operationId", "navigation-1"}, {"input", json::object()}}));
    wait_until([&] { return block_started.load() > started_before_navigation; },
               "navigation target did not start");
    bridge.shutdown_session(auth.at("sessionId").get<std::string>());
    const auto revoked = call(bridge, auth, "task.query", {{"taskId", progress_id}});
    require(!revoked.at("ok").get<bool>() && revoked.at("error").at("code") == "E_UNAUTHORIZED",
            "revoked document retained access to native tasks");
    const auto replacement = json::parse(bridge.handle_json(json{{"requestId", "replacement-bootstrap"},
      {"sessionId", ""}, {"origin", ""}, {"token", ""}, {"method", "bridge.bootstrap"},
      {"params", {{"protocol", "soui.bridge/1"}}}}.dump()));
    require(replacement.at("ok").get<bool>(), "replacement document could not bootstrap");
    const auto stolen = call(bridge, replacement.at("result"), "task.query", {{"taskId", navigation_id}});
    require(!stolen.at("ok").get<bool>() && stolen.at("error").at("code") == "E_TASK_NOT_FOUND",
            "new document could read old session's native task");
    std::cout << "Public native task service: progress, events, cancellation, capacity and session scope passed\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
