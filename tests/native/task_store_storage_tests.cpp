#include <soui/runtime/core/task/task_store.h>
#ifndef SOUI_TASK_STORE_STORAGE_TEST_ONLY
#include <soui/json_bridge.h>
#endif
#include "../acceptance-app/native-owner-metrics.h"
#include <atomic>
#include <chrono>
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
struct retained_storage {
  dm::task_store_storage metadata;
  dm::allocation_requests strings, aggregate;
};
retained_storage read_retained(const task_store& store) {
  retained_storage value;
  require(store.read_retained_string_storage(value.metadata, value.strings, value.aggregate) == dm::status::ready,
      "Retained string storage not ready");
  require(!value.metadata.observation.counter_saturated && !value.metadata.observation.clock_invalid,
      "Unqualified retained observation");
  const auto& maps = value.metadata.map_requests;
  require(maps.current_bytes_exact && value.strings.current_bytes_exact && value.aggregate.current_bytes_exact &&
      maps.current_allocations_exact && value.strings.current_allocations_exact && value.aggregate.current_allocations_exact,
      "Live private requests are not exact");
  require(value.aggregate.current_bytes == maps.current_bytes + value.strings.current_bytes &&
      value.aggregate.current_allocations == maps.current_allocations + value.strings.current_allocations &&
      value.aggregate.allocations_total == maps.allocations_total + value.strings.allocations_total &&
      value.aggregate.deallocations_total == maps.deallocations_total + value.strings.deallocations_total &&
      value.aggregate.failed_allocations_total == maps.failed_allocations_total + value.strings.failed_allocations_total &&
      value.aggregate.allocated_bytes_total == maps.allocated_bytes_total + value.strings.allocated_bytes_total &&
      value.aggregate.deallocated_bytes_total == maps.deallocated_bytes_total + value.strings.deallocated_bytes_total,
      "Aggregate is not the actual sum of both request domains");
  return value;
}
dm::task_store_private_storage read_private(const task_store& store) {
  dm::task_store_private_storage value;
  require(store.read_private_storage_metrics(value) == dm::status::ready &&
      !value.observation.counter_saturated && !value.observation.clock_invalid && value.observation.owner_epoch != 0,
      "Independent private Store DTO not ready");
  require(value.coverage_mask == 7 && value.gap_mask == 31 && value.map_requests.current_bytes_exact &&
      value.private_string_requests.current_bytes_exact && value.aggregate_requests.current_bytes_exact &&
      value.aggregate_requests.current_bytes == value.map_requests.current_bytes + value.private_string_requests.current_bytes &&
      value.aggregate_requests.current_allocations == value.map_requests.current_allocations + value.private_string_requests.current_allocations,
      "Independent private DTO combined inconsistent domains or scope");
  return value;
}
#ifdef SOUI_TASK_STORE_STORAGE_TEST_ONLY
void require_refunded(const dm::allocation_requests& before, const dm::allocation_requests& after) {
  require(after.current_bytes == before.current_bytes && after.current_allocations == before.current_allocations,
      "Rollback leaked live requests");
  require(after.allocated_bytes_total - before.allocated_bytes_total ==
      after.deallocated_bytes_total - before.deallocated_bytes_total, "Rollback did not refund successful requests");
}
#endif

