#include <soui/runtime/services/task/task_service.h>
#include <soui/runtime/core/task/task_store.h>
#include "../gui/gui_commands_internal.h"

#include "../../../../../third_party/include/json/json.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#ifdef SOUI_TASK_FAMILY_TEST_ONLY
#include "task_family_test_hooks.h"
#endif

namespace soui::runtime {
namespace {
using json = nlohmann::json;
constexpr std::size_t max_task_message_bytes = 4096U;

std::string error(const char* code, const char* message) {
  return json{{"error", code}, {"message", message}}.dump();
}

bool valid_name(std::string_view text) {
  if (text.empty() || text.size() > 128U) return false;
  return std::all_of(text.begin(), text.end(), [](unsigned char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || value == '-' || value == '_' ||
           value == '.' || value == ':';
  });
}

bool terminal(task_state state) {
  return state == task_state::succeeded || state == task_state::failed ||
         state == task_state::cancelled || state == task_state::partial;
}

const char* state_name(task_state state) {
  switch (state) {
    case task_state::queued: return "queued";
    case task_state::running: return "running";
    case task_state::timeout_observed: return "timeout";
    case task_state::cancel_requested: return "cancel-pending";
    case task_state::succeeded: return "succeeded";
    case task_state::failed: return "failed";
    case task_state::cancelled: return "cancelled";
    case task_state::partial: return "partial";
  }
  return "failed";
}

std::string field(const json& value, const char* name) {
  const auto found = value.find(name);
  return found != value.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

json parameters(const bridge_request& request) {
  return json::parse(request.params_json, nullptr, false);
}

json bounded_json(std::string_view text, std::size_t limit = bridge_transport::max_params_bytes) {
  if (text.empty() || text.size() > limit) return json(json::value_t::discarded);
  try {
    return json::parse(text.begin(), text.end(), [](int depth, json::parse_event_t, json&) {
      if (static_cast<std::size_t>(depth) > bridge_transport::max_json_depth)
        throw std::invalid_argument("Task JSON exceeds the depth bound");
      return true;
    });
  } catch (...) { return json(json::value_t::discarded); }
}

json object_schema(json properties = json::object(), json required = json::array()) {
  return {{"type", "object"}, {"properties", std::move(properties)},
          {"required", std::move(required)}, {"additionalProperties", false}};
}

json identifier_schema(std::size_t maximum = 128U) {
  return {{"type", "string"}, {"minLength", 1}, {"maxLength", maximum}};
}

std::string contract_json(const std::string& title, const json& input, const json& output) {
  return json{{"protocol", "soui.action/1"}, {"title", title},
              {"description", "Explicit native local task family."},
              {"inputSchema", input}, {"outputSchema", output}}.dump();
}

bool authorized(const bridge_request& request, const char* capability) {
  return request.authorization && request.authorization->matches(request.session_id, request.origin) &&
         request.authorization->allows(capability) && request.authorization->active();
}
}  // namespace

task_control::task_control(std::function<bool()> cancelled,
                           std::function<bool(std::uint32_t, std::string)> progress)
    : cancelled_{std::move(cancelled)}, progress_{std::move(progress)} {}

bool task_control::cancellation_requested() const { return cancelled_(); }

bool task_control::report_progress(std::uint32_t percent, std::string message) const {
  if (percent > 100U || message.size() > max_task_message_bytes) return false;
  try { (void)json(message).dump(); }
  catch (...) { return false; }
  return progress_(percent, std::move(message));
}

struct local_task_family::state {
  std::weak_ptr<const void> issuer;
  std::string name, kind;
  std::string start_method, query_method, cancel_method;
  std::string start_capability, query_capability, cancel_capability;
  std::function<std::string(const task_outcome&)> project_result;
  std::shared_ptr<const task_service::handler> callback;
  std::shared_ptr<const detail::gui_family_hooks> gui;
  std::shared_ptr<const action_contract> start_contract, query_contract, cancel_contract, result_contract;
};

local_task_family::local_task_family(std::shared_ptr<const state> data) : data_{std::move(data)} {}
local_task_family::~local_task_family() = default;
bool local_task_family::active() const noexcept { return !data_->issuer.expired(); }

struct task_service::impl {
  struct scope {
    std::string session;
    std::string origin;
    std::shared_ptr<const authorization_scope> authorization;
    bool matches(const bridge_request& request) const {
      return session == request.session_id && origin == request.origin && authorization &&
             authorization == request.authorization;
    }
    bool executable() const {
      return authorization && authorization->matches(session, origin) && authorization->allows("task.start");
    }
  };
  struct job {
    std::string id;
    task_invocation invocation;
    std::shared_ptr<const handler> callback;
    std::shared_ptr<const local_task_family> family;
    std::shared_ptr<detail::gui_job_context> gui;
    std::atomic<bool> cancel_requested{false};
    void cancel() noexcept {
      cancel_requested.store(true, std::memory_order_release);
      if (gui) gui->request_cancel();
    }
  };
  struct result_cache {
    std::string json{"{}"};
    bool projection_failed{false};
  };
  struct ownership {
    scope context;
    std::string operation_key;
    std::string scope_key;
    std::shared_ptr<result_cache> result;
    std::shared_ptr<job> active;
    bool revoked{false};
    std::string kind;
    std::shared_ptr<const local_task_family> family;
  };
  struct subscription {
    scope context;
    std::string task_id;
  };

