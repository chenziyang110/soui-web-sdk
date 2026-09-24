#include <soui/json_bridge.h>
#include <json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
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

}  // namespace

int main() {
  try {
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
    const auto subscription = call(bridge, auth, "task.subscribe",
        {{"taskId", progress_id}, {"eventName", "task.progress"}});
    require(subscription.at("ok").get<bool>(), "task subscription failed");
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
