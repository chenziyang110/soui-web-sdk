#include <soui/runtime/bridge/bridge_transport.h>
#include <soui/runtime/services/permissions/permission_store.h>

#include <json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace soui::runtime {
// Private test seam: no clock/entropy controls are exposed through Bridge.
struct permission_store_test_access {
  static void entropy(permission_store& store, bool (*source)(unsigned char*, std::size_t)) {
    store.entropy_ = source;
  }
  static std::size_t token_count(const permission_store& store) {
    std::lock_guard<std::mutex> lock(store.mutex_);
    return store.grants_.size();
  }
};
}  // namespace soui::runtime

namespace {
using namespace soui::runtime;
using json = nlohmann::json;
constexpr std::int64_t minute = 60000;
constexpr std::int64_t lifetime = 30 * minute;
constexpr std::int64_t renew_at = 25 * minute;
constexpr const char* session = "renewal-session";
constexpr const char* origin = "app://renewal-test";
constexpr const char* capability = "work.read";
constexpr const char* renewal = "bridge.session.renew";

static_assert(!std::is_default_constructible_v<authorization_scope>);
static_assert(!std::is_copy_constructible_v<authorization_scope>);
static_assert(!std::is_copy_constructible_v<permission_store>);

// All elapsed time is simulated; these tests do not start a window, COM,
// WebView or an eight-hour wall-clock workload.
std::atomic<std::int64_t> clock_ms{0};
std::atomic<std::uint64_t> entropy_sequence{1};
std::atomic<std::size_t> entropy_size{0};
std::atomic<unsigned> entropy_calls{0};

permission_store::clock_type::time_point now() {
  return permission_store::clock_type::time_point{} + std::chrono::milliseconds(clock_ms.load());
}
bool next_entropy(unsigned char* bytes, std::size_t size) {
  entropy_size.store(size);
  entropy_calls.fetch_add(1);
  std::fill(bytes, bytes + size, static_cast<unsigned char>(0xA5));
  auto value = entropy_sequence.fetch_add(1);
  for (std::size_t index = 0; index < size && index < sizeof(value); ++index) {
    bytes[index] = static_cast<unsigned char>(value & 0xFFU);
    value >>= 8U;
  }
  return true;
}
bool fixed_entropy(unsigned char* bytes, std::size_t size) {
  std::fill(bytes, bytes + size, static_cast<unsigned char>(0x3C));
  return true;
}
bool failed_entropy(unsigned char*, std::size_t) { return false; }

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}
void require_error(const permission_lease& value, const char* code) {
  require(!value.ok() && value.error_code == code, std::string{"Expected lease error: "} + code);
  require(value.token.empty() && value.generation == 0 && value.expires_in_ms == 0,
          "A rejected lease disclosed usable credential data");
}
void require_lease(const permission_lease& value, const std::string& token,
                   std::uint64_t generation, std::uint32_t remaining) {
  require(value.ok() && value.session_id == session && value.origin == origin && value.token == token,
          "Lease identity changed unexpectedly");
  require(value.generation == generation && value.expires_in_ms == remaining,
          "Lease generation or native expiration changed unexpectedly");
  require(value.renew_after_ms == (remaining > 300000U ? remaining - 300000U : 0U),
          "Lease renewal schedule does not describe its fixed expiration");
}
void require_denied(const checked_authorization& value) {
  require(value.result == authorization_result::denied && !value.scope,
          "Unauthorized credentials received an authorization scope");
}

struct fixture {
  permission_store store{&now};
  std::string token;
  std::shared_ptr<const authorization_scope> scope;
  fixture() {
    clock_ms.store(0);
    permission_store_test_access::entropy(store, &next_entropy);
    token = store.issue(session, origin, {capability, renewal, capability, ""});
    scope = store.resolve(session, origin, token, capability).scope;
    require(scope != nullptr, "Initial authorization scope was not issued");
  }
  permission_lease rotate(const std::string& operation = "renew-1") {
    return store.renew(session, origin, token, operation);
  }
};

