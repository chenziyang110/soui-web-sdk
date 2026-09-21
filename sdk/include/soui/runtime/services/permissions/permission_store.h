#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace soui::runtime {

enum class authorization_result { authorized, denied, expired };

struct permission_grant {
  std::string session_id;
  std::string origin;
  std::string token;
  std::unordered_set<std::string> capabilities;
  std::chrono::steady_clock::time_point expires_at;
};

class permission_store {
 public:
  using clock_type = std::chrono::steady_clock;
  using clock_source = clock_type::time_point (*)();
  permission_store() = default;
  // Tests may supply a monotonic clock; production defaults and the 30-minute
  // grant duration are unchanged. No clock control is exposed through Bridge.
  explicit permission_store(clock_source now) : now_(now ? now : &clock_type::now) {}
  permission_store(const permission_store&) = delete;
  permission_store& operator=(const permission_store&) = delete;

  [[nodiscard]] std::string issue(std::string session_id, std::string origin,
                                  const std::vector<std::string>& capabilities);
  [[nodiscard]] bool authorize(const std::string& session_id, const std::string& origin,
                               const std::string& token, const std::string& capability) const;
  [[nodiscard]] authorization_result check(const std::string& session_id, const std::string& origin,
                                            const std::string& token, const std::string& capability) const;
  void revoke_session(const std::string& session_id);

 private:
  clock_source now_{&clock_type::now};
  mutable std::mutex mutex_;
  std::uint64_t next_token_{1};
  std::unordered_map<std::string, permission_grant> grants_;
};

}  // namespace soui::runtime
