#include <soui/runtime/core/task/task_store.h>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <utility>
#ifdef SOUI_TASK_FAMILY_TEST_ONLY
#include "../../services/task/task_family_test_hooks.h"
#endif

namespace soui::runtime {
namespace {

std::string make_task_id(std::string_view prefix, std::uint64_t number) {
  std::ostringstream stream;
  stream << prefix << std::setw(6) << std::setfill('0') << number;
  return stream.str();
}

}  // namespace

task_snapshot task_store::start(std::string operation_id) {
  return start_scoped(operation_id, operation_id);
}

task_snapshot task_store::start_scoped(std::string operation_id, std::string scope_key,
                                       bool* duplicate, bool* conflict, bool* capacity) {
  auto prepared = prepare_start_scoped(std::move(operation_id), std::move(scope_key));
  // The return value is fully allocated before changing identities or retention.
  auto snapshot = prepared->snapshot;
  if (duplicate) *duplicate = prepared->duplicate;
  if (conflict) *conflict = prepared->conflict;
  if (capacity) *capacity = prepared->capacity;
  commit_start_scoped(*prepared);
  return snapshot;
}

std::unique_ptr<task_store::prepared_start> task_store::prepare_start_scoped(
    std::string operation_id, std::string scope_key) {
  auto prepared = std::make_unique<prepared_start>(mutex_);
  const auto existing = operation_to_task_.find(operation_id);
  if (existing != operation_to_task_.end()) {
    const auto scope = operation_to_scope_.find(operation_id);
    if (scope != operation_to_scope_.end() && scope->second != scope_key) {
      prepared->conflict = true;
      auto& rejected = prepared->snapshot;
      rejected.operation_id = std::move(operation_id);
      rejected.state = task_state::failed;
      rejected.message = "operationId already exists for different request scope";
      rejected.result = operation_result{result_kind::failure, {}, rejected.message};
      return prepared;
    }
    prepared->duplicate = true;
    prepared->snapshot = tasks_.at(existing->second);
    return prepared;
  }
  std::string evicted_id;
  if (tasks_.size() >= max_tasks) {
    auto oldest = tasks_.end();
    for (auto iterator = tasks_.begin(); iterator != tasks_.end(); ++iterator) {
      if (!is_terminal(iterator->second.state)) continue;
      if (oldest == tasks_.end() || iterator->second.sequence < oldest->second.sequence) oldest = iterator;
    }
    if (oldest != tasks_.end()) {
      evicted_id = oldest->first;
    } else {
      prepared->capacity = true;
      auto& rejected = prepared->snapshot;
      rejected.operation_id = std::move(operation_id);
      rejected.state = task_state::failed;
      rejected.message = "The native task store capacity is exhausted.";
      rejected.result = operation_result{result_kind::failure, {}, rejected.message};
      return prepared;
    }
  }

  auto& snapshot = prepared->snapshot;
  snapshot.task_id = make_task_id(id_prefix_, next_task_number_);
  snapshot.operation_id = std::move(operation_id);
  snapshot.state = task_state::queued;
  snapshot.sequence = next_sequence_;
  snapshot.result = operation_result{result_kind::input_required, snapshot.task_id};
  #ifdef SOUI_TASK_FAMILY_TEST_ONLY
  detail::task_family_test::before_prepare(detail::task_family_test::prepare_point::store_tasks);
  #endif
  prepared->tasks = tasks_;
  #ifdef SOUI_TASK_FAMILY_TEST_ONLY
  detail::task_family_test::before_prepare(detail::task_family_test::prepare_point::store_operations);
  #endif
  prepared->operations = operation_to_task_;
  #ifdef SOUI_TASK_FAMILY_TEST_ONLY
  detail::task_family_test::before_prepare(detail::task_family_test::prepare_point::store_scopes);
  #endif
  prepared->scopes = operation_to_scope_;
  if (!evicted_id.empty()) {
    const auto& evicted = prepared->tasks.at(evicted_id);
    prepared->operations.erase(evicted.operation_id);
    prepared->scopes.erase(evicted.operation_id);
    prepared->tasks.erase(evicted_id);
  }
  prepared->tasks.emplace(snapshot.task_id, snapshot);
  prepared->operations.emplace(snapshot.operation_id, snapshot.task_id);
  prepared->scopes.emplace(snapshot.operation_id, std::move(scope_key));
  prepared->evicted_id = std::move(evicted_id);
  #ifdef SOUI_TASK_FAMILY_TEST_ONLY
  detail::task_family_test::before_prepare(detail::task_family_test::prepare_point::store_receipt);
  #endif
  prepared->publish = true;
  return prepared;
}

void task_store::commit_start_scoped(prepared_start& prepared) noexcept {
  if (!prepared.publish) return;
  static_assert(noexcept(tasks_.swap(prepared.tasks)), "Task commit must not throw");
  static_assert(noexcept(operation_to_task_.swap(prepared.operations)), "Operation commit must not throw");
  static_assert(noexcept(operation_to_scope_.swap(prepared.scopes)), "Scope commit must not throw");
  tasks_.swap(prepared.tasks);
  operation_to_task_.swap(prepared.operations);
  operation_to_scope_.swap(prepared.scopes);
  ++next_task_number_;
  ++next_sequence_;
  prepared.publish = false;
}

bool task_store::mark_running(const std::string& task_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = tasks_.find(task_id);
  if (found == tasks_.end() || is_terminal(found->second.state) ||
      found->second.state == task_state::cancel_requested) {
    return false;
  }
  found->second.state = task_state::running;
  found->second.result = operation_result{result_kind::timeout_observed, task_id};
  found->second.sequence = next_sequence_++;
  return true;
}

bool task_store::update_progress(const std::string& task_id, std::uint32_t progress,
                                 std::optional<std::string> message) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = tasks_.find(task_id);
  if (found == tasks_.end() || is_terminal(found->second.state) ||
      found->second.state == task_state::cancel_requested) {
    return false;
  }
  found->second.state = task_state::running;
  found->second.progress = std::min<std::uint32_t>(progress, 100U);
  if (message) found->second.message = std::move(*message);
  found->second.result = operation_result{result_kind::timeout_observed, task_id, found->second.message};
  found->second.sequence = next_sequence_++;
  return true;
}

