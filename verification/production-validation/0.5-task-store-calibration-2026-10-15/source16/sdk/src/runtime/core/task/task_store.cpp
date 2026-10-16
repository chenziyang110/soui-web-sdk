#include <soui/runtime/core/task/task_store.h>

#include "../../diagnostics/runtime_metrics_internal.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <utility>
#ifdef SOUI_TASK_FAMILY_TEST_ONLY
#include "../../services/task/task_family_test_hooks.h"
#endif

namespace dm = soui::sdk::diagnostics;
namespace soui::runtime {
namespace {

std::string make_task_id(std::string_view prefix, std::uint64_t number) {
  std::ostringstream stream;
  stream << prefix << std::setw(6) << std::setfill('0') << number;
  return stream.str();
}

}  // namespace

task_store::task_store(std::string id_prefix, dm::mode metrics)
    : task_store(std::move(id_prefix), metrics, std::nullopt) {}

task_store::task_store(std::string id_prefix, dm::mode metrics,
    std::optional<task_store_request_quota_policy> quota)
    : metrics_mode_(metrics), metrics_epoch_(metrics == dm::mode::scalar ? dm::detail::epoch() : 0),
      allocation_owner_(metrics == dm::mode::scalar, quota), id_prefix_(id_prefix, allocation_owner_),
      tasks_(storage_map<retained_snapshot>::allocator_type{&allocation_owner_}),
      operation_to_task_(storage_map<retained_string>::allocator_type{&allocation_owner_}),
      operation_to_scope_(storage_map<retained_string>::allocator_type{&allocation_owner_}) {}

task_store_request_quota_status task_store::read_request_quota(
    task_store_request_quota_snapshot& out) const noexcept {
  out = {};
  try {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) { out.status = task_store_request_quota_status::busy; return out.status; }
    out = allocation_owner_.quota.snapshot();
    return out.status;
  } catch (...) { out = {}; return out.status; }
}

dm::status task_store::read_storage_metrics(dm::task_store_storage& out) const noexcept {
  out = {};
  if (metrics_mode_ != dm::mode::scalar) return dm::status::disabled;
  try {
    out.observation = dm::detail::begin(metrics_mode_, metrics_epoch_);
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
      out.observation.state = dm::status::busy;
      dm::detail::end(out.observation); return out.observation.state;
    }
    read_storage_locked(out);
    dm::detail::end(out.observation); return out.observation.state;
  } catch (...) { out = {}; out.observation.state = dm::status::unavailable; return out.observation.state; }
}

dm::status task_store::read_retained_string_storage(dm::task_store_storage& metadata,
    dm::allocation_requests& strings, dm::allocation_requests& aggregate) const noexcept {
  metadata = {}; strings = {}; aggregate = {};
  if (metrics_mode_ != dm::mode::scalar) return dm::status::disabled;
  try {
    metadata.observation = dm::detail::begin(metrics_mode_, metrics_epoch_);
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
      metadata.observation.state = dm::status::busy;
      dm::detail::end(metadata.observation); return metadata.observation.state;
    }
    read_storage_locked(metadata);
    strings = allocation_owner_.strings.snapshot();
    aggregate = allocation_owner_.aggregate.snapshot();
    metadata.observation.counter_saturated |= allocation_owner_.strings.saturated || allocation_owner_.aggregate.saturated;
    if (allocation_owner_.strings.invalid || allocation_owner_.aggregate.invalid) metadata.observation.state = dm::status::unavailable;
    dm::detail::end(metadata.observation); return metadata.observation.state;
  } catch (...) {
    metadata = {}; strings = {}; aggregate = {};
    metadata.observation.state = dm::status::unavailable; return metadata.observation.state;
  }
}

