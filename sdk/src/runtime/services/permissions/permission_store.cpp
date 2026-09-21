#include <soui/runtime/services/permissions/permission_store.h>

#include <iomanip>
#include <random>
#include <sstream>
#include <utility>

namespace soui::runtime {
namespace {

std::string make_token(std::uint64_t number) {
  std::random_device random;
  std::ostringstream stream;
  stream << "root-token-" << std::hex << random() << "-" << std::setw(8) << std::setfill('0') << number;
  return stream.str();
}

}  // namespace

std::string permission_store::issue(std::string session_id, std::string origin,
                                    const std::vector<std::string>& capabilities) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string token = make_token(next_token_++);
  permission_grant grant{std::move(session_id), std::move(origin), token, {},
                         now_() + std::chrono::minutes(30)};
  for (const auto& capability : capabilities) {
    if (!capability.empty()) {
      grant.capabilities.insert(capability);
    }
  }
  grants_[token] = std::move(grant);
  return token;
}

bool permission_store::authorize(const std::string& session_id, const std::string& origin,
                                 const std::string& token, const std::string& capability) const {
  return check(session_id, origin, token, capability) == authorization_result::authorized;
}

authorization_result permission_store::check(const std::string& session_id, const std::string& origin,
                                              const std::string& token, const std::string& capability) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = grants_.find(token);
  if (found == grants_.end()) {
    return authorization_result::denied;
  }
  const auto& grant = found->second;
  if (grant.session_id != session_id || grant.origin != origin ||
      grant.capabilities.find(capability) == grant.capabilities.end()) return authorization_result::denied;
  return now_() < grant.expires_at ? authorization_result::authorized : authorization_result::expired;
}

void permission_store::revoke_session(const std::string& session_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto iterator = grants_.begin(); iterator != grants_.end();) {
    if (iterator->second.session_id == session_id) {
      iterator = grants_.erase(iterator);
    } else {
      ++iterator;
    }
  }
}

}  // namespace soui::runtime