void public_contract_and_disabled() {
  static_assert(std::is_same_v<decltype(soui::runtime::task_snapshot{}.operation_id), std::string>);
  static_assert(std::is_same_v<decltype(soui::runtime::task_snapshot{}.message), std::string>);
  static_assert(std::is_same_v<decltype(soui::runtime::task_snapshot{}.task_id), std::string>);
  static_assert(std::is_same_v<decltype(soui::runtime::operation_result{}.message), std::string>);
  static_assert(std::is_standard_layout_v<dm::task_store_storage>);
  static_assert(std::is_trivially_copyable_v<dm::task_store_storage>);
  static_assert(std::is_standard_layout_v<dm::task_store_private_storage>);
  static_assert(std::is_trivially_copyable_v<dm::task_store_private_storage>);
  static_assert(dm::task_store_private_storage::schema_version == 1);
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
  dm::allocation_requests strings, aggregate;
  strings.current_bytes = aggregate.current_bytes = 99;
  require(store.read_retained_string_storage(value, strings, aggregate) == dm::status::disabled &&
      value.coverage_mask == 0 && value.map_requests.current_bytes == 0 &&
      strings.current_bytes == 0 && aggregate.current_bytes == 0 &&
      !strings.current_bytes_exact && !aggregate.current_bytes_exact,
      "Disabled new getter manufactured observed zeros");
  dm::task_store_private_storage private_storage;
  private_storage.aggregate_requests.current_bytes = 99;
  require(store.read_private_storage_metrics(private_storage) == dm::status::disabled &&
      private_storage.coverage_mask == 0 && private_storage.aggregate_requests.current_bytes == 0 &&
      !private_storage.aggregate_requests.current_bytes_exact && !private_storage.private_string_requests.current_bytes_exact,
      "Disabled independent DTO retained or qualified data");
#ifndef SOUI_TASK_STORE_STORAGE_TEST_ONLY
  soui::sdk::json_bridge bridge("app://storage-disabled");
  require(bridge.read_task_store_storage_metrics(value) == dm::status::disabled, "Facade must preserve default disabled");
  require(bridge.read_task_store_private_storage_metrics(private_storage) == dm::status::disabled &&
      private_storage.observation.state == dm::status::disabled && private_storage.coverage_mask == 0,
      "New facade must preserve default disabled");
  soui::runtime::task_service disabled_service;
  require(disabled_service.read_private_storage_metrics(private_storage) == dm::status::disabled &&
      private_storage.coverage_mask == 0 && !private_storage.aggregate_requests.current_bytes_exact,
      "New service getter must preserve default disabled");
  soui::sdk::json_bridge enabled("app://storage-enabled", dm::mode::scalar);
  require(enabled.read_task_store_storage_metrics(value) == dm::status::ready &&
      value.coverage_mask == 3 && value.gap_mask == 63 && value.task_entries == 0,
      "Public facade did not observe its actual Store");
  require(enabled.read_task_store_private_storage_metrics(private_storage) == dm::status::ready &&
      private_storage.coverage_mask == 7 && private_storage.gap_mask == 31 && private_storage.task_entries == 0 &&
      private_storage.aggregate_requests.current_bytes_exact, "Public facade lost independent private DTO");
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
  require(store.query(item.task_id)->message == "x" && large.snapshot_strings.capacity_bytes < capacity &&
      large.snapshot_strings.size_bytes <= large.snapshot_strings.capacity_bytes,
      "Private storage must copy the value without stealing foreign reserve");
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
  const auto retained = read_retained(store);
  require(retained.strings.current_bytes > 0 && retained.strings.high_water_bytes > retained.strings.current_bytes &&
      retained.aggregate.high_water_bytes > retained.aggregate.current_bytes && retained.strings.deallocations_total > 0,
      "Candidate/retiring string overlap was not charged at allocation time");
  const auto again = read_retained(store);
  require(again.aggregate.allocations_total == retained.aggregate.allocations_total &&
      again.aggregate.deallocations_total == retained.aggregate.deallocations_total &&
      again.aggregate.high_water_bytes == retained.aggregate.high_water_bytes, "New getter allocated or changed peaks");
}