void issuance_and_isolation() {
  fixture f;
  require(entropy_size.load() == 32U, "Bearer generation did not request 256 bits of entropy");
  require_lease(f.store.lease(session, origin, f.token), f.token, 1, lifetime);
  require(f.scope->active() && f.scope->allows(capability) && f.scope->allows(renewal),
          "Issued scope omitted granted capabilities");
  require(!f.scope->allows("work.write") && !f.scope->allows(""), "Scope expanded its capabilities");
  require(f.scope->matches(session, origin) && !f.scope->matches("other-session", origin) &&
          !f.scope->matches(session, "app://other"), "Scope identity did not bind session and origin");
  require_denied(f.store.resolve("other-session", origin, f.token, capability));
  require_denied(f.store.resolve(session, "app://other", f.token, capability));
  require_denied(f.store.resolve(session, origin, "unknown-token", capability));
  require_denied(f.store.resolve(session, origin, f.token, "work.write"));
  require_error(f.store.lease("other-session", origin, f.token), "E_UNAUTHORIZED");
  require_error(f.store.lease(session, "app://other", f.token), "E_UNAUTHORIZED");
  const auto limited = f.store.issue(session, origin, {capability});
  require_error(f.store.lease(session, origin, limited), "E_UNAUTHORIZED");
  clock_ms.store(renew_at);
  require_error(f.store.renew(session, origin, limited, "no-capability"), "E_UNAUTHORIZED");
}

void exact_renewal_boundary_and_invalid_operations() {
  fixture f;
  clock_ms.store(renew_at - 1);
  require_error(f.rotate(), "E_RENEW_TOO_EARLY");
  const std::vector<std::string> invalid{"", "with space", "bad/id", "bad\nid",
    std::string("a\0b", 3), std::string(129, 'a'), "\xE4\xB8\xAD"};
  for (const auto& operation : invalid) require_error(f.rotate(operation), "E_INVALID_PARAMS");
  require_lease(f.store.lease(session, origin, f.token), f.token, 1, 300001U);
  require(permission_store_test_access::token_count(f.store) == 1U, "Rejected renewal changed token storage");
  clock_ms.store(renew_at);
  const auto rotated = f.rotate(std::string(128, 'a'));
  require(rotated.token != f.token, "Renewal reused the old bearer");
  require_lease(rotated, rotated.token, 2, lifetime);
  require(f.store.resolve(session, origin, rotated.token, capability).scope == f.scope,
          "Token rotation replaced stable native resource identity");
  require(!f.scope->allows("work.write"), "Renewal granted an unapproved capability");
  require_error(f.store.renew(session, origin, rotated.token, "second-operation"), "E_RENEW_TOO_EARLY");
  require(permission_store_test_access::token_count(f.store) == 2U, "One renewal must retain exactly two tokens");
}

void lost_ack_and_overlap_deadline() {
  fixture f;
  clock_ms.store(renew_at);
  const auto rotated = f.rotate("renew-_.:Az09");
  require(rotated.ok(), "Lost-ack setup could not renew");
  require_error(f.store.lease(session, origin, f.token), "E_UNAUTHORIZED");
  clock_ms.store(renew_at + 29999);
  require(f.store.resolve(session, origin, f.token, capability).scope == f.scope,
          "Previous bearer lost admitted-business overlap too early");
  require_lease(f.rotate("renew-_.:Az09"), rotated.token, 2, lifetime - 29999U);
  require_error(f.rotate("another-operation"), "E_UNAUTHORIZED");
  require_lease(f.store.renew(session, origin, rotated.token, "renew-_.:Az09"),
                rotated.token, 2, lifetime - 29999U);
  clock_ms.store(renew_at + 30000);
  require_denied(f.store.resolve(session, origin, f.token, capability));
  require_error(f.rotate("renew-_.:Az09"), "E_UNAUTHORIZED");
  require_lease(f.store.renew(session, origin, rotated.token, "renew-_.:Az09"),
                rotated.token, 2, lifetime - 30000U);
  clock_ms.store(2 * renew_at);
  require_lease(f.store.renew(session, origin, rotated.token, "renew-_.:Az09"),
                rotated.token, 2, 300000U);
  const auto next = f.store.renew(session, origin, rotated.token, "renew-2");
  require_lease(next, next.token, 3, lifetime);
  require_denied(f.store.resolve(session, origin, f.token, capability));
  require(permission_store_test_access::token_count(f.store) == 2U, "Expired previous mapping was retained across rotations");
}

