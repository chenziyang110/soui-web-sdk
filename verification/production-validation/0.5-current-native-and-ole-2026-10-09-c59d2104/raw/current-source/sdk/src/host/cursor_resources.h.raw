#pragma once

#include "cursor_queue.h"

namespace soui::sdk::detail {

// Only unselected construction resources reach this interface. A returned
// cursor transfers to cursor_queue and follows its separate retirement proof.
class cursor_resource_api {
 public:
  virtual ~cursor_resource_api() = default;
  virtual void* color_bitmap(const cursor_descriptor&, void*& pixels) noexcept = 0;
  virtual void* mask_bitmap(const cursor_descriptor&) noexcept = 0;
  virtual void* cursor(const cursor_descriptor&, void* color, void* mask) noexcept = 0;
  virtual bool delete_bitmap(void*) noexcept = 0;
  virtual bool destroy_unpublished_cursor(void*) noexcept = 0;
};

class cursor_resource_owner final {
 public:
  explicit cursor_resource_owner(cursor_resource_api& api) noexcept : api_(api) {}
  ~cursor_resource_owner();
  cursor_resource_owner(const cursor_resource_owner&) = delete;
  cursor_resource_owner& operator=(const cursor_resource_owner&) = delete;
  [[nodiscard]] void* create(const cursor_descriptor&) noexcept;
  [[nodiscard]] bool cleanup() noexcept;
  [[nodiscard]] unsigned unpublished_cursors() const noexcept { return cursor_ ? 1U : 0U; }
  [[nodiscard]] unsigned pending_bitmaps() const noexcept {
    return static_cast<unsigned>(color_ != nullptr) + static_cast<unsigned>(mask_ != nullptr);
  }
 private:
  [[nodiscard]] bool cleanup_bitmaps() noexcept;
  cursor_resource_api& api_;
  void* color_{nullptr};
  void* mask_{nullptr};
  void* cursor_{nullptr};
};

}  // namespace soui::sdk::detail