void owned_bytes_lookup_and_terminal_retention() {
  const std::string prefix(256, 'p'), message(4096, 'm');
  std::string operation(256, 'o'); operation += std::string("\0one", 4);
  std::string scope(4096, 's'); scope += std::string("\0one", 4);
  task_store store(prefix, dm::mode::scalar);
  const auto item = store.start_scoped(operation, scope);
  require(item.task_id == prefix + "000001" && item.operation_id == operation, "Owned ID/op bytes changed");
  require(store.complete(item.task_id, result_kind::success, message), "Terminal long-string complete");
  const auto terminal = read_retained(store);
  require(terminal.strings.current_bytes >= message.size() * 2 + scope.size(), "Actual retained char buffers missing");
  const auto collected = read_private(store);
  require(collected.observation.owner_epoch == terminal.metadata.observation.owner_epoch && collected.task_entries == 1 &&
      collected.private_string_requests.current_bytes == terminal.strings.current_bytes &&
      collected.aggregate_requests.allocations_total == terminal.aggregate.allocations_total &&
      collected.aggregate_requests.high_water_bytes == terminal.aggregate.high_water_bytes,
      "DTO mapping changed owner identity or allocator events");
  for (int index = 0; index < 5; ++index) (void)store.start("other-" + std::to_string(index));
  auto queried = store.query(item.task_id);
  require(queried && queried->message == message && queried->result.message == message &&
      queried->result.task_id == item.task_id && queried->state == soui::runtime::task_state::succeeded,
      "Candidate copy lost terminal owned strings");
  const auto before_lookup = read_retained(store);
  for (int index = 0; index < 8; ++index) {
    require(store.query(item.task_id).has_value() && !store.query(prefix + "missing") &&
        !store.mark_running(item.task_id) && !store.erase(prefix + "missing"), "Borrowed lookup changed behavior");
  }
  const auto after_lookup = read_retained(store);
  require(after_lookup.aggregate.allocations_total == before_lookup.aggregate.allocations_total &&
      after_lookup.aggregate.deallocations_total == before_lookup.aggregate.deallocations_total,
      "Lookup allocated private storage");
  bool duplicate = false, conflict = false;
  require(store.start_scoped(operation, scope, &duplicate, &conflict).task_id == item.task_id && duplicate && !conflict,
      "Owned scope duplicate failed");
  scope.back() = '2';
  require(store.start_scoped(operation, scope, &duplicate, &conflict).task_id.empty() && !duplicate && conflict,
      "Scope equality ignored bytes after NUL");
  operation.back() = '2';
  require(store.start_scoped(operation, scope).task_id != item.task_id, "Hash/equality ignored operation bytes after NUL");
  const auto before_erase = read_retained(store);
  require(store.erase(item.task_id), "Terminal erase failed");
  const auto after_erase = read_retained(store);
  require(!store.query(item.task_id) && after_erase.strings.current_bytes < before_erase.strings.current_bytes &&
      after_erase.strings.deallocations_total > before_erase.strings.deallocations_total,
      "Actual erase did not release retained char requests");
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

void private_serialization_scope_and_status() {
  dm::task_store_private_storage value;
  value.observation.state = dm::status::ready;
  value.coverage_mask = 7; value.gap_mask = 31;
  value.map_requests.current_bytes = 9007199254740993ULL;
  value.private_string_requests.current_bytes = 9007199254740995ULL;
  value.aggregate_requests.current_bytes = 18014398509481988ULL;
  value.aggregate_requests.high_water_bytes = (std::numeric_limits<std::uint64_t>::max)();
  value.map_requests.current_bytes_exact = value.private_string_requests.current_bytes_exact = value.aggregate_requests.current_bytes_exact = true;
  const auto record = soui::acceptance::metrics::json::parse(soui::acceptance::metrics::serialize(value).dump());
  require(record.at("schemaVersion") == 1 && record.at("scope") == "task-store-private-allocator-requests" &&
      record.at("map_requests").at("current_bytes") == "9007199254740993" &&
      record.at("private_string_requests").at("current_bytes") == "9007199254740995" &&
      record.at("aggregate_requests").at("current_bytes") == "18014398509481988" &&
      record.at("aggregate_requests").at("high_water_bytes") == "18446744073709551615",
      "Independent DTO lost native integer precision or scope");
  require(record.at("coverage_mask") == "7" && record.at("gap_mask") == "31" && record.at("gaps").size() == 5 &&
      record.at("privateStringAllocatorRequestsCovered") == true && !record.contains("stringAllocatorExact") &&
      record.at("publicCopiesObserved") == false && record.at("phasePartitionObserved") == false &&
      record.at("wholeHeapObserved") == false && record.at("finalDisposalReceiptObserved") == false &&
      record.at("cacheBudgetGate") == "not-evaluated" && record.at("configuration").is_null() &&
      record.at("budgets").is_null() && record.at("quota").is_null() && record.at("calibrationPin").is_null(),
      "New private scope claimed public copies/phases/heap/budgets");
  for (const auto state : {dm::status::disabled, dm::status::busy, dm::status::unavailable}) {
    dm::task_store_private_storage incomplete;
    incomplete.observation.state = state;
    if (state == dm::status::unavailable) {
      incomplete.coverage_mask = 7; incomplete.gap_mask = 31;
      incomplete.observation.counter_saturated = true;
      incomplete.aggregate_requests.current_bytes = (std::numeric_limits<std::uint64_t>::max)();
    }
    const auto encoded = soui::acceptance::metrics::serialize(incomplete);
    require(encoded.at("observation").at("state") == soui::acceptance::metrics::status_name(state) &&
        encoded.at("aggregate_requests").at("current_bytes_exact") == false &&
        encoded.at("private_string_requests").at("current_bytes_exact") == false &&
        encoded.at("cacheBudgetGate") == "not-evaluated", "Incomplete private DTO became exact ready zeros");
  }
}

#ifndef SOUI_TASK_STORE_STORAGE_TEST_ONLY
template <class Getter>
dm::task_store_private_storage completed_public_storage(Getter&& getter) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  for (;;) {
    dm::task_store_private_storage value;
    const auto state = getter(value);
    if (state == dm::status::ready && value.task_entries == 1 && value.private_string_requests.current_bytes >= 8192) {
      require(value.coverage_mask == 7 && value.gap_mask == 31 && value.observation.owner_epoch != 0 &&
          !value.observation.clock_invalid && !value.observation.counter_saturated && value.aggregate_requests.current_bytes_exact &&
          value.aggregate_requests.current_bytes == value.map_requests.current_bytes + value.private_string_requests.current_bytes,
          "Public new getter broke the same-Store observation");
      return value;
    }
    require(state == dm::status::ready || state == dm::status::busy, "Public private getter unavailable or disabled");
    require(std::chrono::steady_clock::now() < deadline, "Real public task did not retain result strings");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}
void service_and_facade_private_storage() {
  using namespace soui::runtime;
  using json = soui::acceptance::metrics::json;
  const auto handler = [](const task_invocation&, const task_control&) {
    return task_outcome{result_kind::success, "{}", std::string(4096, 'm')};
  };
  const auto input = json{{"payload", std::string(4096, 'i')}};
  permission_store permissions;
  const auto bearer = permissions.issue("storage-native", "app://storage-native", {"task.start", "task.query"});
  task_service service(dm::mode::scalar);
  require(service.register_handler("ledger", handler), "Real service handler registration");
  bridge_request request;
  request.session_id = "storage-native"; request.origin = "app://storage-native"; request.token = bearer;
  request.authorization = permissions.resolve(request.session_id, request.origin, bearer, "task.start").scope;
  request.params_json = json{{"kind", "ledger"}, {"operationId", "native-ledger"}, {"input", input}}.dump();
  const auto started = json::parse(service.start(request));
  require(started.contains("taskId"), "Real native service start failed");
  const auto native = completed_public_storage([&](dm::task_store_private_storage& out) { return service.read_private_storage_metrics(out); });
  request.params_json = json{{"taskId", started.at("taskId")}}.dump();
  require(json::parse(service.query(request)).at("state") == "succeeded", "New service getter did not observe terminal retention");
  dm::task_store_storage legacy;
  require(service.read_storage_metrics(legacy) == dm::status::ready && legacy.coverage_mask == 3 && legacy.gap_mask == 63 &&
      legacy.observation.owner_epoch == native.observation.owner_epoch, "Service changed legacy scope or Store identity");

  soui::sdk::json_bridge bridge("app://storage-facade", dm::mode::scalar);
  require(bridge.register_task_handler("ledger", handler), "Real facade handler registration");
  const auto bootstrap = json::parse(bridge.handle_json(json{{"requestId", "storage-bootstrap"}, {"sessionId", ""},
      {"origin", ""}, {"token", ""}, {"method", "bridge.bootstrap"}, {"params", {{"protocol", "soui.bridge/1"}}}}.dump()));
  require(bootstrap.at("ok").get<bool>(), "Real facade bootstrap failed");
  const auto& auth = bootstrap.at("result");
  const auto call = [&](const char* id, const char* method, json params) {
    return json::parse(bridge.handle_json(json{{"requestId", id}, {"sessionId", auth.at("sessionId")},
        {"origin", auth.at("origin")}, {"token", auth.at("token")}, {"method", method}, {"params", std::move(params)}, {"timeoutMs", 10000}}.dump()));
  };
  const auto facade_start = call("storage-start", "task.start", {{"kind", "ledger"}, {"operationId", "facade-ledger"}, {"input", input}});
  require(facade_start.at("ok").get<bool>(), "Real facade start failed");
  const auto facade = completed_public_storage([&](dm::task_store_private_storage& out) { return bridge.read_task_store_private_storage_metrics(out); });
  const auto queried = call("storage-query", "task.query", {{"taskId", facade_start.at("result").at("taskId")}});
  require(queried.at("ok").get<bool>() && queried.at("result").at("state") == "succeeded", "Facade lost retained task state");
  require(bridge.read_task_store_storage_metrics(legacy) == dm::status::ready && legacy.coverage_mask == 3 && legacy.gap_mask == 63 &&
      legacy.observation.owner_epoch == facade.observation.owner_epoch && facade.observation.owner_epoch != native.observation.owner_epoch,
      "Facade changed legacy scope or fused different Store owners");
}
#endif
}