void overlap_never_extends_previous_expiration() {
  fixture f;
  clock_ms.store(lifetime - 1);
  const auto rotated = f.rotate();
  require(rotated.ok(), "Unexpired bearer could not renew one millisecond before expiration");
  require_lease(f.rotate(), rotated.token, 2, lifetime);
  require(f.store.authorize(session, origin, f.token, capability), "Old bearer was denied before its original deadline");
  clock_ms.store(lifetime);
  require_denied(f.store.resolve(session, origin, f.token, capability));
  require_error(f.rotate(), "E_UNAUTHORIZED");
  require_lease(f.store.lease(session, origin, rotated.token), rotated.token, 2, lifetime - 1U);
}

void expiration_is_not_recoverable() {
  fixture f;
  clock_ms.store(lifetime - 1);
  require(f.scope->active(), "Grant expired before its exact deadline");
  clock_ms.store(lifetime);
  const auto expired = f.store.resolve(session, origin, f.token, capability);
  require(expired.result == authorization_result::expired && !expired.scope, "Exact expiration did not fail closed");
  require(!f.scope->active() && !f.scope->allows(capability), "Borrowed scope kept an expired grant alive");
  require_error(f.store.lease(session, origin, f.token), "E_SESSION_EXPIRED");
  require_error(f.rotate(), "E_SESSION_EXPIRED");
  require(permission_store_test_access::token_count(f.store) == 1U, "Expired renewal created a new mapping");
}

void eight_simulated_hours_keep_identity_and_bounded_storage() {
  fixture f;
  auto current = f.token;
  std::uint64_t generation = 1;
  std::int64_t last_rotation = 0;
  for (std::int64_t elapsed = renew_at; elapsed <= 8 * 60 * minute; elapsed += renew_at) {
    clock_ms.store(elapsed);
    const auto previous = current;
    const auto operation = "eight-hours:" + std::to_string(generation);
    const auto rotated = f.store.renew(session, origin, previous, operation);
    ++generation;
    require_lease(rotated, rotated.token, generation, lifetime);
    current = rotated.token;
    last_rotation = elapsed;
    require(current != previous, "Long-session renewal did not rotate bearer");
    require(f.store.resolve(session, origin, current, capability).scope == f.scope,
            "Long-session resource ownership changed");
    clock_ms.store(elapsed + 10000);
    require_lease(f.store.renew(session, origin, previous, operation), current, generation, lifetime - 10000U);
    require(permission_store_test_access::token_count(f.store) == 2U, "Renewal history grew beyond two token mappings");
    clock_ms.store(elapsed + 30000);
    require_denied(f.store.resolve(session, origin, previous, capability));
  }
  clock_ms.store(8 * 60 * minute);
  require(generation == 20U && f.scope->active(), "Eight simulated hours did not cross nineteen renewal boundaries");
  require_lease(f.store.lease(session, origin, current), current, generation,
                static_cast<std::uint32_t>(last_rotation + lifetime - clock_ms.load()));
  f.store.revoke_session(session);
  require(!f.scope->active() && permission_store_test_access::token_count(f.store) == 0U,
          "Long-session revoke left token mappings or borrowed authority live");
}

void independent_grants_and_revocation() {
  fixture f;
  const auto other_token = f.store.issue(session, origin, {capability, renewal});
  const auto other_scope = f.store.resolve(session, origin, other_token, capability).scope;
  require(other_scope && other_scope != f.scope, "A second grant for the same document reused native identity");
  const auto unrelated = f.store.issue("unrelated-session", origin, {capability});
  permission_store independent{&now};
  permission_store_test_access::entropy(independent, &next_entropy);
  const auto cross_token = independent.issue(session, origin, {capability, renewal});
  const auto cross_scope = independent.resolve(session, origin, cross_token, capability).scope;
  require(cross_scope && cross_scope != f.scope && cross_scope != other_scope, "Independent stores shared authorization identity");
  require_denied(f.store.resolve(session, origin, cross_token, capability));
  clock_ms.store(renew_at);
  const auto rotated = f.rotate();
  require(rotated.ok() && f.scope->active(), "Revocation setup failed");
  f.store.revoke_session(session);
  require(!f.scope->active() && !other_scope->active() && !f.scope->allows(renewal),
          "Revocation failed to invalidate all borrowed grants for this session");
  require_denied(f.store.resolve(session, origin, f.token, capability));
  require_denied(f.store.resolve(session, origin, rotated.token, capability));
  require_error(f.store.renew(session, origin, rotated.token, "renew-2"), "E_UNAUTHORIZED");
  require(independent.authorize(session, origin, cross_token, capability) && cross_scope->active(),
          "Revoking one store changed another store");
  require(f.store.authorize("unrelated-session", origin, unrelated, capability), "Revocation affected an unrelated session");
  const auto replacement = f.store.issue(session, origin, {capability, renewal});
  require(f.store.resolve(session, origin, replacement, capability).scope != f.scope && !f.scope->active(),
          "Reissuing a grant resurrected old resource ownership");
}

