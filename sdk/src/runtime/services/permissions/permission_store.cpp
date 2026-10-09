#include <soui/runtime/services/permissions/permission_store.h>
#include "scope_id_sequence.h"

#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace soui::runtime {
namespace {

constexpr auto lifetime = std::chrono::minutes(30);
constexpr auto renewal_window = std::chrono::minutes(5);
constexpr auto overlap = std::chrono::seconds(30);
constexpr const char* renewal_capability = "bridge.session.renew";
detail::scope_id_sequence& scope_ids() {
  static detail::scope_id_sequence sequence;
  return sequence;
}
bool valid_operation(const std::string& value) {
  return !value.empty() && value.size() <= 128U &&
      std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ':';
      });
}

bool valid_local_text(const std::string& value, std::size_t bound) {
  return !value.empty() && value.size() <= bound && value.find('\0') == std::string::npos &&
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), nullptr, 0) > 0;
}

void validate_local_policy(const std::vector<std::string>& capabilities,
                           const std::vector<std::string>& methods) {
  if (capabilities.empty() || methods.empty() || capabilities.size() > 64U || methods.size() > 64U)
    throw std::invalid_argument("Local authorization requires bounded explicit capability and method policies");
  std::unordered_set<std::string> names;
  for (const auto& capability : capabilities)
    if (!valid_local_text(capability, 512U) || !names.insert(capability).second)
      throw std::invalid_argument("Invalid or duplicate local capability");
  names.clear();
  for (const auto& method : methods)
    if (!valid_operation(method) || !names.insert(method).second)
      throw std::invalid_argument("Invalid or duplicate local method");
}

}  // namespace

struct authorization_scope::state {
  mutable std::mutex mutex;
  permission_store::clock_source now{&permission_store::clock_type::now};
  std::string scope_id, session_id, origin, token, previous_token, last_operation;
  std::unordered_set<std::string> capabilities;
  std::shared_ptr<const native_connection_binding> local_binding;
  std::unordered_set<std::string> local_methods;
  permission_store::clock_type::time_point expires_at{}, previous_until{};
  std::uint64_t generation{1};
  bool revoked{false};

  bool active_at(permission_store::clock_type::time_point time) const {
    return !revoked && time < expires_at && (!local_binding || local_binding->active());
  }
  authorization_result check(const std::string& session, const std::string& source,
      const std::string& bearer, const std::string& capability,
      permission_store::clock_type::time_point time,
      const std::shared_ptr<const native_connection_binding>& binding,
      const std::string& method) const {
    // A token never changes its native channel or actual connection ownership.
    if (local_binding != binding || (local_binding &&
        (!local_binding->active() || !local_methods.count(method))))
      return authorization_result::denied;
    if (revoked || session != session_id || source != origin || !capabilities.count(capability))
      return authorization_result::denied;
    if (time >= expires_at) return authorization_result::expired;
    if (bearer == token || (bearer == previous_token && !previous_token.empty() && time < previous_until))
      return authorization_result::authorized;
    return authorization_result::denied;
  }
  permission_lease snapshot(permission_store::clock_type::time_point time) const {
    permission_lease result;
    result.error_code.clear();
    result.session_id = session_id; result.origin = origin; result.token = token;
    result.generation = generation;
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(expires_at - time).count();
    result.expires_in_ms = static_cast<std::uint32_t>(std::max<std::int64_t>(0, remaining));
    result.renew_after_ms = result.expires_in_ms > 300000U ? result.expires_in_ms - 300000U : 0U;
    return result;
  }
};

native_connection_binding::native_connection_binding(std::weak_ptr<const void> issuer,
    std::string session_id, std::string origin)
    : issuer_(std::move(issuer)), session_id_(std::move(session_id)), origin_(std::move(origin)) {}
bool native_connection_binding::active() const noexcept {
  return !closed_.load() && !issuer_.expired();
}
void native_connection_binding::close() noexcept { closed_.store(true); }

authorization_scope::authorization_scope(std::unique_ptr<state> data) : data_(std::move(data)) {}
authorization_scope::~authorization_scope() = default;
const std::string& authorization_scope::scope_id() const noexcept { return data_->scope_id; }
bool authorization_scope::active() const {
  std::lock_guard<std::mutex> lock(data_->mutex);
  return data_->active_at(data_->now());
}
bool authorization_scope::allows(const std::string& capability) const {
  std::lock_guard<std::mutex> lock(data_->mutex);
  return data_->active_at(data_->now()) && data_->capabilities.count(capability) != 0U;
}
bool authorization_scope::allows_local_method(const std::string& method, const std::string& capability) const {
  std::lock_guard<std::mutex> lock(data_->mutex);
  return data_->local_binding && data_->active_at(data_->now()) &&
      data_->local_methods.count(method) != 0U && data_->capabilities.count(capability) != 0U;
}
bool authorization_scope::matches(const std::string& session, const std::string& origin) const {
  // Identity fields never change; the live check is separate and must occur at admission.
  return data_->session_id == session && data_->origin == origin;
}

