#pragma once
// Application-only observation of the public bridge JSON envelope. No SDK
// private headers, credential storage, clock changes, or desktop calls.
#include <cstddef>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace renewal_probe {
inline void require(bool condition, const char* reason) {
  if (!condition) throw std::runtime_error(reason);
}
struct metadata {
  std::string method, request_id, session_id, origin;
  bool has_ok{false}, ok{false}, result_object{false};
};

// Full bounded JSON syntax validation, with selective string projection.
// In particular token values are validated/skipped and are never copied into
// metadata. Escaped member names and duplicate keys are handled at every level.
class metadata_reader final {
 public:
  explicit metadata_reader(std::string_view text) : text_(text) {}
  metadata read() {
    require(!text_.empty() && text_.size() <= 65536, "Bridge observation size differs");
    space(); require(peek() == '{', "Bridge envelope must be an object");
    object(0, 0); space(); require(at_ == text_.size(), "Trailing bridge JSON");
    return result_;
  }
 private:
  char peek() const { return at_ < text_.size() ? text_[at_] : '\0'; }
  void space() { while (peek() == ' ' || peek() == '\r' || peek() == '\n' || peek() == '\t') ++at_; }
  void take(char value) { require(peek() == value, "Malformed bridge JSON"); ++at_; }
  std::uint32_t hex4() {
    std::uint32_t value = 0;
    for (unsigned n = 0; n < 4; ++n) {
      const char c = peek(); ++at_;
      require((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'), "Invalid JSON unicode escape");
      value = value * 16 + (c <= '9' ? c - '0' : (c <= 'F' ? c - 'A' + 10 : c - 'a' + 10));
    }
    return value;
  }
  static void append(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7f) out += static_cast<char>(cp);
    else if (cp <= 0x7ff) { out += static_cast<char>(0xc0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 63)); }
    else if (cp <= 0xffff) { out += static_cast<char>(0xe0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 63)); out += static_cast<char>(0x80 | (cp & 63)); }
    else { out += static_cast<char>(0xf0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 63)); out += static_cast<char>(0x80 | ((cp >> 6) & 63)); out += static_cast<char>(0x80 | (cp & 63)); }
  }
  std::string string(bool capture) {
    take('"'); std::string out;
    while (peek() != '"') {
      require(at_ < text_.size(), "Unterminated bridge JSON string");
      auto cp = static_cast<std::uint32_t>(static_cast<unsigned char>(text_[at_++]));
      require(cp >= 0x20, "Control character in JSON string");
      if (cp == '\\') {
        const char escaped = peek(); require(at_ < text_.size(), "Unterminated JSON escape"); ++at_;
        if (escaped == 'u') {
          cp = hex4();
          if (cp >= 0xd800 && cp <= 0xdbff) {
            take('\\'); take('u'); const auto low = hex4();
            require(low >= 0xdc00 && low <= 0xdfff, "Invalid JSON surrogate pair");
            cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
          } else require(cp < 0xdc00 || cp > 0xdfff, "Unpaired JSON surrogate");
        } else if (escaped == 'b') cp = 8;
        else if (escaped == 'f') cp = 12;
        else if (escaped == 'n') cp = 10;
        else if (escaped == 'r') cp = 13;
        else if (escaped == 't') cp = 9;
        else { require(escaped == '"' || escaped == '\\' || escaped == '/', "Invalid JSON escape"); cp = static_cast<unsigned char>(escaped); }
      } else if (cp >= 0x80) {
        unsigned count = 0; std::uint32_t minimum = 0;
        if (cp >= 0xc2 && cp <= 0xdf) { count = 1; minimum = 0x80; cp &= 31; }
        else if (cp >= 0xe0 && cp <= 0xef) { count = 2; minimum = 0x800; cp &= 15; }
        else if (cp >= 0xf0 && cp <= 0xf4) { count = 3; minimum = 0x10000; cp &= 7; }
        else throw std::runtime_error("Invalid JSON UTF-8");
        for (unsigned n = 0; n < count; ++n) {
          require(at_ < text_.size(), "Truncated JSON UTF-8");
          const auto next = static_cast<unsigned char>(text_[at_++]);
          require((next & 0xc0) == 0x80, "Invalid JSON UTF-8 continuation"); cp = (cp << 6) | (next & 63);
        }
        require(cp >= minimum && cp <= 0x10ffff && (cp < 0xd800 || cp > 0xdfff), "Invalid JSON code point");
      }
      if (capture) append(out, cp);
    }
    take('"'); return out;
  }
  void literal(std::string_view value) {
    require(text_.substr(at_, value.size()) == value, "Invalid JSON literal"); at_ += value.size();
  }
  void number() {
    if (peek() == '-') ++at_;
    if (peek() == '0') ++at_;
    else { require(peek() >= '1' && peek() <= '9', "Invalid JSON number"); while (peek() >= '0' && peek() <= '9') ++at_; }
    if (peek() == '.') { ++at_; require(peek() >= '0' && peek() <= '9', "Invalid JSON fraction"); while (peek() >= '0' && peek() <= '9') ++at_; }
    if (peek() == 'e' || peek() == 'E') { ++at_; if (peek() == '+' || peek() == '-') ++at_;
      require(peek() >= '0' && peek() <= '9', "Invalid JSON exponent"); while (peek() >= '0' && peek() <= '9') ++at_; }
  }
  void value(unsigned depth, int context = 2, const std::string& key = {}) {
    require(depth <= 32 && ++nodes_ <= 8192, "Bridge JSON structure limit exceeded"); space();
    std::string* projected = nullptr;
    if (context == 0 && key == "method") projected = &result_.method;
    if (context == 0 && key == "requestId") projected = &result_.request_id;
    if (context == 1 && key == "sessionId") projected = &result_.session_id;
    if (context == 1 && key == "origin") projected = &result_.origin;
    if (projected) {
      require(peek() == '"', "Observed identity/method must be a string");
      *projected = string(true); require(projected->size() <= 512, "Observed identity/method too long"); return;
    }
    if (context == 0 && key == "ok") {
      result_.has_ok = true; result_.ok = peek() == 't'; literal(result_.ok ? "true" : "false"); return;
    }
    if (context == 0 && key == "result") {
      require(peek() == '{', "Bridge result must be an object"); result_.result_object = true; object(depth, 1); return;
    }
    if (peek() == '{') { object(depth, 2); return; }
    if (peek() == '[') { ++at_; space(); if (peek() != ']') for (;;) {
      value(depth + 1); space(); if (peek() != ',') break; ++at_;
    } take(']'); return; }
    if (peek() == '"') { (void)string(false); return; }
    if (peek() == 't') literal("true"); else if (peek() == 'f') literal("false");
    else if (peek() == 'n') literal("null"); else number();
  }
  void object(unsigned depth, int context) {
    require(depth <= 32, "Bridge JSON depth exceeded"); take('{'); space(); std::set<std::string> names;
    if (peek() != '}') for (;;) {
      const auto name = string(true);
      require(name.size() <= 4096 && names.insert(name).second, "Duplicate/oversized bridge JSON member");
      space(); take(':'); value(depth + 1, context, name); space(); if (peek() != ',') break; ++at_; space();
    }
    take('}');
  }
  std::string_view text_; std::size_t at_{0}, nodes_{0}; metadata result_;
};