void store_destruction_revokes_borrowed_scope() {
  clock_ms.store(0);
  std::shared_ptr<const authorization_scope> borrowed;
  {
    permission_store store{&now};
    permission_store_test_access::entropy(store, &next_entropy);
    const auto token = store.issue(session, origin, {capability, renewal});
    borrowed = store.resolve(session, origin, token, capability).scope;
    clock_ms.store(renew_at);
    require(store.renew(session, origin, token, "destruction").ok(), "Store destruction setup failed");
  }
  require(borrowed && !borrowed->active() && !borrowed->allows(capability),
          "An externally held scope survived its store's authority");
}

void entropy_failure_preserves_state() {
  fixture f;
  permission_store_test_access::entropy(f.store, &failed_entropy);
  bool threw = false;
  try { (void)f.store.issue("failed-issue", origin, {capability}); }
  catch (const std::runtime_error&) { threw = true; }
  require(threw && permission_store_test_access::token_count(f.store) == 1U,
          "Failed entropy during issue published a grant or silently downgraded");
  clock_ms.store(renew_at);
  require_error(f.rotate(), "E_ENTROPY_UNAVAILABLE");
  require_lease(f.store.lease(session, origin, f.token), f.token, 1, 300000U);
  require(f.store.resolve(session, origin, f.token, capability).scope == f.scope,
          "Failed renewal damaged previous authorization");
  permission_store_test_access::entropy(f.store, &next_entropy);
  const auto retry = f.rotate();
  require_lease(retry, retry.token, 2, lifetime);
  require(permission_store_test_access::token_count(f.store) == 2U, "Entropy retry leaked failed operation state");
}

void token_collision_preserves_all_grants() {
  clock_ms.store(0);
  permission_store store{&now};
  permission_store_test_access::entropy(store, &fixed_entropy);
  const auto first = store.issue(session, origin, {capability, renewal});
  const auto scope = store.resolve(session, origin, first, capability).scope;
  bool threw = false;
  try { (void)store.issue("collision-session", origin, {capability}); }
  catch (const std::runtime_error&) { threw = true; }
  require(threw && permission_store_test_access::token_count(store) == 1U, "Issue collision replaced an existing grant");
  permission_store_test_access::entropy(store, &next_entropy);
  const auto second = store.issue("second-session", origin, {capability, renewal});
  permission_store_test_access::entropy(store, &fixed_entropy);
  clock_ms.store(renew_at);
  require_error(store.renew(session, origin, first, "self-collision"), "E_ENTROPY_UNAVAILABLE");
  require_error(store.renew("second-session", origin, second, "other-collision"), "E_ENTROPY_UNAVAILABLE");
  require_lease(store.lease(session, origin, first), first, 1, 300000U);
  require(store.resolve(session, origin, first, capability).scope == scope &&
          store.authorize("second-session", origin, second, capability) &&
          permission_store_test_access::token_count(store) == 2U, "Renewal collision overwrote another grant");
}

