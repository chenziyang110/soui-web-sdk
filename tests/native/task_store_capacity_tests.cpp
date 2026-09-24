#include <soui/runtime/core/task/task_store.h>
#include <soui/runtime/services/filesystem-readonly/directory_scanner.h>
#include <soui/runtime/services/permissions/permission_store.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void test_full_capacity_and_recovery() {
  soui::runtime::task_store store;
  std::vector<std::string> ids;
  ids.reserve(soui::runtime::task_store::max_tasks);
  for (std::size_t i = 0; i < soui::runtime::task_store::max_tasks; ++i) {
    const auto task = store.start_scoped("operation-" + std::to_string(i), "scope");
    require(!task.task_id.empty(), "capacity boundary rejected an available slot");
    ids.push_back(task.task_id);
  }

  bool duplicate = false;
  bool conflict = false;
  bool capacity = false;
  const auto existing = store.start_scoped("operation-0", "scope", &duplicate, &conflict, &capacity);
  require(duplicate && !conflict && !capacity && existing.task_id == ids.front(),
          "matching operationId must remain idempotent at capacity");
  const auto conflicting = store.start_scoped("operation-0", "different", &duplicate, &conflict, &capacity);
  require(!duplicate && conflict && !capacity && conflicting.task_id.empty(),
          "conflicting operationId must keep its distinct rejection");

  const auto rejected = store.start_scoped("over-limit", "scope", &duplicate, &conflict, &capacity);
  require(!duplicate && !conflict && capacity && rejected.task_id.empty() &&
              rejected.state == soui::runtime::task_state::failed &&
              rejected.result.kind == soui::runtime::result_kind::failure,
          "full active task store must reject without an accepted task ID");
  require(store.start("legacy-over-limit").task_id.empty(),
          "the start convenience API must also respect capacity");
  require(store.request_cancel(ids.front()), "cancel request must be accepted");
  const auto still_full = store.start_scoped("cancel-pending", "scope", nullptr, nullptr, &capacity);
  require(capacity && still_full.task_id.empty(), "cancel-requested task still occupies capacity");
  require(store.confirm_cancelled(ids.front()), "native cancellation confirmation must succeed");

  const auto replacement = store.start_scoped("replacement", "scope", nullptr, nullptr, &capacity);
  require(!capacity && !replacement.task_id.empty(), "confirmed cancellation must restore capacity");
  require(!store.query(ids.front()).has_value(), "the terminal task should be evicted to reclaim its slot");
  require(store.query(ids.back()).has_value(), "reclaiming a slot must not evict an active task");
  require(replacement.task_id == "task-001025", "rejected attempts must not consume task IDs");
}

void test_parallel_admission_is_bounded() {
  soui::runtime::task_store store;
  constexpr int workers = 8;
  constexpr int starts_per_worker = 160;
  std::atomic<int> accepted{0};
  std::atomic<int> full{0};
  std::atomic<int> invalid{0};
  std::mutex ids_mutex;
  std::vector<std::string> ids;
  std::vector<std::thread> threads;
  threads.reserve(workers);
  for (int worker = 0; worker < workers; ++worker) {
    threads.emplace_back([&, worker] {
      for (int attempt = 0; attempt < starts_per_worker; ++attempt) {
        bool capacity = false;
        const auto task = store.start_scoped("parallel-" + std::to_string(worker) + "-" +
                                                 std::to_string(attempt), "scope", nullptr, nullptr, &capacity);
        if (capacity && task.task_id.empty()) {
          ++full;
        } else if (!capacity && !task.task_id.empty()) {
          ++accepted;
          std::lock_guard<std::mutex> lock(ids_mutex);
          ids.push_back(task.task_id);
        } else {
          ++invalid;
        }
      }
    });
  }
  for (auto& thread : threads) thread.join();
  require(invalid == 0, "parallel admission returned an inconsistent status");
  require(accepted == static_cast<int>(soui::runtime::task_store::max_tasks) &&
              full == workers * starts_per_worker - accepted,
          "parallel admission exceeded the hard task limit");
  std::sort(ids.begin(), ids.end());
  require(std::adjacent_find(ids.begin(), ids.end()) == ids.end(), "parallel admission reused a task ID");
}

void test_scanner_reports_capacity_to_caller() {
  soui::runtime::task_store store;
  for (std::size_t i = 0; i < soui::runtime::task_store::max_tasks; ++i) {
    require(!store.start("scanner-fill-" + std::to_string(i)).task_id.empty(),
            "scanner setup did not fill the task store");
  }
  soui::runtime::permission_store permissions;
  soui::runtime::scan_request_context context{"capacity-session", "app://capacity", {}};
  context.token = permissions.issue(context.session_id, context.origin,
                                    {"desktop.dialog.pickDirectory", "filesystem.scan"});
  soui::runtime::directory_scanner scanner(permissions, store);
  const auto root = scanner.register_root(context, std::filesystem::current_path());
  require(!root.empty(), "scanner setup could not register an authorized root");
  const auto response = scanner.start(context, "scanner-over-limit", root);
  require(!response.ok() && response.code == "E_BUSY" && response.task_id.empty(),
          "scanner must surface a capacity rejection without launching a task");
}

}  // namespace

int main() {
  try {
    test_full_capacity_and_recovery();
    test_parallel_admission_is_bounded();
    test_scanner_reports_capacity_to_caller();
    std::cout << "Native task store capacity: boundary, cancellation, parallel admission, and scanner rejection passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
