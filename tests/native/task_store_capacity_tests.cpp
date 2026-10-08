#include <soui/runtime/core/task/task_store.h>
#include <soui/runtime/services/filesystem-readonly/directory_scanner.h>
#include <soui/runtime/services/permissions/permission_store.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
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

std::atomic<std::int64_t> scan_clock_seconds{0};

soui::runtime::permission_store::clock_type::time_point scan_now() {
  return soui::runtime::permission_store::clock_type::time_point{} +
         std::chrono::seconds(scan_clock_seconds.load());
}

class scan_fixture_directory {
 public:
  scan_fixture_directory() {
    static std::atomic<unsigned> number{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() /
           ("soui-scan-renewal-" + std::to_string(stamp) + "-" + std::to_string(++number));
    require(std::filesystem::create_directory(path), "could not create a unique scan fixture");
    std::ofstream(path / "a.txt") << "alpha";
    std::ofstream(path / "b.txt") << "beta";
  }
  ~scan_fixture_directory() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
  std::filesystem::path path;
};

// Coordinate with the actual worker at its first running event. No timing
// assumptions or sleeps are needed to keep a task live across token rotation.
class scan_event_probe {
 public:
  void receive(std::string_view text) {
    std::unique_lock<std::mutex> lock(mutex);
    ++events;
    if (text.find("\"state\":\"running\"") != std::string_view::npos && pause_running) {
      pause_running = false;
      blocked = true;
      changed.notify_all();
      if (!changed.wait_for(lock, std::chrono::seconds(5), [&] { return released; })) gate_timed_out = true;
    }
    if (text.find("\"state\":\"succeeded\"") != std::string_view::npos ||
        text.find("\"state\":\"cancelled\"") != std::string_view::npos ||
        text.find("\"state\":\"partial\"") != std::string_view::npos) ++terminal_events;
    changed.notify_all();
  }
  void wait_terminal(unsigned count) {
    std::unique_lock<std::mutex> lock(mutex);
    require(changed.wait_for(lock, std::chrono::seconds(5), [&] { return terminal_events >= count; }),
            "scan did not deliver its terminal event");
    require(!gate_timed_out, "scan worker gate timed out");
  }
  void pause_next() {
    std::lock_guard<std::mutex> lock(mutex);
    pause_running = true;
    blocked = false;
    released = false;
  }
  void wait_blocked() {
    std::unique_lock<std::mutex> lock(mutex);
    require(changed.wait_for(lock, std::chrono::seconds(5), [&] { return blocked; }),
            "scan worker did not reach the event gate");
  }
  void release() {
    std::lock_guard<std::mutex> lock(mutex);
    released = true;
    changed.notify_all();
  }
  unsigned count() {
    std::lock_guard<std::mutex> lock(mutex);
    return events;
  }
 private:
  std::mutex mutex;
  std::condition_variable changed;
  unsigned events{0};
  unsigned terminal_events{0};
  bool pause_running{false};
  bool blocked{false};
  bool released{false};
  bool gate_timed_out{false};
};

struct scan_gate_release {
  scan_event_probe& probe;
  ~scan_gate_release() { probe.release(); }
};

const std::vector<std::string> scan_capabilities{
    "desktop.dialog.pickDirectory", "filesystem.scan", "task.query", "task.cancel", "bridge.session.renew"};

void test_scanner_resources_survive_credential_rotation() {
  using namespace soui::runtime;
  scan_clock_seconds = 0;
  scan_fixture_directory fixture;
  permission_store permissions(scan_now);
  task_store store;
  scan_event_probe probe;
  directory_scanner scanner(permissions, store);
  scan_gate_release release_on_failure{probe};
  scanner.set_event_sink([&](std::string_view event) { probe.receive(event); });
  scan_request_context original{"renew-scan-session", "app://renew-scan", {}};
  original.token = permissions.issue(original.session_id, original.origin, scan_capabilities);
  original.authorization = permissions.resolve(original.session_id, original.origin, original.token, "filesystem.scan").scope;
  require(original.authorization != nullptr, "original scan grant was not resolved");
  const auto root = scanner.register_root(original, fixture.path);
  require(!root.empty(), "original scan root registration failed");
  const auto started = scanner.start(original, "scan-page-operation", root);
  require(started.ok(), "original scan was not accepted");
  probe.wait_terminal(1);
  const auto first_page = scanner.read_page(original, started.task_id, {}, 1);
  require(first_page.ok() && first_page.entries.size() == 1 && first_page.has_more &&
              first_page.entries.front().relative_path == "a.txt" && !first_page.next_cursor.empty(),
          "scan did not retain the expected first page and cursor");

  scan_clock_seconds = 25 * 60;
  const auto lease = permissions.renew(original.session_id, original.origin, original.token, "scan-renew-1");
  require(lease.ok() && lease.token != original.token, "scan grant did not rotate");
  auto current = original;
  current.token = lease.token;
  const auto second_page = scanner.read_page(current, started.task_id, first_page.next_cursor, 1);
  require(second_page.ok() && !second_page.has_more && second_page.entries.size() == 1 &&
              second_page.entries.front().relative_path == "b.txt" && second_page.file_count == 2,
          "rotation lost the original scan cursor or completed results");
  require(scanner.owns_task(current, started.task_id), "rotation lost scan ownership");
  const auto duplicate = scanner.start(current, "scan-page-operation", root);
  require(duplicate.ok() && duplicate.task_id == started.task_id,
          "rotation restarted an existing scan operation");

  scan_clock_seconds += 31;
  require(!scanner.owns_task(original, started.task_id) &&
              scanner.read_page(original, started.task_id, {}).code == "E_PERMISSION_DENIED" &&
              !scanner.cancel(original, started.task_id),
          "a retained native identity allowed an expired previous bearer");
  auto sibling = original;
  sibling.token = permissions.issue(original.session_id, original.origin, scan_capabilities);
  // Deliberately forge the supplied native identity; the scanner must resolve
  // this sibling's actual credential and reject access to the original grant.
  sibling.authorization = original.authorization;
  require(scanner.start(sibling, "sibling-root-attempt", root).code == "E_ROOT_NOT_FOUND" &&
              scanner.read_page(sibling, started.task_id, {}).code == "E_TASK_NOT_FOUND" &&
              !scanner.owns_task(sibling, started.task_id) && !scanner.cancel(sibling, started.task_id),
          "a distinct grant with the same session/origin accessed original scan resources");

  probe.pause_next();
  const auto live = scanner.start(current, "scan-live-operation", root);
  require(live.ok(), "renewed credential could not reuse the original root");
  probe.wait_blocked();
  scan_clock_seconds = 50 * 60;
  const auto next_lease = permissions.renew(current.session_id, current.origin, current.token, "scan-renew-2");
  require(next_lease.ok(), "second scan grant rotation failed");
  current.token = next_lease.token;
  require(scanner.owns_task(current, live.task_id) && scanner.cancel(current, live.task_id),
          "renewed credential could not own and cancel the original live scan");
  require(scanner.read_page(sibling, live.task_id, {}).code == "E_TASK_NOT_FOUND",
          "another grant learned the cancellation state before ownership validation");
  probe.release();
  probe.wait_terminal(2);
  require(scanner.read_page(current, live.task_id, {}).kind == result_kind::task_cancelled,
          "the same scan was not cancelled after rotation");
}

void test_scanner_revoke_rejects_admission_and_new_events() {
  using namespace soui::runtime;
  scan_clock_seconds = 0;
  scan_fixture_directory fixture;
  permission_store permissions(scan_now);
  task_store store;
  scan_event_probe probe;
  directory_scanner scanner(permissions, store);
  scan_gate_release release_on_failure{probe};
  scanner.set_event_sink([&](std::string_view event) { probe.receive(event); });
  scan_request_context context{"revoke-scan-session", "app://revoke-scan", {}};
  context.token = permissions.issue(context.session_id, context.origin, scan_capabilities);
  context.authorization = permissions.resolve(context.session_id, context.origin, context.token, "filesystem.scan").scope;
  const auto root = scanner.register_root(context, fixture.path);
  require(!root.empty(), "revocation setup did not register its root");
  probe.pause_next();
  const auto started = scanner.start(context, "revoke-running-scan", root);
  require(started.ok(), "revocation setup did not start its scan");
  probe.wait_blocked();
  permissions.revoke_session(context.session_id);
  const auto delivered_before_revoke = probe.count();
  require(context.authorization && !context.authorization->active(), "retained scan grant survived revocation");
  require(scanner.register_root(context, fixture.path).empty() &&
              scanner.start(context, "revoked-new-scan", root).code == "E_PERMISSION_DENIED" &&
              !scanner.owns_task(context, started.task_id) && !scanner.cancel(context, started.task_id) &&
              scanner.read_page(context, started.task_id, {}).code == "E_PERMISSION_DENIED",
          "revoked native identity authorized a scanner operation");
  probe.release();
  scanner.shutdown_session(context.session_id);
  require(probe.count() == delivered_before_revoke,
          "scanner delivered a new event after its grant was revoked");
}

void test_scanner_listener_failure_does_not_escape_worker() {
  using namespace soui::runtime;
  scan_fixture_directory fixture;
  permission_store permissions;
  task_store store;
  directory_scanner scanner(permissions, store);
  std::atomic<unsigned> callbacks{0};
  scanner.set_event_sink([&](std::string_view) {
    ++callbacks;
    throw std::runtime_error("deliberate scan listener failure");
  });
  scan_request_context context{"throwing-scan", "app://throwing-scan", {}};
  context.token = permissions.issue(context.session_id, context.origin, scan_capabilities);
  const auto root = scanner.register_root(context, fixture.path);
  const auto started = scanner.start(context, "throwing-listener-scan", root);
  require(started.ok(), "throwing listener escaped scan admission");
  scan_response page;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  do {
    page = scanner.read_page(context, started.task_id, {});
    if (page.ok()) break;
    std::this_thread::yield();
  } while (std::chrono::steady_clock::now() < deadline);
  require(page.ok() && page.entries.size() == 2 && callbacks.load() >= 2,
          "throwing listener escaped the real scan worker or prevented its result");
  permissions.revoke_session(context.session_id);
  scanner.shutdown_session(context.session_id);
}

}  // namespace

int main() {
  try {
    test_full_capacity_and_recovery();
    test_parallel_admission_is_bounded();
    test_scanner_reports_capacity_to_caller();
    test_scanner_resources_survive_credential_rotation();
    test_scanner_revoke_rejects_admission_and_new_events();
    test_scanner_listener_failure_does_not_escape_worker();
    std::cout << "Native task store capacity and scanner authorization: admission, rotation, ownership, cancellation, and revocation passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