void concurrent_lost_ack_retries_mint_once() {
  fixture f;
  clock_ms.store(renew_at);
  constexpr std::size_t callers = 8;
  std::array<permission_lease, callers> results;
  std::array<std::thread, callers> threads;
  std::atomic<bool> failed{false};
  const auto before = entropy_calls.load();
  try {
    for (std::size_t index = 0; index < callers; ++index) {
      threads[index] = std::thread([&, index] {
        try { results[index] = f.rotate("concurrent-operation"); }
        catch (...) { failed.store(true); }
      });
    }
  } catch (...) {
    for (auto& thread : threads) if (thread.joinable()) thread.join();
    throw;
  }
  for (auto& thread : threads) thread.join();
  require(!failed.load(), "Concurrent renewal threw an exception");
  for (const auto& result : results) require_lease(result, results.front().token, 2, lifetime);
  require(entropy_calls.load() == before + 1U && permission_store_test_access::token_count(f.store) == 2U,
          "Concurrent retries minted multiple tokens or retained unbounded history");
  require(f.store.resolve(session, origin, results.front().token, capability).scope == f.scope,
          "Concurrent rotation replaced stable grant identity");

  const auto current = results.front().token;
  clock_ms.store(2 * renew_at);
  const auto before_conflict = entropy_calls.load();
  try {
    for (std::size_t index = 0; index < callers; ++index) {
      threads[index] = std::thread([&, index] {
        try { results[index] = f.store.renew(session, origin, current, "competing:" + std::to_string(index)); }
        catch (...) { failed.store(true); }
      });
    }
  } catch (...) {
    for (auto& thread : threads) if (thread.joinable()) thread.join();
    throw;
  }
  for (auto& thread : threads) thread.join();
  require(!failed.load(), "Competing renewal threw an exception");
  unsigned accepted = 0;
  for (const auto& result : results) {
    if (result.ok()) { ++accepted; require_lease(result, result.token, 3, lifetime); }
    else require_error(result, "E_UNAUTHORIZED");
  }
  require(accepted == 1U && entropy_calls.load() == before_conflict + 1U &&
          permission_store_test_access::token_count(f.store) == 2U,
          "Different concurrent operations minted more than one generation from the same bearer");
}

bridge_request request_for(const std::string& token, const std::string& method, json params = json::object()) {
  bridge_request request;
  request.request_id = "renewal-request";
  request.session_id = session;
  request.origin = origin;
  request.token = token;
  request.method = method;
  request.params_json = params.dump();
  return request;
}
json envelope_for(const std::string& token, const std::string& method, json params = json::object()) {
  return json{{"requestId", "renewal-request"}, {"sessionId", session}, {"origin", origin},
              {"token", token}, {"method", method}, {"params", std::move(params)}};
}
void require_bridge_error(const bridge_response& value, const char* code) {
  require(!value.ok && value.error_code == code && value.payload_json.empty(),
          std::string{"Expected transport error: "} + code);
}

void transport_overwrites_native_identity_and_rejects_json_injection() {
  fixture f;
  permission_store other{&now};
  permission_store_test_access::entropy(other, &next_entropy);
  const auto other_token = other.issue(session, origin, {capability, renewal, "work.write"});
  const auto forged = other.resolve(session, origin, other_token, capability).scope;
  capability_registry registry;
  bridge_transport transport{registry, f.store};
  unsigned calls = 0;
  require(registry.register_contextual_method("work.inspect", capability, [&](const bridge_request& request) {
    require(request.authorization == f.scope && request.authorization != forged,
            "Transport trusted the caller-supplied native authorization");
    require(request.native_user_gesture, "Transport overwrote the native input-gesture evidence");
    ++calls;
    return std::string{"{}"};
  }), "Transport test method registration failed");
  auto request = request_for(f.token, "work.inspect");
  request.authorization = forged;
  request.native_user_gesture = true;
  require(transport.handle(request).ok && calls == 1U, "Valid credentials were not independently resolved");
  require(request.authorization == forged, "Transport mutated the caller's request object");
  request.token = other_token;
  request.authorization = f.scope;
  require_bridge_error(transport.handle(request), "E_UNAUTHORIZED");
  for (const auto& field : {"authorization", "authorization_id", "native_user_gesture"}) {
    auto envelope = envelope_for(f.token, "work.inspect");
    envelope[field] = "forged";
    const auto response = json::parse(transport.handle_json(envelope.dump(), true));
    require(response.at("ok") == false && response.at("error").at("code") == "E_SCHEMA_INVALID",
            "JSON supplied a native-only authorization or gesture field");
  }
  require(calls == 1U, "Rejected credentials reached a native handler");
  f.store.revoke_session(session);
  request.token = f.token;
  require_bridge_error(transport.handle(request), "E_UNAUTHORIZED");
  require(calls == 1U, "A borrowed revoked scope bypassed transport validation");
}

