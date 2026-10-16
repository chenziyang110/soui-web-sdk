#include <soui/runtime/core/task/task_store.h>
#include <soui/runtime/services/task/task_service.h>
#include <soui/runtime/services/permissions/permission_store.h>
#include <json.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if !defined(SOUI_TASK_STORE_STORAGE_TEST_ONLY) || !defined(SOUI_TASK_TERMINAL_QUOTA_TEST_ONLY)
#error "This target must instrument the actual candidate Store and service sources"
#endif
namespace dm = soui::sdk::diagnostics;
using namespace soui::runtime;
using json = nlohmann::json;
using namespace std::chrono_literals;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct fault_control { bool record{false}; std::size_t fail_nth{0}, calls{0}; };
thread_local fault_control fault;
std::atomic<std::size_t> recorded_calls{0};
std::atomic<std::uint64_t> quota_peak{0}, retired_pools{0}, invalid_retirements{0};
void arm(std::size_t nth) { fault = {true, nth, 0}; recorded_calls = 0; }
void disarm() { fault = {}; }
void peak(std::uint64_t value) noexcept {
  auto old = quota_peak.load();
  while (old < value && !quota_peak.compare_exchange_weak(old, value)) {}
}
class gate {
 public:
  void arrive() {
    std::unique_lock<std::mutex> lock(mutex_); entered_ = true; changed_.notify_all();
    require(changed_.wait_for(lock, 5s, [this] { return open_; }), "handler gate deadline");
  }
  void wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    require(changed_.wait_for(lock, 5s, [this] { return entered_; }), "handler was not entered");
  }
  void release() noexcept { std::lock_guard<std::mutex> lock(mutex_); open_ = true; changed_.notify_all(); }
 private:
  std::mutex mutex_; std::condition_variable changed_; bool entered_{false}, open_{false};
};
struct release_guard { gate& value; ~release_guard() { value.release(); } };
}

namespace soui::runtime::detail::task_store_storage_test {
void before_allocate(task_store_allocation_ledger&, std::size_t) {
  if (!fault.record) return;
  ++fault.calls; recorded_calls = fault.calls;
  if (fault.fail_nth == fault.calls) throw std::bad_alloc{};
}
void quota_before_allocate(const task_store_request_quota& quota) noexcept { peak(quota.live_requested_bytes()); }
void quota_retired(const task_store_request_quota& quota) noexcept {
  ++retired_pools;
  if (!quota.valid() || quota.live_requested_bytes() != 0) ++invalid_retirements;
}
void ledger_retired(const task_store_allocation_ledger&) noexcept {}
}