bool task_store::observe_timeout(const std::string& task_id, std::string message) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = tasks_.find(task_id);
  if (found == tasks_.end() || is_terminal(found->second.state)) {
    return false;
  }
  found->second.state = task_state::timeout_observed;
  found->second.message = std::move(message);
  found->second.result = operation_result{result_kind::timeout_observed, task_id, found->second.message};
  found->second.sequence = next_sequence_++;
  return true;
}

bool task_store::request_cancel(const std::string& task_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = tasks_.find(task_id);
  if (found == tasks_.end() || is_terminal(found->second.state)) {
    return false;
  }
  found->second.state = task_state::cancel_requested;
  found->second.sequence = next_sequence_++;
  found->second.message = "cancel requested; awaiting native confirmation";
  found->second.result = operation_result{result_kind::timeout_observed, task_id, found->second.message};
  return true;
}

bool task_store::confirm_cancelled(const std::string& task_id, std::string message) {
  return complete(operation_result{result_kind::task_cancelled, task_id, std::move(message)});
}

bool task_store::complete(const std::string& task_id, result_kind kind, std::string message) {
  return complete(operation_result{kind, task_id, std::move(message)});
}

bool task_store::complete(operation_result result) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = tasks_.find(result.task_id);
  if (found == tasks_.end() || is_terminal(found->second.state)) {
    return false;
  }
  found->second.state = state_for(result.kind);
  if (found->second.state == task_state::succeeded || found->second.state == task_state::partial) {
    found->second.progress = 100;
  }
  found->second.message = result.message;
  found->second.result = std::move(result);
  found->second.sequence = next_sequence_++;
  return true;
}

std::optional<task_snapshot> task_store::query(const std::string& task_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = tasks_.find(task_id);
  if (found == tasks_.end()) {
    return std::nullopt;
  }
  return found->second;
}

bool task_store::erase(const std::string& task_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = tasks_.find(task_id);
  if (found == tasks_.end() || !is_terminal(found->second.state)) return false;
  operation_to_task_.erase(found->second.operation_id);
  operation_to_scope_.erase(found->second.operation_id);
  tasks_.erase(found);
  return true;
}

bool task_store::is_terminal(task_state state) noexcept {
  return state == task_state::succeeded || state == task_state::failed ||
         state == task_state::cancelled || state == task_state::partial;
}

task_state task_store::state_for(result_kind kind) noexcept {
  switch (kind) {
    case result_kind::success:
      return task_state::succeeded;
    case result_kind::failure:
      return task_state::failed;
    case result_kind::task_cancelled:
    case result_kind::user_cancelled:
      return task_state::cancelled;
    case result_kind::partial:
      return task_state::partial;
    case result_kind::timeout_observed:
    case result_kind::input_required:
      return task_state::running;
  }
  return task_state::failed;
}

}  // namespace soui::runtime
