// SOURCE_ONLY: actual public SDK protocol fixture, not compiled or executed here.
// Link the new real Soui::Sdk; no product CPP or private hooks are copied.
#include "json_bridge_endpoint/public_endpoint_client.h"
#include <soui/json_bridge.h>
#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

namespace {
using namespace public_endpoint_tests;
using namespace soui::runtime;
using namespace soui::runtime::agent;
namespace dm = soui::sdk::diagnostics;
using clock_type = std::chrono::steady_clock;
clock_type::time_point case_deadline;
void bounded() { require(clock_type::now() < case_deadline, "Typed quota fixture deadline"); }
std::uint64_t creation() {
  FILETIME born{}, exited{}, kernel{}, user{};
  require(::GetProcessTimes(::GetCurrentProcess(), &born, &exited, &kernel, &user) != FALSE,
          "Actual subject FILETIME unavailable");
  return (static_cast<std::uint64_t>(born.dwHighDateTime) << 32U) | born.dwLowDateTime;
}
json object(json properties, json required) {
  return {{"type", "object"}, {"properties", std::move(properties)},
      {"required", std::move(required)}, {"additionalProperties", false}};
}
json input_schema() {
  return object({{"payload", {{"type", "string"}, {"maxLength", 8192}}}}, {"payload"});
}
json output_schema() {
  return object({{"safe", {{"type", "integer"}, {"minimum", 1}, {"maximum", 10}}}}, {"safe"});
}
json bootstrap(soui::sdk::json_bridge& bridge) {
  const auto answer = json::parse(bridge.handle_json(R"({"requestId":"bootstrap","sessionId":"","origin":"","token":"","method":"bridge.bootstrap","params":{"protocol":"soui.bridge/1"}})"));
  require(answer.at("ok") == true, "Actual page bootstrap failed"); return answer.at("result");
}
json page(soui::sdk::json_bridge& bridge, const json& auth, const char* method, json params) {
  return json::parse(bridge.handle_json(json{{"requestId", "quota-page"}, {"sessionId", auth.at("sessionId")},
      {"origin", auth.at("origin")}, {"token", auth.at("token")}, {"method", method},
      {"params", std::move(params)}}.dump()));
}
json remote(client& pipe, const std::string& method, json params) {
  require(pipe.write_raw(json{{"kind", "request"}, {"schemaVersion", "soui.bridge/1"},
      {"requestId", "quota-native"}, {"method", method}, {"params", std::move(params)},
      {"timeoutMs", 5000}}.dump()), "Actual native request write failed");
  const auto raw = pipe.read_raw(); require(!raw.empty(), "Actual native response missing");
  const auto response = json::parse(raw);
  require(response.at("requestId") == "quota-native" && !response.contains("token") &&
      !response.contains("sessionId") && !response.contains("origin") && !response.contains("subject"),
      "Actual local response exposed authority");
  return response; // No successful server response is synthesized by this test.
}
json start(client& pipe, const std::string& family, std::size_t bytes) {
  return remote(pipe, family + ".start", {{"operationId", "quota-operation"},
      {"input", {{"payload", std::string(bytes, 'x')}}}});
}
std::string accepted(const json& response) {
  require(response.at("ok") == true && response.at("result").at("accepted") == true,
          "Lawful native start not accepted");
  return response.at("result").at("taskId").get<std::string>();
}
void denied(const json& response) {
  // Existing typed transport projects action callback errors to this code.
  require(response.at("ok") == false && response.at("error").at("code") == "E_ACTION_REJECTED" &&
      !response.contains("result") && !response.contains("taskId") && !response.contains("accepted"),
      "Typed refusal published success/ID or changed existing error projection");
}
bool terminal(const json& response) {
  if (response.at("ok") != true) return false;
  const auto state = response.at("result").at("state").get<std::string>();
  return state == "succeeded" || state == "failed" || state == "partial" || state == "cancelled";
}

struct environment final {
  // Every captured context precedes the Bridge and survives its real worker join.
  const DWORD owner = ::GetCurrentThreadId(), subject_pid = ::GetCurrentProcessId();
  const std::uint64_t subject_creation = creation();
  const std::string name = component();
  const local_endpoint_scope key{71, 72, 73, 74};
  const bool gui;
  bool contexts_alive{true}, current{true}, closed{false};
  std::atomic<unsigned> handlers{0}; std::atomic<bool> entered{false}, released{false};
  unsigned authorizations{0}; std::atomic<unsigned> events{0}, projections{0};
  json page_auth, actual_command;
  std::unique_ptr<soui::sdk::json_bridge> bridge;
  windows_local_endpoint* endpoint{nullptr};

