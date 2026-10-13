#include <soui/runtime/core/task/task_store.h>
#ifndef SOUI_TASK_STORE_STORAGE_TEST_ONLY
#include <soui/json_bridge.h>
#endif
#include "../acceptance-app/native-owner-metrics.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

namespace dm = soui::sdk::diagnostics;
using soui::runtime::task_store;
using soui::runtime::result_kind;
namespace {
void require(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
dm::task_store_storage read(const task_store& store) {
  dm::task_store_storage value;
  require(store.read_storage_metrics(value) == dm::status::ready, "Store storage not ready");
  require(!value.observation.counter_saturated && !value.observation.clock_invalid, "Unqualified Store observation");
  return value;
}

void public_contract_and_disabled() {
  static_assert(std::is_same_v<decltype(soui::runtime::task_snapshot{}.operation_id), std::string>);
  static_assert(std::is_same_v<decltype(soui::runtime::task_snapshot{}.message), std::string>);
  static_assert(std::is_standard_layout_v<dm::task_store_storage>);
  static_assert(std::is_trivially_copyable_v<dm::task_store_storage>);
  static_assert(sizeof(wkeRendererProcessSnapshot) == 168); // Old ABIs remain independent.
  static_assert(sizeof(wkeRendererModelSnapshot) == 264);
  static_assert(sizeof(wkeCacheSnapshot) == 216);
  task_store store;
  const auto item = store.start_scoped("disabled", "input retained");
  require(store.complete(item.task_id, result_kind::success, "done"), "disabled complete");
  dm::task_store_storage value; value.map_requests.current_bytes = 99; value.coverage_mask = 99;
  require(store.read_storage_metrics(value) == dm::status::disabled && value.coverage_mask == 0 &&
      value.map_requests.current_bytes == 0 && value.task_entries == 0 && value.snapshot_strings.fields == 0 &&
      !value.map_requests.current_bytes_exact, "Disabled getter retained diagnostic data");
#ifndef SOUI_TASK_STORE_STORAGE_TEST_ONLY
  soui::sdk::json_bridge bridge("app://storage-disabled");
  require(bridge.read_task_store_storage_metrics(value) == dm::status::disabled, "Facade must preserve default disabled");
  soui::sdk::json_bridge enabled("app://storage-enabled", dm::mode::scalar);
  require(enabled.read_task_store_storage_metrics(value) == dm::status::ready &&
      value.coverage_mask == 3 && value.gap_mask == 63 && value.task_entries == 0,
      "Public facade did not observe its actual Store");
#endif
}

void retained_strings_and_prepare_peak() {
  task_store store("task-", dm::mode::scalar);
  const std::string scope(8192, 's');
  const auto item = store.start_scoped("first", scope);
  std::string message = "x"; message.reserve(65536);
  const auto capacity = message.capacity();
  require(store.update_progress(item.task_id, 10, std::move(message)), "Progress update");
  const auto large = read(store);
  require(large.snapshot_strings.capacity_bytes >= capacity &&
      large.snapshot_strings.size_bytes < large.snapshot_strings.capacity_bytes, "Capacity must retain large short string");
  require(large.scope_strings.fields == 2 && large.scope_strings.size_bytes >= scope.size(), "Scope-map strings missing");
  require(store.complete(item.task_id, result_kind::success, "terminal"), "Complete retained task");
  for (int index = 0; index < 8; ++index)
    (void)store.start_scoped("other-" + std::to_string(index), "input");
  const auto value = read(store);
  require(value.task_entries == 9 && value.operation_entries == 9 && value.scope_entries == 9, "Store identity maps differ");
  require(value.snapshot_strings.fields == 45 && value.task_keys.fields == 9 &&
      value.operation_strings.fields == 18 && value.scope_strings.fields == 18 && value.id_prefix.fields == 1,
      "Committed string field coverage differs");
  require(value.coverage_mask == 3 && value.gap_mask == 63, "Coverage or gaps lost");
  require(value.map_requests.current_bytes_exact && value.map_requests.current_allocations_exact &&
      value.map_requests.current_bytes > 0 && value.map_requests.high_water_bytes > value.map_requests.current_bytes &&
      value.map_requests.deallocations_total > 0, "Prepare double-copy peak not retained");
  const auto unchanged = read(store);
  require(unchanged.map_requests.allocations_total == value.map_requests.allocations_total &&
      unchanged.map_requests.deallocations_total == value.map_requests.deallocations_total &&
      unchanged.map_requests.high_water_bytes == value.map_requests.high_water_bytes, "Getter mutated ledger");
  require(store.query(item.task_id)->message == "terminal", "Terminal history disappeared");
  require(store.start_scoped("first", scope).task_id == item.task_id, "Duplicate lookup semantics changed");
}

void oldest_terminal_eviction() {
  task_store store("task-", dm::mode::scalar);
  const auto oldest = store.start_scoped("oldest", "payload");
  require(store.complete(oldest.task_id, result_kind::success, "retained"), "Oldest complete");
  std::string active;
  for (std::size_t index = 1; index < task_store::max_tasks; ++index) {
    const auto item = store.start_scoped("active-" + std::to_string(index), "payload");
    if (index == 1) active = item.task_id;
  }
  const auto replacement = store.start_scoped("replacement", "payload");
  require(!store.query(oldest.task_id) && store.query(active) && !replacement.task_id.empty(), "Eviction crossed terminal boundary");
  const auto value = read(store);
  require(value.task_entries == task_store::max_tasks && value.operation_entries == value.task_entries &&
      value.scope_entries == value.task_entries, "Eviction broke map retention count");
  require(!store.erase(active), "Active erase changed");
}

void serialization_is_independent() {
  dm::task_store_storage value;
  value.map_requests.current_bytes = 9007199254740993ULL;
  value.snapshot_strings.capacity_bytes = 9007199254740995ULL;
  value.coverage_mask = 3; value.gap_mask = 63;
  const auto encoded = soui::acceptance::metrics::serialize(value);
  require(encoded.at("map_requests").at("current_bytes") == "9007199254740993" &&
      encoded.at("snapshot_strings").at("capacity_bytes") == "9007199254740995", "U64 lost precision");
  require(encoded.at("cacheBudgetGate") == "not-evaluated" && encoded.at("configuration").is_null() && encoded.at("budgets").is_null() &&
      encoded.at("quota").is_null() && encoded.at("calibrationPin").is_null() &&
      encoded.at("phasePartitionObserved") == false && encoded.at("stringAllocatorExact") == false,
      "Serialization invented qualification or phase/heap accounting");
}
}

#ifdef SOUI_TASK_STORE_STORAGE_TEST_ONLY
namespace {
std::atomic<std::size_t> calls{0}, fail_on{0};
std::atomic<bool> block_next{false}, saturate_next{false};
std::mutex allocation_mutex;
std::condition_variable allocation_changed;
bool allocation_entered{false}, allocation_released{false};
dm::allocation_requests retired_totals;
bool retired_saturated{false};
soui::runtime::detail::task_store_allocation_ledger* last_allocation_ledger{nullptr};
}
namespace soui::runtime::detail::task_store_storage_test {
void before_allocate(task_store_allocation_ledger& ledger, std::size_t bytes) {
  last_allocation_ledger = &ledger;
  const auto call = ++calls;
  if (call == fail_on.load()) throw std::bad_alloc();
  if (saturate_next.exchange(false)) ledger.totals.current_bytes = (std::numeric_limits<std::uint64_t>::max)() - bytes + 1;
  if (block_next.exchange(false)) {
    std::unique_lock<std::mutex> lock(allocation_mutex);
    allocation_entered = true; allocation_changed.notify_all();
    allocation_changed.wait(lock, [] { return allocation_released; });
  }
}
void ledger_retired(const task_store_allocation_ledger& ledger) noexcept {
  if (ledger.enabled) { retired_totals = ledger.snapshot(); retired_saturated = ledger.saturated; }
}
}
namespace {
void nth_allocator_failure_rolls_back() {
  std::size_t preparation_calls = 0;
  {
    task_store probe("task-", dm::mode::scalar);
    for (int index = 0; index < 4; ++index) (void)probe.start("base-" + std::to_string(index));
    calls = 0; (void)probe.start("new"); preparation_calls = calls.load();
  }
  require(preparation_calls > 3, "Preparation allocation path not exercised");
  for (std::size_t fail = 1; fail <= preparation_calls; ++fail) {
    task_store store("task-", dm::mode::scalar);
    const auto first = store.start("base-0");
    for (int index = 1; index < 4; ++index) (void)store.start("base-" + std::to_string(index));
    const auto before = read(store);
    calls = 0; fail_on = fail;
    bool threw = false;
    try { (void)store.start("new"); } catch (const std::bad_alloc&) { threw = true; }
    fail_on = 0;
    require(threw, "nth allocation did not fail");
    const auto after = read(store);
    require(after.task_entries == before.task_entries && after.map_requests.current_bytes == before.map_requests.current_bytes &&
        after.map_requests.current_allocations == before.map_requests.current_allocations &&
        after.map_requests.failed_allocations_total == before.map_requests.failed_allocations_total + 1,
        "Rollback leaked allocator credits or changed retention");
    require(after.map_requests.allocated_bytes_total - before.map_requests.allocated_bytes_total ==
        after.map_requests.deallocated_bytes_total - before.map_requests.deallocated_bytes_total, "Successful temporary allocations not refunded");
    require(store.query(first.task_id).has_value() && store.start("base-0").task_id == first.task_id,
        "Failure changed committed operation lookup");
    require(store.start("new").task_id == "task-000005", "Failed preparation consumed task identity");
  }
}

void busy_and_actual_final_refund() {
  {
    task_store store("task-", dm::mode::scalar);
    (void)store.start("existing");
    { std::lock_guard<std::mutex> lock(allocation_mutex); allocation_entered = allocation_released = false; }
    block_next = true;
    std::exception_ptr error;
    std::thread worker([&] { try { (void)store.start("held"); } catch (...) { error = std::current_exception(); } });
    { std::unique_lock<std::mutex> lock(allocation_mutex); allocation_changed.wait(lock, [] { return allocation_entered; }); }
    dm::task_store_storage value;
    const auto state = store.read_storage_metrics(value);
    { std::lock_guard<std::mutex> lock(allocation_mutex); allocation_released = true; }
    allocation_changed.notify_all(); worker.join();
    if (error) std::rethrow_exception(error);
    require(state == dm::status::busy && value.coverage_mask == 0 && !value.map_requests.current_bytes_exact,
        "Busy observation pretended exact storage");
  }
  require(!retired_saturated && retired_totals.current_bytes == 0 && retired_totals.current_allocations == 0 &&
      retired_totals.allocations_total == retired_totals.deallocations_total &&
      retired_totals.allocated_bytes_total == retired_totals.deallocated_bytes_total,
      "Actual map deallocation did not precede ledger retirement");
}

void sticky_current_saturation_and_invalid_underflow() {
  {
    task_store store("task-", dm::mode::scalar);
    saturate_next = true;
    const auto item = store.start("saturated");
    dm::task_store_storage value;
    require(store.read_storage_metrics(value) == dm::status::ready && value.observation.counter_saturated &&
        !value.map_requests.current_bytes_exact && value.map_requests.current_bytes == (std::numeric_limits<std::uint64_t>::max)(),
        "Injected saturation lost unknown state");
    require(store.complete(item.task_id, result_kind::success) && store.erase(item.task_id), "Saturation changed normal data semantics");
    require(store.read_storage_metrics(value) == dm::status::ready && value.observation.counter_saturated &&
        value.map_requests.current_bytes == (std::numeric_limits<std::uint64_t>::max)(), "Refund manufactured finite saturated current");
  }
  // Exercise the same real allocator's underflow guard with an isolated private ledger.
  soui::runtime::detail::task_store_allocation_ledger ledger(true);
  soui::runtime::detail::task_store_allocator<int> allocator(&ledger);
  auto* allocation = allocator.allocate(1);
  ledger.totals.current_bytes = 0;
  allocator.deallocate(allocation, 1);
  require(ledger.invalid && ledger.saturated && !ledger.snapshot().current_bytes_exact &&
      ledger.snapshot().current_bytes == (std::numeric_limits<std::uint64_t>::max)(), "Mismatch manufactured a zero current");
  task_store store("task-", dm::mode::scalar);
  const auto item = store.start("mismatched");
  require(last_allocation_ledger != nullptr, "Actual map allocator ledger absent");
  last_allocation_ledger->totals.current_bytes = 0; // Inject a diagnostic mismatch in the actual Store ledger.
  require(store.complete(item.task_id, result_kind::success) && store.erase(item.task_id), "Mismatch changed data operations");
  dm::task_store_storage value;
  require(store.read_storage_metrics(value) == dm::status::unavailable && value.observation.counter_saturated &&
      !value.map_requests.current_bytes_exact && value.map_requests.current_bytes == (std::numeric_limits<std::uint64_t>::max)(),
      "Unavailable mismatch was replaced with an exact zero");
}
}
#endif

int main() {
  try {
    public_contract_and_disabled(); retained_strings_and_prepare_peak();
    oldest_terminal_eviction(); serialization_is_independent();
#ifdef SOUI_TASK_STORE_STORAGE_TEST_ONLY
    nth_allocator_failure_rolls_back(); busy_and_actual_final_refund();
    sticky_current_saturation_and_invalid_underflow();
#endif
    std::cout << "TaskStore storage observation contract passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
