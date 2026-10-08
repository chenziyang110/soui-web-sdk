#include <soui/runtime/services/permissions/permission_store.h>
#include <soui/runtime/services/task/task_service.h>
#include <json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace soui::runtime {
// TEST_ONLY deterministic entropy seam. No control is exposed on either wire.
struct permission_store_test_access {
  static void entropy(permission_store& store, bool (*source)(unsigned char*, std::size_t)) {
    store.entropy_ = source;
  }
};
}

namespace {
using namespace soui::runtime;
using json = nlohmann::json;
using namespace std::chrono_literals;
constexpr const char* session = "local-document";
constexpr const char* origin = "app://local-grants";
constexpr const char* capability = "work.shared";
constexpr const char* method = "work.first";
constexpr const char* renewal = "bridge.session.renew";
constexpr std::int64_t renew_at = 25 * 60 * 1000;
constexpr std::int64_t lifetime = 30 * 60 * 1000;
std::atomic<std::int64_t> clock_ms{0};
std::uint64_t entropy_sequence = 1;
std::shared_ptr<native_connection_binding> close_on_entropy;
bool close_hook_called = false;

static_assert(!std::is_default_constructible_v<native_connection_binding>);
static_assert(!std::is_copy_constructible_v<native_connection_binding>);
static_assert(!std::is_copy_constructible_v<authorization_scope>);

permission_store::clock_type::time_point now() {
  return permission_store::clock_type::time_point{} + std::chrono::milliseconds(clock_ms.load());
}
bool next_entropy(unsigned char* bytes, std::size_t count) {
  std::fill(bytes, bytes + count, static_cast<unsigned char>(0xA5));
  auto sequence = entropy_sequence++;
  for (std::size_t index = 0; index < count && index < sizeof(sequence); ++index) {
    bytes[index] = static_cast<unsigned char>(sequence & 0xffU);
    sequence >>= 8U;
  }
  return true;
}
bool closing_entropy(unsigned char* bytes, std::size_t count) {
  close_hook_called = true;
  close_on_entropy->close();
  return next_entropy(bytes, count);
}
bool unavailable_entropy(unsigned char*, std::size_t) { return false; }
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void denied(const checked_authorization& result) {
  require(result.result == authorization_result::denied && !result.scope,
          "Rejected authorization disclosed a usable scope");
}
void lease_error(const permission_lease& result, const char* expected = "E_UNAUTHORIZED") {
  require(!result.ok() && result.error_code == expected && result.token.empty() && result.generation == 0,
          "Rejected lease disclosed or accepted authority");
}
template<typename Call> void invalid(Call&& call) {
  try { call(); } catch (const std::invalid_argument&) { return; }
  throw std::runtime_error("Invalid native policy/binding was admitted");
}
struct fixture {
  permission_store store{&now};
  std::shared_ptr<native_connection_binding> binding;
  fixture() {
    clock_ms.store(0);
    permission_store_test_access::entropy(store, &next_entropy);
    binding = store.create_local_binding(session, origin);
  }
  issued_local_grant issue() {
    return store.issue_local(binding, {capability, renewal}, {method, renewal});
  }
};

void channel_separation_and_real_binding_identity() {
  fixture f;
  const auto local = f.issue();
  const auto page = f.store.issue(session, origin, {capability, renewal});
  require(local.scope && f.store.resolve_local(f.binding, local.token, method, capability).scope == local.scope,
          "Local issue lost the precise native cleanup owner");
  denied(f.store.resolve(session, origin, local.token, capability));
  require(f.store.check(session, origin, local.token, capability) == authorization_result::denied &&
          !f.store.authorize(session, origin, local.token, capability), "Page entry accepted a local bearer");
  lease_error(f.store.lease(session, origin, local.token));
  denied(f.store.resolve_local(f.binding, page, method, capability));
  lease_error(f.store.lease_local(f.binding, page));
  clock_ms.store(renew_at);
  lease_error(f.store.renew(session, origin, local.token, "page-copy"));
  lease_error(f.store.renew_local(f.binding, page, "local-copy"));
  require(f.store.resolve(session, origin, page, capability).scope != nullptr,
          "Channel rejection revoked an unrelated page grant");

  const auto reconnect = f.store.create_local_binding(session, origin);
  denied(f.store.resolve_local(reconnect, local.token, method, capability));
  lease_error(f.store.lease_local(reconnect, local.token));
  lease_error(f.store.renew_local(reconnect, local.token, "reconnect"));
  permission_store other{&now};
  const auto foreign = other.create_local_binding(session, origin);
  denied(f.store.resolve_local(foreign, local.token, method, capability));
  denied(other.resolve_local(f.binding, local.token, method, capability));
  invalid([&] { (void)other.issue_local(f.binding, {capability}, {method}); });
  invalid([&] { (void)f.store.issue_local(foreign, {capability}, {method}); });
  invalid([&] { (void)f.store.issue_local({}, {capability}, {method}); });
  denied(f.store.resolve_local({}, local.token, method, capability));
}

void methods_and_capabilities_are_independent() {
  fixture f;
  const auto limited = f.store.issue_local(f.binding, {capability}, {method});
  require(limited.scope->allows(capability), "Explicit capability was not issued");
  denied(f.store.resolve_local(f.binding, limited.token, "work.second", capability));
  denied(f.store.resolve_local(f.binding, limited.token, method, "work.write"));
  denied(f.store.resolve_local(f.binding, limited.token, "", capability));
  const auto cap_only = f.store.issue_local(f.binding, {capability, renewal}, {method});
  const auto method_only = f.store.issue_local(f.binding, {capability}, {method, renewal});
  for (const auto* grant : {&cap_only, &method_only}) lease_error(f.store.lease_local(f.binding, grant->token));
  clock_ms.store(renew_at);
  for (const auto* grant : {&cap_only, &method_only})
    lease_error(f.store.renew_local(f.binding, grant->token, "missing-independent-policy"));
}

void explicit_policy_and_identifier_bounds() {
  fixture f;
  const std::vector<std::string> bad_capabilities{"", std::string(513, 'a'), std::string("a\0b", 3), "\xff"};
  const std::vector<std::string> bad_methods{"", "with space", "bad/path", "\xe4\xb8\xad", std::string(129, 'a'), std::string("a\0b", 3)};
  for (const auto& value : bad_capabilities) invalid([&] { (void)f.store.issue_local(f.binding, {value}, {method}); });
  for (const auto& value : bad_methods) invalid([&] { (void)f.store.issue_local(f.binding, {capability}, {value}); });
  invalid([&] { (void)f.store.issue_local(f.binding, {}, {method}); });
  invalid([&] { (void)f.store.issue_local(f.binding, {capability}, {}); });
  invalid([&] { (void)f.store.issue_local(f.binding, {capability, capability}, {method}); });
  invalid([&] { (void)f.store.issue_local(f.binding, {capability}, {method, method}); });
  std::vector<std::string> caps, methods;
  for (unsigned index = 0; index < 64; ++index) {
    caps.push_back("cap-" + std::to_string(index)); methods.push_back("method-" + std::to_string(index));
  }
  const auto bounded = f.store.issue_local(f.binding, caps, methods);
  require(f.store.resolve_local(f.binding, bounded.token, methods.back(), caps.back()).scope == bounded.scope,
          "Exact 64-item policy was rejected");
  caps.push_back("cap-64"); invalid([&] { (void)f.store.issue_local(f.binding, caps, methods); });
  caps.pop_back(); methods.push_back("method-64");
  invalid([&] { (void)f.store.issue_local(f.binding, caps, methods); });
  const auto exact = f.store.issue_local(f.binding, {std::string(512, 'a'), "utf8.\xe4\xb8\xad"}, {std::string(128, 'a')});
  require(exact.scope->allows("utf8.\xe4\xb8\xad"), "Valid UTF-8 native capability was rejected");
  for (const auto& value : std::vector<std::string>{"", "bad/session", std::string(129, 'a')})
    invalid([&] { (void)f.store.create_local_binding(value, origin); });
  for (const auto& value : std::vector<std::string>{"", "https://external", std::string("app://a\0b", 9), "app://\xff", "app://" + std::string(507, 'a')})
    invalid([&] { (void)f.store.create_local_binding(session, value); });
  require(f.store.create_local_binding(std::string(128, 'a'), "app://" + std::string(506, 'a'))->active(),
          "Exact server identifier bounds were rejected");
  require(f.store.create_local_binding(session, "file://C:/application")->active(), "Valid file origin was rejected");
}

void renewal_preserves_scope_and_alias_boundaries() {
  fixture f; const auto grant = f.issue();
  lease_error(f.store.renew_local(f.binding, grant.token, "early"), "E_RENEW_TOO_EARLY");
  clock_ms.store(renew_at);
  lease_error(f.store.renew_local(f.binding, grant.token, "bad/op"), "E_INVALID_PARAMS");
  const auto rotated = f.store.renew_local(f.binding, grant.token, "renew-once");
  require(rotated.ok() && rotated.token != grant.token && rotated.generation == 2,
          "Valid local renewal did not rotate its bearer");
  require(f.store.resolve_local(f.binding, rotated.token, method, capability).scope == grant.scope,
          "Renewal changed stable native scope ownership");
  const auto retry = f.store.renew_local(f.binding, grant.token, "renew-once");
  require(retry.ok() && retry.token == rotated.token && retry.generation == 2,
          "Lost acknowledgement broke bounded idempotency");
  lease_error(f.store.lease_local(f.binding, grant.token));
  lease_error(f.store.renew_local(f.binding, grant.token, "another-operation"));
  clock_ms.store(renew_at + 29999);
  require(f.store.resolve_local(f.binding, grant.token, method, capability).scope == grant.scope,
          "Old alias lost its 30-second overlap early");
  clock_ms.store(renew_at + 30000);
  denied(f.store.resolve_local(f.binding, grant.token, method, capability));
  lease_error(f.store.renew_local(f.binding, grant.token, "renew-once"));
  require(f.store.lease_local(f.binding, rotated.token).ok(), "Current bearer lost its lease");
  clock_ms.store(renew_at + lifetime);
  const auto expired = f.store.resolve_local(f.binding, rotated.token, method, capability);
  require(expired.result == authorization_result::expired && !expired.scope && !grant.scope->active(),
          "Renewed local grant did not expire at its exact deadline");
  lease_error(f.store.renew_local(f.binding, rotated.token, "expired"), "E_SESSION_EXPIRED");
}

void revocation_and_close_are_scoped_and_irreversible() {
  fixture f; const auto grant = f.issue();
  const auto other_binding = f.store.create_local_binding(session, origin);
  const auto peer = f.store.issue_local(other_binding, {capability, renewal}, {method, renewal});
  const auto page = f.store.issue(session, origin, {capability, renewal});
  clock_ms.store(renew_at);
  const auto rotated = f.store.renew_local(f.binding, grant.token, "rotate-before-revoke");
  require(rotated.ok() && f.store.revoke_scope(grant.scope), "Exact local scope revocation failed");
  require(!grant.scope->active() && !grant.scope->allows(capability), "Revoked held scope remained live");
  denied(f.store.resolve_local(f.binding, grant.token, method, capability));
  denied(f.store.resolve_local(f.binding, rotated.token, method, capability));
  require(!f.store.revoke_scope(grant.scope), "Removed scope was reported as newly revoked");
  require(f.store.resolve_local(other_binding, peer.token, method, capability).scope == peer.scope &&
          f.store.authorize(session, origin, page, capability), "Exact revocation crossed scope/channel boundaries");
  const auto closing = f.issue(); f.binding->close(); f.binding->close();
  require(!f.binding->active() && !closing.scope->active() && !closing.scope->allows(capability),
          "Closed binding was reactivated or kept a live grant");
  denied(f.store.resolve_local(f.binding, closing.token, method, capability));
  lease_error(f.store.lease_local(f.binding, closing.token));
  lease_error(f.store.renew_local(f.binding, closing.token, "closed"));
  invalid([&] { (void)f.issue(); });
  require(f.store.revoke_scope(closing.scope), "Inactive issued owner could not be precisely cleaned up");
  require(peer.scope->active() && f.store.authorize(session, origin, page, capability), "Binding close revoked another owner");
  permission_store foreign;
  require(!foreign.revoke_scope(peer.scope), "Foreign store revoked an actual scope it did not issue");
  f.store.revoke_session(session);
  require(!peer.scope->active() && !f.store.authorize(session, origin, page, capability),
          "Session revocation omitted a native channel");
}

void close_during_issue_keeps_exact_cleanup_owner() {
  fixture f;
  const auto peer_binding = f.store.create_local_binding(session, origin);
  const auto peer = f.store.issue_local(peer_binding, {capability}, {method});
  close_on_entropy = f.binding; close_hook_called = false;
  permission_store_test_access::entropy(f.store, &closing_entropy);
  const auto issued = f.issue();
  permission_store_test_access::entropy(f.store, &next_entropy); close_on_entropy.reset();
  require(close_hook_called && !issued.token.empty() && issued.scope && !issued.scope->active(),
          "Close-raced issuance lost its exact inactive cleanup owner");
  denied(f.store.resolve_local(f.binding, issued.token, method, capability));
  require(f.store.revoke_scope(issued.scope) && peer.scope->active(), "Close-raced cleanup crossed grant ownership");
}

void close_during_renewal_never_revives_authority() {
  fixture f; const auto grant = f.issue(); clock_ms.store(renew_at);
  close_on_entropy = f.binding; close_hook_called = false;
  permission_store_test_access::entropy(f.store, &closing_entropy);
  const auto rotated = f.store.renew_local(f.binding, grant.token, "close-inside-renewal");
  permission_store_test_access::entropy(f.store, &next_entropy); close_on_entropy.reset();
  require(close_hook_called && rotated.ok() && rotated.generation == 2 && !grant.scope->active(),
          "Deterministic admitted renewal did not preserve the documented close race");
  denied(f.store.resolve_local(f.binding, grant.token, method, capability));
  denied(f.store.resolve_local(f.binding, rotated.token, method, capability));
  lease_error(f.store.lease_local(f.binding, rotated.token));
  require(f.store.revoke_scope(grant.scope), "Close-raced renewal aliases lost their cleanup owner");
}

void entropy_failure_never_issues_or_rotates_authority() {
  fixture f; const auto grant = f.issue(); clock_ms.store(renew_at);
  permission_store_test_access::entropy(f.store, &unavailable_entropy);
  bool failed = false;
  try { (void)f.issue(); } catch (const std::runtime_error&) { failed = true; }
  require(failed, "Failed system entropy issued a new local grant");
  lease_error(f.store.renew_local(f.binding, grant.token, "entropy-fail"), "E_ENTROPY_UNAVAILABLE");
  require(f.store.resolve_local(f.binding, grant.token, method, capability).scope == grant.scope &&
          f.store.lease_local(f.binding, grant.token).generation == 1, "Entropy failure corrupted existing authority");
}

void binding_retention_and_store_destruction() {
  std::shared_ptr<const authorization_scope> retained_scope;
  std::shared_ptr<native_connection_binding> retained_binding;
  {
    fixture f; const auto grant = f.issue();
    std::weak_ptr<native_connection_binding> weak = f.binding;
    f.binding.reset();
    retained_binding = weak.lock(); retained_scope = grant.scope;
    require(retained_binding && retained_binding->active() && retained_scope->active(),
            "Dropping an external binding reference implicitly closed retained authority");
    require(f.store.resolve_local(retained_binding, grant.token, method, capability).scope == retained_scope,
            "Retained actual binding no longer resolved its grant");
  }
  require(!retained_binding->active() && !retained_scope->active() && !retained_scope->allows(capability),
          "Borrowed binding/scope outlived the issuing store's authority");
  retained_binding->close();
}

class worker_gate {
 public:
  void block() {
    std::unique_lock<std::mutex> lock(mutex_); ++arrivals_; changed_.notify_all();
    require(changed_.wait_for(lock, 5s, [&] { return released_; }), "Worker gate timed out");
  }
  void await(unsigned count) {
    std::unique_lock<std::mutex> lock(mutex_);
    require(changed_.wait_for(lock, 5s, [&] { return arrivals_ >= count; }), "Worker queue was not deterministically blocked");
  }
  void release() { std::lock_guard<std::mutex> lock(mutex_); released_ = true; changed_.notify_all(); }
 private:
  std::mutex mutex_; std::condition_variable changed_; unsigned arrivals_{0}; bool released_{false};
};
struct release_on_exit { worker_gate& gate; ~release_on_exit() { gate.release(); } };
bridge_request task_request(const std::shared_ptr<const authorization_scope>& scope, const char* kind, const std::string& operation) {
  bridge_request result; result.session_id = session; result.origin = origin; result.authorization = scope;
  result.params_json = json{{"kind", kind}, {"operationId", operation}, {"input", json::object()}}.dump(); return result;
}
void close_rejects_already_queued_worker_admission() {
  fixture f;
  const auto grant = f.store.issue_local(f.binding, {"task.start"}, {"task.start"});
  const auto live_binding = f.store.create_local_binding(session, origin);
  const auto live = f.store.issue_local(live_binding, {"task.start"}, {"task.start"});
  worker_gate gate, drained; std::atomic<unsigned> entered{0};
  {
    task_service tasks; release_on_exit cleanup{gate}, drain_cleanup{drained};
    require(tasks.register_handler("block", [&](const auto&, const auto&) { gate.block(); return task_outcome{}; }), "Block registration failed");
    require(tasks.register_handler("probe", [&](const auto&, const auto&) { ++entered; return task_outcome{}; }), "Probe registration failed");
    require(tasks.register_handler("sentinel", [&](const auto&, const auto&) { drained.block(); return task_outcome{}; }), "Sentinel registration failed");
    for (std::size_t index = 0; index < task_service::max_workers; ++index)
      require(json::parse(tasks.start(task_request(live.scope, "block", "block-" + std::to_string(index)))).contains("taskId"), "Live worker did not start");
    gate.await(static_cast<unsigned>(task_service::max_workers));
    require(json::parse(tasks.start(task_request(grant.scope, "probe", "queued-before-close"))).contains("taskId"), "Probe was not queued while active");
    f.binding->close();
    require(json::parse(tasks.start(task_request(grant.scope, "probe", "after-close"))).value("error", "") == "E_UNAUTHORIZED", "Closed held scope admitted another task");
    gate.release();
    for (std::size_t index = 0; index < task_service::max_workers; ++index)
      require(json::parse(tasks.start(task_request(live.scope, "sentinel", "sentinel-" + std::to_string(index)))).contains("taskId"), "Live sentinel start failed");
    // With every worker now held in a later FIFO sentinel, the earlier probe
    // has been processed before destruction/cancellation can hide a regression.
    drained.await(static_cast<unsigned>(task_service::max_workers));
    require(entered.load() == 0, "Closed queued scope entered business before queue drain");
    drained.release();
  }
  require(entered.load() == 0 && live.scope->active(), "Closed queued owner entered native business or closed its peer");
}

}  // namespace

