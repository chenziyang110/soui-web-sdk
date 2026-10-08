#include <soui/runtime/services/filesystem-readonly/directory_scanner.h>

#include <cctype>
#include <algorithm>
#include <charconv>
#include <iomanip>
#include <random>
#include <sstream>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "../../../../../third_party/include/json/json.hpp"

namespace soui::runtime {
namespace {

using json = nlohmann::json;

std::string make_root_reference(std::uint64_t number) {
  std::random_device random;
  std::ostringstream stream;
  stream << "root-" << std::hex << random() << random() << '-' << std::setw(8)
         << std::setfill('0') << number;
  return stream.str();
}

std::string normalize_relative(const std::filesystem::path& value) {
  // generic_string() follows the process locale on Windows (often CP936),
  // which is not valid UTF-8 for JSON transport. Keep every path crossing the
  // Bridge in the portable UTF-8 representation instead.
  return value.generic_u8string();
}

char ascii_lower(char value) {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

bool wildcard_match(std::string_view pattern, std::string_view value) {
  std::size_t pattern_index = 0;
  std::size_t value_index = 0;
  std::size_t star_index = std::string_view::npos;
  std::size_t match_index = 0;

  while (value_index < value.size()) {
    if (pattern_index < pattern.size() &&
        (pattern[pattern_index] == '?' ||
         ascii_lower(pattern[pattern_index]) == ascii_lower(value[value_index]))) {
      ++pattern_index;
      ++value_index;
      continue;
    }
    if (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
      star_index = ++pattern_index;
      match_index = value_index;
      continue;
    }
    if (star_index != std::string_view::npos) {
      pattern_index = star_index;
      value_index = ++match_index;
      continue;
    }
    return false;
  }

  while (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
    ++pattern_index;
  }
  return pattern_index == pattern.size();
}

bool matches_filter(const std::string& filter, const std::filesystem::path& relative) {
  if (filter.empty()) return true;
  const auto generic = relative.generic_string();
  const auto filename = relative.filename().generic_string();
  return wildcard_match(filter, generic) || wildcard_match(filter, filename);
}

std::string extension_key(const std::filesystem::path& relative) {
  const auto extension = relative.filename().extension().generic_string();
  if (extension.empty() || extension == ".") return "[none]";
  std::string normalized;
  normalized.reserve(extension.size());
  for (const auto character : extension) normalized.push_back(ascii_lower(character));
  return normalized;
}

bool is_link_or_reparse_point(const std::filesystem::path& path, std::error_code& error) {
  const auto status = std::filesystem::symlink_status(path, error);
  if (error) return false;
  if (std::filesystem::is_symlink(status)) return true;
#ifdef _WIN32
  const auto attributes = ::GetFileAttributesW(path.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
    return true;
  }
#endif
  return false;
}

}  // namespace

directory_scanner::directory_scanner(const permission_store& permissions, task_store& tasks)
    : permissions_(permissions), tasks_(tasks) {}

directory_scanner::~directory_scanner() {
  std::vector<std::unique_ptr<scan_worker>> workers;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : workers_) {
      entry.second->cancel_requested = true;
      workers.push_back(std::move(entry.second));
    }
    workers_.clear();
  }
  for (auto& worker : workers) {
    if (worker && worker->thread.joinable()) worker->thread.join();
  }
}

void directory_scanner::set_event_sink(event_sink sink) {
  std::lock_guard<std::mutex> lock(mutex_);
  sink_ = std::move(sink);
}

void directory_scanner::shutdown_session(const std::string& session_id) {
  std::vector<std::unique_ptr<scan_worker>> workers;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto iterator = roots_.begin(); iterator != roots_.end();) {
      if (iterator->second.session_id == session_id) iterator = roots_.erase(iterator);
      else ++iterator;
    }
    for (auto iterator = scans_.begin(); iterator != scans_.end();) {
      if (iterator->second.context.session_id == session_id) {
        const auto worker = workers_.find(iterator->first);
        if (worker != workers_.end()) {
          worker->second->cancel_requested = true;
          workers.push_back(std::move(worker->second));
          workers_.erase(worker);
        }
        iterator = scans_.erase(iterator);
      } else {
        ++iterator;
      }
    }
  }
  for (auto& worker : workers) {
    if (worker && worker->thread.joinable()) worker->thread.join();
  }
}

