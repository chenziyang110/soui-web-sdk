#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace soui::runtime {

enum class authorization_result { authorized, denied, expired };

// Native identity for one connection, minted by the issuing store. Creation is
// not OS authentication: the trusted endpoint must verify and authorize first.
// This object and local bearers are never accepted from page/wire JSON.
class native_connection_binding final {
 public:
  native_connection_binding(const native_connection_binding&) = delete;
  native_connection_binding& operator=(const native_connection_binding&) = delete;
  [[nodiscard]] bool active() const noexcept;
  // Irreversible admission revocation, not a worker/callback/handle barrier.
  void close() noexcept;

 private:
  friend class permission_store;
  native_connection_binding(std::weak_ptr<const void> issuer, std::string session_id, std::string origin);
  std::weak_ptr<const void> issuer_;
  const std::string session_id_, origin_;
  std::atomic<bool> closed_{false};
};

// Stable native identity. Holding it does not keep expired/revoked grants live.
class authorization_scope final {
 public:
  ~authorization_scope();
  authorization_scope(const authorization_scope&) = delete;
  authorization_scope& operator=(const authorization_scope&) = delete;
  // Immutable module-local correlation ID, never an authorization credential.
  // Renewal preserves it; revocation/expiry cannot make the grant active again.
  [[nodiscard]] const std::string& scope_id() const noexcept;
  [[nodiscard]] bool active() const;
  [[nodiscard]] bool allows(const std::string& capability) const;
  [[nodiscard]] bool matches(const std::string& session_id, const std::string& origin) const;

 private:
  friend class permission_store;
  struct state;
  explicit authorization_scope(std::unique_ptr<state> data);
  std::unique_ptr<state> data_;
};

struct checked_authorization {
  authorization_result result{authorization_result::denied};
  std::shared_ptr<const authorization_scope> scope;
};

// Always preserves the precise native cleanup owner, even if close wins a
// concurrent issuance. Holding this scope never replaces method resolution.
struct issued_local_grant {
  std::string token;
  std::shared_ptr<const authorization_scope> scope;
};

struct permission_lease {
  std::string error_code{"E_UNAUTHORIZED"};
  std::string session_id;
  std::string origin;
  std::string token;
  std::uint64_t generation{0};
  std::uint32_t expires_in_ms{0};
  std::uint32_t renew_after_ms{0};
  [[nodiscard]] bool ok() const { return error_code.empty(); }
};

class permission_store {
 public:
  using clock_type = std::chrono::steady_clock;
  using clock_source = clock_type::time_point (*)();
  permission_store() = default;
  // Tests may supply a monotonic clock; production defaults and the 30-minute
  // grant duration are unchanged. No clock control is exposed through Bridge.
  explicit permission_store(clock_source now) : now_(now ? now : &clock_type::now) {}
  ~permission_store();
  permission_store(const permission_store&) = delete;
  permission_store& operator=(const permission_store&) = delete;

  [[nodiscard]] std::string issue(std::string session_id, std::string origin,
                                  const std::vector<std::string>& capabilities);
  [[nodiscard]] std::shared_ptr<native_connection_binding> create_local_binding(
      std::string session_id, std::string origin) const;
  // Explicit bounded native policy. Empty/invalid/duplicate policies throw.
  [[nodiscard]] issued_local_grant issue_local(const std::shared_ptr<const native_connection_binding>& binding,
      const std::vector<std::string>& capabilities, const std::vector<std::string>& methods);
  [[nodiscard]] bool authorize(const std::string& session_id, const std::string& origin,
                               const std::string& token, const std::string& capability) const;
  [[nodiscard]] authorization_result check(const std::string& session_id, const std::string& origin,
                                            const std::string& token, const std::string& capability) const;
  [[nodiscard]] checked_authorization resolve(const std::string& session_id, const std::string& origin,
                                               const std::string& token, const std::string& capability) const;
  [[nodiscard]] checked_authorization resolve_local(
      const std::shared_ptr<const native_connection_binding>& binding, const std::string& token,
      const std::string& method, const std::string& capability) const;
  // Both methods require the explicit bridge.session.renew capability.
  [[nodiscard]] permission_lease lease(const std::string& session_id, const std::string& origin,
                                       const std::string& token) const;
  [[nodiscard]] permission_lease renew(const std::string& session_id, const std::string& origin,
                                       const std::string& token, const std::string& operation_id);
  [[nodiscard]] permission_lease lease_local(
      const std::shared_ptr<const native_connection_binding>& binding, const std::string& token) const;
  [[nodiscard]] permission_lease renew_local(
      const std::shared_ptr<const native_connection_binding>& binding, const std::string& token,
      const std::string& operation_id);
  // Revoke only an actual scope owned by this store, including overlapping
  // bearers. Empty, foreign-store and already removed scopes return false.
  [[nodiscard]] bool revoke_scope(const std::shared_ptr<const authorization_scope>& scope);
  void revoke_session(const std::string& session_id);

 private:
  friend struct permission_store_test_access;
  using entropy_source = bool (*)(unsigned char*, std::size_t);
  static bool system_entropy(unsigned char* bytes, std::size_t size);
  [[nodiscard]] std::string make_token() const;
  [[nodiscard]] bool owns_binding(const std::shared_ptr<const native_connection_binding>& binding) const;
  [[nodiscard]] issued_local_grant issue_impl(std::string session_id, std::string origin,
      const std::vector<std::string>& capabilities,
      const std::shared_ptr<const native_connection_binding>& binding,
      const std::vector<std::string>& methods);
  [[nodiscard]] checked_authorization resolve_impl(const std::string& session_id, const std::string& origin,
      const std::string& token, const std::string& capability,
      const std::shared_ptr<const native_connection_binding>& binding, const std::string& method) const;
  [[nodiscard]] permission_lease lease_impl(const std::string& session_id, const std::string& origin,
      const std::string& token, const std::shared_ptr<const native_connection_binding>& binding) const;
  [[nodiscard]] permission_lease renew_impl(const std::string& session_id, const std::string& origin,
      const std::string& token, const std::string& operation_id,
      const std::shared_ptr<const native_connection_binding>& binding);
  clock_source now_{&clock_type::now};
  mutable std::mutex mutex_;
  entropy_source entropy_{&system_entropy};
  const std::shared_ptr<const void> local_issuer_{std::make_shared<const unsigned char>(0)};
  std::unordered_map<std::string, std::shared_ptr<authorization_scope>> grants_;
};

}  // namespace soui::runtime
