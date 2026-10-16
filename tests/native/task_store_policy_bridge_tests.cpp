// Link the actual production Soui::Sdk. Never compile copied
// Store/service/json_bridge.cpp into this target.
#include <soui/json_bridge.h>
#include <json.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

namespace probe {
using namespace std::chrono_literals;
struct gate {
  std::mutex mutex; std::condition_variable changed; bool entered{false}, open{false};
  void arrive() {
    std::unique_lock<std::mutex> lock(mutex); entered = true; changed.notify_all();
    if (!changed.wait_for(lock, 5s, [this] { return open; })) throw std::runtime_error("bounded gate deadline");
  }
  bool wait() { std::unique_lock<std::mutex> lock(mutex); return changed.wait_for(lock, 5s, [this] { return entered; }); }
  void release() noexcept { std::lock_guard<std::mutex> lock(mutex); open = true; changed.notify_all(); }
};
struct allocation_scope { bool enabled{false}; std::size_t calls{0}, block_nth{0}; gate* blocked{nullptr}; std::size_t fail_nth{0}; };
thread_local allocation_scope allocations;
thread_local bool failure_hit = false;
void observe() {
  if (!allocations.enabled) return;
  ++allocations.calls;
  if (allocations.fail_nth == allocations.calls) { failure_hit = true; allocations.enabled = false; throw std::bad_alloc{}; }
  if (allocations.block_nth == allocations.calls) {
    allocations.enabled = false; // The test gate itself is outside this allocation trace.
    allocations.blocked->arrive();
  }
}
}
void* operator new(std::size_t bytes) { probe::observe(); if (auto* p = std::malloc(bytes ? bytes : 1)) return p; throw std::bad_alloc{}; }
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
using json = nlohmann::json;
using namespace soui::runtime;
namespace dm = soui::sdk::diagnostics;
using snapshot = task_store_request_quota_snapshot;
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
std::chrono::steady_clock::time_point test_deadline;
void check_deadline() {
  require(std::chrono::steady_clock::now() < test_deadline, "Bounded public regression deadline");
}
json call(soui::sdk::json_bridge& bridge, const json& auth, const char* method, json params) {
  return json::parse(bridge.handle_json(json{{"requestId", "quota-probe"}, {"sessionId", auth.at("sessionId")},
      {"origin", auth.at("origin")}, {"token", auth.at("token")}, {"method", method},
      {"params", std::move(params)}, {"timeoutMs", 10000}}.dump()));
}
bool busy_receipt_empty(const snapshot& out) {
  return out.status == task_store_request_quota_status::busy &&
      !out.max_live_requested_bytes && !out.owner_epoch && !out.owner_epoch_valid &&
      !out.charged_live_requested_bytes && !out.charged_peak_requested_bytes &&
      !out.successful_live_requested_bytes && !out.successful_peak_requested_bytes &&
      !out.denied_requests && !out.denied_requests_exact && !out.credit_valid;
}
snapshot sample(soui::sdk::json_bridge& bridge) {
  snapshot out;
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  for (;;) {
    const auto state=bridge.read_task_store_request_quota(out);
    if (state==task_store_request_quota_status::available) {
      require(out.owner_epoch_valid && out.credit_valid && out.denied_requests_exact,
              "Quota sample is invalid or saturated");
      return out;
    }
    require(state==task_store_request_quota_status::busy && busy_receipt_empty(out),
            "Quota read returned invalid/unavailable state or leaked busy facts");
    require(std::chrono::steady_clock::now()<deadline,"Quota snapshot busy deadline");
    std::this_thread::yield();
  }
}
dm::bridge_runtime runtime_sample(soui::sdk::json_bridge& bridge) {
  dm::bridge_runtime out;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  for (;;) {
    const auto state = bridge.read_runtime_metrics(out);
    if (state != dm::status::busy) {
      require(state == dm::status::ready, "Runtime metrics unavailable");
      return out;
    }
    require(std::chrono::steady_clock::now() < deadline, "Runtime metrics busy deadline");
    std::this_thread::yield();
  }
}
struct fixture {
  // Contexts precede the Bridge and outlive its actual destructor/worker join.
  std::atomic<unsigned> calls{0}; probe::gate running;
  soui::sdk::json_bridge bridge; json auth;
  explicit fixture(dm::mode mode, std::optional<task_store_request_quota_policy> quota = std::nullopt)
      : bridge("app://quota-source-probe", mode, quota) {
    require(bridge.register_task_handler("quota", [this](const task_invocation&, const task_control&) {
      ++calls; running.arrive(); return task_outcome{};
    }), "Handler registration");
    const auto boot = json::parse(bridge.handle_json(R"({"requestId":"bootstrap","sessionId":"","origin":"","token":"","method":"bridge.bootstrap","params":{"protocol":"soui.bridge/1"}})"));
    require(boot.at("ok").get<bool>(), "Bootstrap"); auth = boot.at("result");
  }
  ~fixture() { running.release(); } // Bridge member destruction must then naturally join.
  json start(std::size_t bytes, const char* operation = "operation") {
    return call(bridge, auth, "task.start", {{"kind", "quota"}, {"operationId", operation},
        {"input", {{"payload", std::string(bytes, 'x')}}}});
  }
};
void noalloc_read(fixture& f) {
  snapshot out;
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  for (;;) {
    probe::allocations = {true, 0, 0, nullptr};
    const auto state = f.bridge.read_task_store_request_quota(out);
    const auto calls = probe::allocations.calls; probe::allocations = {};
    require(calls==0,"Quota getter allocated on its calling thread");
    if(state==task_store_request_quota_status::available) return;
    require(state==task_store_request_quota_status::busy && busy_receipt_empty(out),
            "Quota no-allocation read returned invalid/unavailable state or leaked busy facts");
    require(std::chrono::steady_clock::now()<deadline,"Quota no-allocation read busy deadline");
    std::this_thread::yield();
  }
}
std::uint64_t measure(dm::mode mode) {
  fixture f(mode); noalloc_read(f);
  require(!sample(f.bridge).max_live_requested_bytes, "Explicit null Native policy must remain unconfigured");
  const auto receipt = f.start(4096); require(receipt.at("ok").get<bool>(), "Baseline start");
  require(f.running.wait(), "Baseline handler");
  const auto out = sample(f.bridge);
  require(out.charged_live_requested_bytes == out.successful_live_requested_bytes &&
      out.successful_peak_requested_bytes <= out.charged_peak_requested_bytes,
      "Reserved credit was reported as successful allocation");
  return out.charged_peak_requested_bytes; // Fixture-only measured boundary, never production calibration.
}
void boundary(dm::mode mode) {
  const auto cap = measure(mode); require(cap > 1, "Missing actual fixture peak");
  {
    fixture exact(mode, task_store_request_quota_policy{cap}); const auto before = sample(exact.bridge);
    require(before.max_live_requested_bytes == cap, "Immutable ctor policy did not reach actual pool");
    const auto first = exact.start(4096); require(first.at("ok").get<bool>(), "Exact fixture boundary refused");
    require(exact.running.wait(), "Exact-boundary handler");
    const auto duplicate = exact.start(4096);
    require(duplicate.at("result").at("duplicate").get<bool>() && exact.calls == 1, "Duplicate re-executed handler");
  }
  {
    fixture denied(mode, task_store_request_quota_policy{cap - 1}); const auto before = sample(denied.bridge);
    const auto rejected = denied.start(4096);
    require(!rejected.at("ok").get<bool>() && rejected.at("error").at("code") == "E_TASK_STORE_QUOTA" &&
        !rejected.contains("result") && !rejected.contains("taskId") && denied.calls == 0,
        "Quota refusal published accepted ID or ran handler");
    const auto after = sample(denied.bridge);
    require(after.denied_requests == before.denied_requests + 1 &&
        after.charged_live_requested_bytes == before.charged_live_requested_bytes &&
        after.successful_live_requested_bytes == before.successful_live_requested_bytes,
        "Admission rollback lost or returned successful credit early");
    const auto absent = call(denied.bridge, denied.auth, "task.query", {{"operationId", "operation"}});
    require(!absent.at("ok").get<bool>() && absent.at("error").at("code") == "E_TASK_NOT_FOUND",
        "Rejected operation was published");
    if (mode == dm::mode::scalar) {
      const auto old = runtime_sample(denied.bridge);
      require(old.task_service.worker_slots > 0 &&
          old.task_service.inflight == 0 && old.task_service.queued == 0 && old.task_service.owner_entries == 0 &&
          old.task_service.operation_entries == 0 && old.task_service.committed_starts_total == 0,
          "Cold refusal must preserve idle workers but no commit/queue/operation");
    } else {
      dm::task_store_storage old;
      require(denied.bridge.read_task_store_storage_metrics(old) == dm::status::disabled,
          "New quota snapshot manufactured old disabled DTO evidence");
    }
    // Human/native caller explicitly retries with a smaller legal payload; no SDK automatic retry.
    const auto accepted = denied.start(1);
    require(accepted.at("ok").get<bool>() && accepted.at("result").at("duplicate") == false,
            "Rejected operation identity could not later be used legally");
    require(denied.running.wait(), "Manual reuse handler"); noalloc_read(denied);
  }
  {
    fixture a(mode), b(mode); const auto a0 = sample(a.bridge), b0 = sample(b.bridge);
    require(a0.owner_epoch != b0.owner_epoch, "Different Bridge owners reused a pool identity");
    require(a.start(64).at("ok").get<bool>() && a.running.wait(), "Owner isolation start");
    const auto b1 = sample(b.bridge);
    require(b1.charged_live_requested_bytes == b0.charged_live_requested_bytes &&
        b1.charged_peak_requested_bytes == b0.charged_peak_requested_bytes && b1.denied_requests == b0.denied_requests,
        "Other Bridge altered this pool");
  }
  try {
    fixture zero(mode, task_store_request_quota_policy{0});
    const auto s = sample(zero.bridge); require(s.max_live_requested_bytes == 0, "Explicit zero became null");
    require(!zero.start(1).at("ok").get<bool>() && zero.calls == 0, "Zero allowed positive private storage");
  } catch (const task_store_request_quota_exceeded&) {
    // Real MSVC STL may require positive empty map buckets at construction.
    // No object exists after this failure and no post-destruction receipt is read.
  }
}
// Prepare every caller-owned JSON object before arming the allocation fault.
std::string prepared_wire(const fixture& f) {
  return json{{"requestId", "quota-probe"}, {"sessionId", f.auth.at("sessionId")},
      {"origin", f.auth.at("origin")}, {"token", f.auth.at("token")}, {"method", "task.start"},
      {"params", {{"kind", "quota"}, {"operationId", "operation"},
          {"input", {{"payload", std::string(32, 'x')}}}}}, {"timeoutMs", 10000}}.dump();
}
struct trace_result { std::size_t calls{0}; bool failure_hit{false}; };
std::string traced_handle(fixture& f, const std::string& wire,
    probe::allocation_scope scope, trace_result& observed) {
  struct disarm {
    trace_result& result;
    ~disarm() noexcept {
      result.calls = probe::allocations.calls;
      result.failure_hit = probe::failure_hit;
      probe::allocations = {};
    }
  } reset{observed};
  probe::failure_hit = false;
  probe::allocations = scope;
  return f.bridge.handle_json(wire); // Only the real public SDK call is injected.
}
void busy_read() {
  std::size_t count;
  { fixture baseline(dm::mode::disabled); const auto wire = prepared_wire(baseline); trace_result observed;
    (void)traced_handle(baseline, wire, {true, 0, 0, nullptr}, observed); count = observed.calls; }
  require(count > 0 && count <= 8192, "Bounded actual direct-call allocation trace");
  bool observed_busy = false;
  for (std::size_t nth = 1; nth <= count && !observed_busy; ++nth) {
    check_deadline();
    fixture f(dm::mode::disabled); const auto wire = prepared_wire(f);
    probe::gate blocked; std::exception_ptr failure; trace_result observed;
    std::thread caller([&] { try { (void)traced_handle(f, wire, {true, 0, nth, &blocked}, observed); }
      catch (...) { failure = std::current_exception(); } });
    const auto entered = blocked.wait(); snapshot out; bool busy_cleared = true;
    if (entered) {
      // A prior successful receipt must not survive a later busy observation.
      out.status=task_store_request_quota_status::available;
      out.max_live_requested_bytes=1234; out.owner_epoch=123; out.owner_epoch_valid=true;
      out.charged_live_requested_bytes=10; out.charged_peak_requested_bytes=20;
      out.successful_live_requested_bytes=10; out.successful_peak_requested_bytes=20;
      out.denied_requests=1; out.denied_requests_exact=true; out.credit_valid=true;
      probe::allocations={true,0,0,nullptr};
      const auto state = f.bridge.read_task_store_request_quota(out);
      const auto read_allocations=probe::allocations.calls; probe::allocations={};
      busy_cleared=read_allocations==0 &&
          (state==task_store_request_quota_status::available || state==task_store_request_quota_status::busy);
      if (state == task_store_request_quota_status::busy) {
        observed_busy = true; busy_cleared = busy_cleared && busy_receipt_empty(out);
      }
    }
    blocked.release(); caller.join(); if (failure) std::rethrow_exception(failure);
    require(busy_cleared, "Busy sample exposed available pool facts");
  }
  require(observed_busy, "No actual busy lock envelope observed");
}
void heap_refusal_is_not_quota() {
  const auto quota = task_store_request_quota_policy{(std::numeric_limits<std::uint64_t>::max)()};
  std::size_t count;
  { fixture baseline(dm::mode::scalar, quota); const auto wire = prepared_wire(baseline); trace_result observed;
    (void)traced_handle(baseline, wire, {true, 0, 0, nullptr}, observed); count = observed.calls; }
  require(count > 0 && count <= 8192, "Bounded actual configured direct-call failure trace");
  bool preparation_failure = false;
  for (std::size_t nth = 1; nth <= count; ++nth) {
    check_deadline();
    fixture f(dm::mode::scalar, quota); const auto wire = prepared_wire(f);
    const auto before = sample(f.bridge); json reply; bool escaped = false; trace_result observed;
    std::string raw_reply;
    try { raw_reply = traced_handle(f, wire, {true, 0, 0, nullptr, nth}, observed); }
    catch (const std::bad_alloc&) { escaped = true; }
    require(!probe::allocations.enabled && observed.failure_hit,
            "Every selected direct-call allocation must be reached, then disarmed");
    if (!escaped) reply = json::parse(raw_reply); // Parsing/assertions/destruction are outside injection.
    snapshot after;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (f.bridge.read_task_store_request_quota(after) == task_store_request_quota_status::busy) {
      require(std::chrono::steady_clock::now() < deadline, "Fault sample deadline"); std::this_thread::yield();
    }
    require(after.status == task_store_request_quota_status::available &&
        after.denied_requests == before.denied_requests, "Upstream heap fault became quota denial");
    if (!escaped && !reply.value("ok", false) && reply.contains("error"))
      require(reply.at("error").value("code", "") != "E_TASK_STORE_QUOTA", "Heap failure returned quota protocol code");
    // Resolve actual operation publication before classifying any failed response.
    const auto query = call(f.bridge, f.auth, "task.query", {{"operationId", "operation"}});
    const bool missing = !query.at("ok").get<bool>() && query.at("error").at("code") == "E_TASK_NOT_FOUND";
    if (missing) {
      require(escaped || (!reply.value("ok", false) && !reply.contains("result") && !reply.contains("taskId")),
              "Unpublished operation must not claim an accepted receipt");
      const auto facts = runtime_sample(f.bridge);
      require(facts.task_service.inflight == 0 && facts.task_service.queued == 0 &&
          facts.task_service.owner_entries == 0 && facts.task_service.operation_entries == 0 &&
          facts.task_service.store_entries == 0 && facts.task_service.committed_starts_total == 0 &&
          facts.task_service.active_handlers == 0 && f.calls == 0 &&
          after.charged_live_requested_bytes == before.charged_live_requested_bytes &&
          after.successful_live_requested_bytes == before.successful_live_requested_bytes,
          "Unpublished heap refusal left candidate/commit/handler/queue storage");
      if (!escaped && !reply.value("ok", false) && reply.contains("error") &&
          reply.at("error").value("code", "") == "E_NATIVE_FAILURE") {
        preparation_failure = true;
        const auto reuse = f.start(1);
        require(reuse.at("ok").get<bool>() && !reuse.at("result").at("duplicate").get<bool>() &&
            f.running.wait() && f.calls == 1, "Native preparation failure prevented lawful one-handler reuse");
      }
    } else {
      // A response serialization failure can follow a valid SDK commit.
      if (!escaped && reply.value("ok", false))
        require(reply.at("result").at("taskId") == query.at("result").at("taskId"),
                "Accepted receipt differs from the same-operation published task");
      require(query.at("ok").get<bool>() && f.running.wait() && f.calls == 1,
              "Published operation is not a valid single actual handler");
    }
  }
  require(preparation_failure, "No real public preparation bad_alloc boundary observed");
}

}
int main() {
  try { test_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    boundary(dm::mode::disabled); boundary(dm::mode::scalar); busy_read(); heap_refusal_is_not_quota();
    std::cout << "Public Bridge TaskStore policy fixture passed; explicit policy overrides verified; production qualification is independent.\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
