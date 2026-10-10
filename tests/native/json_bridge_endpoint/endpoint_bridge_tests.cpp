#include "public_endpoint_client.h"
#include <soui/json_bridge.h>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <functional>
#include <new>
#include <thread>
#include <utility>

namespace { std::atomic<bool> fail_next_allocation{false}; }
void* operator new(std::size_t size) {
  if (fail_next_allocation.exchange(false)) throw std::bad_alloc{};
  if (auto* result = std::malloc(size ? size : 1)) return result;
  throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace public_endpoint_tests;
using namespace soui::runtime;
using namespace soui::runtime::agent;

json object(json properties = json::object(), json required = json::array()) {
  return {{"type", "object"}, {"properties", std::move(properties)},
      {"required", std::move(required)}, {"additionalProperties", false}};
}
const json integer_schema{{"type", "integer"}, {"minimum", 1}, {"maximum", 10}};
std::string action_contract() {
  return json{{"protocol", "soui.action/1"}, {"title", "Public native calculation"},
      {"description", "Native-authorized public SDK composition."},
      {"inputSchema", object({{"value", integer_schema}}, {"value"})},
      {"outputSchema", object({{"value", integer_schema}}, {"value"})}}.dump();
}
local_task_family_definition family_definition() {
  return {"public.family", "public-kind", "native.start", "native.query", "native.cancel",
      object({{"value", integer_schema}}, {"value"}).dump(),
      object({{"safe", integer_schema}}, {"safe"}).dump(),
      [](const task_outcome& outcome) {
        const auto raw = json::parse(outcome.result_json);
        return json{{"safe", raw.at("value")}}.dump();
      }};
}
json page_bootstrap(soui::sdk::json_bridge& bridge) {
  return json::parse(bridge.handle_json(R"({"requestId":"bootstrap","sessionId":"","origin":"","token":"","method":"bridge.bootstrap","params":{"protocol":"soui.bridge/1"}})"));
}
json page_call(soui::sdk::json_bridge& bridge, const json& credentials, const char* method, json params) {
  return json::parse(bridge.handle_json(json{{"requestId", "page"},
      {"sessionId", credentials.at("sessionId")}, {"origin", credentials.at("origin")},
      {"token", credentials.at("token")}, {"method", method}, {"params", std::move(params)}}.dump()));
}
void await(const std::function<bool()>& predicate, windows_local_endpoint& endpoint, const char* message) {
  const auto deadline = ::GetTickCount64() + 5000;
  while (!predicate()) {
    require(::GetTickCount64() < deadline, message);
    (void)endpoint.pump_owner(); ::Sleep(1);
  }
}

struct environment final {
  // Application/task contexts are before bridge and therefore outlive its
  // task_service destruction/join. Endpoint stopped alone is not that barrier.
  const DWORD owner = ::GetCurrentThreadId();
  const std::string name = component();
  const local_endpoint_scope key{11, 22, 33, 44};
  std::atomic<bool> entered{false}, cancelled{false};
  unsigned actions{0}, authorizations{0};
  bool current{true};
  std::shared_ptr<const authorization_scope> actual_scope;
  soui::sdk::json_bridge bridge{"app://public-endpoint-page"};
  windows_local_endpoint* endpoint{nullptr};

  explicit environment(bool long_task = false) {
    require(bridge.register_action("public.calculate", "native.calculate", action_contract(),
        [this](const bridge_request& request) {
          require(::GetCurrentThreadId() == owner && request.authorization &&
              request.authorization->active() &&
              request.authorization->allows_local_method("public.calculate", "native.calculate"),
              "Action did not use the native resolved scope on owner");
          actual_scope = request.authorization; ++actions;
          return json{{"value", json::parse(request.params_json).at("value")}}.dump();
        }), "Public action registration failed");
    require(bridge.allow_local_action("public.calculate"), "Public local action opt-in failed");
    require(bridge.register_task_handler("public-kind", [this, long_task](const task_invocation& invocation,
        const task_control& control) {
          entered.store(true, std::memory_order_release);
          if (long_task) {
            const auto deadline = ::GetTickCount64() + 7000;
            while (!control.cancellation_requested()) {
              if (::GetTickCount64() >= deadline)
                return task_outcome{result_kind::failure, "{}", "Cancellation was never requested."};
              ::Sleep(1);
            }
            cancelled.store(true, std::memory_order_release);
            return task_outcome{result_kind::task_cancelled, "{}", "Cancelled."};
          }
          return task_outcome{result_kind::success,
              json{{"value", json::parse(invocation.parameters_json).at("value")},
                   {"private", "must not escape projection"}}.dump(), "Private worker detail."};
        }), "Public task handler registration failed");
    require(bridge.register_local_task_family(family_definition()), "Public local family registration failed");
    require(!bridge.set_session_capabilities({"native.start"}), "Local family capability entered page grants");
    require(bridge.set_session_capabilities({"bridge.describe", "bridge.probe"}), "Public page policy failed");
    endpoint = bridge.create_local_endpoint();
    require(endpoint && endpoint->state() == local_endpoint_state::stopped, "Factory started or failed endpoint");
  }
  ~environment() {
    if (!endpoint) return;
    endpoint->request_stop();
    const auto deadline = ::GetTickCount64() + 5000;
    while (endpoint->poll_stop() != local_endpoint_stop::stopped) {
      if (::GetTickCount64() >= deadline) native_pending_failure();
      ::Sleep(1);
    }
    // The borrowed pointer is still valid here, and expires with bridge.
  }
  void start() {
    const auto result = endpoint->start({name, 4000, 4000},
        [this](const windows_peer_identity& peer) -> std::optional<local_endpoint_policy> {
          require(::GetCurrentThreadId() == owner && peer.process_id() == ::GetCurrentProcessId() &&
              peer.creation_filetime() && peer.process_state() == held_process_state::alive,
              "Authorizer did not receive actual live native peer on owner");
          ++authorizations;
          return local_endpoint_policy{key, "native-public-consumer", "app://public-endpoint-native",
              {"bridge.describe", "native.calculate", "native.start", "native.query", "native.cancel"},
              {"bridge.describe", "public.calculate", "public.family.start", "public.family.query", "public.family.cancel"}};
        }, [this](const local_endpoint_scope& value) {
          require(::GetCurrentThreadId() == owner, "Scope callback was not owner");
          return current && value.application == key.application && value.window == key.window &&
              value.document == key.document && value.generation == key.generation;
        });
    require(result == local_endpoint_error::none, "Actual endpoint start failed");
  }
  void connect(const std::function<void(client&)>& work, const std::function<void()>& owner_tick = {}) {
    std::atomic<bool> done{false}; std::exception_ptr failure;
    std::thread remote([&] {
      try { client pipe(name); pipe.hello(); work(pipe); }
      catch (...) { failure = std::current_exception(); }
      done.store(true, std::memory_order_release);
    });
    const auto deadline = ::GetTickCount64() + 9000;
    while (!done.load(std::memory_order_acquire)) {
      if (::GetTickCount64() >= deadline) native_pending_failure();
      (void)endpoint->pump_owner();
      // Never unwind a joinable remote or release its borrowed stack data.
      try { if (owner_tick) owner_tick(); } catch (...) { native_pending_failure(); }
      ::Sleep(1);
    }
    remote.join(); // Actual completion precedes release of remote's borrowed data.
    if (failure) std::rethrow_exception(failure);
  }
};

void factory_stopped_registration_freeze() {
  environment f;
  require(f.bridge.create_local_endpoint() == nullptr, "Factory allowed a second frozen creation");
  require(!f.bridge.register_method("late", "late", [](std::string_view) { return "{}"; }) &&
      !f.bridge.register_contextual_method("late", "late", [](const bridge_request&) { return "{}"; }) &&
      !f.bridge.register_action("late", "late", action_contract(), [](const bridge_request&) { return "{}"; }) &&
      !f.bridge.allow_local_action("public.calculate") &&
      !f.bridge.register_task_handler("late", [](const auto&, const auto&) { return task_outcome{}; }) &&
      !f.bridge.register_local_task_family(family_definition()) &&
      !f.bridge.set_session_capabilities({"bridge.describe"}) &&
      !f.bridge.enable_session_renewal() && !f.bridge.enable_directory_service() &&
      !f.bridge.enable_clipboard_service() && !f.bridge.enable_window_service(), "A registration/setter escaped freeze");
  require(page_bootstrap(f.bridge).at("ok") == true, "Stopped factory prevented ordinary page bootstrap");
  require(f.endpoint->state() == local_endpoint_state::stopped && f.authorizations == 0,
      "Factory/bootstrap automatically started or authorized native endpoint");
}
void factory_allocation_failure() {
  soui::sdk::json_bridge bridge{"app://allocation"};
  bool threw = false; fail_next_allocation.store(true);
  try { (void)bridge.create_local_endpoint(); } catch (const std::bad_alloc&) { threw = true; }
  require(threw && !fail_next_allocation.load(), "Actual factory allocation did not fail once");
  require(bridge.register_method("after.failure", "after.failure", [](std::string_view) { return "{}"; }),
      "Failed allocation froze registration");
  auto* endpoint = bridge.create_local_endpoint();
  require(endpoint && endpoint->state() == local_endpoint_state::stopped &&
      endpoint->poll_stop() == local_endpoint_stop::stopped, "Successful retry was not a stopped factory");
}
void factory_after_bootstrap() {
  soui::sdk::json_bridge bridge{"app://bootstrap-first"};
  require(page_bootstrap(bridge).at("ok") == true, "Actual bootstrap failed");
  require(bridge.create_local_endpoint() == nullptr, "Factory permitted mutable registry after bootstrap");
}
void typed_action_page_isolation() {
  environment f; const auto page = page_bootstrap(f.bridge).at("result");
  require(page_call(f.bridge, page, "public.calculate", {{"value", 2}}).at("ok") == false &&
      page_call(f.bridge, page, "public.family.start", {{"operationId", "page"}, {"input", {{"value", 2}}}}).at("ok") == false,
      "Page created native action/family authority");
  std::atomic<bool> received{false}, revoked{false};
  f.start(); f.connect([&](client& pipe) {
    const auto result = pipe.call("public.calculate", {{"value", 2}});
    require(result.at("ok") == true && result.at("result") == json{{"value", 2}}, "Real registered action failed");
    const auto catalog = pipe.call("bridge.describe", {{"cursor", 0}, {"limit", 32}});
    require(catalog.at("ok") == true, "Actual filtered local discovery failed");
    for (const auto& method : catalog.at("result").at("methods"))
      require(method.at("name") != "task.start" && method.at("name") != "task.query", "Discovery exposed ordinary task service");
    received.store(true, std::memory_order_release);
    const auto deadline = ::GetTickCount64() + 4000;
    while (!revoked.load(std::memory_order_acquire)) {
      require(::GetTickCount64() < deadline, "Owner did not revoke the still-connected scope");
      ::Sleep(1);
    }
  }, [&] {
    if (!received.load(std::memory_order_acquire) || revoked.load(std::memory_order_relaxed)) return;
    require(f.actions == 1 && f.authorizations == 1 && f.actual_scope && f.actual_scope->active(),
        "Actual action did not use one still-connected native grant");
    f.current = false; require(f.endpoint->revoke_scope(f.key), "Explicit native navigation revoke failed");
    revoked.store(true, std::memory_order_release);
  });
  await([&] { return !f.actual_scope->active(); }, *f.endpoint, "Precise native scope remained active");
}
void local_family_completion() {
  environment f; f.start();
  f.connect([&](client& pipe) {
    const auto started = pipe.call("public.family.start", {{"operationId", "once"}, {"input", {{"value", 3}}}});
    require(started.at("ok") == true && started.at("result").at("accepted") == true, "Public registered family did not start");
    const auto task_id = started.at("result").at("taskId");
    json result; const auto deadline = ::GetTickCount64() + 4000;
    do {
      result = pipe.call("public.family.query", {{"taskId", task_id}});
      require(result.at("ok") == true, "Actual family query failed");
      if (result.at("result").at("state") == "succeeded") break;
      require(::GetTickCount64() < deadline, "Family did not complete"); ::Sleep(1);
    } while (true);
    require(result.at("result").at("result") == json{{"safe", 3}} &&
        result.dump().find("Private worker detail") == std::string::npos &&
        result.dump().find("must not escape") == std::string::npos, "Local family leaked unprojected worker data");
  });
  require(f.entered.load(), "Registered same-service handler never ran");
}
void local_family_disconnect_cancel() {
  environment f(true); f.start();
  f.connect([&](client& pipe) {
    const auto started = pipe.call("public.family.start", {{"operationId", "disconnect"}, {"input", {{"value", 4}}}});
    require(started.at("ok") == true, "Public registered family did not start");
    const auto task_id = started.at("result").at("taskId");
    const auto deadline = ::GetTickCount64() + 4000;
    while (!f.entered.load(std::memory_order_acquire)) {
      const auto queried = pipe.call("public.family.query", {{"taskId", task_id}});
      require(queried.at("ok") == true && ::GetTickCount64() < deadline, "Registered handler never ran");
      ::Sleep(1);
    }
    // Actual client handle closes after this callback; owner cleanup must cancel
    // the same private task_service which registered the family, not a spare.
  });
  await([&] { return f.cancelled.load(std::memory_order_acquire); }, *f.endpoint,
      "Actual disconnect did not cancel registered same-service task");
}
}  // namespace

int main(int argc, char** argv) {
  ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  try {
    require(argc == 2, "Expected exactly one public composition case");
    const std::string name = argv[1];
    if (name == "factory_stopped_registration_freeze") factory_stopped_registration_freeze();
    else if (name == "factory_allocation_failure") factory_allocation_failure();
    else if (name == "factory_after_bootstrap") factory_after_bootstrap();
    else if (name == "typed_action_page_isolation") typed_action_page_isolation();
    else if (name == "local_family_completion") local_family_completion();
    else if (name == "local_family_disconnect_cancel") local_family_disconnect_cancel();
    else throw std::runtime_error("Unknown public composition case");
    std::cout << "public-json-bridge-endpoint " << name << " passed\n"; return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
