#pragma once
#include <soui/runtime/services/gui/gui_commands.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace soui::runtime::detail {
class gui_commands_state;
struct gui_document final {
  std::shared_ptr<const authorization_scope> page_scope;
  std::string session, origin, id, generation;
  std::atomic<bool> closed{false};
};
struct gui_capacity final { std::atomic<unsigned> used{0}; };

// Private SDK type: neither the public task callback nor a page can mint it.
class gui_job_context final {
 public:
  ~gui_job_context();
  // The only operation permitted from a task_service lock. No mutex/callout.
  void request_cancel() noexcept { cancelled.store(true, std::memory_order_release); changed.notify_all(); }
 private:
  friend class gui_commands_state;
  friend class soui::runtime::bridge_transport;
  gui_job_context() = default;
  bool authorizes_effect(const bridge_request& request) const;
  std::shared_ptr<gui_capacity> capacity;
  std::shared_ptr<const authorization_scope> agent_scope;
  std::shared_ptr<gui_document> document;
  std::shared_ptr<const gui_command_definition> row;
  std::string request_id, approved_input, effect_operation, effect_params, effect_capability, agent_session, agent_origin;
  bool effect_is_family_start{false};
  std::chrono::steady_clock::time_point deadline;
  std::atomic<bool> cancelled{false};
  std::atomic<bool> running{false};
  std::atomic<unsigned> consumption{0}; // 0 prepared, 1 consuming, 2 rejected before effect; never reset.
  bool effect_enqueued{false}; // Protected by the coordinator mutex; one private admission.
  std::mutex mutex;
  std::condition_variable changed;
  bool dispatched{false}, acknowledged{false}, ack_ok{false}, effect_done{false};
  std::string gui_result{"{}"}, effect_result, effect_state{"none"};
};

struct gui_family_hooks final {
  std::function<std::shared_ptr<gui_job_context>(const bridge_request&)> prepare;
  std::function<task_outcome(const std::shared_ptr<gui_job_context>&, const task_control&)> run;
};
struct gui_family_registration final {
  local_task_family_definition definition;
  std::shared_ptr<const gui_family_hooks> hooks;
};

struct gui_effect_request final {
  bridge_request request;
  std::shared_ptr<gui_job_context> context;
  std::chrono::steady_clock::time_point deadline;
  std::atomic<unsigned> phase{0}; // 0 queued, 1 owner started, 2 replied, 3 abandoned before start.
  std::uint64_t metrics_enqueued_us{0}; // Immutable after publication, zero while disabled.
  std::mutex mutex;
  std::condition_variable changed;
  std::string response;
};

class gui_commands_state final : public std::enable_shared_from_this<gui_commands_state> {
 public:
  static std::shared_ptr<gui_commands_state> create(capability_registry&, bridge_transport&,
      task_service&, std::vector<gui_command_definition>,
      soui::sdk::diagnostics::mode = soui::sdk::diagnostics::mode::disabled);
  [[nodiscard]] soui::sdk::diagnostics::status read_runtime_metrics(soui::sdk::diagnostics::gui&) const noexcept;
  bool bind(std::string generation, std::function<bool()> current);
  gui_owner_result poll();
  void close() noexcept;
  void shutdown_session(const std::string&) noexcept;
  void set_page(std::shared_ptr<const authorization_scope>, std::string session, std::string origin);
  void set_sink(std::shared_ptr<const task_service::event_sink>);
 private:
  gui_commands_state(capability_registry&, bridge_transport&, task_service&, soui::sdk::diagnostics::mode);
  std::shared_ptr<gui_job_context> prepare(const std::shared_ptr<const gui_command_definition>&,
      const bridge_request&);
  task_outcome run(const std::shared_ptr<gui_job_context>&, const task_control&);
  std::string page_bind(const bridge_request&);
  std::string page_effect(const bridge_request&);
  std::string effect(const bridge_request&, std::chrono::steady_clock::time_point request_deadline =
      std::chrono::steady_clock::time_point::max());
  std::string ack(const bridge_request&);
  std::shared_ptr<gui_job_context> find(const bridge_request&, const std::string&, const std::string&);
  capability_registry& registry;
  bridge_transport& transport;
  task_service& tasks;
  const std::thread::id owner{std::this_thread::get_id()};
  const soui::sdk::diagnostics::mode metrics_mode;
  const std::uint64_t metrics_epoch;
  std::uint64_t metrics_effect_high_water{0};
  std::shared_ptr<gui_capacity> capacity{std::make_shared<gui_capacity>()};
  std::atomic<bool> closed{false}, binding_started{false};
  mutable std::mutex mutex;
  std::shared_ptr<gui_document> document;
  std::shared_ptr<const std::function<bool()>> current;
  bool polling{false}, page_ready{false};
  unsigned effect_borrows{0};
  std::uint64_t sequence{0};
  std::shared_ptr<const task_service::event_sink> sink;
  std::vector<std::weak_ptr<gui_job_context>> pending;
  std::vector<std::shared_ptr<gui_effect_request>> effects;
};
} // namespace soui::runtime::detail
