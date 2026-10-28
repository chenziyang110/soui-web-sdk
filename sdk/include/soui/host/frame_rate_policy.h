#pragma once

#include <cstdint>
#include <limits>

namespace soui::sdk {
// Integer scheduling targets, not a claim of physical presentation throughput.
[[nodiscard]] constexpr bool is_supported_frame_rate(unsigned rate) noexcept {
  return rate >= 30 && rate <= 1000;
}
[[nodiscard]] constexpr unsigned clamp_frame_rate_to_display(unsigned requested, unsigned display) noexcept {
  if (!is_supported_frame_rate(requested)) return 0;
  const unsigned ceiling = is_supported_frame_rate(display) ? display : 60;
  return requested < ceiling ? requested : ceiling;
}

// Integer QPC deadlines retain the fractional remainder. Every deadline is
// ceil(origin + ordinal * frequency / rate), with no accumulated truncation.
class fractional_frame_phase final {
 public:
  bool reset(std::int64_t origin, std::int64_t frequency, unsigned rate) noexcept {
    const auto maximum = (std::numeric_limits<std::int64_t>::max)();
    if (origin < 0 || !is_supported_frame_rate(rate) || frequency < rate ||
        frequency > maximum / 1000 || origin > maximum - frequency) return false;
    origin_ = origin;
    frequency_ = frequency;
    rate_ = rate;
    ordinal_ = 1;
    return true;
  }
  [[nodiscard]] std::int64_t deadline() const noexcept {
    const auto whole = ordinal_ / rate_;
    const auto partial = ordinal_ % rate_;
    const auto remainder = frequency_ % rate_;
    const auto fraction = partial * (frequency_ / rate_) +
        (partial * remainder + rate_ - 1) / rate_;
    const auto maximum = (std::numeric_limits<std::int64_t>::max)();
    if (whole > (maximum - origin_ - fraction) / frequency_) return maximum;
    return origin_ + whole * frequency_ + fraction;
  }
  // Returns skipped deadlines. No catch-up callback loop, even after sleep.
  std::uint64_t advance_past(std::int64_t now) noexcept {
    if (now < deadline()) return 0;
    const auto elapsed = now > origin_ ? now - origin_ : 0;
    const auto base = (elapsed / frequency_) * rate_ +
        ((elapsed % frequency_) * rate_) / frequency_;
    const auto maximum = (std::numeric_limits<std::int64_t>::max)();
    const auto next = base < maximum ? base + 1 : maximum;
    const auto missed = next > ordinal_ ? next - ordinal_ - 1 : 0;
    ordinal_ = next > ordinal_ ? next : (ordinal_ < maximum ? ordinal_ + 1 : maximum);
    return static_cast<std::uint64_t>(missed);
  }
 private:
  std::int64_t origin_ = 0;
  std::int64_t frequency_ = 1;
  std::int64_t ordinal_ = 1;
  std::int64_t rate_ = 60;
};
}  // namespace soui::sdk
