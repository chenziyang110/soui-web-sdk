#pragma once

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
}  // namespace soui::sdk