  mutable std::mutex mutex;
  std::condition_variable ready;
  bool stopping{false};
  std::size_t inflight{0};
  std::uint64_t next_subscription{1};
  std::unordered_map<std::string, std::shared_ptr<const handler>> handlers;
  std::shared_ptr<const void> family_issuer{std::make_shared<const unsigned char>(0)};
  std::unordered_map<std::string, std::shared_ptr<const local_task_family>> families;
  std::deque<std::shared_ptr<job>> queue;
  std::unordered_map<std::string, ownership> owners;
  std::unordered_map<std::string, std::string> operations;
  std::unordered_map<std::string, subscription> subscriptions;
  std::vector<std::thread> workers;
  task_store tasks{"sdk-task-"};
  std::shared_ptr<const event_sink> sink;

  bool owns_family(const std::shared_ptr<const local_task_family>& family) const {
    if (stopping || !family || !family_issuer || family->data_->issuer.lock() != family_issuer) return false;
    const auto found = families.find(family->data_->name);
    const auto callback = handlers.find(family->data_->kind);
    return found != families.end() && found->second == family && callback != handlers.end() &&
        callback->second == family->data_->callback;
  }

  bool local_authorized(const std::shared_ptr<const local_task_family>& family,
      const bridge_request& request, const std::string& method, const std::string& capability) const {
    return owns_family(family) && request.method == method && request.authorization &&
        request.authorization->matches(request.session_id, request.origin) &&
        request.authorization->allows_local_method(method, capability);
  }

  bool executable(const ownership& owner) const {
    if (!owner.family) return owner.context.executable();
    const auto& data = *owner.family->data_;
    return owns_family(owner.family) && owner.kind == data.kind && owner.context.authorization &&
        owner.context.authorization->matches(owner.context.session, owner.context.origin) &&
        owner.context.authorization->allows_local_method(data.start_method, data.start_capability);
  }