#ifdef SOUI_TASK_STORE_STORAGE_TEST_ONLY
namespace {
std::atomic<std::size_t> calls{0}, fail_on{0};
std::atomic<bool> block_next{false}, saturate_next{false};
std::mutex allocation_mutex;
std::condition_variable allocation_changed;
bool allocation_entered{false}, allocation_released{false};
dm::allocation_requests retired_totals;
dm::allocation_requests retired_domains[3];
std::size_t retirement_count{0};
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
  if (ledger.enabled) {
    retired_totals = ledger.snapshot(); retired_saturated = ledger.saturated;
    retired_domains[retirement_count++ % 3] = retired_totals;
  }
}
}
namespace {
void prepare_base(task_store& store) {
  for (int index = 0; index < 4; ++index) {
    const auto item = store.start_scoped(std::string(256, 'o') + std::to_string(index), std::string(4096, 's'));
    require(store.update_progress(item.task_id, 7, std::string(2048, 'm')), "Preparation base message");
    if (index == 0) require(store.complete(item.task_id, result_kind::success, std::string(2048, 't')), "Preparation terminal base");
  }
}
void nth_allocator_failure_rolls_back() {
  std::size_t preparation_calls = 0;
  const std::string prefix(256, 'p'), next_operation(256, 'n'), next_scope(4096, 's');
  {
    task_store probe(prefix, dm::mode::scalar);
    prepare_base(probe);
    calls = 0; (void)probe.start_scoped(next_operation, next_scope); preparation_calls = calls.load();
  }
  require(preparation_calls > 3, "Preparation map/string allocation path not exercised");
  for (std::size_t fail = 1; fail <= preparation_calls; ++fail) {
    task_store store(prefix, dm::mode::scalar);
    prepare_base(store);
    const auto first = store.query(prefix + "000001");
    const auto before = read_retained(store);
    calls = 0; fail_on = fail;
    bool threw = false;
    try { (void)store.start_scoped(next_operation, next_scope); } catch (const std::bad_alloc&) { threw = true; }
    fail_on = 0;
    require(threw, "nth allocation did not fail");
    const auto after = read_retained(store);
    require(after.metadata.task_entries == before.metadata.task_entries &&
        after.aggregate.failed_allocations_total == before.aggregate.failed_allocations_total + 1,
        "Rollback changed retention or lost failure domain");
    require_refunded(before.metadata.map_requests, after.metadata.map_requests);
    require_refunded(before.strings, after.strings); require_refunded(before.aggregate, after.aggregate);
    require(first && store.query(first->task_id)->message == first->message &&
        store.start_scoped(first->operation_id, std::string(4096, 's')).task_id == first->task_id,
        "Failure changed committed operation lookup");
    require(store.start_scoped(next_operation, next_scope).task_id == prefix + "000005", "Failed preparation consumed task identity");
  }
}

enum class mutation { running, progress, timeout, cancel_request, complete, cancel_confirm };
bool apply_mutation(task_store& store, const std::string& id, mutation which) {
  switch (which) {
    case mutation::running: return store.mark_running(id);
    case mutation::progress: return store.update_progress(id, 88, std::string(4096, 'n'));
    case mutation::timeout: return store.observe_timeout(id, std::string(4096, 'n'));
    case mutation::cancel_request: return store.request_cancel(id);
    case mutation::complete: return store.complete(id, result_kind::success, std::string(4096, 'n'));
    case mutation::cancel_confirm: return store.confirm_cancelled(id, std::string(4096, 'n'));
  }
  return false;
}
void message_result_candidates_roll_back() {
  const std::string prefix(256, 'p');
  for (const auto which : {mutation::running, mutation::progress, mutation::timeout, mutation::cancel_request,
                           mutation::complete, mutation::cancel_confirm}) {
    std::size_t candidate_calls = 0;
    {
      task_store probe(prefix, dm::mode::scalar);
      const auto item = probe.start("operation");
      require(probe.update_progress(item.task_id, 7, std::string(2048, 'b')), "Mutation base");
      calls = 0; require(apply_mutation(probe, item.task_id, which), "Mutation probe"); candidate_calls = calls.load();
    }
    require(candidate_calls > 0, "Mutation char allocator path not exercised");
    for (std::size_t fail = 1; fail <= candidate_calls; ++fail) {
      task_store store(prefix, dm::mode::scalar);
      const auto item = store.start("operation");
      require(store.update_progress(item.task_id, 7, std::string(2048, 'b')), "Mutation setup");
      const auto before_snapshot = *store.query(item.task_id);
      const auto before = read_retained(store);
      calls = 0; fail_on = fail;
      bool threw = false;
      try { (void)apply_mutation(store, item.task_id, which); } catch (const std::bad_alloc&) { threw = true; }
      fail_on = 0;
      require(threw, "Mutation nth allocation did not fail");
      const auto after_snapshot = *store.query(item.task_id);
      const auto after = read_retained(store);
      require(after_snapshot.task_id == before_snapshot.task_id && after_snapshot.operation_id == before_snapshot.operation_id &&
          after_snapshot.state == before_snapshot.state && after_snapshot.progress == before_snapshot.progress &&
          after_snapshot.sequence == before_snapshot.sequence && after_snapshot.message == before_snapshot.message &&
          after_snapshot.result.kind == before_snapshot.result.kind && after_snapshot.result.task_id == before_snapshot.result.task_id &&
          after_snapshot.result.message == before_snapshot.result.message, "Failed candidate partially published business state");
      require_refunded(before.metadata.map_requests, after.metadata.map_requests);
      require_refunded(before.strings, after.strings); require_refunded(before.aggregate, after.aggregate);
      require(after.strings.failed_allocations_total == before.strings.failed_allocations_total + 1 &&
          after.aggregate.failed_allocations_total == before.aggregate.failed_allocations_total + 1,
          "Failed candidate lost char/aggregate failure count");
      require(store.update_progress(item.task_id, 8) && store.query(item.task_id)->sequence == before_snapshot.sequence + 1,
          "Failed candidate consumed sequence or changed active lookup");
    }
  }
}

void busy_and_actual_final_refund() {
  retirement_count = 0;
  {
    task_store store(std::string(256, 'p'), dm::mode::scalar);
    (void)store.start(std::string(256, 'e'));
    { std::lock_guard<std::mutex> lock(allocation_mutex); allocation_entered = allocation_released = false; }
    block_next = true;
    std::exception_ptr error;
    std::thread worker([&] { try { (void)store.start("held"); } catch (...) { error = std::current_exception(); } });
    { std::unique_lock<std::mutex> lock(allocation_mutex); allocation_changed.wait(lock, [] { return allocation_entered; }); }
    dm::task_store_storage value;
    const auto state = store.read_storage_metrics(value);
    retained_storage retained;
    const auto retained_state = store.read_retained_string_storage(retained.metadata, retained.strings, retained.aggregate);
    dm::task_store_private_storage private_storage;
    const auto private_state = store.read_private_storage_metrics(private_storage);
    { std::lock_guard<std::mutex> lock(allocation_mutex); allocation_released = true; }
    allocation_changed.notify_all(); worker.join();
    if (error) std::rethrow_exception(error);
    require(state == dm::status::busy && value.coverage_mask == 0 && !value.map_requests.current_bytes_exact,
        "Busy observation pretended exact storage");
    require(retained_state == dm::status::busy && retained.metadata.coverage_mask == 0 &&
        !retained.strings.current_bytes_exact && !retained.aggregate.current_bytes_exact &&
        retained.strings.current_bytes == 0 && retained.aggregate.current_bytes == 0,
        "Busy new getter pretended observed zero bytes");
    require(private_state == dm::status::busy && private_storage.observation.state == dm::status::busy &&
        private_storage.coverage_mask == 0 && !private_storage.map_requests.current_bytes_exact &&
        !private_storage.private_string_requests.current_bytes_exact && !private_storage.aggregate_requests.current_bytes_exact,
        "Independent DTO turned busy into observed exact zeros");
  }
  require(!retired_saturated && retired_totals.current_bytes == 0 && retired_totals.current_allocations == 0 &&
      retired_totals.allocations_total == retired_totals.deallocations_total &&
      retired_totals.allocated_bytes_total == retired_totals.deallocated_bytes_total,
      "Actual aggregate deallocation did not precede owner retirement");
  require(retirement_count == 3 && retired_domains[2].allocated_bytes_total ==
      retired_domains[0].allocated_bytes_total + retired_domains[1].allocated_bytes_total,
      "Owner retirement did not include both domains");
  for (const auto& domain : retired_domains)
    require(domain.current_bytes == 0 && domain.current_allocations == 0 && domain.current_bytes_exact &&
        domain.allocations_total == domain.deallocations_total && domain.allocated_bytes_total == domain.deallocated_bytes_total,
        "A domain retired before its actual buffer free");
}

void sticky_current_saturation_and_invalid_underflow() {
  {
    task_store store("task-", dm::mode::scalar);
    saturate_next = true;
    const auto item = store.start("saturated");
    retained_storage value;
    require(store.read_retained_string_storage(value.metadata, value.strings, value.aggregate) == dm::status::ready &&
        value.metadata.observation.counter_saturated && !value.aggregate.current_bytes_exact &&
        value.aggregate.current_bytes == (std::numeric_limits<std::uint64_t>::max)(),
        "Injected saturation lost unknown state");
    dm::task_store_private_storage independent;
    require(store.read_private_storage_metrics(independent) == dm::status::ready && independent.coverage_mask == 7 &&
        independent.gap_mask == 31 && independent.observation.counter_saturated &&
        !independent.aggregate_requests.current_bytes_exact &&
        independent.aggregate_requests.current_bytes == (std::numeric_limits<std::uint64_t>::max)(),
        "Independent DTO erased sticky aggregate saturation");
    const auto old = read(store);
    require(old.map_requests.current_bytes_exact, "Aggregate-only saturation changed old map scope");
    require(store.complete(item.task_id, result_kind::success) && store.erase(item.task_id), "Saturation changed normal data semantics");
    require(store.read_retained_string_storage(value.metadata, value.strings, value.aggregate) == dm::status::ready &&
        value.metadata.observation.counter_saturated && value.aggregate.current_bytes == (std::numeric_limits<std::uint64_t>::max)(),
        "Refund manufactured finite saturated current");
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
  require(store.complete(item.task_id, result_kind::success, std::string(4096, 't')), "Mismatch terminal setup");
  require(last_allocation_ledger != nullptr, "Actual aggregate allocator ledger absent");
  last_allocation_ledger->totals.current_bytes = 0; // Inject a mismatch only in the actual aggregate ledger.
  require(store.erase(item.task_id), "Mismatch changed data operations");
  retained_storage value;
  require(store.read_retained_string_storage(value.metadata, value.strings, value.aggregate) == dm::status::unavailable &&
      value.metadata.observation.counter_saturated && !value.aggregate.current_bytes_exact &&
      value.aggregate.current_bytes == (std::numeric_limits<std::uint64_t>::max)(),
      "Unavailable mismatch was replaced with an exact zero");
  dm::task_store_private_storage independent;
  require(store.read_private_storage_metrics(independent) == dm::status::unavailable && independent.coverage_mask == 7 &&
      independent.gap_mask == 31 && independent.observation.counter_saturated &&
      !independent.aggregate_requests.current_bytes_exact &&
      independent.aggregate_requests.current_bytes == (std::numeric_limits<std::uint64_t>::max)(),
      "Independent DTO erased unavailable accounting data");
  require(read(store).map_requests.current_bytes_exact, "Aggregate mismatch hid old exact map requests");
}
}
#endif

int main() {
  try {
    public_contract_and_disabled(); retained_strings_and_prepare_peak(); owned_bytes_lookup_and_terminal_retention();
    oldest_terminal_eviction(); serialization_is_independent(); private_serialization_scope_and_status();
#ifndef SOUI_TASK_STORE_STORAGE_TEST_ONLY
    service_and_facade_private_storage();
#endif
#ifdef SOUI_TASK_STORE_STORAGE_TEST_ONLY
    nth_allocator_failure_rolls_back(); message_result_candidates_roll_back(); busy_and_actual_final_refund();
    sticky_current_saturation_and_invalid_underflow();
#endif
    std::cout << "TaskStore storage observation contract passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
