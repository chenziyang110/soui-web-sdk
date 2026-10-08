#pragma once

#include <cstdint>
#include <limits>
#include <optional>

namespace soui::demo {
struct work_area_rect {
  int left;
  int top;
  int right;
  int bottom;
};

struct initial_window_rect {
  int left;
  int top;
  int width;
  int height;
};

inline std::optional<initial_window_rect> fit_initial_window(
    work_area_rect work, int requested_width, int requested_height) noexcept {
  const auto available_width = static_cast<std::int64_t>(work.right) - work.left;
  const auto available_height = static_cast<std::int64_t>(work.bottom) - work.top;
  if (requested_width <= 0 || requested_height <= 0 ||
      available_width <= 0 || available_height <= 0) return std::nullopt;

  // Leave room to grow the window using its visible lower-right border when
  // the work area can still fit a usable 640x480 initial window.
  constexpr std::int64_t leading_margin = 8;
  constexpr std::int64_t right_growth_room = 84;
  constexpr std::int64_t bottom_growth_room = 64;
  constexpr std::int64_t minimum_resizable_width = 640;
  constexpr std::int64_t minimum_resizable_height = 480;
  const auto max_width = available_width >= minimum_resizable_width + leading_margin + right_growth_room
      ? available_width - leading_margin - right_growth_room : available_width;
  const auto max_height = available_height >= minimum_resizable_height + leading_margin + bottom_growth_room
      ? available_height - leading_margin - bottom_growth_room : available_height;
  const auto width = max_width < requested_width ? max_width : requested_width;
  const auto height = max_height < requested_height ? max_height : requested_height;
  const auto horizontal_slack = available_width - width;
  const auto vertical_slack = available_height - height;
  const auto centered_left = horizontal_slack / 2;
  const auto centered_top = vertical_slack / 2;
  const auto left = static_cast<std::int64_t>(work.left) +
      (horizontal_slack >= right_growth_room && centered_left > horizontal_slack - right_growth_room
          ? horizontal_slack - right_growth_room : centered_left);
  const auto top = static_cast<std::int64_t>(work.top) +
      (vertical_slack >= bottom_growth_room && centered_top > vertical_slack - bottom_growth_room
          ? vertical_slack - bottom_growth_room : centered_top);
  if (left < std::numeric_limits<int>::min() || left > std::numeric_limits<int>::max() ||
      top < std::numeric_limits<int>::min() || top > std::numeric_limits<int>::max())
    return std::nullopt;
  return initial_window_rect{static_cast<int>(left), static_cast<int>(top),
                             static_cast<int>(width), static_cast<int>(height)};
}
} // namespace soui::demo