  std::string admit_locked(const bridge_request& request, const std::string& kind,
      const std::string& operation_id, const std::string& input,
      const std::shared_ptr<const local_task_family>& family = {},
      const std::shared_ptr<detail::gui_job_context>& gui = {}) {
    try {
    const auto live = [&] {
      return family ? local_authorized(family, request, family->data_->start_method, family->data_->start_capability)
                    : authorized(request, "task.start");
    };
    if (stopping) return error("E_BUSY", "The native task service is shutting down.");
    if (!live()) return error("E_UNAUTHORIZED", "A live task authorization is required.");
    const auto operation_key = request.authorization->scope_id() + '\x1f' + operation_id;
    const auto scope_key = kind + '\x1f' + input;
    const auto previous = operations.find(operation_key);
    if (previous != operations.end()) {
      const auto owner = owners.find(previous->second);
      if (owner != owners.end() && owner->second.context.matches(request) && !owner->second.revoked &&
          owner->second.family == family && owner->second.kind == kind) {
        if (owner->second.scope_key != scope_key)
          return error("E_OPERATION_CONFLICT", "operationId belongs to a different task request.");
        return json{{"taskId", previous->second}, {"accepted", true}, {"duplicate", true}}.dump();
      }
      return error("E_OPERATION_CONFLICT", "operationId belongs to a different task scope or family.");
    }
    const auto callback = handlers.find(kind);
    if (!family && std::any_of(families.begin(), families.end(), [&](const auto& entry) {
      return entry.second->data_->kind == kind && entry.second->data_->gui;
    })) return error("E_UNAUTHORIZED", "GUI tasks require their native local family.");
    if (callback == handlers.end()) return error("E_UNKNOWN_METHOD", "No native task handler is registered for kind.");
    if (family && family->data_->gui && !gui) return error("E_BUSY", "GUI command capacity is unavailable.");
    if (inflight >= task_service::max_inflight) return error("E_BUSY", "The native task queue is full.");
    if (workers.empty()) {
      try {
        for (std::size_t index = 0; index < task_service::max_workers; ++index)
          workers.emplace_back([this] { run(); });
      } catch (...) {
        if (workers.empty()) return error("E_NATIVE_FAILURE", "Native task workers are unavailable.");
      }
    }
    auto item = std::make_shared<job>();
    item->invocation = {request.session_id, request.origin, operation_id, input};
    item->callback = callback->second;
    item->family = family;
    item->gui = gui;
    if (!live()) return error("E_UNAUTHORIZED", "Task authorization expired before admission.");
    auto prepared = tasks.prepare_start_scoped(operation_key, scope_key);
    const auto& snapshot = prepared->snapshot;
    if (prepared->capacity) return error("E_BUSY", "The native task store capacity is exhausted.");
    if (prepared->conflict || prepared->duplicate || snapshot.task_id.empty())
      return error("E_OPERATION_CONFLICT", "Native task operation identity is inconsistent.");
    item->id = snapshot.task_id;
    ownership owner{{request.session_id, request.origin, request.authorization}, operation_key, scope_key,
                    std::make_shared<result_cache>(), item, false, kind, family};
    #ifdef SOUI_TASK_FAMILY_TEST_ONLY
    detail::task_family_test::before_prepare(detail::task_family_test::prepare_point::admission_owners);
    #endif
    auto candidate_owners = owners;
    #ifdef SOUI_TASK_FAMILY_TEST_ONLY
    detail::task_family_test::before_prepare(detail::task_family_test::prepare_point::admission_operations);
    #endif
    auto candidate_operations = operations;
    #ifdef SOUI_TASK_FAMILY_TEST_ONLY
    detail::task_family_test::before_prepare(detail::task_family_test::prepare_point::admission_queue);
    #endif
    auto candidate_queue = queue;
    #ifdef SOUI_TASK_FAMILY_TEST_ONLY
    detail::task_family_test::before_prepare(detail::task_family_test::prepare_point::admission_subscriptions);
    #endif
    auto candidate_subscriptions = subscriptions;
    if (!prepared->evicted_id.empty()) {
      const auto evicted = candidate_owners.find(prepared->evicted_id);
      if (evicted != candidate_owners.end()) {
        candidate_operations.erase(evicted->second.operation_key);
        candidate_owners.erase(evicted);
      }
      for (auto iterator = candidate_subscriptions.begin(); iterator != candidate_subscriptions.end();) {
        if (iterator->second.task_id == prepared->evicted_id) iterator = candidate_subscriptions.erase(iterator);
        else ++iterator;
      }
    }
    candidate_owners.emplace(item->id, std::move(owner));
    candidate_operations.emplace(operation_key, item->id);
    candidate_queue.push_back(item);
    #ifdef SOUI_TASK_FAMILY_TEST_ONLY
    detail::task_family_test::before_prepare(detail::task_family_test::prepare_point::admission_receipt);
    #endif
    auto receipt = json{{"taskId", item->id}, {"accepted", true}, {"duplicate", false}}.dump();
    if (!live()) return error("E_UNAUTHORIZED", "Task authorization expired during preparation.");
    static_assert(noexcept(owners.swap(candidate_owners)) && noexcept(operations.swap(candidate_operations)) &&
        noexcept(queue.swap(candidate_queue)) && noexcept(subscriptions.swap(candidate_subscriptions)),
        "Task admission commit must not throw");
    tasks.commit_start_scoped(*prepared);
    owners.swap(candidate_owners);
    operations.swap(candidate_operations);
    queue.swap(candidate_queue);
    subscriptions.swap(candidate_subscriptions);
    ++inflight;
    ready.notify_one();
    return receipt;
    } catch (...) { return error("E_NATIVE_FAILURE", "Native task preparation failed."); }
  }

  std::string start_local(const std::shared_ptr<const local_task_family>& family, const bridge_request& request) {
    const auto params = bounded_json(request.params_json);
    if (!family || !params.is_object() || params.size() != 2U || !params.contains("operationId") ||
        !params.contains("input") || !params["input"].is_object() ||
        !family->data_->start_contract->validates_input(request.params_json))
      return error("E_INVALID_PARAMS", "Expected a bounded operationId and typed input.");
    const auto operation_id = field(params, "operationId");
    if (!valid_name(operation_id) || (!request.operation_id.empty() && request.operation_id != operation_id))
      return error("E_INVALID_PARAMS", "A bounded consistent operationId is required.");
    const auto input = params["input"].dump();
    std::shared_ptr<detail::gui_job_context> context;
    if (family->data_->gui) {
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (!local_authorized(family, request, family->data_->start_method, family->data_->start_capability))
          return error("E_UNAUTHORIZED", "GUI authorization is unavailable.");
        // A duplicate never prepares another slot/context. Final admission below
        // still rechecks scope and resolves a concurrent same-operation winner.
        if (operations.count(request.authorization->scope_id() + '\x1f' + operation_id))
          return admit_locked(request, family->data_->kind, operation_id, input, family);
      }
      try { context = family->data_->gui->prepare(request); }
      catch (...) { return error("E_NATIVE_FAILURE", "GUI preparation failed."); }
      if (!context) return error("E_BUSY", "GUI command is not ready or capacity is unavailable.");
    }
    std::lock_guard<std::mutex> lock(mutex);
    return admit_locked(request, family->data_->kind, operation_id, input, family, context);
  }