std::shared_ptr<const authorization_scope> directory_scanner::resolve_authorization(
    const scan_request_context& context, const std::string& permission) const {
  const auto checked = permissions_.resolve(context.session_id, context.origin, context.token, permission);
  return checked.result == authorization_result::authorized ? checked.scope : nullptr;
}

std::shared_ptr<const authorization_scope> directory_scanner::resolve_scan_authorization(
    const scan_request_context& context) const {
  auto authorization = resolve_authorization(context, "filesystem.scan");
  if (!authorization) authorization = resolve_authorization(context, "demo.scan");
  return authorization;
}

std::string directory_scanner::register_root(const scan_request_context& context,
                                             const std::filesystem::path& root) {
  const auto authorization = resolve_authorization(context, "desktop.dialog.pickDirectory");
  if (!authorization) return {};
  std::error_code error;
  const auto canonical = std::filesystem::weakly_canonical(root, error);
  if (error || !std::filesystem::is_directory(canonical, error) || error) return {};
  std::lock_guard<std::mutex> lock(mutex_);
  const auto reference = make_root_reference(next_root_++);
  // Shutdown first revokes the grant, then takes this resource lock. Anything
  // admitted before revocation is covered by that cleanup; later work is denied.
  if (!authorization->active()) return {};
  roots_.emplace(reference, root_binding{context.session_id, context.origin, authorization, canonical});
  return reference;
}

scan_response directory_scanner::start(const scan_request_context& context, std::string operation_id,
                                       const std::string& root_reference, const std::string& filter,
                                       std::string operation_scope) {
  const auto authorization = resolve_scan_authorization(context);
  if (!authorization) {
    return error("E_PERMISSION_DENIED", "filesystem.scan is required.");
  }
  if (operation_id.empty()) return error("E_INVALID_PARAMS", "operationId is required.");
  join_finished_workers();
  if (operation_scope.empty()) operation_scope = "demo.scan.start";
  const std::string scope_key = context.session_id + "|" + context.origin + "|" + operation_scope + "|" + root_reference + "|" + filter;
  bool duplicate = false;
  bool conflict = false;
  bool capacity = false;
  task_snapshot task;
  // Retained resources contain stable identity, never a bearer credential.
  const scan_request_context owner{context.session_id, context.origin, {}, authorization};
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!authorization->active()) return error("E_PERMISSION_DENIED", "The scan authorization is no longer active.");
    const auto found = roots_.find(root_reference);
    if (found == roots_.end() || found->second.session_id != context.session_id ||
        found->second.origin != context.origin || found->second.authorization != authorization) {
      return error("E_ROOT_NOT_FOUND", "The root reference is not valid for this session.");
    }
    const auto binding = found->second;
    // Handle existing operations before enforcing admission bounds. A retry of
    // an existing task never creates another worker or evicts its own result.
    for (const auto& item : scans_) {
      const auto existing = tasks_.query(item.first);
      if (!existing || existing->operation_id != operation_id) continue;
      task = tasks_.start_scoped(operation_id, scope_key, &duplicate, &conflict, &capacity);
      if (conflict || item.second.context.authorization != authorization) {
        return error("E_OPERATION_CONFLICT", "operationId already belongs to a different request.");
      }
      return {result_kind::success, {}, "duplicate operationId returned existing task", task.task_id, {}, {}, false};
    }
    const auto active_workers = std::count_if(workers_.begin(), workers_.end(), [&](const auto& item) {
      const auto scan = scans_.find(item.first);
      return scan != scans_.end() && scan->second.context.session_id == context.session_id;
    });
    if (active_workers >= max_workers_per_session) {
      return error("E_BUSY", "The session already owns the maximum number of scan workers.");
    }
    std::size_t retained = std::count_if(scans_.begin(), scans_.end(), [&](const auto& item) {
      return item.second.context.session_id == context.session_id;
    });
    while (retained >= max_retained_tasks_per_session) {
      const auto removable = std::find_if(scans_.begin(), scans_.end(), [&](const auto& item) {
        return item.second.context.session_id == context.session_id && item.second.complete &&
               workers_.find(item.first) == workers_.end();
      });
      if (removable == scans_.end()) {
        return error("E_BUSY", "The session result history limit was reached.");
      }
      (void)tasks_.erase(removable->first);
      scans_.erase(removable);
      --retained;
    }
    if (!authorization->active()) return error("E_PERMISSION_DENIED", "The scan authorization is no longer active.");
    task = tasks_.start_scoped(std::move(operation_id), scope_key, &duplicate, &conflict, &capacity);
    if (conflict) return error("E_OPERATION_CONFLICT", "operationId already belongs to a different request.", task.task_id);
    if (capacity) return error("E_BUSY", "The native task store capacity is exhausted.");
    // An operation in the shared store without scanner ownership is never ours
    // to adopt or erase, even if a native caller used the same scope string.
    if (duplicate) return error("E_OPERATION_CONFLICT", "operationId has no matching scan owner.");
    try {
      scans_.emplace(task.task_id, scan_state{owner, {}, filter, result_kind::input_required, {}, {}, false, false});
      auto worker = std::make_unique<scan_worker>();
      auto* control = worker.get();
      // Insert before starting the thread: map allocation failure must not
      // destroy a joinable thread while holding the scanner lock.
      workers_.emplace(task.task_id, std::move(worker));
      if (!authorization->active()) {
        workers_.erase(task.task_id);
        scans_.erase(task.task_id);
        (void)tasks_.complete(task.task_id, result_kind::failure, "Scan authorization was revoked before worker admission.");
        (void)tasks_.erase(task.task_id);
        return error("E_PERMISSION_DENIED", "The scan authorization is no longer active.");
      }
      control->thread = std::thread([this, task_id = task.task_id, binding, owner, filter, control]() {
        run_scan(task_id, binding, owner, filter, control);
        control->cancel_requested = true;
      });
    } catch (...) {
      workers_.erase(task.task_id);
      scans_.erase(task.task_id);
      (void)tasks_.complete(task.task_id, result_kind::failure, "The scan worker could not be created.");
      (void)tasks_.erase(task.task_id);
      return error("E_SCAN_START_FAILED", "The scan worker could not be created.");
    }
  }
  emit_event(context.session_id, task.task_id, "accepted", 0);
  return {result_kind::success, {}, duplicate ? std::string{"duplicate operationId returned existing task"} : std::string{}, task.task_id, {}, "0", false};
}