dm::status task_store::read_private_storage_metrics(dm::task_store_private_storage& out) const noexcept {
  out = {};
  dm::task_store_storage metadata;
  // All three request domains/entries come from this one Store-lock envelope.
  const auto state = read_retained_string_storage(metadata, out.private_string_requests, out.aggregate_requests);
  out.observation = metadata.observation;
  out.map_requests = metadata.map_requests;
  out.task_entries = metadata.task_entries; out.operation_entries = metadata.operation_entries;
  out.scope_entries = metadata.scope_entries;
  if (metadata.coverage_mask) {
    out.coverage_mask = dm::task_store_private_storage::map_allocator_requests |
        dm::task_store_private_storage::private_string_allocator_requests |
        dm::task_store_private_storage::aggregate_allocator_requests;
    out.gap_mask = dm::task_store_private_storage::public_temporary_copies_gap |
        dm::task_store_private_storage::phase_partition_gap |
        dm::task_store_private_storage::other_sdk_owners_gap |
        dm::task_store_private_storage::allocator_heap_external_gap |
        dm::task_store_private_storage::final_disposal_receipt_gap;
  }
  return state;
}

void task_store::read_storage_locked(dm::task_store_storage& out) const noexcept {
  out.coverage_mask = dm::task_store_storage::map_allocator_requests |
      dm::task_store_storage::committed_string_diagnostics;
  out.gap_mask = dm::task_store_storage::string_allocator_gap |
      dm::task_store_storage::phase_partition_gap |
      dm::task_store_storage::prepared_retiring_strings_gap |
      dm::task_store_storage::other_sdk_owners_gap |
      dm::task_store_storage::allocator_heap_external_gap |
      dm::task_store_storage::final_disposal_receipt_gap;
  out.map_requests = allocation_owner_.maps.snapshot();
  out.observation.counter_saturated = allocation_owner_.maps.saturated;
  if (allocation_owner_.maps.invalid) out.observation.state = dm::status::unavailable;
  out.task_entries = tasks_.size(); out.operation_entries = operation_to_task_.size();
  out.scope_entries = operation_to_scope_.size();
  const auto strings = [&](const retained_string& value, dm::storage_strings& totals) {
    dm::detail::increment(totals.fields, out.observation.counter_saturated);
    dm::detail::add(totals.size_bytes, value.size(), out.observation.counter_saturated);
    dm::detail::add(totals.capacity_bytes, value.capacity(), out.observation.counter_saturated);
  };
  for (const auto& entry : tasks_) {
    strings(entry.first, out.task_keys);
    const auto& value = entry.second;
    for (const auto* field : {&value.task_id, &value.operation_id, &value.message,
                              &value.result.task_id, &value.result.message})
      strings(*field, out.snapshot_strings);
  }
  for (const auto& entry : operation_to_task_) {
    strings(entry.first, out.operation_strings); strings(entry.second, out.operation_strings);
  }
  for (const auto& entry : operation_to_scope_) {
    strings(entry.first, out.scope_strings); strings(entry.second, out.scope_strings);
  }
  strings(id_prefix_, out.id_prefix);
}