  std::string query_local(const std::shared_ptr<const local_task_family>& family, const bridge_request& request) const {
    const auto params = bounded_json(request.params_json);
    if (!family || !params.is_object() || params.size() != 1U ||
        (!params.contains("taskId") && !params.contains("operationId")) ||
        !family->data_->query_contract->validates_input(request.params_json))
      return error("E_INVALID_PARAMS", "Expected exactly one bounded taskId or operationId.");
    const bool by_operation = params.contains("operationId");
    const auto selector = field(params, by_operation ? "operationId" : "taskId");
    if (!valid_name(selector)) return error("E_INVALID_PARAMS", "A bounded ASCII selector is required.");
    std::lock_guard<std::mutex> lock(mutex);
    const auto& data = *family->data_;
    if (!local_authorized(family, request, data.query_method, data.query_capability))
      return error("E_UNAUTHORIZED", "A live local query authorization is required.");
    std::string task_id = selector, operation_key;
    if (by_operation) {
      operation_key = request.authorization->scope_id() + '\x1f' + selector;
      const auto operation = operations.find(operation_key);
      if (operation == operations.end()) return error("E_TASK_NOT_FOUND", "No retained task is available.");
      task_id = operation->second;
    }
    const auto owner = owners.find(task_id);
    if (owner == owners.end() || owner->second.revoked || !owner->second.context.matches(request) ||
        owner->second.family != family || owner->second.kind != data.kind ||
        (by_operation && owner->second.operation_key != operation_key))
      return error("E_TASK_NOT_FOUND", "No retained task is available.");
    const auto snapshot = tasks.query(task_id);
    if (!snapshot) return error("E_TASK_NOT_FOUND", "No retained task is available.");
    if (!local_authorized(family, request, data.query_method, data.query_capability))
      return error("E_UNAUTHORIZED", "Local query authorization expired.");
    json result{{"taskId", task_id}, {"state", state_name(snapshot->state)},
                {"progress", snapshot->progress}, {"sequence", std::to_string(snapshot->sequence)}};
    if (terminal(snapshot->state)) {
      if (owner->second.result->projection_failed) result["projectionError"] = "E_RESULT_PROJECTION";
      else result["result"] = json::parse(owner->second.result->json);
    }
    const auto encoded=result.dump();
    if (data.gui && (encoded.size()>bridge_transport::max_params_bytes ||
        !data.query_contract->validates_output(encoded)))
      return error("E_RESULT_PROJECTION", "GUI query exceeds its complete result contract.");
    return encoded;
  }

  std::string cancel_local(const std::shared_ptr<const local_task_family>& family, const bridge_request& request) {
    const auto params = bounded_json(request.params_json);
    if (!family || !params.is_object() || params.size() != 1U || !params.contains("taskId") ||
        !family->data_->cancel_contract->validates_input(request.params_json))
      return error("E_INVALID_PARAMS", "Expected exactly one bounded taskId.");
    const auto task_id = field(params, "taskId");
    if (!valid_name(task_id)) return error("E_INVALID_PARAMS", "A bounded ASCII taskId is required.");
    std::lock_guard<std::mutex> lock(mutex);
    const auto& data = *family->data_;
    if (!local_authorized(family, request, data.cancel_method, data.cancel_capability))
      return error("E_UNAUTHORIZED", "A live local cancel authorization is required.");
    const auto owner = owners.find(task_id);
    if (owner == owners.end() || owner->second.revoked || !owner->second.context.matches(request) ||
        owner->second.family != family || owner->second.kind != data.kind)
      return error("E_TASK_NOT_FOUND", "No retained task is available.");
    const auto snapshot = tasks.query(task_id);
    if (!snapshot) return error("E_TASK_NOT_FOUND", "No retained task is available.");
    if (terminal(snapshot->state)) return error("E_TASK_TERMINAL", "The task already has a terminal result.");
    if (!local_authorized(family, request, data.cancel_method, data.cancel_capability))
      return error("E_UNAUTHORIZED", "Local cancel authorization expired.");
    if (owner->second.active) owner->second.active->cancel();
    if (!tasks.request_cancel(task_id)) return error("E_TASK_TERMINAL", "The task completed before cancellation.");
    return json{{"taskId", task_id}, {"state", "cancel-pending"}, {"cancelRequested", true}}.dump();
  }

  void emit(const std::string& task_id) {
    const auto snapshot = tasks.query(task_id);
    if (!snapshot) return;
    std::shared_ptr<const event_sink> send;
    std::string session;
    std::uint64_t sequence = 0;
    {
      std::lock_guard<std::mutex> lock(mutex);
      const auto owner = owners.find(task_id);
      if (owner == owners.end() || owner->second.family || owner->second.revoked || !owner->second.context.authorization ||
          !owner->second.context.authorization->active()) return;
      const bool subscribed = std::any_of(subscriptions.begin(), subscriptions.end(), [&](const auto& entry) {
        return entry.second.task_id == task_id && entry.second.context.session == owner->second.context.session &&
               entry.second.context.origin == owner->second.context.origin &&
               entry.second.context.authorization == owner->second.context.authorization &&
               entry.second.context.authorization->allows("task.subscribe");
      });
      if (!subscribed || !sink) return;
      send = sink;
      session = owner->second.context.session;
      sequence = snapshot->sequence;
    }
    try {
      const auto event = json{{"sessionId", session}, {"taskId", task_id},
                              {"eventName", "task.progress"}, {"sequence", sequence},
                              {"payload", {{"state", state_name(snapshot->state)},
                                           {"progress", snapshot->progress}, {"message", snapshot->message}}}}.dump();
      (*send)(event);
    } catch (...) { /* Serialization and listeners cannot stop native task cleanup. */ }
  }