namespace soui::runtime::detail {
struct task_terminal_quota_test_access {
  static task_snapshot start_funded(task_store& store) {
    auto prepared = store.prepare_start_scoped("terminal-operation", "terminal-scope", true);
    auto value = prepared->snapshot;
    store.commit_start_scoped(*prepared);
    return value;
  }
  static bool complete(task_store& store, const std::string& id, result_kind kind, std::string_view message) {
    task_store::prepared_completion prepared(store.mutex_);
    store.prepare_complete(prepared, id, kind, message);
    if (!prepared.publish) return false;
    store.commit_complete(prepared);
    return true;
  }
  static bool fallback(task_store& store, const std::string& id) { return store.commit_funded_failure(id); }
  static std::uint64_t live(task_store& store) {
    std::lock_guard<std::mutex> lock(store.mutex_); return store.allocation_owner_.quota.live_requested_bytes();
  }
  static bool funded(task_store& store, const std::string& id) {
    std::lock_guard<std::mutex> lock(store.mutex_);
    const auto found = store.tasks_.find(task_store::retained_string::lookup(id));
    if (found == store.tasks_.end() || !found->second.failure_reserve) return false;
    const auto& reserved = *found->second.failure_reserve;
    return reserved.message.view() == reserved.text && reserved.result.kind == result_kind::failure &&
        reserved.result.task_id.view() == id && reserved.result.message.view() == reserved.text &&
        reserved.message.capacity() >= reserved.text.size() && reserved.result.message.capacity() >= reserved.text.size();
  }
  static bool admission_unpublished(task_store& store) {
    std::lock_guard<std::mutex> lock(store.mutex_);
    return store.tasks_.empty() && store.operation_to_task_.empty() && store.operation_to_scope_.empty() &&
        store.next_task_number_ == 1 && store.next_sequence_ == 1;
  }
  static void make_diagnostics_unavailable(task_store& store) {
    std::lock_guard<std::mutex> lock(store.mutex_);
    store.allocation_owner_.maps.invalid = true;
    store.allocation_owner_.strings.saturated = true;
    store.allocation_owner_.aggregate.saturated = true;
  }
};
}
namespace {
using access = detail::task_terminal_quota_test_access;
constexpr std::string_view failure_text{"Native task terminal storage publication failed."};
void funded_admission_allocation_failures() {
  std::size_t requests = 0;
  {
    task_store probe(std::string(96, 'p'), dm::mode::disabled); arm(0);
    const auto item = access::start_funded(probe); requests = fault.calls; disarm();
    require(access::funded(probe, item.task_id) && requests > 0, "actual funded admission probe missing");
  }
  for (const auto mode : {dm::mode::disabled, dm::mode::scalar}) for (std::size_t nth = 1; nth <= requests; ++nth) {
    task_store store(std::string(96, 'p'), mode); const auto initial = access::live(store); bool rejected = false; arm(nth);
    try { (void)access::start_funded(store); } catch (const std::bad_alloc&) { rejected = true; }
    disarm(); require(rejected && access::admission_unpublished(store) && access::live(store) == initial, "unfunded/partially funded admission published or leaked");
    const auto recovered = access::start_funded(store);
    require(recovered.task_id == std::string(96, 'p') + "000001" && recovered.sequence == 1 && access::funded(store, recovered.task_id), "rejected admission consumed identity/funding");
  }
}
std::size_t completion_request_count() {
  task_store store(std::string(96, 'p'), dm::mode::disabled);
  const auto item = access::start_funded(store);
  require(access::funded(store, item.task_id), "admission did not fund actual fallback");
  arm(0); require(access::complete(store, item.task_id, result_kind::success, std::string(4096, 'm')), "probe complete");
  const auto calls = fault.calls; disarm();
  require(calls >= 3, "long id/message must exercise each actual candidate allocation");
  return calls;
}
void store_allocation_failures(std::size_t requests) {
  for (const auto mode : {dm::mode::disabled, dm::mode::scalar}) for (std::size_t nth = 1; nth <= requests; ++nth) {
    task_store store(std::string(96, 'p'), mode);
    const auto item = access::start_funded(store);
    require(store.mark_running(item.task_id) && store.update_progress(item.task_id, 25, "held"), "running setup");
    const auto before = *store.query(item.task_id); const auto live_before = access::live(store);
    bool rejected = false; arm(nth);
    try { (void)access::complete(store, item.task_id, result_kind::success, std::string(4096, 'm')); }
    catch (const std::bad_alloc&) { rejected = true; }
    disarm(); const auto unchanged = *store.query(item.task_id);
    require(rejected && unchanged.state == before.state && unchanged.sequence == before.sequence &&
        unchanged.message == before.message && unchanged.result.kind == before.result.kind &&
        unchanged.result.task_id == before.result.task_id && unchanged.result.message == before.result.message &&
        access::live(store) == live_before && access::funded(store, item.task_id), "failed preparation changed snapshot/funding or leaked credit");
    arm(1); require(access::fallback(store, item.task_id) && fault.calls == 0, "fallback allocated or failed"); disarm();
    const auto terminal = *store.query(item.task_id);
    require(terminal.state == task_state::failed && terminal.result.kind == result_kind::failure &&
        terminal.message == failure_text && terminal.result.message == failure_text && terminal.result.task_id == item.task_id &&
        terminal.sequence == before.sequence + 1 && terminal.progress == 25 && !access::funded(store, item.task_id), "fallback forged success/cancel or broke result invariant");
    const auto retained = access::live(store);
    require(!access::fallback(store, item.task_id) && access::live(store) == retained, "fallback repeated/refunded live buffers");
    require(store.erase(item.task_id) && access::live(store) < retained, "retained terminal buffers did not physically retire");
  }
}
void store_real_quota_failure() {
  for (const auto mode : {dm::mode::disabled, dm::mode::scalar}) {
    std::uint64_t admission_peak = 0;
    {
      task_store probe(std::string(96, 'p'), mode); quota_peak = 0;
      const auto item = access::start_funded(probe); admission_peak = quota_peak;
      require(admission_peak >= access::live(probe) && access::funded(probe, item.task_id), "actual quota probe");
    }
    task_store store(std::string(96, 'p'), mode, task_store_request_quota_policy{admission_peak});
    const auto item = access::start_funded(store); const auto before = *store.query(item.task_id);
    access::make_diagnostics_unavailable(store);
    bool refused = false;
    try { (void)access::complete(store, item.task_id, result_kind::task_cancelled, std::string(4096, 'm')); }
    catch (const task_store_request_quota_exceeded&) { refused = true; }
    require(refused && store.query(item.task_id)->sequence == before.sequence && access::funded(store, item.task_id), "actual quota did not reject transactionally");
    arm(1); require(access::fallback(store, item.task_id) && fault.calls == 0, "quota fallback allocated"); disarm();
    require(store.query(item.task_id)->state == task_state::failed, "quota rejection forged cancelled");
  }
}
struct fixture {
  fixture(dm::mode mode, std::optional<task_store_request_quota_policy> quota = std::nullopt)
      : service(mode, quota) {
    token = permissions.issue("terminal-session", "app://terminal-tests", {"task.start", "task.query", "task.cancel"});
    scope = permissions.resolve("terminal-session", "app://terminal-tests", token, "task.start").scope;
    require(scope != nullptr, "native authorization missing");
  }
  bridge_request request(const char* method, const json& parameters) const {
    bridge_request value; value.request_id = "terminal-request"; value.session_id = "terminal-session";
    value.origin = "app://terminal-tests"; value.token = token; value.authorization = scope;
    value.method = method; value.params_json = parameters.dump(); return value;
  }
  json start() { return json::parse(service.start(request("task.start", {{"kind", "work"}, {"operationId", "terminal-op"}, {"input", json::object()}}))); }
  json query(const std::string& id) { return json::parse(service.query(request("task.query", {{"taskId", id}}))); }
  json terminal(const std::string& id) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    for (;;) { auto value = query(id); const auto state = value.value("state", "");
      if (state == "succeeded" || state == "failed" || state == "cancelled" || state == "partial") return value;
      require(std::chrono::steady_clock::now() < deadline, "no actual terminal state"); std::this_thread::yield(); }
  }
  permission_store permissions; std::string token; std::shared_ptr<const authorization_scope> scope;
  task_service service; // Joins while permissions, callback gates and captures live.
};
struct service_probe { std::uint64_t before_finish_peak; std::size_t completion_requests; };
service_probe probe_service(dm::mode mode, bool cancelled) {
  gate running; std::atomic<unsigned> invocations{0}; fixture f(mode); release_guard release{running};
  require(f.service.register_handler("work", [&](const auto&, const task_control& control) {
    task_outcome value{cancelled ? result_kind::task_cancelled : result_kind::success, R"({"value":42})", std::string(4096, 'm')};
    ++invocations; running.arrive(); if (cancelled) require(control.cancellation_requested(), "unconfirmed cancellation");
    arm(0); return value;
  }), "probe handler");
  quota_peak = 0; const auto receipt = f.start(); require(receipt.value("accepted", false), "probe admission");
  const auto id = receipt.at("taskId").get<std::string>(); running.wait();
  if (cancelled) require(json::parse(f.service.cancel(f.request("task.cancel", {{"taskId", id}}))).at("state") == "cancel-pending", "actual cancellation receipt");
  const auto admission_peak = quota_peak.load(); running.release();
  const auto result = f.terminal(id);
  require(result.at("state") == (cancelled ? "cancelled" : "succeeded") && invocations == 1, "probe handler outcome");
  return {admission_peak, recorded_calls.load()};
}
void service_failure(dm::mode mode, bool cancelled, std::optional<std::uint64_t> quota, std::size_t fail_nth) {
  gate running; std::atomic<unsigned> invocations{0};
  fixture f(mode, quota ? std::optional<task_store_request_quota_policy>{task_store_request_quota_policy{*quota}} : std::nullopt);
  release_guard release{running};
  require(f.service.register_handler("work", [&](const auto&, const task_control& control) {
    task_outcome value{cancelled ? result_kind::task_cancelled : result_kind::success, R"({"value":42})", std::string(4096, 'm')};
    ++invocations; running.arrive(); if (cancelled) require(control.cancellation_requested(), "handler did not observe actual cancel");
    arm(fail_nth); return value;
  }), "fault handler");
  const auto receipt = f.start(); require(receipt.value("accepted", false), "funded service admission");
  const auto id = receipt.at("taskId").get<std::string>(); running.wait();
  if (cancelled) require(json::parse(f.service.cancel(f.request("task.cancel", {{"taskId", id}}))).at("state") == "cancel-pending", "cancel confirmation setup");
  running.release(); const auto result = f.terminal(id);
  require(result.at("state") == "failed" && result.at("terminalStorageError") == (quota ? "E_TASK_STORE_QUOTA" : "E_TERMINAL_ALLOCATION") &&
      result.at("handlerOutcome") == (cancelled ? "task-cancelled" : "success") && result.at("result") == json{{"value", 42}} &&
      result.at("message").get<std::string>() == failure_text && invocations == 1, "storage failure lost outcome/result or forged success/cancel");
  if (!quota) require(recorded_calls == fail_nth, "failure terminal made another allocation attempt");
  const auto duplicate = f.start(); require(duplicate.at("taskId") == id && duplicate.value("duplicate", false) && invocations == 1, "failed publication replayed business");
  if (mode == dm::mode::scalar) {
    dm::tasks observed; const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (f.service.read_runtime_metrics(observed) != dm::status::ready) {
      require(std::chrono::steady_clock::now() < deadline, "service metrics did not become actually readable");
      std::this_thread::yield();
    }
    require(observed.inflight == 0 && observed.active_handlers == 0 &&
        observed.owner_entries == 1 && observed.store_entries == 1 && observed.result_cache_entries == 1, "service and Store did not commit terminal together");
  }
}
void service_thrown_handler_storage_failure(dm::mode mode) {
  gate running; std::atomic<unsigned> invocations{0}; fixture f(mode); release_guard release{running};
  require(f.service.register_handler("work", [&](const auto&, const task_control&) -> task_outcome {
    ++invocations; running.arrive(); arm(1);
    throw std::runtime_error("original native handler failure");
  }), "throwing handler");
  const auto receipt = f.start(); require(receipt.value("accepted", false), "throwing-handler admission");
  const auto id = receipt.at("taskId").get<std::string>(); running.wait(); running.release();
  const auto result = f.terminal(id);
  require(result.at("state") == "failed" && result.at("terminalStorageError") == "E_TERMINAL_ALLOCATION" &&
      result.at("handlerError") == "E_NATIVE_HANDLER" && result.at("handlerOutcome") == "unknown" &&
      result.at("result") == json::object() && result.at("message").get<std::string>() == failure_text &&
      recorded_calls == 1 && invocations == 1, "handler throw and publication failure were conflated or retried");
  const auto duplicate = f.start();
  require(duplicate.at("taskId") == id && duplicate.value("duplicate", false) && invocations == 1,
      "throwing business callback was replayed after storage refusal");
}
void quota_admission_rejects() {
  for (const auto mode : {dm::mode::disabled, dm::mode::scalar}) {
    try {
      std::atomic<unsigned> calls{0}; fixture f(mode, task_store_request_quota_policy{0});
      require(f.service.register_handler("work", [&](const auto&, const auto&) { ++calls; return task_outcome{}; }), "zero handler");
      const auto result = f.start(); require(!result.value("accepted", false) && result.contains("error") && calls == 0, "unfunded task published");
      require(json::parse(f.service.query(f.request("task.query", {{"operationId", "terminal-op"}}))).at("error") == "E_TASK_NOT_FOUND", "quota rejection published operation");
    } catch (const task_store_request_quota_exceeded&) { /* STL construction itself may require positive private bytes. */ }
  }
}
}
int main() {
  try {
    const auto initial_retired = retired_pools.load();
    funded_admission_allocation_failures(); store_allocation_failures(completion_request_count()); store_real_quota_failure(); quota_admission_rejects();
    for (const auto mode : {dm::mode::disabled, dm::mode::scalar}) for (const bool cancelled : {false, true}) {
      const auto measured = probe_service(mode, cancelled);
      require(measured.completion_requests > 0 && measured.before_finish_peak > 0, "actual service calibration missing");
      service_failure(mode, cancelled, measured.before_finish_peak, 0);
      for (std::size_t nth = 1; nth <= measured.completion_requests; ++nth) service_failure(mode, cancelled, std::nullopt, nth);
    }
    for (const auto mode : {dm::mode::disabled, dm::mode::scalar}) service_thrown_handler_storage_failure(mode);
    require(retired_pools > initial_retired && invalid_retirements == 0, "quota retired before all actual frees or final credit invalid");
    std::cout << "Actual funded terminal Store/service tests passed; private requested-byte scope only\n"; return 0;
  } catch (const std::exception& error) { disarm(); std::cerr << error.what() << '\n'; return 1; }
}