bool task_store::read_metrics_locked(dm::tasks& out) const {
  std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) return false;
  if (metrics_mode_ != dm::mode::scalar || !metrics_epoch_) {
    out.observation.state = dm::status::unavailable; return true;
  }
  out.store_limit = max_tasks; out.store_entries = tasks_.size();
  out.committed_terminal_evictions_total = metrics_evictions_;
  out.observation.counter_saturated |= metrics_saturated_;
  for (const auto& entry : tasks_) {
    const auto& value = entry.second;
    if (is_terminal(value.state)) ++out.terminal_entries;
    if (value.state == task_state::queued) ++out.queued_entries;
    if (value.state == task_state::running) ++out.running_entries;
    if (value.state == task_state::timeout_observed) ++out.timeout_observed_entries;
    if (value.state == task_state::cancel_requested) ++out.cancel_requested_entries;
    for (const auto* field : {&value.task_id, &value.operation_id, &value.message,
                              &value.result.task_id, &value.result.message}) {
      dm::detail::add(out.store_string_size_bytes, field->size(), out.observation.counter_saturated);
      dm::detail::add(out.store_string_capacity_bytes, field->capacity(), out.observation.counter_saturated);
    }
  }
  return true;
}

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
    std::string operation_id, std::string scope_key, bool fund_terminal_failure) {
  auto prepared = std::make_unique<prepared_start>(mutex_, allocation_owner_);
  const auto existing = operation_to_task_.find(retained_string::lookup(operation_id));
  if (existing != operation_to_task_.end()) {
    const auto scope = operation_to_scope_.find(retained_string::lookup(operation_id));
    if (scope != operation_to_scope_.end() && scope->second.view() != std::string_view{scope_key}) {
      prepared->conflict = true;
      auto& rejected = prepared->snapshot;
      rejected.operation_id = std::move(operation_id);
      rejected.state = task_state::failed;
      rejected.message = "operationId already exists for different request scope";
      rejected.result = operation_result{result_kind::failure, {}, rejected.message};
      return prepared;
    }
    prepared->duplicate = true;
    prepared->snapshot = tasks_.at(existing->second).public_copy();
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
      evicted_id = oldest->first.public_copy();
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
  snapshot.task_id = make_task_id(id_prefix_.view(), next_task_number_);
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
    const auto& evicted = prepared->tasks.at(retained_string::lookup(evicted_id));
    prepared->operations.erase(evicted.operation_id);
    prepared->scopes.erase(evicted.operation_id);
    prepared->tasks.erase(retained_string::lookup(evicted_id));
  }
  // Every stored key/value owns its bytes; borrowed lookup views never enter a map.
  prepared->tasks.emplace(retained_string{snapshot.task_id, allocation_owner_},
                         retained_snapshot{snapshot, allocation_owner_, fund_terminal_failure});
  prepared->operations.emplace(retained_string{snapshot.operation_id, allocation_owner_}, retained_string{snapshot.task_id, allocation_owner_});
  prepared->scopes.emplace(retained_string{snapshot.operation_id, allocation_owner_}, retained_string{scope_key, allocation_owner_});
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
  assert(tasks_.get_allocator() == prepared.tasks.get_allocator());
  assert(operation_to_task_.get_allocator() == prepared.operations.get_allocator());
  assert(operation_to_scope_.get_allocator() == prepared.scopes.get_allocator());
  tasks_.swap(prepared.tasks);
  operation_to_task_.swap(prepared.operations);
  operation_to_scope_.swap(prepared.scopes);
  if (metrics_mode_ == dm::mode::scalar && !prepared.evicted_id.empty())
    dm::detail::increment(metrics_evictions_, metrics_saturated_);
  ++next_task_number_;
  ++next_sequence_;
  prepared.publish = false;
}

bool task_store::mark_running(const std::string& task_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = tasks_.find(retained_string::lookup(task_id));
  if (found == tasks_.end() || is_terminal(found->second.state) ||
      found->second.state == task_state::cancel_requested) {
    return false;
  }
  retained_result candidate(allocation_owner_, result_kind::timeout_observed, task_id);
  found->second.result.swap(candidate);
  found->second.state = task_state::running;
  found->second.sequence = next_sequence_++;
  return true;
}

bool task_store::update_progress(const std::string& task_id, std::uint32_t progress,
                                 std::optional<std::string> message) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = tasks_.find(retained_string::lookup(task_id));
  if (found == tasks_.end() || is_terminal(found->second.state) ||
      found->second.state == task_state::cancel_requested) {
    return false;
  }
  const auto text = message ? std::string_view{*message} : found->second.message.view();
  std::optional<retained_string> candidate_message;
  if (message) candidate_message.emplace(text, allocation_owner_);
  retained_result candidate_result(allocation_owner_, result_kind::timeout_observed, task_id, text);
  if (candidate_message) found->second.message.swap(*candidate_message);
  found->second.result.swap(candidate_result);
  found->second.state = task_state::running;
  found->second.progress = std::min<std::uint32_t>(progress, 100U);
  found->second.sequence = next_sequence_++;
  return true;
}