  void finish(const std::shared_ptr<job>& item, task_outcome outcome) {
    // A cancellation request cannot undo work. Preserve the handler's actual
    // terminal outcome; only an unexecuted job or explicit confirmation cancels.
    if (outcome.kind == result_kind::timeout_observed || outcome.kind == result_kind::input_required) {
      outcome.kind = result_kind::failure;
      outcome.result_json = "{}";
      outcome.message = "A task handler returned a nonterminal result.";
    }
    bool valid_result = false;
    try {
      const auto value = bounded_json(outcome.result_json);
      valid_result = value.is_object() && outcome.message.size() <= max_task_message_bytes;
      if (valid_result) (void)value.dump(); // Reject invalid UTF-8 even in ignored fields.
      if (valid_result) (void)json(outcome.message).dump();
    } catch (...) { valid_result = false; }
    if (!valid_result) {
      outcome.kind = result_kind::failure;
      outcome.result_json = "{}";
      outcome.message = "A task handler returned an invalid result object.";
    }
    bool projection_failed = false;
    std::string published = outcome.result_json;
    if (item->family) {
      try {
        published = item->family->data_->project_result(outcome);
        projection_failed = published.size() > bridge_transport::max_params_bytes ||
            !item->family->data_->result_contract->validates_output(published);
      } catch (...) { projection_failed = true; }
      if (projection_failed) published = "{}";
    }
    std::lock_guard<std::mutex> lock(mutex);
    const auto found = owners.find(item->id);
    if (found != owners.end()) {
      found->second.result->json = std::move(published);
      found->second.result->projection_failed = projection_failed;
      found->second.active.reset();
    }
    (void)tasks.complete(item->id, outcome.kind, item->family ? std::string{} : outcome.message);
    if (inflight) --inflight;
  }

  void run() {
    for (;;) {
      std::shared_ptr<job> item;
      bool execute = false;
      {
        std::unique_lock<std::mutex> lock(mutex);
        ready.wait(lock, [this] { return stopping || !queue.empty(); });
        if (queue.empty() && stopping) return;
        item = std::move(queue.front());
        queue.pop_front();
        const auto owner = owners.find(item->id);
        execute = owner != owners.end() && !owner->second.revoked && executable(owner->second) &&
            !item->cancel_requested.load(std::memory_order_acquire) && tasks.mark_running(item->id);
      }
      if (!execute) {
        if (item->gui) item->gui->request_cancel();
        finish(item, {result_kind::task_cancelled, "{}", "Native task cancelled before execution."});
        emit(item->id);
        continue;
      }
      emit(item->id);
      {
        // Event callbacks may delay a worker. Recheck immediately before the
        // business callback is committed, not only when it leaves the queue.
        std::lock_guard<std::mutex> lock(mutex);
        const auto owner = owners.find(item->id);
        execute = owner != owners.end() && !owner->second.revoked && executable(owner->second) &&
            !item->cancel_requested.load(std::memory_order_acquire);
      }
      if (!execute) {
        if (item->gui) item->gui->request_cancel();
        finish(item, {result_kind::task_cancelled, "{}", "Native task cancelled before execution."});
        emit(item->id);
        continue;
      }
      task_control control(
          [item] { return item->cancel_requested.load(std::memory_order_acquire); },
          [this, item](std::uint32_t percent, std::string message) {
            if (item->cancel_requested.load(std::memory_order_acquire) ||
                !tasks.update_progress(item->id, percent, item->family ? std::string{} : std::move(message))) return false;
            emit(item->id);
            return true;
          });
      task_outcome outcome;
      try { outcome = item->gui ? item->family->data_->gui->run(item->gui, control)
                               : (*item->callback)(item->invocation, control); }
      catch (...) { outcome = {result_kind::failure, "{}", "Native task handler failed."}; }
      finish(item, std::move(outcome));
      emit(item->id);
    }
  }

};

task_service::task_service() : impl_{std::make_shared<impl>()} {}

task_service::~task_service() {
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->stopping = true;
    impl_->family_issuer.reset();
    for (auto& entry : impl_->owners) {
      if (entry.second.active) entry.second.active->cancel();
    }
  }
  impl_->ready.notify_all();
  for (auto& worker : impl_->workers) if (worker.joinable()) worker.join();
}

bool task_service::register_handler(std::string kind, handler callback) {
  if (!valid_name(kind) || !callback) return false;
  const auto shared_callback = std::make_shared<const handler>(std::move(callback));
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->stopping || impl_->handlers.size() >= 64U || impl_->handlers.find(kind) != impl_->handlers.end()) return false;
  return impl_->handlers.emplace(std::move(kind), shared_callback).second;
}

