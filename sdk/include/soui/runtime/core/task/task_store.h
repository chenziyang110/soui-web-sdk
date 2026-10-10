#pragma once

#include <soui/diagnostics/runtime_metrics.h>
#include <cstddef>
#include <cstdint>
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
  struct prepared_start {
    explicit prepared_start(std::mutex& mutex) : lock{mutex} {}
    std::unique_lock<std::mutex> lock;
    std::unordered_map<std::string, task_snapshot> tasks;
    std::unordered_map<std::string, std::string> operations, scopes;
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
  mutable std::mutex mutex_;
  std::string id_prefix_;
  std::uint64_t next_task_number_{1};
  std::uint64_t next_sequence_{1};
  std::unordered_map<std::string, task_snapshot> tasks_;
  std::unordered_map<std::string, std::string> operation_to_task_;
  std::unordered_map<std::string, std::string> operation_to_scope_;
};

}  // namespace soui::runtime
