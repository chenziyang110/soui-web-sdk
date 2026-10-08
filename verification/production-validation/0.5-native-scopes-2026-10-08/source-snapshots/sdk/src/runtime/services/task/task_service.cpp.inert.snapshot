#include <soui/runtime/services/task/task_service.h>
#include <soui/runtime/core/task/task_store.h>

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
  return percent <= 100U && message.size() <= max_task_message_bytes &&
         progress_(percent, std::move(message));
}

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
    handler callback;
    std::atomic<bool> cancel_requested{false};
  };
  struct ownership {
    scope context;
    std::string operation_key;
    std::string scope_key;
    std::string result_json{"{}"};
    std::shared_ptr<job> active;
    bool revoked{false};
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
  std::unordered_map<std::string, handler> handlers;
  std::deque<std::shared_ptr<job>> queue;
  std::unordered_map<std::string, ownership> owners;
  std::unordered_map<std::string, std::string> operations;
  std::unordered_map<std::string, subscription> subscriptions;
  std::vector<std::thread> workers;
  task_store tasks{"sdk-task-"};
  event_sink sink;

  void emit(const std::string& task_id) {
    const auto snapshot = tasks.query(task_id);
    if (!snapshot) return;
    event_sink send;
    std::string session;
    std::uint64_t sequence = 0;
    {
      std::lock_guard<std::mutex> lock(mutex);
      const auto owner = owners.find(task_id);
      if (owner == owners.end() || owner->second.revoked || !owner->second.context.authorization ||
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
    const auto event = json{{"sessionId", session}, {"taskId", task_id},
                            {"eventName", "task.progress"}, {"sequence", sequence},
                            {"payload", {{"state", state_name(snapshot->state)},
                                         {"progress", snapshot->progress}, {"message", snapshot->message}}}}.dump();
    try { send(event); } catch (...) { /* A page listener cannot stop native task cleanup. */ }
  }

  void finish(const std::shared_ptr<job>& item, task_outcome outcome) {
    std::lock_guard<std::mutex> lock(mutex);
    // A cancellation request cannot undo work. Preserve the handler's actual
    // terminal outcome; only an unexecuted job or explicit confirmation cancels.
    if (outcome.kind == result_kind::timeout_observed || outcome.kind == result_kind::input_required) {
      outcome.kind = result_kind::failure;
      outcome.result_json = "{}";
      outcome.message = "A task handler returned a nonterminal result.";
    }
    if (outcome.result_json.size() > bridge_transport::max_params_bytes ||
        outcome.message.size() > max_task_message_bytes ||
        !json::parse(outcome.result_json, nullptr, false).is_object()) {
      outcome.kind = result_kind::failure;
      outcome.result_json = "{}";
      outcome.message = "A task handler returned an invalid result object.";
    }
    const auto found = owners.find(item->id);
    if (found != owners.end()) {
      found->second.result_json = std::move(outcome.result_json);
      found->second.active.reset();
    }
    (void)tasks.complete(item->id, outcome.kind, outcome.message);
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
        execute = owner != owners.end() && !owner->second.revoked && owner->second.context.executable() &&
            !item->cancel_requested.load(std::memory_order_acquire) && tasks.mark_running(item->id);
      }
      if (!execute) {
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
        execute = owner != owners.end() && !owner->second.revoked && owner->second.context.executable() &&
            !item->cancel_requested.load(std::memory_order_acquire);
      }
      if (!execute) {
        finish(item, {result_kind::task_cancelled, "{}", "Native task cancelled before execution."});
        emit(item->id);
        continue;
      }
      task_control control(
          [item] { return item->cancel_requested.load(std::memory_order_acquire); },
          [this, item](std::uint32_t percent, std::string message) {
            if (item->cancel_requested.load(std::memory_order_acquire) ||
                !tasks.update_progress(item->id, percent, std::move(message))) return false;
            emit(item->id);
            return true;
          });
      task_outcome outcome;
      try { outcome = item->callback(item->invocation, control); }
      catch (...) { outcome = {result_kind::failure, "{}", "Native task handler failed."}; }
      finish(item, std::move(outcome));
      emit(item->id);
    }
  }

  void prune_evicted() {
    for (auto iterator = owners.begin(); iterator != owners.end();) {
      if (!tasks.query(iterator->first)) {
        operations.erase(iterator->second.operation_key);
        for (auto subscription = subscriptions.begin(); subscription != subscriptions.end();) {
          if (subscription->second.task_id == iterator->first) subscription = subscriptions.erase(subscription);
          else ++subscription;
        }
        iterator = owners.erase(iterator);
      } else ++iterator;
    }
  }
};

task_service::task_service() : impl_{std::make_unique<impl>()} {}

task_service::~task_service() {
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->stopping = true;
    for (auto& entry : impl_->owners) {
      if (entry.second.active) entry.second.active->cancel_requested.store(true, std::memory_order_release);
    }
  }
  impl_->ready.notify_all();
  for (auto& worker : impl_->workers) if (worker.joinable()) worker.join();
}

bool task_service::register_handler(std::string kind, handler callback) {
  if (!valid_name(kind) || !callback) return false;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->stopping || impl_->handlers.size() >= 64U || impl_->handlers.find(kind) != impl_->handlers.end()) return false;
  return impl_->handlers.emplace(std::move(kind), std::move(callback)).second;
}