std::shared_ptr<const local_task_family> task_service::register_local_family(
    capability_registry& registry, local_task_family_definition definition) {
  return register_local_family_impl(registry, std::move(definition), {});
}

std::shared_ptr<const local_task_family> task_service::register_local_family_impl(
    capability_registry& registry, local_task_family_definition definition,
    std::shared_ptr<const detail::gui_family_hooks> gui) {
  try {
    if (!valid_name(definition.name) || definition.name == "task" || !valid_name(definition.kind) || !definition.project_result ||
        !valid_name(definition.name + ".start") || !valid_name(definition.name + ".query") ||
        !valid_name(definition.name + ".cancel")) return {};
    const auto input = bounded_json(definition.input_schema_json, action_contract::max_schema_bytes);
    const auto output = bounded_json(definition.result_schema_json, action_contract::max_schema_bytes);
    const auto id = identifier_schema();
    const auto start_input = object_schema({{"operationId", id}, {"input", input}}, {"operationId", "input"});
    const auto start_output = object_schema({{"taskId", id}, {"accepted", {{"type", "boolean"}}},
                                             {"duplicate", {{"type", "boolean"}}}},
                                            {"taskId", "accepted", "duplicate"});
    const auto query_input = object_schema({{"taskId", id}, {"operationId", id}});
    const json states{{"type", "string"}, {"maxLength", 16},
                      {"enum", {"queued", "running", "timeout", "cancel-pending", "succeeded", "failed", "cancelled", "partial"}}};
    const auto query_output = object_schema({{"taskId", id}, {"state", states},
        {"progress", {{"type", "integer"}, {"minimum", 0}, {"maximum", 100}}},
        {"sequence", identifier_schema(20)}, {"result", output},
        {"projectionError", {{"type", "string"}, {"maxLength", 32}, {"enum", {"E_RESULT_PROJECTION"}}}}},
        {"taskId", "state", "progress", "sequence"});
    const auto cancel_input = object_schema({{"taskId", id}}, {"taskId"});
    const auto cancel_output = object_schema({{"taskId", id},
        {"state", {{"type", "string"}, {"maxLength", 16}, {"enum", {"cancel-pending"}}}},
        {"cancelRequested", {{"type", "boolean"}}}}, {"taskId", "state", "cancelRequested"});
    auto data = std::make_shared<local_task_family::state>();
    data->gui = std::move(gui);
    data->name = std::move(definition.name); data->kind = std::move(definition.kind);
    data->start_method = data->name + ".start"; data->query_method = data->name + ".query";
    data->cancel_method = data->name + ".cancel";
    data->start_capability = std::move(definition.start_capability);
    data->query_capability = std::move(definition.query_capability);
    data->cancel_capability = std::move(definition.cancel_capability);
    data->project_result = std::move(definition.project_result);
    const auto start_contract = contract_json(data->start_method, start_input, start_output);
    const auto query_contract = contract_json(data->query_method, query_input, query_output);
    const auto cancel_contract = contract_json(data->cancel_method, cancel_input, cancel_output);
    data->start_contract = action_contract::parse(start_contract);
    data->query_contract = action_contract::parse(query_contract);
    data->cancel_contract = action_contract::parse(cancel_contract);
    data->result_contract = action_contract::parse(contract_json(data->name, object_schema(), output));
    if (!data->start_contract || !data->query_contract || !data->cancel_contract || !data->result_contract) return {};
    const auto family = std::shared_ptr<const local_task_family>(new local_task_family(data));
    const std::weak_ptr<impl> weak = impl_;
    // Existing registry callbacks are immutable shared objects. Snapshotting
    // cannot execute a user functor copy under either bridge or service locks.
    const auto prepared = registry.prepare_local_actions({
        {data->start_method, data->start_capability, start_contract, [weak, family](const bridge_request& request) {
          const auto state = weak.lock();
          return state ? state->start_local(family, request) : error("E_UNAUTHORIZED", "Task family is unavailable.");
        }},
        {data->query_method, data->query_capability, query_contract, [weak, family](const bridge_request& request) {
          const auto state = weak.lock();
          return state ? state->query_local(family, request) : error("E_UNAUTHORIZED", "Task family is unavailable.");
        }},
        {data->cancel_method, data->cancel_capability, cancel_contract, [weak, family](const bridge_request& request) {
          const auto state = weak.lock();
          return state ? state->cancel_local(family, request) : error("E_UNAUTHORIZED", "Task family is unavailable.");
        }}});
    if (!prepared) return {};
    {
      std::lock_guard<std::mutex> lock(impl_->mutex);
      if (impl_->stopping || !impl_->family_issuer || !impl_->handlers.count(data->kind) ||
          impl_->families.size() >= 64U || impl_->families.count(data->name)) return {};
      data->issuer = impl_->family_issuer;
      data->callback = impl_->handlers.at(data->kind);
      #ifdef SOUI_TASK_FAMILY_TEST_ONLY
      detail::task_family_test::before_prepare(detail::task_family_test::prepare_point::registration_families);
      #endif
      auto candidate = impl_->families;
      candidate.emplace(data->name, family);
      // No allocation, callback, validation or failing operation follows.
      static_assert(noexcept(impl_->families.swap(candidate)), "Family commit must not throw");
      impl_->families.swap(candidate);
      registry.commit_local_actions(*prepared);
    }
    return family;
  } catch (...) { return {}; }
}

