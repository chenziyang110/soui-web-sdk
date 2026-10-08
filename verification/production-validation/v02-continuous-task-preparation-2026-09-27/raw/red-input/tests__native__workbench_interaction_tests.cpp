#include "../../demos/workbench/integration/demo_bridge_adapter.h"
#include <json.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using json = nlohmann::json;
using namespace std::chrono_literals;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

struct fixture {
  struct observed_event { json value; std::thread::id thread; };
  std::mutex mutex;
  std::condition_variable changed;
  std::vector<observed_event> events;
  unsigned sequence{0};
  // The adapter is destroyed before the callback state above.
  soui::demo::demo_bridge_adapter bridge;

  fixture() {
    bridge.set_event_sink([this](std::string_view text) {
      const auto event = json::parse(text.begin(), text.end(), nullptr, false);
      if (!event.is_object() || event.value("eventName", "") != "task.progress") return;
      {
        std::lock_guard<std::mutex> lock(mutex);
        events.push_back({event, std::this_thread::get_id()});
      }
      changed.notify_all();
    });
  }

  json call(const json& auth, const char* method, json params) {
    return json::parse(bridge.handle_json(json{{"requestId", "v02-test-" + std::to_string(++sequence)},
        {"sessionId", auth.value("sessionId", "")}, {"origin", auth.value("origin", "")},
        {"token", auth.value("token", "")}, {"method", method}, {"params", std::move(params)}}.dump()));
  }

  json bootstrap() {
    const auto result = call(json::object(), "bridge.bootstrap", {{"protocol", "soui.bridge/1"}});
    require(result.value("ok", false), "Demo bootstrap rejected");
    return result.at("result");
  }

  std::string start(const json& auth, const char* kind, json input) {
    const auto result = call(auth, "task.start", {{"kind", kind},
        {"operationId", "v02-operation-" + std::to_string(sequence)}, {"input", std::move(input)}});
    require(result.value("ok", false), "Demo interaction task kind is not available through task.start");
    return result.at("result").at("taskId").get<std::string>();
  }

  json query(const json& auth, const std::string& task) {
    const auto result = call(auth, "task.query", {{"taskId", task}});
    require(result.value("ok", false), "Owned task.query rejected");
    return result.at("result");
  }

  std::string subscribe(const json& auth, const std::string& task) {
    const auto result = call(auth, "task.subscribe", {{"taskId", task}, {"eventName", "task.progress"}});
    require(result.value("ok", false), "Owned task.subscribe rejected");
    return result.at("result").at("subscriptionId").get<std::string>();
  }

  json terminal(const json& auth, const std::string& task) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    for (;;) {
      const auto value = query(auth, task);
      const auto state = value.value("state", "");
      if (state == "succeeded" || state == "failed" || state == "cancelled") return value;
      require(std::chrono::steady_clock::now() < deadline, "Task did not reach a terminal state");
      std::unique_lock<std::mutex> lock(mutex);
      const auto seen = events.size();
      changed.wait_for(lock, 25ms, [&] { return events.size() != seen; });
    }
  }

  void running_progress(const std::string& task) {
    std::unique_lock<std::mutex> lock(mutex);
    const bool delivered = changed.wait_for(lock, 5s, [&] {
      for (const auto& event : events) {
        if (event.value.value("taskId", "") == task &&
            event.value.at("payload").value("state", "") == "running" &&
            event.value.at("payload").value("progress", 0U) > 0) return true;
      }
      return false;
    });
    require(delivered, "No real running task.progress arrived");
  }
};