void task_service::set_event_sink(event_sink sink) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->sink = std::move(sink);
}

bool task_service::shutdown_scope(const std::shared_ptr<const authorization_scope>& scope) {
  if (!scope) return false;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (scope->active()) return false;
  for (auto& entry : impl_->owners) {
    if (entry.second.context.authorization != scope) continue;
    entry.second.revoked = true;
    if (entry.second.active) {
      entry.second.active->cancel_requested.store(true, std::memory_order_release);
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
      entry.second.active->cancel_requested.store(true, std::memory_order_release);
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
  const auto scope_key = kind + '\x1f' + input;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->stopping) return error("E_BUSY", "The native task service is shutting down.");
  // A request may have waited after transport validation. Recheck under the
  // resource lock so revoke-before-shutdown cannot miss a late admission.
  if (!authorized(request, "task.start")) return error("E_UNAUTHORIZED", "A live task.start authorization is required.");
  const auto operation_key = request.authorization->scope_id() + '\x1f' + operation_id;
  const auto previous = impl_->operations.find(operation_key);
  if (previous != impl_->operations.end()) {
    const auto owner = impl_->owners.find(previous->second);
    if (owner != impl_->owners.end() && owner->second.context.matches(request) && !owner->second.revoked) {
      if (owner->second.scope_key != scope_key)
        return error("E_OPERATION_CONFLICT", "operationId belongs to a different task request.");
      return json{{"taskId", previous->second}, {"accepted", true}, {"duplicate", true}}.dump();
    }
    return error("E_OPERATION_CONFLICT", "operationId belongs to a different task scope.");
  }
  const auto handler = impl_->handlers.find(kind);
  if (handler == impl_->handlers.end()) return error("E_UNKNOWN_METHOD", "No native task handler is registered for kind.");
  if (impl_->inflight >= max_inflight) return error("E_BUSY", "The native task queue is full.");
  if (impl_->workers.empty()) {
    try {
      for (std::size_t index = 0; index < max_workers; ++index)
        impl_->workers.emplace_back([state = impl_.get()] { state->run(); });
    } catch (...) {
      if (impl_->workers.empty()) return error("E_NATIVE_FAILURE", "Native task workers are unavailable.");
    }
  }
  auto item = std::make_shared<impl::job>();
  item->invocation = {request.session_id, request.origin, operation_id, input};
  item->callback = handler->second;
  if (!authorized(request, "task.start")) return error("E_UNAUTHORIZED", "Task authorization expired before admission.");
  bool duplicate = false, conflict = false, capacity = false;
  const auto snapshot = impl_->tasks.start_scoped(operation_key, scope_key, &duplicate, &conflict, &capacity);
  if (capacity) return error("E_BUSY", "The native task store capacity is exhausted.");
  if (conflict || duplicate || snapshot.task_id.empty())
    return error("E_OPERATION_CONFLICT", "Native task operation identity is inconsistent.");
  item->id = snapshot.task_id;
  impl_->owners.emplace(item->id, impl::ownership{{request.session_id, request.origin, request.authorization},
      operation_key, scope_key, "{}", item, false});
  impl_->operations.emplace(operation_key, item->id);
  impl_->queue.push_back(item);
  ++impl_->inflight;
  if (impl_->owners.size() > task_store::max_tasks) impl_->prune_evicted();
  impl_->ready.notify_one();
  return json{{"taskId", item->id}, {"accepted", true}, {"duplicate", false}}.dump();
}

std::string task_service::query(const bridge_request& request) const {
  const auto params = parameters(request);
  const auto task_id = params.is_object() ? field(params, "taskId") : std::string{};
  if (!valid_name(task_id)) return error("E_INVALID_PARAMS", "A bounded taskId is required.");
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!authorized(request, "task.query")) return error("E_UNAUTHORIZED", "A live task.query authorization is required.");
  const auto owner = impl_->owners.find(task_id);
  if (owner == impl_->owners.end() || owner->second.revoked || !owner->second.context.matches(request))
    return error("E_TASK_NOT_FOUND", "The task is not valid for this session.");
  const auto snapshot = impl_->tasks.query(task_id);
  if (!snapshot) return error("E_TASK_NOT_FOUND", "The native task is no longer retained.");
  json result = {{"taskId", task_id}, {"state", state_name(snapshot->state)},
                 {"progress", snapshot->progress}, {"message", snapshot->message},
                 {"sequence", snapshot->sequence}};
  if (terminal(snapshot->state)) result["result"] = json::parse(owner->second.result_json);
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
    if (owner == impl_->owners.end() || owner->second.revoked || !owner->second.context.matches(request))
      return error("E_TASK_NOT_FOUND", "The task is not valid for this session.");
    const auto snapshot = impl_->tasks.query(task_id);
    if (!snapshot) return error("E_TASK_NOT_FOUND", "The native task is no longer retained.");
    if (terminal(snapshot->state)) return error("E_TASK_TERMINAL", "The task already has a terminal result.");
    if (owner->second.active) owner->second.active->cancel_requested.store(true, std::memory_order_release);
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
  if (owner == impl_->owners.end() || owner->second.revoked || !owner->second.context.matches(request) ||
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