int main() {
  struct test { const char* name; void (*run)(); };
  const test cases[]{
    {"native channel and object identity", &channel_separation_and_real_binding_identity},
    {"method and capability intersection", &methods_and_capabilities_are_independent},
    {"explicit bounded native policies", &explicit_policy_and_identifier_bounds},
    {"scope-preserving renewal and aliases", &renewal_preserves_scope_and_alias_boundaries},
    {"precise revoke and irreversible close", &revocation_and_close_are_scoped_and_irreversible},
    {"close raced issuance keeps cleanup owner", &close_during_issue_keeps_exact_cleanup_owner},
    {"close raced renewal never revives", &close_during_renewal_never_revives_authority},
    {"entropy failure remains closed", &entropy_failure_never_issues_or_rotates_authority},
    {"retained binding and store lifetime", &binding_retention_and_store_destruction},
    {"closed queued worker admission", &close_rejects_already_queued_worker_admission}
  };
  try {
    for (const auto& item : cases) { item.run(); std::cout << "PASS " << item.name << '\n'; }
    std::cout << "10/10 native policy cases; controlled time, no OS identity or endpoint validation\n";
    return 0;
  } catch (const std::exception& error) {
    close_on_entropy.reset(); std::cerr << "FAIL " << error.what() << '\n'; return 1;
  }
}