bool task_service::register_gui_families(capability_registry& registry,
    std::vector<detail::gui_family_registration> entries) {
  // Called only by json_bridge's pre-bootstrap registration transaction. No
  // endpoint/page can dispatch while the bridge's registration lock is held.
  decltype(impl_->handlers) previous_handlers;
  decltype(impl_->families) previous_families;
  try {
    {
      std::lock_guard<std::mutex> lock(impl_->mutex);
      if (impl_->stopping || !impl_->workers.empty() || !impl_->owners.empty()) return false;
      previous_handlers = impl_->handlers;
      previous_families = impl_->families;
    }
    auto rollback = [&] {
      std::lock_guard<std::mutex> lock(impl_->mutex);
      impl_->handlers.swap(previous_handlers);
      impl_->families.swap(previous_families);
    };
    try {
      capability_registry candidate = registry;
      for (auto& entry : entries) {
        if (!entry.hooks || !entry.hooks->prepare || !entry.hooks->run ||
            !register_handler(entry.definition.kind, [](const task_invocation&, const task_control&) {
              return task_outcome{result_kind::failure, "{}", "GUI tasks require native admission."};
            }) || !register_local_family_impl(candidate, std::move(entry.definition), std::move(entry.hooks))) {
          rollback(); return false;
        }
      }
      static_assert(noexcept(registry.methods_.swap(candidate.methods_)), "GUI table commit must not throw");
      registry.methods_.swap(candidate.methods_);
      return true;
    } catch (...) { rollback(); return false; }
  } catch (...) { return false; }
}

std::string task_service::start_local(const std::shared_ptr<const local_task_family>& family,
                                     const bridge_request& request) {
  return impl_->start_local(family, request);
}
std::string task_service::query_local(const std::shared_ptr<const local_task_family>& family,
                                     const bridge_request& request) const {
  return impl_->query_local(family, request);
}
std::string task_service::cancel_local(const std::shared_ptr<const local_task_family>& family,
                                      const bridge_request& request) {
  return impl_->cancel_local(family, request);
}

void task_service::set_event_sink(event_sink sink) {
  auto prepared = sink ? std::make_shared<const event_sink>(std::move(sink)) : nullptr;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->sink.swap(prepared);
  }
}

bool task_service::shutdown_scope(const std::shared_ptr<const authorization_scope>& scope) {
  if (!scope) return false;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (scope->active()) return false;
  for (auto& entry : impl_->owners) {
    if (entry.second.context.authorization != scope) continue;
    entry.second.revoked = true;
    if (entry.second.active) {
      entry.second.active->cancel();
      (void)impl_->tasks.request_cancel(entry.first);
    }
  }
  for (auto iterator = impl_->subscriptions.begin(); iterator != impl_->subscriptions.end();) {
    if (iterator->second.context.authorization == scope) iterator = impl_->subscriptions.erase(iterator);
    else ++iterator;
  }
  return true;
}

void task_service::shutdown_session(const std::string& session_id) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  for (auto& entry : impl_->owners) {
    if (entry.second.context.session != session_id) continue;
    entry.second.revoked = true;
    if (entry.second.active) {
      entry.second.active->cancel();
      (void)impl_->tasks.request_cancel(entry.first);
    }
  }
  for (auto iterator = impl_->subscriptions.begin(); iterator != impl_->subscriptions.end();) {
    if (iterator->second.context.session == session_id) iterator = impl_->subscriptions.erase(iterator);
    else ++iterator;
  }
}

std::string task_service::start(const bridge_request& request) {
  const auto params = parameters(request);
  if (!params.is_object() || params.size() != 3U || !params.contains("kind") ||
      !params.contains("operationId") || !params.contains("input") || !params["input"].is_object())
    return error("E_INVALID_PARAMS", "kind, operationId and object input are required.");
  const auto kind = field(params, "kind");
  const auto operation_id = field(params, "operationId");
  if (!valid_name(kind) || !valid_name(operation_id))
    return error("E_INVALID_PARAMS", "kind and operationId must be bounded identifiers.");
  const auto input = params["input"].dump();
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->admit_locked(request, kind, operation_id, input);
}

