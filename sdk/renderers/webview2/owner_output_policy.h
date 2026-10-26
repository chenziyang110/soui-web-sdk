#pragma once
#include <chrono>
#include <cstddef>

namespace soui::sdk::detail {
class owner_output_budget final {
 public:
  using clock = std::chrono::steady_clock;
  explicit owner_output_budget(unsigned microseconds, clock::time_point started = clock::now()) noexcept
      : started_(started), microseconds_(microseconds >= 100 && microseconds <= 5000 ? microseconds : 0) {}
  [[nodiscard]] bool allows_next(unsigned completed, clock::time_point now = clock::now()) const noexcept {
    return completed < 16 && (!completed || !microseconds_ || now - started_ < std::chrono::microseconds(microseconds_));
  }
  [[nodiscard]] static bool may_cache(std::size_t raw, std::size_t wire_characters, std::size_t limit) noexcept {
    return raw <= limit && wire_characters <= (limit - raw) / sizeof(wchar_t);
  }
  [[nodiscard]] static unsigned parse_microseconds(const wchar_t* text, std::size_t length) noexcept {
    if (!text || !length || length >= 16) return 0;
    unsigned value = 0;
    for (std::size_t index = 0; index < length; ++index) {
      if (text[index] < L'0' || text[index] > L'9' || value > 5000) return 0;
      value = value * 10 + static_cast<unsigned>(text[index] - L'0');
    }
    return value >= 100 && value <= 5000 ? value : 0;
  }
 private:
  clock::time_point started_;
  unsigned microseconds_;
};
} // namespace soui::sdk::detail
