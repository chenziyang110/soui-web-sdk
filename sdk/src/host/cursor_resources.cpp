#include "cursor_resources.h"

#include <cstring>
#include <exception>
#include <utility>

namespace soui::sdk::detail {

cursor_resource_owner::~cursor_resource_owner() {
  // Dropping a failed DeleteObject/DestroyCursor is not completed cleanup.
  if (color_ || mask_ || cursor_) std::terminate();
}
bool cursor_resource_owner::cleanup_bitmaps() noexcept {
  if (mask_ && api_.delete_bitmap(mask_)) mask_ = nullptr;
  if (color_ && api_.delete_bitmap(color_)) color_ = nullptr;
  return !mask_ && !color_;
}
bool cursor_resource_owner::cleanup() noexcept {
  const bool bitmaps_done = cleanup_bitmaps();
  if (cursor_ && api_.destroy_unpublished_cursor(cursor_)) cursor_ = nullptr;
  return bitmaps_done && !cursor_;
}
void* cursor_resource_owner::create(const cursor_descriptor& value) noexcept {
  if (!valid_cursor_descriptor(value) || value.kind != cursor_kind::custom || !cleanup()) return nullptr;
  void* pixels = nullptr;
  color_ = api_.color_bitmap(value, pixels);
  if (!color_ || !pixels) { (void)cleanup(); return nullptr; }
  std::memcpy(pixels, value.pixels.data(), value.pixels.size());
  mask_ = api_.mask_bitmap(value);
  if (!mask_) { (void)cleanup(); return nullptr; }
  cursor_ = api_.cursor(value, color_, mask_);
  // CreateIconIndirect copies both unselected bitmaps. Keep each failed
  // deletion and any unpublished cursor; no later admission can overwrite it.
  if (!cleanup_bitmaps() || !cursor_) { (void)cleanup(); return nullptr; }
  return std::exchange(cursor_, nullptr);
}

}  // namespace soui::sdk::detail
