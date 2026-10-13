#pragma once

#include <soui/diagnostics/runtime_metrics.h>
#include <soui/diagnostics/storage_observation.h>
#include <soui/runtime/core/task/detail/storage_allocator.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include <soui/runtime/core/result/result.h>

namespace soui::runtime {

enum class task_state {
  queued,
  running,
  timeout_observed,
  cancel_requested,
  succeeded,
  failed,
  cancelled,
  partial,
};

struct task_snapshot {
  std::string task_id;
  std::string operation_id;
  task_state state{task_state::queued};
  std::uint32_t progress{0};
  std::uint64_t sequence{0};
  std::string message;
  operation_result result;
};

class task_store {
 public:
  static constexpr std::size_t max_tasks{1024};

  explicit task_store(std::string id_prefix = "task-") : task_store(std::move(id_prefix), soui::sdk::diagnostics::mode::disabled) {}
  task_store(std::string id_prefix, soui::sdk::diagnostics::mode metrics);
  task_store(const task_store&) = delete;
  task_store& operator=(const task_store&) = delete;

  [[nodiscard]] soui::sdk::diagnostics::status read_storage_metrics(
      soui::sdk::diagnostics::task_store_storage&) const noexcept;

  [[nodiscard]] task_snapshot start(std::string operation_id);
  [[nodiscard]] task_snapshot start_scoped(std::string operation_id, std::string scope_key,
                                           bool* duplicate = nullptr, bool* conflict = nullptr,
                                           bool* capacity = nullptr);
  [[nodiscard]] bool mark_running(const std::string& task_id);
  [[nodiscard]] bool update_progress(const std::string& task_id, std::uint32_t progress,
                                     std::optional<std::string> message = std::nullopt);
  [[nodiscard]] bool observe_timeout(const std::string& task_id, std::string message = {});
  [[nodiscard]] bool request_cancel(const std::string& task_id);
  [[nodiscard]] bool confirm_cancelled(const std::string& task_id, std::string message = {});
  [[nodiscard]] bool complete(const std::string& task_id, result_kind kind, std::string message = {});
  [[nodiscard]] bool complete(operation_result result);
  [[nodiscard]] std::optional<task_snapshot> query(const std::string& task_id) const;
  [[nodiscard]] bool erase(const std::string& task_id);

 private:
  friend class task_service;
  template <class Value>
  using storage_map = std::unordered_map<std::string, Value, std::hash<std::string>,
      std::equal_to<std::string>, detail::task_store_allocator<std::pair<const std::string, Value>>>;
  struct prepared_start {
    prepared_start(std::mutex& mutex, detail::task_store_allocation_ledger& ledger)
        : lock{mutex}, tasks{storage_map<task_snapshot>::allocator_type{&ledger}},
          operations{storage_map<std::string>::allocator_type{&ledger}},
          scopes{storage_map<std::string>::allocator_type{&ledger}} {}
    std::unique_lock<std::mutex> lock;
    storage_map<task_snapshot> tasks;
    storage_map<std::string> operations, scopes;
    task_snapshot snapshot;
    std::string evicted_id;
    bool duplicate{false}, conflict{false}, capacity{false}, publish{false};
  };
  [[nodiscard]] std::unique_ptr<prepared_start> prepare_start_scoped(
      std::string operation_id, std::string scope_key);
  void commit_start_scoped(prepared_start& prepared) noexcept;
  [[nodiscard]] static bool is_terminal(task_state state) noexcept;
  [[nodiscard]] static task_state state_for(result_kind kind) noexcept;

  [[nodiscard]] bool read_metrics_locked(soui::sdk::diagnostics::tasks&) const;
  const soui::sdk::diagnostics::mode metrics_mode_;
  const std::uint64_t metrics_epoch_;
  std::uint64_t metrics_evictions_{0};
  bool metrics_saturated_{false};
  // Declared before maps: the ledger outlives every actual map deallocation.
  detail::task_store_allocation_ledger allocation_ledger_;
  mutable std::mutex mutex_;
  std::string id_prefix_;
  std::uint64_t next_task_number_{1};
  std::uint64_t next_sequence_{1};
  storage_map<task_snapshot> tasks_;
  storage_map<std::string> operation_to_task_;
  storage_map<std::string> operation_to_scope_;
};

}  // namespace soui::runtime