void directory_scanner::run_scan(std::string task_id, root_binding binding, scan_request_context context,
                                 std::string filter, scan_worker* control) {
  (void)tasks_.mark_running(task_id);
  // A recursive directory scan has no total-entry count up front. Keep the
  // running signal separate from percentage progress instead of presenting a
  // fabricated 1% value to the WebView.
  emit_event(context.session_id, task_id, "running", 0);

  bool traversal_partial{false};
  std::string traversal_message;
  std::size_t skipped_count{0};
  std::size_t error_count{0};
  std::size_t processed_count{0};
  bool summary_limit_reached{false};
  std::vector<std::string> issues;
  const auto note_partial = [&](const std::error_code& error) {
    if (!error) return;
    traversal_partial = true;
    if (traversal_message.empty()) traversal_message = error.message();
    ++skipped_count;
    ++error_count;
    if (issues.size() < 20U) issues.push_back(error.message());
  };
  try {
    std::error_code iterator_error;
    std::filesystem::recursive_directory_iterator iterator(
        binding.root, std::filesystem::directory_options::skip_permission_denied, iterator_error);
    const std::filesystem::recursive_directory_iterator end;
    if (iterator_error) note_partial(iterator_error);
    while (iterator != end) {
      bool cancel_requested = false;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (control != nullptr && control->cancel_requested) {
          cancel_requested = true;
          (void)tasks_.confirm_cancelled(task_id, "Native scan cancellation confirmed.");
          if (auto scan = scans_.find(task_id); scan != scans_.end()) {
            scan->second.kind = result_kind::task_cancelled;
            scan->second.code = "E_TASK_CANCELLED";
            scan->second.message = "Native scan cancellation confirmed.";
            scan->second.complete = true;
          }
        }
      }
      if (cancel_requested) {
        emit_event(context.session_id, task_id, "cancelled", 0, "Native scan cancellation confirmed.");
        return;
      }
      const auto current = iterator->path();
      std::error_code entry_error;
      if (is_link_or_reparse_point(current, entry_error)) {
        ++skipped_count;
        if (issues.size() < 20U) {
          std::error_code relative_error;
          const auto relative = std::filesystem::relative(current, binding.root, relative_error);
          issues.push_back("Skipped symbolic link or reparse point: " +
                           (relative_error ? std::string{"<unresolved>"} : normalize_relative(relative)));
        }
        entry_error.clear();
        iterator.increment(entry_error);
        note_partial(entry_error);
        ++processed_count;
        if ((processed_count % 128U) == 0U) {
          emit_event(context.session_id, task_id, "running", 0,
                     "Scanned " + std::to_string(processed_count) + " filesystem entries.");
        }
        continue;
      }
      const auto canonical = std::filesystem::weakly_canonical(current, entry_error);
      if (!entry_error && contains(binding.root, canonical)) {
        const auto relative = std::filesystem::relative(canonical, binding.root, entry_error);
        if (!entry_error && !relative.empty() && relative != ".") {
          const bool is_directory = iterator->is_directory(entry_error);
          const auto size = is_directory || entry_error ? 0 : iterator->file_size(entry_error);
          if (!entry_error && matches_filter(filter, relative)) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto found_scan = scans_.find(task_id);
            if (found_scan == scans_.end()) return;
            auto& scan = found_scan->second;
            if (scan.entries.size() >= max_cached_entries) {
              traversal_partial = true;
              traversal_message = "The bounded result cache limit was reached.";
              ++skipped_count;
              ++error_count;
              if (issues.size() < 20U) issues.push_back(traversal_message);
              break;
            }
            scan.entries.push_back({normalize_relative(relative), is_directory, size});
            if (!is_directory) {
              const auto key = extension_key(relative);
              ++scan.file_count;
              scan.total_bytes += size;
              auto group = std::find_if(scan.groups.begin(), scan.groups.end(),
                                        [&](const scan_group_summary& item) { return item.extension == key; });
              if (group == scan.groups.end()) {
                constexpr std::size_t max_summary_groups = 256U;
                if (scan.groups.size() >= max_summary_groups) {
                  if (!summary_limit_reached) {
                    summary_limit_reached = true;
                    traversal_partial = true;
                    traversal_message = "The bounded extension summary limit was reached.";
                    ++skipped_count;
                    ++error_count;
                    if (issues.size() < 20U) issues.push_back(traversal_message);
                  }
                } else {
                  scan.groups.push_back({key, 1U, size});
                }
              } else {
                ++group->file_count;
                group->total_bytes += size;
              }
            }
          }
        }
      }
      note_partial(entry_error);
      ++processed_count;
      if ((processed_count % 128U) == 0U) {
        emit_event(context.session_id, task_id, "running", 0,
                   "Scanned " + std::to_string(processed_count) + " filesystem entries.");
      }
#ifdef SOUI_PROGRESS_STRESS
      // Diagnostic load only: production scanning retains its existing event policy.
      emit_event(context.session_id, task_id, "running", 0);
#endif
      entry_error.clear();
      iterator.increment(entry_error);
      note_partial(entry_error);
    }
  } catch (const std::filesystem::filesystem_error& exception) {
    (void)exception;
    const std::string safe_message = "Directory scan completed with inaccessible entries.";
    (void)tasks_.complete(task_id, result_kind::partial, safe_message);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto found_scan = scans_.find(task_id);
      if (found_scan == scans_.end()) return;
      auto& scan = found_scan->second;
      if (scan.kind == result_kind::task_cancelled) return;
      scan.kind = result_kind::partial;
      scan.code = "E_PARTIAL_SCAN";
      scan.message = safe_message;
      scan.complete = true;
      scan.skipped_count = std::max<std::size_t>(skipped_count, 1U);
      scan.error_count = std::max<std::size_t>(error_count, 1U);
      scan.issues = issues.empty() ? std::vector<std::string>{safe_message} : issues;
    }
    emit_event(context.session_id, task_id, "partial", 100, safe_message);
    return;
  }
  if (control != nullptr && control->cancel_requested) {
    (void)tasks_.confirm_cancelled(task_id, "Native scan cancellation confirmed.");
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto found_scan = scans_.find(task_id);
      if (found_scan == scans_.end()) return;
      auto& scan = found_scan->second;
      scan.kind = result_kind::task_cancelled;
      scan.code = "E_TASK_CANCELLED";
      scan.message = "Native scan cancellation confirmed.";
      scan.complete = true;
    }
    emit_event(context.session_id, task_id, "cancelled", 0, "Native scan cancellation confirmed.");
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found_scan = scans_.find(task_id);
    if (found_scan == scans_.end()) return;
    auto& scan = found_scan->second;
    if (scan.kind == result_kind::task_cancelled) return;
    std::sort(scan.entries.begin(), scan.entries.end(), [](const scan_entry& left, const scan_entry& right) {
      return left.relative_path < right.relative_path;
    });
    std::sort(scan.groups.begin(), scan.groups.end(), [](const scan_group_summary& left,
                                                        const scan_group_summary& right) {
      return left.extension < right.extension;
    });
    scan.kind = traversal_partial ? result_kind::partial : result_kind::success;
    scan.code = traversal_partial ? "E_PARTIAL_SCAN" : "";
    scan.message = traversal_partial
                       ? (traversal_message.empty() ? "Directory scan completed with partial traversal."
                                                    : traversal_message)
                       : "Directory scan completed.";
    scan.complete = true;
    scan.skipped_count = skipped_count;
    scan.error_count = error_count;
    scan.issues = std::move(issues);
  }
  (void)tasks_.complete(task_id, traversal_partial ? result_kind::partial : result_kind::success,
                        traversal_partial ? traversal_message : "Directory scan completed.");
  emit_event(context.session_id, task_id, traversal_partial ? "partial" : "succeeded", 100,
             traversal_partial ? traversal_message : "Directory scan completed.");
}

