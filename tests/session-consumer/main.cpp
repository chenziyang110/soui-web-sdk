#include <soui/json_bridge.h>
#include <soui/runtime/services/permissions/permission_store.h>
#include <soui/runtime/services/task/task_service.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <thread>

namespace {
using namespace soui::runtime;
std::atomic<std::int64_t> simulated_ms{0};
permission_store::clock_type::time_point simulated_now() {
  return permission_store::clock_type::time_point{} + std::chrono::milliseconds(simulated_ms.load());
}
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
// This consumer has no dependency on the delivery's private JSON library. These
// bounded fields are read only from SDK-produced results, never external input.
std::string field(const std::string& value, const char* name) {
  std::smatch match;
  const std::regex expression(std::string{"\""} + name + "\":\"([^\"]+)\"");
  return std::regex_search(value, match, expression) ? match[1].str() : std::string{};
}
}
int main() {
  try {
    soui::sdk::json_bridge bridge("app://installed-session-consumer");
    require(bridge.enable_session_renewal(), "public opt-in registration");
    const auto bootstrap = bridge.handle_json(R"({"requestId":"boot","sessionId":"","origin":"","token":"","method":"bridge.bootstrap","params":{"protocol":"soui.bridge/1"}})");
    require(bootstrap.find("\"lease\":{") != std::string::npos, "bootstrap announces renewable lease");

    permission_store permissions(&simulated_now);
    task_service tasks;
    std::atomic<unsigned> invocations{0};
    require(tasks.register_handler("consumer.count", [&](const task_invocation&, const task_control& control) {
      ++invocations;
      (void)control.report_progress(100, "complete");
      return task_outcome{result_kind::success, "{\"value\":42}", "done"};
    }), "register public task handler");
    const std::string session = "installed-session", origin = "app://consumer";
    auto token = permissions.issue(session, origin,
        {"bridge.session.renew", "task.start", "task.query", "task.cancel", "task.subscribe"});
    const auto scope = permissions.resolve(session, origin, token, "task.start").scope;
    auto request = [&](const char* method, std::string params, const char* capability) {
      bridge_request value;
      value.request_id = "consumer.request"; value.session_id = session; value.origin = origin;
      value.token = token; value.method = method; value.params_json = std::move(params);
      value.authorization = permissions.resolve(session, origin, token, capability).scope;
      return value;
    };
    const std::string start_params = R"({"kind":"consumer.count","operationId":"same-operation","input":{}})";
    const auto started = tasks.start(request("task.start", start_params, "task.start"));
    const auto task_id = field(started, "taskId");
    require(!task_id.empty(), "public task admitted");
    const std::string query_params = "{\"taskId\":\"" + task_id + "\"}";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    std::string queried;
    do {
      queried = tasks.query(request("task.query", query_params, "task.query"));
      if (queried.find("\"state\":\"succeeded\"") != std::string::npos) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    require(queried.find("\"value\":42") != std::string::npos, "public task completes before simulation");
    const auto subscribed = tasks.subscribe(request("task.subscribe",
        "{\"taskId\":\"" + task_id + "\",\"eventName\":\"task.progress\"}", "task.subscribe"));
    const auto subscription_id = field(subscribed, "subscriptionId");
    require(!subscription_id.empty(), "subscription created");
    for (unsigned round = 0; round < 20; ++round) {
      simulated_ms += 25 * 60 * 1000;
      const auto next = permissions.renew(session, origin, token, "rotation:" + std::to_string(round));
      require(next.ok() && next.generation == round + 2U, "renewal generation");
      token = next.token;
      require(permissions.resolve(session, origin, token, "task.start").scope == scope, "stable authorization identity");
      queried = tasks.query(request("task.query", query_params, "task.query"));
      require(queried.find("\"value\":42") != std::string::npos, "retained completed task across renewal");
      const auto duplicate = tasks.start(request("task.start", start_params, "task.start"));
      require(field(duplicate, "taskId") == task_id && invocations == 1U, "operation is not replayed after renewal");
    }
    const auto removed = tasks.unsubscribe(request("task.unsubscribe",
        "{\"subscriptionId\":\"" + subscription_id + "\"}", "task.subscribe"));
    require(removed.find("\"error\"") == std::string::npos, "original subscription removable after twenty renewals");
    permissions.revoke_session(session);
    tasks.shutdown_session(session);
    require(!scope->active(), "revocation invalidates retained scope");
    std::cout << "Installed public SDK: 20 simulated renewals, retained result/subscription, one invocation, revocation passed. No real-time or desktop claim.\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
