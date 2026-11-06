#pragma once

#include <json.hpp>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace soui::sdk::detail {

// Routing information only. The original Bridge still parses/authenticates raw.
// Bounded strings avoid retaining another JSON DOM or unbounded duplicate data.
class request_dispatch_metadata final {
 public:
  static constexpr std::size_t method_limit = 128;
  static constexpr std::size_t session_limit = 256;

  static request_dispatch_metadata capture(const nlohmann::json& packet) noexcept {
    request_dispatch_metadata result;
    try {
      if (!packet.is_object() ||
          !capture_field(packet, "method", method_limit, result.method_) ||
          !capture_field(packet, "sessionId", session_limit, result.session_)) return {};
      result.available_ = true;
    } catch (...) {
      // Cache failure must not introduce a new admission/exception path.
      return {};
    }
    return result;
  }

  bool available() const noexcept { return available_; }
  std::size_t cached_payload_bytes() const noexcept { return method_.text.size() + session_.text.size(); }
  const std::string& method() const { return read(method_); }
  const std::string& session() const { return read(session_); }

 private:
  enum class field_state { missing, text, invalid };
  struct field { field_state state{field_state::missing}; std::string text; };
  static bool capture_field(const nlohmann::json& packet, const char* name,
      std::size_t limit, field& output) {
    const auto found = packet.find(name);
    if (found == packet.end()) return true;
    if (!found->is_string()) { output.state = field_state::invalid; return true; }
    const auto& text = found->get_ref<const std::string&>();
    if (text.size() > limit) return false;
    output.text = text;
    output.state = field_state::text;
    return true;
  }
  static const std::string& read(const field& value) {
    if (value.state == field_state::invalid)
      throw std::runtime_error("Invalid request routing field");
    return value.text; // Missing retains packet.value(name, string{}) semantics.
  }
  field method_, session_;
  bool available_{false};
};

}  // namespace soui::sdk::detail