std::string task_service::query(const bridge_request& request) const {
  const auto params = parameters(request);
  if (!params.is_object() || params.size() != 1U ||
      (!params.contains("taskId") && !params.contains("operationId")))
    return error("E_INVALID_PARAMS", "Expected exactly one taskId or operationId.");
  const bool by_operation = params.contains("operationId");
  const auto selector = field(params, by_operation ? "operationId" : "taskId");
  if (!valid_name(selector)) return error("E_INVALID_PARAMS", "A bounded ASCII taskId or operationId is required.");
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!authorized(request, "task.query")) return error("E_UNAUTHORIZED", "A live task.query authorization is required.");
  std::string task_id = selector;
  std::string operation_key;
  if (by_operation) {
    // This public ID only locates a candidate; actual native ownership is checked below.
    operation_key = request.authorization->scope_id() + '\x1f' + selector;
    const auto operation = impl_->operations.find(operation_key);
    if (operation == impl_->operations.end())
      return error("E_TASK_NOT_FOUND", "The task is not valid for this scope.");
    task_id = operation->second;
  }
  const auto owner = impl_->owners.find(task_id);
  if (owner == impl_->owners.end() || owner->second.family || owner->second.revoked || !owner->second.context.matches(request) ||
      (by_operation && owner->second.operation_key != operation_key))
    return error("E_TASK_NOT_FOUND", "The task is not valid for this session.");
  const auto snapshot = impl_->tasks.query(task_id);
  if (!snapshot) return error("E_TASK_NOT_FOUND", "The native task is no longer retained.");
  if (!authorized(request, "task.query")) return error("E_UNAUTHORIZED", "Task authorization expired during lookup.");
  json result = {{"taskId", task_id}, {"state", state_name(snapshot->state)},
                 {"progress", snapshot->progress}, {"message", snapshot->message},
                 {"sequence", snapshot->sequence}};
  if (terminal(snapshot->state)) result["result"] = json::parse(owner->second.result->json);
  return result.dump();
}

std::string task_service::cancel(const bridge_request& request) {
  const auto params = parameters(request);
  const auto task_id = params.is_object() ? field(params, "taskId") : std::string{};
  if (!valid_name(task_id)) return error("E_INVALID_PARAMS", "A bounded taskId is required.");
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!authorized(request, "task.cancel")) return error("E_UNAUTHORIZED", "A live task.cancel authorization is required.");
    const auto owner = impl_->owners.find(task_id);
    if (owner == impl_->owners.end() || owner->second.family || owner->second.revoked || !owner->second.context.matches(request))
      return error("E_TASK_NOT_FOUND", "The task is not valid for this session.");
    const auto snapshot = impl_->tasks.query(task_id);
    if (!snapshot) return error("E_TASK_NOT_FOUND", "The native task is no longer retained.");
    if (terminal(snapshot->state)) return error("E_TASK_TERMINAL", "The task already has a terminal result.");
    if (owner->second.active) owner->second.active->cancel();
    if (!impl_->tasks.request_cancel(task_id)) return error("E_TASK_TERMINAL", "The task completed before cancellation.");
  }
  impl_->emit(task_id);
  return json{{"taskId", task_id}, {"state", "cancel-pending"}, {"cancelRequested", true}}.dump();
}

std::string task_service::subscribe(const bridge_request& request) {
  const auto params = parameters(request);
  const auto task_id = params.is_object() ? field(params, "taskId") : std::string{};
  const auto event_name = params.is_object() ? field(params, "eventName") : std::string{};
  if (!valid_name(task_id) || event_name != "task.progress")
    return error("E_INVALID_PARAMS", "A taskId and task.progress event are required.");
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!authorized(request, "task.subscribe")) return error("E_UNAUTHORIZED", "A live task.subscribe authorization is required.");
  const auto owner = impl_->owners.find(task_id);
  if (owner == impl_->owners.end() || owner->second.family || owner->second.revoked || !owner->second.context.matches(request) ||
      !impl_->tasks.query(task_id)) return error("E_TASK_NOT_FOUND", "The task is not valid for this session.");
  if (impl_->subscriptions.size() >= max_subscriptions)
    return error("E_BUSY", "Native task subscription capacity is exhausted.");
  const auto id = "sdk-subscription-" + std::to_string(impl_->next_subscription++);
  impl_->subscriptions.emplace(id, impl::subscription{{request.session_id, request.origin, request.authorization}, task_id});
  return json{{"subscriptionId", id}}.dump();
}

std::string task_service::unsubscribe(const bridge_request& request) {
  const auto params = parameters(request);
  const auto id = params.is_object() ? field(params, "subscriptionId") : std::string{};
  if (!valid_name(id)) return error("E_INVALID_PARAMS", "A bounded subscriptionId is required.");
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!authorized(request, "task.subscribe")) return error("E_UNAUTHORIZED", "A live task.subscribe authorization is required.");
  const auto found = impl_->subscriptions.find(id);
  if (found == impl_->subscriptions.end() || !found->second.context.matches(request))
    return error("E_SUBSCRIPTION_NOT_FOUND", "The subscription is not valid for this session.");
  impl_->subscriptions.erase(found);
  return R"({"removed":true})";
}

}  // namespace soui::runtime
