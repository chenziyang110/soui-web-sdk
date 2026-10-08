#include <soui/runtime/services/permissions/permission_store.h>

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
bool valid_operation(const std::string& value) {
  return !value.empty() && value.size() <= 128U &&
      std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ':';
      });
}

}  // namespace

struct authorization_scope::state {
  mutable std::mutex mutex;
  permission_store::clock_source now{&permission_store::clock_type::now};
  std::string session_id, origin, token, previous_token, last_operation;
  std::unordered_set<std::string> capabilities;
  permission_store::clock_type::time_point expires_at{}, previous_until{};
  std::uint64_t generation{1};
  bool revoked{false};

  bool active_at(permission_store::clock_type::time_point time) const {
    return !revoked && time < expires_at;
  }
  authorization_result check(const std::string& session, const std::string& source,
      const std::string& bearer, const std::string& capability,
      permission_store::clock_type::time_point time) const {
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

authorization_scope::authorization_scope(std::unique_ptr<state> data) : data_(std::move(data)) {}
authorization_scope::~authorization_scope() = default;
bool authorization_scope::active() const {
  std::lock_guard<std::mutex> lock(data_->mutex);
  return data_->active_at(data_->now());
}
bool authorization_scope::allows(const std::string& capability) const {
  std::lock_guard<std::mutex> lock(data_->mutex);
  return data_->active_at(data_->now()) && data_->capabilities.count(capability) != 0U;
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
  auto data = std::make_unique<authorization_scope::state>();
  data->session_id = std::move(session_id); data->origin = std::move(origin); data->now = now_;
  for (const auto& capability : capabilities) if (!capability.empty()) data->capabilities.insert(capability);
  std::lock_guard<std::mutex> lock(mutex_);
  const auto token = make_token();
  if (grants_.count(token)) throw std::runtime_error("Authorization entropy collision");
  data->token = token; data->expires_at = now_() + lifetime;
  auto scope = std::shared_ptr<authorization_scope>(new authorization_scope(std::move(data)));
  grants_.emplace(token, std::move(scope));
  return token;
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
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = grants_.find(token);
  if (found == grants_.end()) return {};
  const auto& scope = found->second;
  std::lock_guard<std::mutex> state_lock(scope->data_->mutex);
  const auto result = scope->data_->check(session, origin, token, capability, now_());
  return {result, result == authorization_result::authorized ? scope : nullptr};
}
permission_lease permission_store::lease(const std::string& session, const std::string& origin,
                                         const std::string& token) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = grants_.find(token);
  if (found == grants_.end()) return {};
  auto& data = *found->second->data_;
  std::lock_guard<std::mutex> state_lock(data.mutex);
  const auto now = now_();
  const auto checked = data.check(session, origin, token, renewal_capability, now);
  if (checked != authorization_result::authorized || token != data.token) {
    permission_lease result;
    if (checked == authorization_result::expired) result.error_code = "E_SESSION_EXPIRED";
    return result;
  }
  return data.snapshot(now);
}
permission_lease permission_store::renew(const std::string& session, const std::string& origin,
                                         const std::string& token, const std::string& operation_id) {
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
  const auto checked = data.check(session, origin, token, renewal_capability, now);
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
