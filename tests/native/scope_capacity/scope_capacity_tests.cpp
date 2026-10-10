#include <soui/diagnostics/runtime_metrics.h>
#include <soui/json_bridge.h>
#include <soui/runtime/services/permissions/permission_store.h>
#include <json.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace allocation_probe {
std::atomic<bool> fail_next{false};
std::atomic<int> fail_after{-1};
std::atomic<soui::runtime::permission_store*> retirement_owner{nullptr};
std::atomic<unsigned> ready{0}, busy{0}, other{0};
void retiring() noexcept {
  auto* owner = retirement_owner.load();
  if (!owner) return;
  soui::sdk::diagnostics::permissions value;
  const auto state = owner->read_runtime_metrics(value);
  if (state == soui::sdk::diagnostics::status::ready) ++ready;
  else if (state == soui::sdk::diagnostics::status::busy) ++busy;
  else ++other;
}
}
// Real allocation faults and retirement observation; no replacement store.
void* operator new(std::size_t size) {
  if (allocation_probe::fail_next.exchange(false)) throw std::bad_alloc{};
  auto remaining = allocation_probe::fail_after.load();
  if (remaining >= 0 && allocation_probe::fail_after.fetch_sub(1) == 0) {
    allocation_probe::fail_after.store(-1); throw std::bad_alloc{};
  }
  if (void* memory = std::malloc(size ? size : 1)) return memory;
  throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept {
  if (memory) { allocation_probe::retiring(); std::free(memory); }
}
void operator delete[](void* memory) noexcept { ::operator delete(memory); }
void operator delete(void* memory, std::size_t) noexcept { ::operator delete(memory); }
void operator delete[](void* memory, std::size_t) noexcept { ::operator delete(memory); }

namespace soui::runtime {
// Existing native entropy seam only; no wire control or authority substitution.
struct permission_store_test_access {
  static void entropy(permission_store& store, bool (*value)(unsigned char*, std::size_t)) {
    store.entropy_ = value;
  }
};
}
namespace {
namespace dm = soui::sdk::diagnostics;
using namespace soui::runtime;
using namespace std::chrono_literals;
using json = nlohmann::json;
constexpr const char* origin = "app://scope-capacity";
constexpr const char* capability = "scope.work";
constexpr const char* renewal = "bridge.session.renew";
const std::vector<std::string> capabilities{capability, renewal};
std::atomic<std::int64_t> clock_ms{0};
std::atomic<std::uint64_t> entropy_sequence{1};
std::atomic<unsigned> entropy_calls{0};
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
permission_store::clock_type::time_point now() {
  return permission_store::clock_type::time_point{} + std::chrono::milliseconds(clock_ms.load());
}
bool entropy(unsigned char* bytes, std::size_t size) {
  ++entropy_calls;
  std::fill(bytes, bytes + size, static_cast<unsigned char>(0xA7));
  auto value = entropy_sequence.fetch_add(1);
  for (std::size_t i = 0; i < size && i < sizeof(value); ++i) {
    bytes[i] = static_cast<unsigned char>(value & 255U); value >>= 8U;
  }
  return true;
}
bool fixed_entropy(unsigned char* bytes, std::size_t size) {
  ++entropy_calls; std::fill(bytes, bytes + size, static_cast<unsigned char>(0x3D)); return true;
}
bool failed_entropy(unsigned char*, std::size_t) { ++entropy_calls; return false; }
bool throwing_entropy(unsigned char*, std::size_t) { ++entropy_calls; throw std::runtime_error("entropy fault"); }
dm::permissions read(permission_store& store) {
  dm::permissions value;
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  for (;;) {
    const auto state = store.read_runtime_metrics(value);
    if (state == dm::status::ready) break;
    require(state == dm::status::busy && std::chrono::steady_clock::now() < deadline,
        "Actual permission metrics unavailable");
    std::this_thread::yield();
  }
  require(value.observation.owner_epoch && !value.observation.clock_invalid &&
      value.observation.end_monotonic_us >= value.observation.begin_monotonic_us,
      "Actual scalar clock/epoch invalid");
  return value;
}
void bounds(const dm::permissions& value) {
  require(value.has_scope_capacity_bound && value.stored_scope_limit == 128 &&
      value.committed_alias_limit == 256 && value.renewal_alias_transient_limit == 257 &&
      value.module_scope_limit == 512 && value.stored_scope_owners + value.pending_scope_reservations <= 128 &&
      value.bearer_alias_entries + value.pending_alias_reservations <= 256 &&
      value.module_scope_slots_used <= 512, "Actual fixed scope/alias budgets changed");
}
template<class Function> void capacity(Function action, scope_capacity_reason expected) {
  bool refused = false;
  try { action(); }
  catch (const scope_capacity_error& error) {
    refused = error.reason() == expected && std::string(error.code()) == "E_LIMIT_SCOPE_CAPACITY";
  }
  require(refused, "Actual native capacity type/reason changed");
}
std::vector<std::string> fill(permission_store& store, unsigned count, const std::string& prefix) {
  std::vector<std::string> tokens; tokens.reserve(count);
  for (unsigned i = 0; i < count; ++i)
    tokens.push_back(store.issue(prefix + std::to_string(i), origin, capabilities));
  return tokens;
}
void store_limit_expiry() {
  clock_ms.store(0); permission_store store{&now, dm::mode::scalar};
  permission_store_test_access::entropy(store, &entropy);
  const auto tokens = fill(store, 128, "scope-");
  const auto held = store.resolve("scope-0", origin, tokens[0], capability).scope;
  const auto calls = entropy_calls.load();
  capacity([&] { (void)store.issue("overflow", origin, capabilities); }, scope_capacity_reason::store);
  auto value = read(store); bounds(value);
  require(value.stored_scope_owners == 128 && value.module_scope_slots_used == 128 &&
      value.pending_scope_reservations == 0 && value.store_scope_capacity_rejections_total == 1 &&
      entropy_calls.load() == calls, "Store refusal reached entropy or leaked reservation");
  clock_ms.store(31 * 60000);
  require(store.check("scope-0", origin, tokens[0], capability) == authorization_result::expired &&
      store.lease("scope-0", origin, tokens[0]).error_code == "E_SESSION_EXPIRED" &&
      store.renew("scope-0", origin, tokens[0], "expired").error_code == "E_SESSION_EXPIRED",
      "Expired grant changed error precedence");
  capacity([&] { (void)store.issue("expired-overflow", origin, capabilities); }, scope_capacity_reason::store);
  require(read(store).stored_scope_owners == 128 && held && !held->active(),
      "Expiry silently erased stored owner");
  store.revoke_session("scope-1");
  require(read(store).stored_scope_owners == 127, "Explicit revoke did not release store slot");
  (void)store.issue("replacement", origin, capabilities);
  require(read(store).stored_scope_owners == 128, "Explicit store slot could not be reused");
}
void module_limit_held() {
  clock_ms.store(0);
  std::vector<std::unique_ptr<permission_store>> stores;
  std::vector<std::vector<std::string>> tokens;
  for (unsigned i = 0; i < 4; ++i) {
    stores.push_back(std::make_unique<permission_store>(&now, dm::mode::scalar));
    tokens.push_back(fill(*stores.back(), 128, "m-"));
  }
  permission_store fifth{&now, dm::mode::scalar};
  permission_store_test_access::entropy(fifth, &entropy);
  const auto calls = entropy_calls.load();
  capacity([&] { (void)fifth.issue("module-overflow", origin, capabilities); }, scope_capacity_reason::module);
  require(read(fifth).stored_scope_owners == 0 && read(fifth).module_scope_slots_used == 512 &&
      read(fifth).module_scope_capacity_rejections_total == 1 && entropy_calls.load() == calls,
      "Module refusal minted entropy/authority");
  auto held = stores[0]->resolve("m-0", origin, tokens[0][0], capability).scope;
  const auto identity = held->scope_id();
  require(stores[0]->revoke_scope(held) && !held->active() && held->scope_id() == identity,
      "Revocation changed/revived scope identity");
  require(read(*stores[0]).stored_scope_owners == 127 && read(fifth).module_scope_slots_used == 512,
      "External revoked owner did not retain module slot");
  capacity([&] { (void)fifth.issue("still-held", origin, capabilities); }, scope_capacity_reason::module);
  held.reset();
  require(read(fifth).module_scope_slots_used == 511, "Last owner did not refund module slot");
  (void)fifth.issue("available", origin, capabilities);
  require(read(fifth).stored_scope_owners == 1 && read(fifth).module_scope_slots_used == 512,
      "Module slot was not reused once");
}
void issuer_death() {
  permission_store observer{dm::mode::scalar};
  std::shared_ptr<const authorization_scope> held;
  std::shared_ptr<native_connection_binding> binding;
  {
    permission_store issuer{dm::mode::scalar};
    binding = issuer.create_local_binding("local-owner", origin);
    auto grant = issuer.issue_local(binding, {capability}, {"scope.run"});
    held = grant.scope;
    require(held->allows_local_method("scope.run", capability) && binding->active(),
        "Actual local grant not live");
  }
  require(!binding->active() && !held->active() && !held->allows_local_method("scope.run", capability) &&
      read(observer).module_scope_slots_used == 1, "Borrow kept issuer authority or lost slot");
  held.reset();
  require(read(observer).module_scope_slots_used == 0, "Issuer-dead last owner did not refund");
}
void aliases_bounded() {
  clock_ms.store(0); permission_store store{&now, dm::mode::scalar};
  auto tokens = fill(store, 128, "a-");
  std::vector<std::shared_ptr<const authorization_scope>> owners; owners.reserve(128);
  for (unsigned i = 0; i < 128; ++i)
    owners.push_back(store.resolve("a-" + std::to_string(i), origin, tokens[i], capability).scope);
  for (unsigned round = 0; round < 20; ++round) {
    clock_ms.fetch_add(25 * 60000);
    for (unsigned i = 0; i < 128; ++i) {
      const auto old = tokens[i], id = owners[i]->scope_id();
      const auto op = "rotation-" + std::to_string(round);
      auto rotated = store.renew("a-" + std::to_string(i), origin, old, op);
      require(rotated.ok() && rotated.generation == round + 2 && owners[i]->scope_id() == id,
          "Renewal changed owner/generation");
      const auto retry = store.renew("a-" + std::to_string(i), origin, old, op);
      require(retry.ok() && retry.token == rotated.token, "Overlap idempotent renewal changed");
      tokens[i] = rotated.token;
    }
    const auto value = read(store); bounds(value);
    require(value.stored_scope_owners == 128 && value.bearer_alias_entries == 256 &&
        value.module_scope_slots_used == 128 && value.stored_previous_aliases == 128 &&
        value.bearer_alias_high_water <= 257, "Same-scope history exceeded budget");
  }
  require(read(store).bearer_alias_high_water == 257, "Real transient alias not observed");
  store.revoke_session("a-0");
  require(!owners[0]->active() && read(store).bearer_alias_entries == 254 &&
      read(store).module_scope_slots_used == 128, "Alias removal refunded held owner");
  owners[0].reset();
  require(read(store).module_scope_slots_used == 127, "Last held owner retained module slot");
}
void exception_refund() {
  clock_ms.store(0); permission_store store{&now, dm::mode::scalar};
  const std::string session = "alloc", source = "app://cap";
  bool allocation_failed = false;
  allocation_probe::fail_next.store(true);
  try { (void)store.issue(session, source, capabilities); }
  catch (const std::bad_alloc&) { allocation_failed = true; }
  allocation_probe::fail_next.store(false);
  require(allocation_failed, "Real grant-state allocation fault not exercised");
  auto zero = read(store);
  require(zero.stored_scope_owners == 0 && zero.pending_scope_reservations == 0 &&
      zero.module_scope_slots_used == 0, "Allocation exception leaked credits");
  permission_store_test_access::entropy(store, &entropy);
  unsigned allocation_failures = 0;
  bool completed = false;
  for (int ordinal = 0; ordinal < 64; ++ordinal) {
    allocation_probe::fail_after.store(ordinal);
    try { (void)store.issue(session, source, capabilities); completed=true; }
    catch (const std::bad_alloc&) { ++allocation_failures; }
    allocation_probe::fail_after.store(-1);
    if (completed) {
      require(read(store).stored_scope_owners == 1, "Successful allocation sweep did not commit once");
      store.revoke_session(session);
    }
    zero = read(store);
    require(zero.stored_scope_owners == 0 && zero.pending_scope_reservations == 0 &&
        zero.module_scope_slots_used == 0, "Actual nth allocation left authority/credits");
    if (completed) break;
  }
  require(completed && allocation_failures >= 8, "Actual allocation sweep missed grant/map/return preparation");
  for (const auto source_entropy : {&failed_entropy, &throwing_entropy}) {
    permission_store_test_access::entropy(store, source_entropy);
    bool refused = false;
    try { (void)store.issue("entropy", origin, capabilities); }
    catch (const std::runtime_error&) { refused = true; }
    zero = read(store);
    require(refused && zero.stored_scope_owners == 0 && zero.pending_scope_reservations == 0 &&
        zero.module_scope_slots_used == 0, "Entropy exception leaked credits");
  }
  permission_store_test_access::entropy(store, &fixed_entropy);
  const auto token = store.issue("collision", origin, capabilities);
  bool collided = false;
  try { (void)store.issue("other", origin, capabilities); }
  catch (const std::runtime_error&) { collided = true; }
  auto value = read(store); bounds(value);
  require(collided && value.stored_scope_owners == 1 && value.pending_scope_reservations == 0 &&
      value.module_scope_slots_used == 1 && store.authorize("collision", origin, token, capability),
      "Collision replaced authority/leaked reservation");
  clock_ms.store(25 * 60000);
  const auto before = store.lease("collision", origin, token);
  for (const auto source_entropy : {&failed_entropy, &throwing_entropy, &fixed_entropy}) {
    permission_store_test_access::entropy(store, source_entropy);
    require(store.renew("collision", origin, token, "refused-rotation").error_code ==
        "E_ENTROPY_UNAVAILABLE", "Renewal fault/collision changed original error");
    const auto after = store.lease("collision", origin, token);
    value = read(store);
    require(before.ok() && after.ok() && after.token == token && after.generation == before.generation &&
        value.stored_scope_owners == 1 && value.bearer_alias_entries == 1 &&
        value.pending_scope_reservations == 0 && value.module_scope_slots_used == 1,
        "Failed renewal committed alias/generation or changed slot ownership");
  }
}
class clock_gate final {
 public:
  void arm() { std::lock_guard<std::mutex> lock(mutex_); armed_=true; open_=false; fail_=false; arrivals_=0; }
  void wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!armed_) return;
    ++arrivals_; changed_.notify_all();
    require(changed_.wait_for(lock, 5s, [&] { return open_; }), "Actual clock admission not released");
    require(!fail_, "Actual clock construction fault");
  }
  void arrivals(unsigned expected) {
    std::unique_lock<std::mutex> lock(mutex_);
    require(changed_.wait_for(lock, 5s, [&] { return arrivals_ >= expected; }), "Missing pending issue");
  }
  void release(bool fail = false) noexcept {
    std::lock_guard<std::mutex> lock(mutex_); fail_=fail; open_=true; armed_=false; changed_.notify_all();
  }
 private:
  std::mutex mutex_; std::condition_variable changed_;
  bool armed_{false}, open_{false}, fail_{false}; unsigned arrivals_{0};
};
clock_gate held_clock;
permission_store::clock_type::time_point pending_now() { held_clock.wait(); return now(); }
struct thread_retire final {
  std::vector<std::thread>& threads;
  ~thread_retire() {
    held_clock.release();
    for (auto& thread : threads) if (thread.joinable()) thread.join();
  }
};
void pending_store_boundary() {
  clock_ms.store(0); permission_store store{&pending_now, dm::mode::scalar};
  permission_store_test_access::entropy(store, &entropy);
  std::vector<std::thread> threads; threads.reserve(136);
  std::vector<int> results(136, -1);
  held_clock.arm(); thread_retire retire{threads};
  for (unsigned i = 0; i < 136; ++i) threads.emplace_back([&, i] {
    try { (void)store.issue("p-" + std::to_string(i), origin, capabilities); results[i]=0; }
    catch (const scope_capacity_error& error) { results[i]=error.reason()==scope_capacity_reason::store ? 1 : 3; }
    catch (...) { results[i]=4; }
  });
  held_clock.arrivals(128);
  auto value = read(store); bounds(value);
  require(value.stored_scope_owners == 0 && value.pending_scope_reservations == 128 &&
      value.pending_alias_reservations == 128 && value.module_scope_slots_used == 128,
      "Pending issuance bypassed reservations");
  held_clock.release();
  for (auto& thread : threads) thread.join();
  require(std::count(results.begin(), results.end(), 0) == 128 &&
      std::count(results.begin(), results.end(), 1) == 8, "Store concurrent outcomes changed");
  value = read(store); bounds(value);
  require(value.stored_scope_owners == 128 && value.pending_scope_reservations == 0 &&
      value.module_scope_slots_used == 128, "Pending commit leaked/overcommitted");
  for (unsigned i = 0; i < 136; ++i) store.revoke_session("p-" + std::to_string(i));
  require(read(store).module_scope_slots_used == 0, "Committed concurrent owners not retired");
  threads.clear(); results.assign(128, -1);
  held_clock.arm();
  for (unsigned i = 0; i < 128; ++i) threads.emplace_back([&, i] {
    try { (void)store.issue("rollback-" + std::to_string(i), origin, capabilities); results[i]=0; }
    catch (...) { results[i]=4; }
  });
  held_clock.arrivals(128);
  value = read(store); bounds(value);
  require(value.pending_scope_reservations == 128 && value.module_scope_slots_used == 128,
      "Concurrent fault did not hold real reservations");
  held_clock.release(true);
  for (auto& thread : threads) thread.join();
  value = read(store); bounds(value);
  require(std::count(results.begin(), results.end(), 4) == 128 && value.stored_scope_owners == 0 &&
      value.pending_scope_reservations == 0 && value.pending_alias_reservations == 0 &&
      value.module_scope_slots_used == 0, "Concurrent exception rollback leaked any credit");
  (void)store.issue("post-rollback", origin, capabilities);
  require(read(store).stored_scope_owners == 1 && read(store).module_scope_slots_used == 1,
      "Refunded concurrent capacity could not be reused");
  store.revoke_session("post-rollback");
  permission_store_test_access::entropy(store, &fixed_entropy);
  threads.clear(); results.assign(2, -1); held_clock.arm();
  for (unsigned i = 0; i < 2; ++i) threads.emplace_back([&, i] {
    try { (void)store.issue("race-" + std::to_string(i), origin, capabilities); results[i]=0; }
    catch (const std::runtime_error&) { results[i]=4; }
    catch (...) { results[i]=5; }
  });
  held_clock.arrivals(2);
  require(read(store).pending_scope_reservations == 2 && read(store).module_scope_slots_used == 2,
      "Collision race did not reach two real preparations");
  held_clock.release();
  for (auto& thread : threads) thread.join();
  value = read(store); bounds(value);
  require(std::count(results.begin(), results.end(), 0) == 1 &&
      std::count(results.begin(), results.end(), 4) == 1 && value.stored_scope_owners == 1 &&
      value.pending_scope_reservations == 0 && value.module_scope_slots_used == 1,
      "Commit-time collision replaced authority or leaked reservation");
}
void concurrent_module_boundary() {
  clock_ms.store(0);
  std::vector<std::unique_ptr<permission_store>> full, candidates;
  for (unsigned i = 0; i < 4; ++i) {
    full.push_back(std::make_unique<permission_store>(&now, dm::mode::scalar));
    (void)fill(*full.back(), 126, "fund-");
  }
  for (unsigned i = 0; i < 16; ++i)
    candidates.push_back(std::make_unique<permission_store>(&pending_now, dm::mode::scalar));
  std::vector<std::thread> threads; threads.reserve(16); std::vector<int> results(16, -1);
  held_clock.arm(); thread_retire retire{threads};
  for (unsigned i = 0; i < 16; ++i) threads.emplace_back([&, i] {
    try { (void)candidates[i]->issue("one", origin, capabilities); results[i]=0; }
    catch (const scope_capacity_error& error) { results[i]=error.reason()==scope_capacity_reason::module ? 1 : 3; }
    catch (...) { results[i]=4; }
  });
  held_clock.arrivals(8);
  require(read(*full[0]).module_scope_slots_used == 512, "Pending module bound exceeded");
  held_clock.release();
  for (auto& thread : threads) thread.join();
  require(std::count(results.begin(), results.end(), 0) == 8 &&
      std::count(results.begin(), results.end(), 1) == 8, "Module concurrent outcomes changed");
  require(read(*full[0]).stored_scope_owners == 126 && read(*full[0]).module_scope_slots_used == 512,
      "Module count attributed to one store");
}
void disabled_zero() {
  permission_store store;
  (void)fill(store, 128, "off-");
  capacity([&] { (void)store.issue("off-overflow", origin, capabilities); }, scope_capacity_reason::store);
  dm::permissions value;
  require(store.read_runtime_metrics(value) == dm::status::disabled &&
      value.observation.owner_epoch == 0 && value.observation.begin_monotonic_us == 0 &&
      value.observation.end_monotonic_us == 0 && !value.observation.counter_saturated &&
      !value.observation.clock_invalid && !value.has_scope_capacity_bound &&
      value.stored_scope_owners == 0 && value.bearer_alias_entries == 0 && value.stored_previous_aliases == 0 &&
      value.issued_scopes_total == 0 && value.rotated_leases_total == 0 && value.idempotent_renewals_total == 0 &&
      value.renewal_too_early_total == 0 && value.checked_renewal_rejections_total == 0 &&
      value.revoked_scopes_total == 0 && value.stored_scope_limit == 0 && value.pending_scope_reservations == 0 &&
      value.pending_alias_reservations == 0 && value.committed_alias_limit == 0 &&
      value.renewal_alias_transient_limit == 0 && value.bearer_alias_high_water == 0 &&
      value.module_scope_limit == 0 && value.module_scope_slots_used == 0 &&
      value.store_scope_capacity_rejections_total == 0 && value.module_scope_capacity_rejections_total == 0 &&
      value.alias_capacity_rejections_total == 0, "Default-off exposed state/policy");
}
void bootstrap_capacity_envelope() {
  std::vector<std::unique_ptr<permission_store>> stores;
  for (unsigned i = 0; i < 4; ++i) {
    stores.push_back(std::make_unique<permission_store>(dm::mode::scalar));
    (void)fill(*stores.back(), 128, "full-");
  }
  soui::sdk::json_bridge bridge{"app://capacity-envelope", dm::mode::scalar};
  std::atomic<unsigned> business{0};
  require(bridge.register_method("business", capability, [&](std::string_view) {
    ++business; return std::string{"{}"};
  }), "Actual method registration failed");
  const json packet{{"requestId","capacity"},{"method","bridge.bootstrap"},{"sessionId",""},
      {"origin",""},{"token",""},{"params",{{"protocol","soui.bridge/1"}}}};
  const auto response = json::parse(bridge.handle_json(packet.dump()));
  require(response.at("ok")==false && response.at("error").at("code")=="E_LIMIT_SCOPE_CAPACITY" &&
      response.at("requestId")=="capacity" && business.load()==0,
      "Bootstrap disguised capacity as params/authentication or ran business");
  permission_store foreign;
  auto binding = stores[0]->create_local_binding("foreign", origin);
  bool denied = false;
  try { (void)foreign.issue_local(binding, {capability}, {"scope.run"}); }
  catch (const std::invalid_argument&) { denied=true; }
  require(denied && binding->active(), "Binding denial order changed at module capacity");
}
void retirement_outside_lock() {
  permission_store store{dm::mode::scalar};
  (void)fill(store, 32, "retire-");
  allocation_probe::ready.store(0); allocation_probe::busy.store(0); allocation_probe::other.store(0);
  struct observation_end {
    ~observation_end() { allocation_probe::retirement_owner.store(nullptr); }
  } finished;
  allocation_probe::retirement_owner.store(&store);
  for (unsigned i = 0; i < 32; ++i) store.revoke_session("retire-" + std::to_string(i));
  allocation_probe::retirement_owner.store(nullptr);
  require(allocation_probe::ready.load()>0 && allocation_probe::busy.load()==0 &&
      allocation_probe::other.load()==0 && read(store).stored_scope_owners==0 &&
      read(store).module_scope_slots_used==0, "Actual retirement locked store/leaked owner");
}
struct entry { const char* name; void (*run)(); };
const entry cases[]={
  {"store-limit-expiry",store_limit_expiry},{"module-limit-held",module_limit_held},
  {"issuer-death",issuer_death},{"aliases-bounded",aliases_bounded},
  {"exception-refund",exception_refund},{"pending-store-boundary",pending_store_boundary},
  {"concurrent-module-boundary",concurrent_module_boundary},{"disabled-zero",disabled_zero},
  {"bootstrap-capacity-envelope",bootstrap_capacity_envelope},{"retirement-outside-lock",retirement_outside_lock}
};
}
int main(int argc, char** argv) {
  if (argc != 2) { std::cerr << "E_SCOPE_CAPACITY_CASE\n"; return 2; }
  for (const auto& selected : cases) if (selected.name == std::string(argv[1])) {
    try { selected.run(); std::cout << "scope-capacity.passed " << selected.name << "\n"; return 0; }
    catch (...) { std::cerr << "E_SCOPE_CAPACITY_TEST\n"; return 1; }
  }
  std::cerr << "E_SCOPE_CAPACITY_CASE\n"; return 2;
}