bool task_store::observe_timeout(const std::string& task_id, std::string message) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = tasks_.find(retained_string::lookup(task_id));
  if (found == tasks_.end() || is_terminal(found->second.state)) {
    return false;
  }
  retained_string candidate_message(message, allocation_owner_);
  retained_result candidate_result(allocation_owner_, result_kind::timeout_observed, task_id, message);
  found->second.message.swap(candidate_message);
  found->second.result.swap(candidate_result);
  found->second.state = task_state::timeout_observed;
  found->second.sequence = next_sequence_++;
  return true;
}

bool task_store::request_cancel(const std::string& task_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = tasks_.find(retained_string::lookup(task_id));
  if (found == tasks_.end() || is_terminal(found->second.state)) {
    return false;
  }
  const std::string_view message = "cancel requested; awaiting native confirmation";
  retained_string candidate_message(message, allocation_owner_);
  retained_result candidate_result(allocation_owner_, result_kind::timeout_observed, task_id, message);
  found->second.message.swap(candidate_message);
  found->second.result.swap(candidate_result);
  found->second.state = task_state::cancel_requested;
  found->second.sequence = next_sequence_++;
  return true;
}

bool task_store::confirm_cancelled(const std::string& task_id, std::string message) {
  return complete(operation_result{result_kind::task_cancelled, task_id, std::move(message)});
}

bool task_store::complete(const std::string& task_id, result_kind kind, std::string message) {
  return complete(operation_result{kind, task_id, std::move(message)});
}

bool task_store::complete(operation_result result) {
  prepared_completion prepared(mutex_);
  prepare_complete(prepared, result.task_id, result.kind, result.message);
  if (!prepared.publish) return false;
  commit_complete(prepared);
  return true;
}

void task_store::prepare_complete(prepared_completion& prepared, const std::string& task_id,
                                 result_kind kind, std::string_view message) {
  assert(prepared.lock.owns_lock() && prepared.lock.mutex() == &mutex_ && !prepared.publish);
  const auto found = tasks_.find(retained_string::lookup(task_id));
  if (found == tasks_.end() || is_terminal(found->second.state)) return;
  prepared.message.emplace(message, allocation_owner_);
  prepared.result.emplace(allocation_owner_, kind, task_id, message);
  prepared.target = &found->second;
  prepared.state = state_for(kind);
  prepared.publish = true;
}

void task_store::commit_complete(prepared_completion& prepared) noexcept {
  if (!prepared.publish) return;
  assert(prepared.lock.owns_lock() && prepared.lock.mutex() == &mutex_ && prepared.target);
  auto& target = *prepared.target;
  target.message.swap(*prepared.message);
  target.result.swap(*prepared.result);
  target.state = prepared.state;
  if (target.state == task_state::succeeded || target.state == task_state::partial) target.progress = 100;
  target.sequence = next_sequence_++;
  // Credit is returned by the actual buffer deallocations, never by phase.
  if (is_terminal(target.state)) target.failure_reserve.reset();
  prepared.publish = false;
}

bool task_store::commit_funded_failure(const std::string& task_id) noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = tasks_.find(retained_string::lookup(task_id));
  if (found == tasks_.end() || is_terminal(found->second.state) || !found->second.failure_reserve) return false;
  auto& target = found->second;
  auto& funded = *target.failure_reserve;
  target.message.swap(funded.message);
  target.result.swap(funded.result);
  target.state = task_state::failed;
  target.sequence = next_sequence_++;
  // Swapped-out old buffers remain charged until this physical destruction.
  target.failure_reserve.reset();
  return true;
}

std::optional<task_snapshot> task_store::query(const std::string& task_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = tasks_.find(retained_string::lookup(task_id));
  if (found == tasks_.end()) {
    return std::nullopt;
  }
  return found->second.public_copy();
}

bool task_store::erase(const std::string& task_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = tasks_.find(retained_string::lookup(task_id));
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
