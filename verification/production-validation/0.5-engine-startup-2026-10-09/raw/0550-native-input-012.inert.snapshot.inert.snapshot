#pragma once
#include <array>
#include <cstddef>
#include <mutex>

namespace soui::sdk {
enum class chrome_action { none, drag, minimize, maximize, close };
struct chrome_region {
  chrome_action action{chrome_action::none};
  int left{}, top{}, right{}, bottom{};
};
// Only the fixed top-level caption participates. Native hit tests never enter
// Blink or wait for JavaScript. Short value-copy updates hold no callback lock.
class window_chrome {
 public:
  static constexpr std::size_t capacity = 16;
  void set_viewport_width(int width) {
    std::lock_guard<std::mutex> lock(mutex_);
    expected_width_ = width;
  }
  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    count_ = 0;
  }
  bool publish(int width, const std::array<chrome_region, capacity>& regions, std::size_t count) {
    if (width <= 0 || width > 32768 || count > capacity) return false;
    for (std::size_t i = 0; i < count; ++i) {
      const auto& r = regions[i];
      if (r.action == chrome_action::none || r.left < 0 || r.top < 0 ||
          r.right > width || r.bottom > 128 || r.right <= r.left || r.bottom <= r.top)
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    // UI resize can lead Blink by several queued layouts. Keep the previous
    // anchored caption until a snapshot for the current geometry arrives.
    if (expected_width_ > 0 && width != expected_width_) return false;
    width_ = width;
    regions_ = regions;
    count_ = count;
    return true;
  }
  chrome_action hit(int x, int y, int current_width) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_width <= 0 || x < 0 || x >= current_width || y < 0) return chrome_action::none;
    for (std::size_t i = 0; i < count_; ++i) {
      auto r = regions_[i];
      // Caption controls stay right-anchored and its blank drag spacer stretches
      // while JS is blocked during maximize/restore. New layout replaces this map.
      const int delta = current_width - width_;
      r.right += delta;
      if (r.action != chrome_action::drag) r.left += delta;
      if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return r.action;
    }
    return chrome_action::none;
  }
 private:
  mutable std::mutex mutex_;
  std::array<chrome_region, capacity> regions_{};
  std::size_t count_{};
  int width_{};
  int expected_width_{};
};
}