scan_response directory_scanner::read_page(const scan_request_context& context,
                                           const std::string& task_id, std::string cursor,
                                           std::size_t page_size) {
  const auto authorization = resolve_authorization(context, "task.query");
  if (!authorization) return error("E_PERMISSION_DENIED", "task.query is required.", task_id);
  if (page_size == 0) return error("E_INVALID_ARGUMENT", "page_size must be greater than zero.", task_id);
  page_size = std::min<std::size_t>(page_size, 1000);
  scan_state state;
  std::vector<scan_entry> entries;
  std::size_t total_entries{0};
  std::size_t end{0};
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!authorization->active()) return error("E_PERMISSION_DENIED", "The scan authorization is no longer active.", task_id);
    const auto found = scans_.find(task_id);
    if (found == scans_.end() || found->second.context.session_id != context.session_id ||
        found->second.context.origin != context.origin || found->second.context.authorization != authorization) {
      return error("E_TASK_NOT_FOUND", "The scan task is not valid for this session.", task_id);
    }
    const auto task = tasks_.query(task_id);
    if (!task.has_value()) return error("E_TASK_NOT_FOUND", "The scan task does not exist.");
    if (task->state == task_state::cancel_requested) {
      return {result_kind::timeout_observed, "E_CANCEL_PENDING", "Native scan cancellation is pending.", task_id, {}, {}, false};
    }
    const auto& source = found->second;
    state.complete = source.complete;
    state.kind = source.kind;
    state.code = source.code;
    state.message = source.message;
    state.skipped_count = source.skipped_count;
    state.error_count = source.error_count;
    state.issues = source.issues;
    state.file_count = source.file_count;
    state.total_bytes = source.total_bytes;
    state.groups = source.groups;
    // Snapshot only this page while holding the lock. Never retain references
    // into scans_ after unlocking; session shutdown can erase the scan.
    if (source.complete && source.kind != result_kind::task_cancelled) {
      total_entries = source.entries.size();
      const auto offset = cursor_offset(cursor);
      if (offset > total_entries) return error("E_INVALID_CURSOR", "The scan cursor is invalid.", task_id);
      end = offset + std::min(page_size, total_entries - offset);
      entries.assign(source.entries.begin() + static_cast<std::ptrdiff_t>(offset),
                     source.entries.begin() + static_cast<std::ptrdiff_t>(end));
    }
  }
  if (!state.complete) {
    return {result_kind::timeout_observed, "E_TIMEOUT", "Scan is still running.", task_id, {}, cursor.empty() ? "0" : cursor, true,
            state.skipped_count, state.error_count, state.issues, state.file_count, state.total_bytes, state.groups};
  }
  if (state.kind == result_kind::task_cancelled) {
    return {result_kind::task_cancelled, state.code.empty() ? "E_TASK_CANCELLED" : state.code,
            state.message.empty() ? "Native scan cancellation confirmed." : state.message,
            task_id, {}, {}, false, state.skipped_count, state.error_count, state.issues,
            state.file_count, state.total_bytes, state.groups};
  }
  const bool has_more = end < total_entries;
  if (has_more) {
    (void)tasks_.update_progress(task_id, static_cast<std::uint32_t>((end * 100U) / total_entries));
  }
  if (has_more) {
    return {result_kind::success, {}, {}, task_id, std::move(entries), std::to_string(end), true,
            state.skipped_count, state.error_count, state.issues, state.file_count, state.total_bytes, state.groups};
  }
  const auto final_kind = state.kind == result_kind::partial ? result_kind::partial : result_kind::success;
  const auto final_message = state.kind == result_kind::partial
                                 ? state.message
                                 : std::string{"Directory scan completed."};
  (void)tasks_.complete(task_id, final_kind, final_message);
  return {final_kind, state.code, final_message, task_id, std::move(entries), "", false,
          state.skipped_count, state.error_count, state.issues, state.file_count, state.total_bytes, state.groups};
}