class session_phase final {
 public:
  const std::string& session() const noexcept { return session_; }
  const std::string& origin() const noexcept { return origin_; }
  unsigned bootstrap_calls() const noexcept { return bootstrap_calls_; }
  unsigned prebootstrap_retirements() const noexcept { return prebootstrap_retirements_; }
  unsigned bound_closes() const noexcept { return bound_closes_; }
  unsigned shutdown_callbacks() const noexcept { return shutdown_callbacks_; }
  bool bound() const noexcept { return !session_.empty(); }
  const char* phase() const noexcept {
    if (closing_) return bound() ? "closing-bound" : "closing-unbound";
    if (!bootstrap_calls_) return "before-bootstrap";
    if (!bound()) return "bootstrap-in-flight";
    if (!described_) return "bound-before-describe";
    return ready_ ? "ready" : "described-before-ready";
  }
  void begin_bootstrap() {
    require(!closing_ && bootstrap_calls_ == 0 && !bound(), "Bootstrap must occur exactly once"); ++bootstrap_calls_;
  }
  void bind(const metadata& response, const std::string& request_id) {
    require(!closing_ && bootstrap_calls_ == 1 && !bound(), "Unexpected bootstrap binding");
    require(response.has_ok && response.ok && response.result_object && !request_id.empty() &&
      response.request_id == request_id && !response.session_id.empty() && response.session_id.size() <= 256 &&
      response.origin == "app://installed-renewal-probe", "Invalid public bootstrap result identity");
    require(response.session_id != retired_session_, "Authorized session reused retired lifecycle identity");
    session_ = response.session_id; origin_ = response.origin;
  }
  void describe(const std::string& session, const std::string& origin) {
    require(!closing_ && bound() && !described_ && session == session_ && origin == origin_, "Describe must match bootstrap identity"); described_ = true;
  }
  void ready() { require(!closing_ && described_ && !ready_, "Ready phase differs"); ready_ = true; }
  void begin_close() noexcept { closing_ = true; }
  const char* shutdown(const std::string& session) {
    ++shutdown_callbacks_;
    require(!session.empty() && session.size() <= 256, "Invalid opaque shutdown identity");
    if (!closing_ && bootstrap_calls_ == 0 && !bound() && !described_ && !ready_ && prebootstrap_retirements_ == 0) {
      retired_session_ = session; ++prebootstrap_retirements_; return "pre-bootstrap-lifecycle";
    }
    require(closing_ && bound() && session == session_ && bound_closes_ == 0, "Unexpected bound session shutdown or navigation");
    ++bound_closes_; return "bound-close";
  }
 private:
  std::string session_, origin_, retired_session_;
  unsigned bootstrap_calls_{0}, prebootstrap_retirements_{0}, bound_closes_{0}, shutdown_callbacks_{0};
  bool described_{false}, ready_{false}, closing_{false};
};
} // namespace renewal_probe