void succeeds_with_elapsed_time_and_events() {
  fixture app;
  const auto auth = app.bootstrap();
  const auto began = std::chrono::steady_clock::now();
  const auto task = app.start(auth, "demo.interaction", {{"durationMs", 1000}});
  const auto subscription = app.subscribe(auth, task);
  app.running_progress(task);
  const auto result = app.terminal(auth, task);
  require(result.value("state", "") == "succeeded", "Bounded task did not complete");
  require(std::chrono::steady_clock::now() - began >= 1000ms, "Task completed before its real duration");
  require(result.at("result").at("durationMs") == 1000, "Result lost requested duration");
  require(result.at("result").at("elapsedMs").get<std::uint64_t>() >= 1000, "Result fabricated elapsed time");
  require(result.at("result").at("iterations").get<std::uint64_t>() > 0, "Native worker did no iterations");
  unsigned count = 0, previous = 0;
  bool worker_delivery = false;
  {
    std::lock_guard<std::mutex> lock(app.mutex);
    for (const auto& event : app.events) {
      if (event.value.value("taskId", "") != task) continue;
      const auto progress = event.value.at("payload").value("progress", 0U);
      require(progress >= previous && progress <= 100, "Task progress regressed or exceeded 100");
      previous = progress;
      if (progress > 0) { ++count; worker_delivery |= event.thread != std::this_thread::get_id(); }
    }
  }
  require(count >= 2 && worker_delivery, "Missing repeated asynchronous progress");
  require(app.call(auth, "task.unsubscribe", {{"subscriptionId", subscription}}).value("ok", false), "Unsubscribe rejected");
  std::cout << "duration completion, real elapsed time and public progress passed\n";
}

void cancels_active_task_and_enforces_ownership() {
  fixture app;
  const auto owner = app.bootstrap(), stranger = app.bootstrap();
  const auto task = app.start(owner, "demo.interaction", {{"durationMs", 60000}});
  const auto subscription = app.subscribe(owner, task);
  app.running_progress(task);
  require(app.query(owner, task).value("state", "") == "running", "Long task already ended");
  require(!app.call(stranger, "task.query", {{"taskId", task}}).value("ok", true), "Cross-session query accepted");
  require(!app.call(stranger, "task.cancel", {{"taskId", task}}).value("ok", true), "Cross-session cancel accepted");
  require(app.call(owner, "task.cancel", {{"taskId", task}}).value("ok", false), "Owned cancellation rejected");
  require(app.terminal(owner, task).value("state", "") == "cancelled", "Active task ignored cancellation");
  require(app.call(owner, "task.unsubscribe", {{"subscriptionId", subscription}}).value("ok", false), "Cancel unsubscribe rejected");
  std::cout << "active cancellation and session ownership passed\n";
}

void rejects_invalid_duration_before_work() {
  fixture app;
  const auto auth = app.bootstrap();
  const std::vector<json> inputs{json::object(), json::array(), json{{"durationMs", nullptr}},
      json{{"durationMs", true}}, json{{"durationMs", "30000"}}, json{{"durationMs", 1000.5}},
      json{{"durationMs", -1}}, json{{"durationMs", 0}}, json{{"durationMs", 999}},
      json{{"durationMs", 60001}}, json{{"durationMs", std::numeric_limits<std::uint64_t>::max()}},
      json{{"durationMs", 4294997296ULL}}, json{{"durationMs", 1000}, {"extra", 1}}};
  for (const auto& input : inputs) {
    const auto task = app.start(auth, "demo.interaction", input);
    const auto result = app.terminal(auth, task);
    require(result.value("state", "") == "failed", "Invalid duration was accepted");
    require(result.value("progress", 0U) == 0, "Invalid duration started work");
  }
  std::cout << "13 invalid duration and schema cases passed\n";
}

void closes_running_session_and_retains_fast_task() {
  fixture app;
  const auto auth = app.bootstrap();
  const auto fast = app.start(auth, "demo.count", {{"steps", 20}});
  require(app.terminal(auth, fast).at("result").at("sum") == 210, "Original fast task changed");
  const auto running = app.start(auth, "demo.interaction", {{"durationMs", 30000}});
  app.subscribe(auth, running);
  app.running_progress(running);
  const auto began = std::chrono::steady_clock::now();
  app.bridge.shutdown_session(auth.at("sessionId").get<std::string>());
  require(std::chrono::steady_clock::now() - began < 5s, "Session shutdown waited for the full duration");
  require(!app.call(auth, "task.query", {{"taskId", running}}).value("ok", true), "Closed session still owned the task");
  std::cout << "session shutdown and original fast task passed\n";
}
}  // namespace

int main() {
  try {
    succeeds_with_elapsed_time_and_events();
    cancels_active_task_and_enforces_ownership();
    rejects_invalid_duration_before_work();
    closes_running_session_and_retains_fast_task();
    std::cout << "Workbench interaction task checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