bool directory_scanner::cancel(const scan_request_context& context, const std::string& task_id) {
  const auto authorization = resolve_authorization(context, "task.cancel");
  if (!authorization) return false;
  bool accepted = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!authorization->active()) return false;
    const auto found = scans_.find(task_id);
    if (found == scans_.end() || found->second.context.session_id != context.session_id ||
        found->second.context.origin != context.origin || found->second.context.authorization != authorization) {
      return false;
    }
    accepted = tasks_.request_cancel(task_id);
    if (accepted) {
      const auto worker = workers_.find(task_id);
      if (worker != workers_.end()) worker->second->cancel_requested = true;
      found->second.message = "Native scan cancellation accepted.";
    }
  }
  if (accepted) emit_event(context.session_id, task_id, "cancel-pending", 0, "Native scan cancellation accepted.");
  return accepted;
}

bool directory_scanner::owns_task(const scan_request_context& context,
                                  const std::string& task_id) const {
  // Every scan owner necessarily held one of these capabilities at admission.
  // Resolve the bearer again, even when the caller supplies a native identity.
  const auto authorization = resolve_scan_authorization(context);
  if (!authorization) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = scans_.find(task_id);
  return authorization->active() && found != scans_.end() &&
         found->second.context.session_id == context.session_id &&
         found->second.context.origin == context.origin && found->second.context.authorization == authorization;
}