bool permission_store::system_entropy(unsigned char* bytes, std::size_t size) {
  return size <= std::numeric_limits<ULONG>::max() && BCryptGenRandom(nullptr, bytes, static_cast<ULONG>(size),
                                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}
std::string permission_store::make_token() const {
  std::array<unsigned char, 32> bytes{};
  if (!entropy_(bytes.data(), bytes.size())) throw std::runtime_error("Authorization entropy unavailable");
  constexpr char hex[] = "0123456789abcdef";
  std::string value = "soui-token-";
  value.reserve(value.size() + bytes.size() * 2U);
  for (const auto byte : bytes) { value += hex[byte >> 4U]; value += hex[byte & 15U]; }
  return value;
}
permission_store::~permission_store() {
  // Borrowed authorization objects may outlive the issuing store, never its authority.
  for (auto& item : grants_) {
    std::lock_guard<std::mutex> lock(item.second->data_->mutex);
    item.second->data_->revoked = true;
  }
}
std::string permission_store::issue(std::string session_id, std::string origin,
                                    const std::vector<std::string>& capabilities) {
  return issue_impl(std::move(session_id), std::move(origin), capabilities, {}, {}).token;
}
std::shared_ptr<native_connection_binding> permission_store::create_local_binding(
    std::string session_id, std::string origin) const {
  if (!valid_operation(session_id) || !valid_local_text(origin, 512U) ||
      (origin.rfind("app://", 0) != 0 && origin.rfind("file://", 0) != 0))
    throw std::invalid_argument("Invalid local server session/origin");
  return std::shared_ptr<native_connection_binding>(new native_connection_binding(
      local_issuer_, std::move(session_id), std::move(origin)));
}
bool permission_store::owns_binding(const std::shared_ptr<const native_connection_binding>& binding) const {
  return binding && binding->issuer_.lock() == local_issuer_ && binding->active();
}
issued_local_grant permission_store::issue_local(const std::shared_ptr<const native_connection_binding>& binding,
    const std::vector<std::string>& capabilities, const std::vector<std::string>& methods) {
  if (!owns_binding(binding)) throw std::invalid_argument("An active local binding from this store is required");
  validate_local_policy(capabilities, methods);
  return issue_impl(binding->session_id_, binding->origin_, capabilities, binding, methods);
}
issued_local_grant permission_store::issue_impl(std::string session_id, std::string origin,
    const std::vector<std::string>& capabilities,
    const std::shared_ptr<const native_connection_binding>& binding,
    const std::vector<std::string>& methods) {
  auto data = std::make_unique<authorization_scope::state>();
  data->session_id = std::move(session_id); data->origin = std::move(origin); data->now = now_;
  data->local_binding = binding;
  data->local_methods.insert(methods.begin(), methods.end());
  for (const auto& capability : capabilities) if (!capability.empty()) data->capabilities.insert(capability);
  std::lock_guard<std::mutex> lock(mutex_);
  const auto token = make_token();
  if (grants_.count(token)) throw std::runtime_error("Authorization entropy collision");
  data->scope_id = "soui-scope-" + std::to_string(scope_ids().allocate());
  data->token = token; data->expires_at = now_() + lifetime;
  auto scope = std::shared_ptr<authorization_scope>(new authorization_scope(std::move(data)));
  // All allocating return preparation precedes map admission. After successful
  // insertion the owner is returned by noexcept moves, including a close race.
  issued_local_grant issued{token, scope};
  grants_.emplace(token, std::move(scope));
  return issued;
}
bool permission_store::authorize(const std::string& session, const std::string& origin,
                                 const std::string& token, const std::string& capability) const {
  return check(session, origin, token, capability) == authorization_result::authorized;
}
authorization_result permission_store::check(const std::string& session, const std::string& origin,
                                             const std::string& token, const std::string& capability) const {
  return resolve(session, origin, token, capability).result;
}
checked_authorization permission_store::resolve(const std::string& session, const std::string& origin,
                                                const std::string& token, const std::string& capability) const {
  return resolve_impl(session, origin, token, capability, {}, {});
}
checked_authorization permission_store::resolve_local(
    const std::shared_ptr<const native_connection_binding>& binding, const std::string& token,
    const std::string& method, const std::string& capability) const {
  if (!owns_binding(binding)) return {};
  return resolve_impl(binding->session_id_, binding->origin_, token, capability, binding, method);
}
checked_authorization permission_store::resolve_impl(const std::string& session, const std::string& origin,
    const std::string& token, const std::string& capability,
    const std::shared_ptr<const native_connection_binding>& binding, const std::string& method) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = grants_.find(token);
  if (found == grants_.end()) return {};
  const auto& scope = found->second;
  std::lock_guard<std::mutex> state_lock(scope->data_->mutex);
  const auto result = scope->data_->check(session, origin, token, capability, now_(), binding, method);
  return {result, result == authorization_result::authorized ? scope : nullptr};
}
permission_lease permission_store::lease(const std::string& session, const std::string& origin,
                                         const std::string& token) const {
  return lease_impl(session, origin, token, {});
}
permission_lease permission_store::lease_local(
    const std::shared_ptr<const native_connection_binding>& binding, const std::string& token) const {
  if (!owns_binding(binding)) return {};
  return lease_impl(binding->session_id_, binding->origin_, token, binding);
}
permission_lease permission_store::lease_impl(const std::string& session, const std::string& origin,
    const std::string& token, const std::shared_ptr<const native_connection_binding>& binding) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = grants_.find(token);
  if (found == grants_.end()) return {};
  auto& data = *found->second->data_;
  std::lock_guard<std::mutex> state_lock(data.mutex);
  const auto now = now_();
  const auto checked = data.check(session, origin, token, renewal_capability, now, binding, renewal_capability);
  if (checked != authorization_result::authorized || token != data.token) {
    permission_lease result;
    if (checked == authorization_result::expired) result.error_code = "E_SESSION_EXPIRED";
    return result;
  }
  return data.snapshot(now);
}
permission_lease permission_store::renew(const std::string& session, const std::string& origin,
                                         const std::string& token, const std::string& operation_id) {
  return renew_impl(session, origin, token, operation_id, {});
}
permission_lease permission_store::renew_local(
    const std::shared_ptr<const native_connection_binding>& binding, const std::string& token,
    const std::string& operation_id) {
  if (!owns_binding(binding)) return {};
  return renew_impl(binding->session_id_, binding->origin_, token, operation_id, binding);
}
permission_lease permission_store::renew_impl(const std::string& session, const std::string& origin,
    const std::string& token, const std::string& operation_id,
    const std::shared_ptr<const native_connection_binding>& binding) {
  permission_lease failure;
  if (!valid_operation(operation_id)) { failure.error_code = "E_INVALID_PARAMS"; return failure; }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = grants_.find(token);
  if (found == grants_.end()) return failure;
  // Keep the shared owner, not an iterator: inserting a token can rehash the map.
  const auto scope = found->second;
  auto& data = *scope->data_;
  std::lock_guard<std::mutex> state_lock(data.mutex);
  const auto now = now_();
  const auto checked = data.check(session, origin, token, renewal_capability, now, binding, renewal_capability);
  if (checked != authorization_result::authorized) {
    if (checked == authorization_result::expired) failure.error_code = "E_SESSION_EXPIRED";
    return failure;
  }
  if (operation_id == data.last_operation) return data.snapshot(now);
  if (token != data.token) return failure;
  if (data.expires_at - now > renewal_window) { failure.error_code = "E_RENEW_TOO_EARLY"; return failure; }
  if (data.generation >= 9007199254740991ULL) { failure.error_code = "E_LIMIT_GENERATION"; return failure; }
  std::string next_token;
  try { next_token = make_token(); }
  catch (const std::runtime_error&) { failure.error_code = "E_ENTROPY_UNAVAILABLE"; return failure; }
  if (grants_.count(next_token)) { failure.error_code = "E_ENTROPY_UNAVAILABLE"; return failure; }
  auto next_operation = operation_id;
  // All potentially allocating preparation precedes the state transition.
  grants_.emplace(next_token, scope);
  if (!data.previous_token.empty()) grants_.erase(data.previous_token);
  data.previous_token.swap(data.token);
  data.token.swap(next_token);
  data.last_operation.swap(next_operation);
  data.previous_until = std::min(now + overlap, data.expires_at);
  data.expires_at = now + lifetime;
  ++data.generation;
  return data.snapshot(now);
}

bool permission_store::revoke_scope(const std::shared_ptr<const authorization_scope>& scope) {
  if (!scope) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = std::find_if(grants_.begin(), grants_.end(), [&](const auto& entry) {
    return entry.second == scope;
  });
  if (found == grants_.end()) return false;
  {
    std::lock_guard<std::mutex> state_lock(scope->data_->mutex);
    scope->data_->revoked = true;
  }
  // Invalidate the held native object before removing every bearer alias.
  for (auto iterator = grants_.begin(); iterator != grants_.end();) {
    if (iterator->second == scope) iterator = grants_.erase(iterator);
    else ++iterator;
  }
  return true;
}

void permission_store::revoke_session(const std::string& session_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto iterator = grants_.begin(); iterator != grants_.end();) {
    const auto scope = iterator->second;
    auto& data = *scope->data_;
    if (data.session_id == session_id) {
      std::lock_guard<std::mutex> state_lock(data.mutex);
      data.revoked = true;
      iterator = grants_.erase(iterator);
    } else {
      ++iterator;
    }
  }
}

}  // namespace soui::runtime