void renewal_transport_contract() {
  fixture f;
  capability_registry registry;
  bridge_transport transport{registry, f.store};
  require_bridge_error(transport.handle(request_for(f.token, renewal, {{"operationId", "first"}})), "E_UNKNOWN_METHOD");
  require(transport.enable_session_renewal() && !transport.enable_session_renewal(),
          "Renewal registration was not explicit and unique");
  require_bridge_error(transport.handle(request_for(f.token, renewal, {{"operationId", "first"}})), "E_RENEW_TOO_EARLY");
  clock_ms.store(renew_at);
  const std::vector<json> invalid{json::object(), {{"operationId", 7}}, {{"operationId", ""}},
    {{"operationId", "first"}, {"expiresInMs", 999999999}},
    {{"operationId", "first"}, {"capabilities", json::array({"work.write"})}},
    {{"operationId", "first"}, {"sessionId", "other"}},
    {{"operationId", "first"}, {"origin", "app://other"}}};
  for (const auto& params : invalid)
    require_bridge_error(transport.handle(request_for(f.token, renewal, params)), "E_INVALID_PARAMS");
  require(permission_store_test_access::token_count(f.store) == 1U, "Invalid renewal parameters changed credentials");
  const auto wire = json::parse(transport.handle_json(envelope_for(f.token, renewal, {{"operationId", "first"}}).dump()));
  require(wire.at("ok") == true && wire.at("requestId") == "renewal-request" && wire.at("sessionId") == session,
          "Renewal response lost request or session correlation");
  const auto payload = wire.at("result");
  require(payload.size() == 4U && payload.at("sessionId") == session && payload.at("origin") == origin &&
          payload.at("lease") == json{{"generation", 2}, {"expiresInMs", lifetime}, {"renewAfterMs", renew_at}},
          "Renewal serialized an invalid lease or unexpected authority fields");
  const auto token = payload.at("token").get<std::string>();
  require(token != f.token && f.store.resolve(session, origin, token, capability).scope == f.scope,
          "Serialized renewal changed native ownership identity");
  clock_ms.store(renew_at + 10000);
  const auto recovered = transport.handle(request_for(f.token, renewal, {{"operationId", "first"}}));
  require(recovered.ok && json::parse(recovered.payload_json).at("token") == token &&
          json::parse(recovered.payload_json).at("lease").at("expiresInMs") == lifetime - 10000,
          "Transport lost-ack recovery extended lifetime or minted another token");
  require_bridge_error(transport.handle(request_for(f.token, renewal, {{"operationId", "other"}})), "E_UNAUTHORIZED");
  clock_ms.store(renew_at + lifetime);
  require_bridge_error(transport.handle(request_for(token, renewal, {{"operationId", "first"}})), "E_SESSION_EXPIRED");
  require(!f.scope->active(), "Expired transport scope remained active");
}

void transport_capability_and_failure_paths() {
  fixture f;
  capability_registry registry;
  bridge_transport transport{registry, f.store};
  require(transport.enable_session_renewal(), "Renewal registration failed");
  const auto limited = f.store.issue(session, origin, {capability});
  clock_ms.store(renew_at);
  require_bridge_error(transport.handle(request_for(limited, renewal, {{"operationId", "denied"}})), "E_UNAUTHORIZED");
  permission_store_test_access::entropy(f.store, &failed_entropy);
  require_bridge_error(transport.handle(request_for(f.token, renewal, {{"operationId", "entropy"}})), "E_ENTROPY_UNAVAILABLE");
  require_lease(f.store.lease(session, origin, f.token), f.token, 1, 300000U);
  const auto error = json::parse(bridge_transport::serialize_lease(f.rotate("entropy")));
  require(error.size() == 2U && error.at("error") == "E_ENTROPY_UNAVAILABLE" && error.contains("message"),
          "Failure serialization leaked credentials or omitted its explicit error");
}
}  // namespace

int main() {
  try {
    using test = void (*)();
    const std::array<test, 14> tests{{issuance_and_isolation, exact_renewal_boundary_and_invalid_operations,
      lost_ack_and_overlap_deadline, overlap_never_extends_previous_expiration, expiration_is_not_recoverable,
      eight_simulated_hours_keep_identity_and_bounded_storage, independent_grants_and_revocation,
      store_destruction_revokes_borrowed_scope, entropy_failure_preserves_state, token_collision_preserves_all_grants,
      concurrent_lost_ack_retries_mint_once, transport_overwrites_native_identity_and_rejects_json_injection,
      renewal_transport_contract, transport_capability_and_failure_paths}};
    for (const auto test_case : tests) test_case();
    std::cout << "session_renewal: " << tests.size()
              << " no-desktop contract groups passed (fake clock; no real-duration or GUI claim)\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << "session_renewal: " << failure.what() << '\n';
    return 1;
  }
}