  explicit environment(bool gui_case, std::optional<task_store_request_quota_policy> policy = std::nullopt)
      : gui(gui_case), bridge(std::make_unique<soui::sdk::json_bridge>("app://quota-typed-page", dm::mode::scalar, policy)) {
    require(bridge->register_task_handler("quota-local", [this](const task_invocation&, const task_control& control) {
      require(contexts_alive, "Handler capture released before join"); ++handlers; entered.store(true);
      const auto until = clock_type::now() + std::chrono::seconds(5);
      while (!released.load() && !control.cancellation_requested()) {
        if (clock_type::now() >= until) return task_outcome{result_kind::failure, "{}", "Fixture handler deadline"};
        std::this_thread::yield();
      }
      if (!released.load()) return task_outcome{result_kind::task_cancelled, "{}", "Confirmed before fixture work"};
      return task_outcome{result_kind::success, R"({"safe":2})", {}};
    }), "Public native handler registration failed");
    require(bridge->register_local_task_family({"quota.local", "quota-local", "quota.start", "quota.query", "quota.cancel",
        input_schema().dump(), output_schema().dump(), [this](const task_outcome& outcome) {
          require(contexts_alive, "Projection capture released before join");
          return json{{"safe", json::parse(outcome.result_json).at("safe")}}.dump();
        }}), "Actual local task family registration failed");
    if (gui) {
      gui_command_definition row{"quota.gui", "quota.table", "table", gui_operation::snapshot, "",
          "gui.quota.start", "gui.quota.query", "gui.quota.cancel", input_schema().dump(), output_schema().dump(), "",
          [this](const task_outcome& outcome) {
            require(contexts_alive, "GUI projection capture released before join"); ++projections;
            return json{{"safe", json::parse(outcome.result_json).at("safe")}}.dump();
          }};
      require(bridge->enable_gui_commands({std::move(row)}), "Actual GUI row registration failed");
      require(bridge->set_session_capabilities({"bridge.describe", "bridge.probe", "gui.bind", "gui.ack"}),
              "Page GUI route grant failed");
    } else require(bridge->set_session_capabilities({"bridge.describe", "bridge.probe"}), "Page grant failed");
    endpoint = bridge->create_local_endpoint();
    require(endpoint && endpoint->state() == local_endpoint_state::stopped, "Actual endpoint factory was not stopped");
    if (gui) {
      bridge->set_event_sink([this](std::string_view raw) {
        require(contexts_alive && ::GetCurrentThreadId() == owner, "Actual GUI callback outlived owner capture");
        const auto event = json::parse(raw);
        require(event.at("eventName") == "gui.command" && event.at("sessionId") == page_auth.at("sessionId") &&
            !event.contains("taskId"), "Invalid actual GUI dispatch");
        actual_command = event.at("payload"); ++events;
        // Application request on the actual dispatched document/context; never a fabricated server frame.
        const auto answer = page(*bridge, page_auth, "gui.ack", {{"requestId", actual_command.at("requestId")},
            {"documentId", actual_command.at("documentId")}, {"ok", true}, {"resultJson", R"({"safe":2})"}});
        require(answer.at("ok") == true, "Actual page ACK rejected");
      });
      page_auth = bootstrap(*bridge);
      require(page(*bridge, page_auth, "gui.bind", json::object()).at("ok") == true, "Actual page route bind failed");
      // Protocol fixture owner-context proof, not a claim of actual Host/renderer/resource readiness.
      require(bridge->bind_gui_document("quota-generation-" + std::to_string(subject_pid) + "-" +
          std::to_string(subject_creation), [this] {
            return contexts_alive && current && ::GetCurrentThreadId() == owner;
          }), "Actual native coordinator binding failed");
    }
  }
  ~environment() { close(); }
  void close() noexcept {
    if (closed) return;
    released.store(true); current = false;
    if (gui) {
      bridge->close_gui_commands();
      if (bridge->poll_gui_commands() != gui_owner_result::closed) native_pending_failure();
    }
    endpoint->request_stop(); const auto until = clock_type::now() + std::chrono::seconds(5);
    while (endpoint->poll_stop() != local_endpoint_stop::stopped) {
      if (clock_type::now() >= until) native_pending_failure(); ::Sleep(1);
    }
    bridge.reset(); // Real Service destructor joins workers before any captured context is released.
    endpoint = nullptr; contexts_alive = false; closed = true;
  }
  void listen() {
    require(endpoint->start({name, 5000, 5000}, [this](const windows_peer_identity& peer) -> std::optional<local_endpoint_policy> {
      require(contexts_alive && ::GetCurrentThreadId() == owner && peer.process_id() == subject_pid &&
          peer.creation_filetime() == subject_creation && peer.process_state() == held_process_state::alive,
          "Lost actual SDK-held native peer PID+FILETIME/alive identity"); ++authorizations;
      return local_endpoint_policy{key, "quota-native-session", "app://quota-native",
          {"bridge.describe", "quota.start", "quota.query", "quota.cancel", "gui.quota.start", "gui.quota.query", "gui.quota.cancel"},
          {"bridge.describe", "quota.local.start", "quota.local.query", "quota.local.cancel", "quota.gui.start", "quota.gui.query", "quota.gui.cancel"}};
    }, [this](const local_endpoint_scope& scope) {
      return contexts_alive && current && scope.application == key.application && scope.window == key.window &&
          scope.document == key.document && scope.generation == key.generation;
    }) == local_endpoint_error::none, "Actual public endpoint start failed");
  }
  void connect(const std::function<void(client&)>& work, const std::function<bool()>& poll_gui) {
    std::atomic<bool> done{false}; std::exception_ptr failure;
    std::thread caller([&] { try { client pipe(name); pipe.hello(); work(pipe); }
      catch (...) { failure = std::current_exception(); } done.store(true, std::memory_order_release); });
    const auto until = clock_type::now() + std::chrono::seconds(15);
    while (!done.load(std::memory_order_acquire)) {
      if (clock_type::now() >= until || clock_type::now() >= case_deadline) native_pending_failure();
      try { (void)endpoint->pump_owner(); if (gui && poll_gui()) (void)bridge->poll_gui_commands(); }
      catch (...) { native_pending_failure(); } // Never unwind live native I/O/captured caller storage.
      ::Sleep(1);
    }
    caller.join(); // All peer requests/overlapped buffers finish before teardown.
    if (failure) std::rethrow_exception(failure);
  }
  task_store_request_quota_snapshot quota() {
    task_store_request_quota_snapshot out;
    const auto until = clock_type::now() + std::chrono::seconds(3);
    while (bridge->read_task_store_request_quota(out) == task_store_request_quota_status::busy) {
      require(clock_type::now() < until, "Quota observation remained busy"); std::this_thread::yield();
    }
    require(out.status == task_store_request_quota_status::available && out.owner_epoch_valid &&
        out.credit_valid && out.denied_requests_exact, "Actual same Bridge quota unavailable/invalid"); return out;
  }
  dm::bridge_runtime metrics() {
    dm::bridge_runtime out; const auto until = clock_type::now() + std::chrono::seconds(3);
    while (bridge->read_runtime_metrics(out) == dm::status::busy) {
      require(clock_type::now() < until, "Actual scalar facts remained busy"); std::this_thread::yield();
    }
    require(out.task_service.observation.state == dm::status::ready &&
        (!gui || out.gui_commands.observation.state == dm::status::ready), "Required real owner facts missing"); return out;
  }
  dm::task_store_private_storage store_facts() {
    dm::task_store_private_storage out; const auto until = clock_type::now() + std::chrono::seconds(3);
    while (bridge->read_task_store_private_storage_metrics(out) == dm::status::busy) {
      require(clock_type::now() < until, "Actual Store facts remained busy"); std::this_thread::yield();
    }
    require(out.observation.state == dm::status::ready && !out.observation.counter_saturated &&
        !out.observation.clock_invalid, "Actual Store observation invalid"); return out;
  }
};
json complete(client& pipe, const std::string& family, const std::string& operation) {
  const auto until = clock_type::now() + std::chrono::seconds(5);
  for (;;) { const auto reply = remote(pipe, family + ".query", {{"operationId", operation}});
    require(reply.at("ok") == true, "Actual operation query failed"); if (terminal(reply)) return reply;
    require(clock_type::now() < until, "Actual task did not reach terminal"); ::Sleep(1); }
}
std::uint64_t measured_fixture_cap(bool gui) {
  environment f(gui); f.listen(); std::atomic<bool> proceed{false}; std::uint64_t cap = 0;
  f.connect([&](client& pipe) {
    const auto id = accepted(start(pipe, gui ? "quota.gui" : "quota.local", 4096));
    const auto until = clock_type::now() + std::chrono::seconds(3);
    while (remote(pipe, gui ? "quota.gui.query" : "quota.local.query", {{"operationId", "quota-operation"}})
        .at("result").at("state") != "running") { require(clock_type::now() < until, "Baseline handler not running"); ::Sleep(1); }
    const auto q = f.quota(); require(!q.max_live_requested_bytes, "Fixture default must remain nullopt");
    cap = q.charged_peak_requested_bytes; require(cap > 1, "Actual fixture peak absent");
    f.released.store(true); proceed.store(true);
    const auto result = complete(pipe, gui ? "quota.gui" : "quota.local", "quota-operation");
    require(result.at("result").at("taskId") == id && result.at("result").at("state") == "succeeded",
            "Actual baseline completion failed");
  }, [&] { return proceed.load(); });
  require(f.authorizations == 1, "No real native peer authorized"); f.close(); require(f.closed, "Fixture not closed");
  return cap; // This tuple-specific small fixture value is never production calibration.
}
void rejection_reuse(bool gui) {
  const auto cap = measured_fixture_cap(gui);
  environment f(gui, task_store_request_quota_policy{cap - 1}); f.listen(); std::atomic<bool> proceed{false};
  f.connect([&](client& pipe) {
    const auto before = f.quota(); const auto old = f.metrics(); const auto old_store = f.store_facts();
    require(before.max_live_requested_bytes == cap - 1, "Explicit policy did not reach this actual pool");
    denied(start(pipe, gui ? "quota.gui" : "quota.local", 4096));
    const auto after = f.quota(); const auto now = f.metrics(); const auto now_store = f.store_facts();
    require(after.owner_epoch == before.owner_epoch && after.denied_requests == before.denied_requests + 1 &&
        after.charged_live_requested_bytes == before.charged_live_requested_bytes &&
        after.successful_live_requested_bytes == before.successful_live_requested_bytes,
        "Quota cause/actual rollback not established");
    require(now.task_service.worker_slots > 0 && now.task_service.inflight == 0 && now.task_service.queued == 0 &&
        now.task_service.owner_entries == old.task_service.owner_entries && now.task_service.operation_entries == old.task_service.operation_entries &&
        now.task_service.store_entries == old.task_service.store_entries &&
        now.task_service.committed_starts_total == old.task_service.committed_starts_total &&
        now.task_service.active_handlers == 0 && f.handlers.load() == 0, "Cold refusal published owner/queue/handler");
    require(now_store.task_entries == old_store.task_entries && now_store.operation_entries == old_store.operation_entries &&
        now_store.scope_entries == old_store.scope_entries, "Refusal partially published Store maps");
    const auto missing = remote(pipe, gui ? "quota.gui.query" : "quota.local.query", {{"operationId", "quota-operation"}});
    denied(missing); // Actual Service rejects a missing operation; typed transport keeps its existing code.
    if (gui) require(now.gui_commands.retained_weak_slots == old.gui_commands.retained_weak_slots + 1 &&
        now.gui_commands.context_slots_used == 0 && now.gui_commands.pending_live_contexts == 0 &&
        now.gui_commands.effect_borrows == 0 && now.gui_commands.queued_effects == 0 && !now.gui_commands.owner_poll_active &&
        f.events.load() == 0 && f.projections.load() == 0,
        "Real GUI prepare context was not formed/retired on quota refusal");
    const auto id = accepted(start(pipe, gui ? "quota.gui" : "quota.local", 1));
    const auto duplicate = start(pipe, gui ? "quota.gui" : "quota.local", 1);
    require(accepted(duplicate) == id && duplicate.at("result").at("duplicate") == true,
            "Manual lawful reuse or same-operation duplicate failed");
    f.released.store(true); proceed.store(true);
    const auto result = complete(pipe, gui ? "quota.gui" : "quota.local", "quota-operation");
    require(result.at("result").at("state") == "succeeded", "Actual reused task did not succeed");
    if (gui) require(f.events.load() == 1 && f.projections.load() == 1, "Duplicate prepared/re-ran actual GUI job");
    else require(f.handlers.load() == 1, "Duplicate re-ran actual local handler");
  }, [&] { return proceed.load(); });
  if (gui) {
    const auto until = clock_type::now() + std::chrono::seconds(3);
    while (f.metrics().gui_commands.context_slots_used != 0) { require(clock_type::now() < until, "GUI context still physically borrowed"); ::Sleep(1); }
    require(f.metrics().gui_commands.pending_live_contexts == 0, "Completed actual GUI context not retired");
  }
  require(f.authorizations == 1, "Different/no native peer used"); f.close();
  require(f.closed && !f.contexts_alive && !f.bridge && !f.endpoint, "Native contexts released before actual close/join");
}
} // namespace
int main(int argc, char** argv) {
  try {
    require(argc == 2, "Expected one exact typed quota case");
    case_deadline = clock_type::now() + std::chrono::seconds(45);
    const std::string name = argv[1];
    if (name == "typed-local-quota") rejection_reuse(false);
    else if (name == "gui-prepare-quota") rejection_reuse(true);
    else return 2;
    std::cout << json{{"case", name}, {"passed", true}, {"protocolFixtureOnly", true},
        {"rootPid", ::GetCurrentProcessId()}, {"rootCreationFileTime", std::to_string(creation())},
        {"productionDefaultCalibrated", false}, {"releaseReady", false}}.dump() << '\n'; return 0;
  } catch (const std::exception& error) { std::cerr << "E_TYPED_QUOTA_REGRESSION: " << error.what() << '\n'; return 1; }
}
