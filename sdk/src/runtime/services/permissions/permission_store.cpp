#include <soui/runtime/services/permissions/permission_store.h>
#include "scope_id_sequence.h"
#include "scope_budget_ledger.h"
#include "../../diagnostics/runtime_metrics_internal.h"

#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace dm = soui::sdk::diagnostics;
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
const std::shared_ptr<detail::scope_budget_ledger>& scope_budget() {
  static const auto budget = std::make_shared<detail::scope_budget_ledger>();
  return budget;
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
  // Declared first, destroyed last, after all actual owned state fields.
  detail::scope_budget_slot module_slot;
  mutable std::mutex mutex;
  permission_store::clock_source now{&permission_store::clock_type::now};
  std::string scope_id, session_id, origin, token, previous_token, last_operation;
  std::unordered_set<std::string> capabilities;
  std::shared_ptr<const native_connection_binding> local_binding;
  std::unordered_set<std::string> local_methods;
  permission_store::clock_type::time_point expires_at{}, previous_until{};
  std::uint64_t generation{1};
  bool revoked{false};
  bool stored_owner_counted{false}, metrics_previous_counted{false};

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
permission_store::permission_store(clock_source now, dm::mode enabled)
    : metrics_mode_(enabled), metrics_epoch_(enabled == dm::mode::scalar ? dm::detail::epoch() : 0),
      now_(now ? now : &clock_type::now), scope_budget_(scope_budget()) {}
dm::status permission_store::read_runtime_metrics(dm::permissions& out) const noexcept {
  out = {};
  if (metrics_mode_ != dm::mode::scalar) return dm::status::disabled;
  try {
    auto observed = dm::detail::begin(metrics_mode_, metrics_epoch_);
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) { observed.state=dm::status::busy; dm::detail::end(observed); out.observation=observed; return observed.state; }
    out=metrics_; out.observation=observed; out.observation.counter_saturated=metrics_saturated_;
    out.stored_scope_owners=stored_scope_count_; out.bearer_alias_entries=grants_.size();
    out.has_scope_capacity_bound=true; out.stored_scope_limit=scope_limit;
    out.pending_scope_reservations=pending_scope_reservations_;
    out.pending_alias_reservations=pending_scope_reservations_;
    out.committed_alias_limit=alias_limit; out.renewal_alias_transient_limit=renewal_alias_limit;
    out.module_scope_limit=detail::scope_budget_ledger::limit;
    out.module_scope_slots_used=scope_budget_->outstanding();
    dm::detail::end(out.observation); return out.observation.state;
  } catch (...) { out={}; out.observation.state=dm::status::unavailable; return out.observation.state; }
}
permission_store::~permission_store() {
  std::array<grant_map::node_type, alias_limit> retired;
  std::size_t retired_count = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // Borrowed scopes may outlive the issuer, never its authority.
    for (auto iterator = grants_.begin(); iterator != grants_.end();) {
      {
        std::lock_guard<std::mutex> state_lock(iterator->second->data_->mutex);
        iterator->second->data_->revoked = true;
        iterator->second->data_->stored_owner_counted = false;
      }
      retired[retired_count++] = grants_.extract(iterator++);
    }
    stored_scope_count_ = 0;
  }
  // Actual final scope destruction and module refund occur without store lock.
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
  struct pending_refund {
    permission_store& owner;
    bool active{false};
    ~pending_refund() {
      if (active) {
        std::lock_guard<std::mutex> lock(owner.mutex_);
        --owner.pending_scope_reservations_;
      }
    }
  } pending{*this};
  detail::scope_budget_slot module_slot;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stored_scope_count_ + pending_scope_reservations_ >= scope_limit) {
      if (metrics_mode_ == dm::mode::scalar)
        dm::detail::increment(metrics_.store_scope_capacity_rejections_total, metrics_saturated_);
      throw scope_capacity_error(scope_capacity_reason::store);
    }
    if (grants_.size() + pending_scope_reservations_ >= alias_limit) {
      if (metrics_mode_ == dm::mode::scalar)
        dm::detail::increment(metrics_.alias_capacity_rejections_total, metrics_saturated_);
      throw scope_capacity_error(scope_capacity_reason::aliases);
    }
    module_slot = detail::scope_budget_slot::acquire(scope_budget_);
    if (!module_slot) {
      if (metrics_mode_ == dm::mode::scalar)
        dm::detail::increment(metrics_.module_scope_capacity_rejections_total, metrics_saturated_);
      throw scope_capacity_error(scope_capacity_reason::module);
    }
    ++pending_scope_reservations_;
    pending.active = true;
  }
  auto data = std::make_unique<authorization_scope::state>();
  data->module_slot = std::move(module_slot);
  data->session_id = std::move(session_id); data->origin = std::move(origin); data->now = now_;
  data->local_binding = binding;
  data->local_methods.insert(methods.begin(), methods.end());
  for (const auto& capability : capabilities) if (!capability.empty()) data->capabilities.insert(capability);
  std::string token;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    token = make_token();
    if (grants_.count(token)) throw std::runtime_error("Authorization entropy collision");
  }
  data->scope_id = "soui-scope-" + std::to_string(scope_ids().allocate());
  data->token = token; data->expires_at = now_() + lifetime;
  auto scope = std::shared_ptr<authorization_scope>(new authorization_scope(std::move(data)));
  issued_local_grant issued{token, scope};
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // A concurrent in-flight issuance may have minted the same token.
    if (grants_.count(token)) throw std::runtime_error("Authorization entropy collision");
    grants_.emplace(token, scope);
    scope->data_->stored_owner_counted = true;
    ++stored_scope_count_; --pending_scope_reservations_; pending.active = false;
    if (metrics_mode_ == dm::mode::scalar) {
      ++metrics_.stored_scope_owners;
      dm::detail::high_water(metrics_.bearer_alias_high_water, grants_.size());
      dm::detail::increment(metrics_.issued_scopes_total, metrics_saturated_);
    }
  }
  // Return moves cannot throw; all real owners were prepared before map admission.
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
  grant_map::node_type retired_alias;
  permission_lease failure;
  if (!valid_operation(operation_id)) { failure.error_code = "E_INVALID_PARAMS"; return failure; }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = grants_.find(token);
  if (found == grants_.end()) {
    if (metrics_mode_ == dm::mode::scalar) dm::detail::increment(metrics_.checked_renewal_rejections_total, metrics_saturated_);
    return failure;
  }
  // Keep the shared owner, not an iterator: inserting a token can rehash the map.
  const auto scope = found->second;
  auto& data = *scope->data_;
  std::lock_guard<std::mutex> state_lock(data.mutex);
  const auto now = now_();
  const auto checked = data.check(session, origin, token, renewal_capability, now, binding, renewal_capability);
  if (checked != authorization_result::authorized) {
    if (metrics_mode_ == dm::mode::scalar) dm::detail::increment(metrics_.checked_renewal_rejections_total, metrics_saturated_);
    if (checked == authorization_result::expired) failure.error_code = "E_SESSION_EXPIRED";
    return failure;
  }
  if (operation_id == data.last_operation) {
    auto result=data.snapshot(now);
    if (metrics_mode_ == dm::mode::scalar) dm::detail::increment(metrics_.idempotent_renewals_total, metrics_saturated_);
    return result;
  }
  if (token != data.token) {
    if (metrics_mode_ == dm::mode::scalar) dm::detail::increment(metrics_.checked_renewal_rejections_total, metrics_saturated_);
    return failure;
  }
  if (data.expires_at - now > renewal_window) {
    if (metrics_mode_ == dm::mode::scalar) {
      dm::detail::increment(metrics_.renewal_too_early_total, metrics_saturated_);
      dm::detail::increment(metrics_.checked_renewal_rejections_total, metrics_saturated_);
    }
    failure.error_code = "E_RENEW_TOO_EARLY"; return failure;
  }
  if (data.generation >= 9007199254740991ULL) {
    if (metrics_mode_ == dm::mode::scalar) dm::detail::increment(metrics_.checked_renewal_rejections_total, metrics_saturated_);
    failure.error_code = "E_LIMIT_GENERATION"; return failure;
  }
  if (grants_.size() + pending_scope_reservations_ >= renewal_alias_limit) {
    if (metrics_mode_ == dm::mode::scalar) {
      dm::detail::increment(metrics_.alias_capacity_rejections_total, metrics_saturated_);
      dm::detail::increment(metrics_.checked_renewal_rejections_total, metrics_saturated_);
    }
    failure.error_code = "E_LIMIT_SCOPE_CAPACITY"; return failure;
  }
  std::string next_token;
  try { next_token = make_token(); }
  catch (const std::runtime_error&) {
    if (metrics_mode_ == dm::mode::scalar) dm::detail::increment(metrics_.checked_renewal_rejections_total, metrics_saturated_);
    failure.error_code = "E_ENTROPY_UNAVAILABLE"; return failure;
  }
  if (grants_.count(next_token)) {
    if (metrics_mode_ == dm::mode::scalar) dm::detail::increment(metrics_.checked_renewal_rejections_total, metrics_saturated_);
    failure.error_code = "E_ENTROPY_UNAVAILABLE"; return failure;
  }
  auto next_operation = operation_id;
  auto result = data.snapshot(now);
  result.token = next_token; result.generation = data.generation + 1;
  result.expires_in_ms = 1800000U; result.renew_after_ms = 1500000U;
  // Prepare allocating response fields before the alias/state commit.
  grants_.emplace(next_token, scope);
  if (metrics_mode_ == dm::mode::scalar)
    dm::detail::high_water(metrics_.bearer_alias_high_water, grants_.size());
  if (!data.previous_token.empty()) retired_alias = grants_.extract(data.previous_token);
  data.previous_token.swap(data.token);
  data.token.swap(next_token);
  data.last_operation.swap(next_operation);
  data.previous_until = std::min(now + overlap, data.expires_at);
  data.expires_at = now + lifetime;
  ++data.generation;
  if (metrics_mode_ == dm::mode::scalar) {
    if (!data.metrics_previous_counted) { data.metrics_previous_counted=true; ++metrics_.stored_previous_aliases; }
    dm::detail::increment(metrics_.rotated_leases_total, metrics_saturated_);
  }
  return result;
}
bool permission_store::revoke_scope(const std::shared_ptr<const authorization_scope>& scope) {
  if (!scope) return false;
  std::array<grant_map::node_type, alias_limit> retired;
  std::size_t retired_count = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = std::find_if(grants_.begin(), grants_.end(), [&](const auto& entry) {
      return entry.second == scope;
    });
    if (found == grants_.end()) return false;
    {
      std::lock_guard<std::mutex> state_lock(scope->data_->mutex);
      scope->data_->revoked = true;
      if (scope->data_->stored_owner_counted) {
        scope->data_->stored_owner_counted = false; --stored_scope_count_;
        if (metrics_mode_ == dm::mode::scalar) {
          --metrics_.stored_scope_owners;
          if (scope->data_->metrics_previous_counted) { scope->data_->metrics_previous_counted=false; --metrics_.stored_previous_aliases; }
          dm::detail::increment(metrics_.revoked_scopes_total, metrics_saturated_);
        }
      }
    }
    for (auto iterator = grants_.begin(); iterator != grants_.end();) {
      if (iterator->second == scope) retired[retired_count++] = grants_.extract(iterator++);
      else ++iterator;
    }
  }
  return true;
}

void permission_store::revoke_session(const std::string& session_id) {
  std::array<grant_map::node_type, alias_limit> retired;
  std::size_t retired_count = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto iterator = grants_.begin(); iterator != grants_.end();) {
      const auto scope = iterator->second;
      auto& data = *scope->data_;
      if (data.session_id == session_id) {
        std::lock_guard<std::mutex> state_lock(data.mutex);
        data.revoked = true;
        if (data.stored_owner_counted) {
          data.stored_owner_counted = false; --stored_scope_count_;
          if (metrics_mode_ == dm::mode::scalar) {
            --metrics_.stored_scope_owners;
            if (data.metrics_previous_counted) { data.metrics_previous_counted=false; --metrics_.stored_previous_aliases; }
            dm::detail::increment(metrics_.revoked_scopes_total, metrics_saturated_);
          }
        }
        retired[retired_count++] = grants_.extract(iterator++);
      } else ++iterator;
    }
  }
}
}  // namespace soui::runtime