bool directory_scanner::contains(const std::filesystem::path& root,
                                 const std::filesystem::path& candidate) {
  const auto relative = candidate.lexically_relative(root);
  if (relative.empty() || relative == ".") return true;
  const auto first = *relative.begin();
  return first != ".." && first != relative.root_path();
}

std::size_t directory_scanner::cursor_offset(const std::string& cursor) {
  if (cursor.empty()) return 0;
  std::size_t value = 0;
  const auto* begin = cursor.data();
  const auto* end = cursor.data() + cursor.size();
  const auto parsed = std::from_chars(begin, end, value);
  return parsed.ec == std::errc{} && parsed.ptr == end ? value : static_cast<std::size_t>(-1);
}

scan_response directory_scanner::error(std::string code, std::string message,
                                       std::string task_id) const {
  return {result_kind::failure, std::move(code), std::move(message), std::move(task_id), {}, {}, false};
}

void directory_scanner::emit_event(const std::string& session_id, const std::string& task_id,
                                   const std::string& name, std::uint32_t progress,
                                   const std::string& message) const {
  event_sink sink;
  std::uint64_t sequence = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = scans_.find(task_id);
    if (found == scans_.end() || found->second.context.session_id != session_id ||
        !found->second.context.authorization || !found->second.context.authorization->active()) return;
    sink = sink_;
    sequence = next_event_sequence_++;
  }
  if (!sink) return;
  json payload;
  payload["sessionId"] = session_id;
  payload["sequence"] = sequence;
  payload["eventName"] = "task.progress";
  payload["taskId"] = task_id;
  payload["payload"] = {{"state", name}, {"progress", progress}, {"message", message},
                        {"indeterminate", name == "accepted" || name == "running" ||
                                              name == "cancel-pending"}};
  try { sink(payload.dump()); }
  catch (...) { /* A listener cannot escape the worker or prevent scan cleanup. */ }
}

void directory_scanner::join_finished_workers() {
  std::vector<std::unique_ptr<scan_worker>> finished;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto iterator = workers_.begin(); iterator != workers_.end();) {
      if (iterator->second->cancel_requested) {
        finished.push_back(std::move(iterator->second));
        iterator = workers_.erase(iterator);
      } else {
        ++iterator;
      }
    }
  }
  for (auto& worker : finished) {
    if (worker && worker->thread.joinable()) worker->thread.join();
  }
}

}  // namespace soui::runtime
