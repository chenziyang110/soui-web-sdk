#pragma once

#include <soui/diagnostics/runtime_metrics.h>
#include <soui/runtime/bridge/bridge_transport.h>
#include <soui/runtime/core/result/result.h>

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <string_view>

namespace soui::runtime {
namespace detail { class gui_commands_state; struct gui_family_hooks; struct gui_family_registration; }

struct task_invocation {
  std::string session_id;
  std::string origin;
  std::string operation_id;
  std::string parameters_json;
};

struct task_outcome {
  result_kind kind{result_kind::success};
  std::string result_json{"{}"};
  std::string message;
};

struct local_task_family_definition {
  std::string name, kind;
  std::string start_capability, query_capability, cancel_capability;
  std::string input_schema_json, result_schema_json;
  // Runs once on a worker, outside service locks. Publish only reviewed data.
  std::function<std::string(const task_outcome&)> project_result;
};

// Service-issued identity, never a wire credential. A kind string is not a family.
class local_task_family final {
 public:
  ~local_task_family();
  local_task_family(const local_task_family&) = delete;
  local_task_family& operator=(const local_task_family&) = delete;
  [[nodiscard]] bool active() const noexcept;
 private:
  friend class task_service;
  struct state;
  explicit local_task_family(std::shared_ptr<const state> data);
  std::shared_ptr<const state> data_;
};

// This reference is valid only while a registered handler is executing.
class task_control final {
 public:
  task_control(const task_control&) = delete;
  task_control& operator=(const task_control&) = delete;

  [[nodiscard]] bool cancellation_requested() const;
  [[nodiscard]] bool report_progress(std::uint32_t percent, std::string message = {}) const;

 private:
  friend class task_service;
  task_control(std::function<bool()> cancelled,
               std::function<bool(std::uint32_t, std::string)> progress);
  std::function<bool()> cancelled_;
  std::function<bool(std::uint32_t, std::string)> progress_;
};

// Optional public Bridge task module. Applications register bounded native
// handlers before bootstrap; the service owns a fixed worker pool and a bounded
// queue. Handlers must cooperate with cancellation and must not retain control.
// Every direct request needs the trusted authorization returned by
// permission_store::resolve. Empty scopes are rejected; the scope, session and
// origin own tasks/subscriptions across bearer rotation. Revoke authorization
// before shutdown_scope/shutdown_session so earlier requests cannot enqueue late.
class task_service final {
 public:
  static constexpr std::size_t max_workers{2};
  static constexpr std::size_t max_inflight{32};
  static constexpr std::size_t max_subscriptions{128};
  using handler = std::function<task_outcome(const task_invocation&, const task_control&)>;
  using event_sink = std::function<void(std::string_view)>;

  task_service();
  explicit task_service(soui::sdk::diagnostics::mode);
  [[nodiscard]] soui::sdk::diagnostics::status read_runtime_metrics(
      soui::sdk::diagnostics::tasks&) const noexcept;
  ~task_service();
  task_service(const task_service&) = delete;
  task_service& operator=(const task_service&) = delete;

  [[nodiscard]] bool register_handler(std::string kind, handler callback);
  // Before bootstrap only. Binds an existing handler and atomically publishes
  // name.start/query/cancel as local-only typed actions; no page grant is added.
  [[nodiscard]] std::shared_ptr<const local_task_family> register_local_family(
      capability_registry& registry, local_task_family_definition definition);
  [[nodiscard]] std::string start_local(const std::shared_ptr<const local_task_family>& family,
                                        const bridge_request& request);
  [[nodiscard]] std::string query_local(const std::shared_ptr<const local_task_family>& family,
                                        const bridge_request& request) const;
  [[nodiscard]] std::string cancel_local(const std::shared_ptr<const local_task_family>& family,
                                         const bridge_request& request);
  void set_event_sink(event_sink sink);
  // Requires a revoked/expired native scope. Active/empty scopes return false;
  // true means cleanup/cancellation was requested, not that workers have exited.
  // It removes only this object's subscriptions, including on repeated calls.
  [[nodiscard]] bool shutdown_scope(const std::shared_ptr<const authorization_scope>& scope);
  void shutdown_session(const std::string& session_id);
  [[nodiscard]] std::string start(const bridge_request& request);
  // Read-only exact params: {taskId} XOR {operationId}, one bounded ASCII
  // identifier (1-128 characters, letters/digits/-_.:), no other fields.
  // Operation lookup uses this request's actual active scope and retained task;
  // not-found never proves that work was not executed or authorizes a replay.
  [[nodiscard]] std::string query(const bridge_request& request) const;
  [[nodiscard]] std::string cancel(const bridge_request& request);
  [[nodiscard]] std::string subscribe(const bridge_request& request);
  [[nodiscard]] std::string unsubscribe(const bridge_request& request);

 private:
  friend class detail::gui_commands_state;
  [[nodiscard]] std::shared_ptr<const local_task_family> register_local_family_impl(
      capability_registry&, local_task_family_definition,
      std::shared_ptr<const detail::gui_family_hooks>);
  [[nodiscard]] bool register_gui_families(capability_registry&,
      std::vector<detail::gui_family_registration>);
  struct impl;
  std::shared_ptr<impl> impl_;
};

}  // namespace soui::runtime
