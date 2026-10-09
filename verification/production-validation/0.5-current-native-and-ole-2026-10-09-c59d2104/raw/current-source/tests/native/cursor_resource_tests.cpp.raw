#include "../../sdk/src/host/cursor_resources.h"

#include <array>
#include <iostream>
#include <set>
#include <stdexcept>

using namespace soui::sdk::detail;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
cursor_descriptor custom() {
  cursor_descriptor value;
  value.kind = cursor_kind::custom; value.type = 43;
  value.width = 17; value.height = 13; value.stride = 68;
  value.pixels.resize(value.stride * value.height);
  for (std::size_t i = 3; i < value.pixels.size(); i += 4) value.pixels[i] = 255;
  return value;
}
struct fake_resources final : cursor_resource_api {
  std::array<unsigned char, 256 * 256 * 4> pixels{};
  unsigned bitmap_creates{0}, cursor_creates{0}, bitmap_deletes{0}, cursor_deletes{0};
  bool null_pixels{false}, fail_mask{false}, fail_cursor{false};
  bool fail_bitmap_delete{false}, fail_cursor_delete{false};
  std::set<void*> bitmaps, cursors;
  void* color_bitmap(const cursor_descriptor&, void*& output) noexcept override {
    output = null_pixels ? nullptr : pixels.data();
    auto handle = reinterpret_cast<void*>(std::uintptr_t(1000 + ++bitmap_creates));
    bitmaps.insert(handle); return handle;
  }
  void* mask_bitmap(const cursor_descriptor&) noexcept override {
    if (fail_mask) return nullptr;
    auto handle = reinterpret_cast<void*>(std::uintptr_t(1000 + ++bitmap_creates));
    bitmaps.insert(handle); return handle;
  }
  void* cursor(const cursor_descriptor&, void* color, void* mask) noexcept override {
    if (fail_cursor || !bitmaps.count(color) || !bitmaps.count(mask)) return nullptr;
    auto handle = reinterpret_cast<void*>(std::uintptr_t(100 + ++cursor_creates));
    cursors.insert(handle); return handle;
  }
  bool delete_bitmap(void* handle) noexcept override {
    if (fail_bitmap_delete || !bitmaps.erase(handle)) return false;
    ++bitmap_deletes; return true;
  }
  bool destroy_unpublished_cursor(void* handle) noexcept override {
    if (fail_cursor_delete || !cursors.erase(handle)) return false;
    ++cursor_deletes; return true;
  }
};
struct resource_platform final : cursor_platform {
  fake_resources api;
  cursor_resource_owner resources{api};
  void* local{reinterpret_cast<void*>(1)};
  std::uint32_t thread_id() const noexcept override { return 7; }
  void* standard(std::uint32_t) noexcept override { return reinterpret_cast<void*>(1); }
  void* create(const cursor_descriptor& value) noexcept override { return resources.create(value); }
  void select(void* value) noexcept override { local = value; }
  void* local_selection() const noexcept override { return local; }
  cursor_global_observation global_selection() const noexcept override { return {true, 1, local}; }
  bool destroy(void* value) noexcept override {
    return value != local && api.destroy_unpublished_cursor(value);
  }
  bool cleanup_unpublished() noexcept override { return resources.cleanup(); }
  unsigned unpublished_cursors() const noexcept override { return resources.unpublished_cursors(); }
};
void failed_bitmap_and_cursor_cleanup() {
  resource_platform platform; cursor_queue queue(platform); int owner = 0;
  require(queue.apply(&owner, 1, custom()).selected, "first selected cursor");
  platform.api.fail_bitmap_delete = platform.api.fail_cursor_delete = true;
  require(queue.apply(&owner, 2, custom()).status == cursor_queue_status::allocation_failed,
          "failed unpublished construction is not transferred");
  require(queue.retained_handles() == 2 && platform.resources.pending_bitmaps() == 2,
          "current plus unpublished cursor and both bitmaps remain owned");
  for (unsigned i = 0; i < 100; ++i) {
    require(!queue.apply(&owner, 3 + i, custom()).selected && !queue.withdraw(&owner),
            "failed cleanup gates allocation and completed close");
  }
  require(platform.api.cursor_creates == 2 && platform.api.bitmap_creates == 4 &&
          platform.api.cursors.size() == 2 && platform.api.bitmaps.size() == 2,
          "repeated retries cannot create orphan resources");
  platform.api.fail_bitmap_delete = false;
  require(!queue.withdraw(&owner) && platform.resources.pending_bitmaps() == 0 &&
          queue.retained_handles() == 2, "bitmap recovery does not discard failed cursor destruction");
  platform.api.fail_cursor_delete = false;
  require(queue.withdraw(&owner) && platform.api.cursors.empty() && platform.api.bitmaps.empty() &&
          platform.api.cursor_deletes == 2 && platform.api.bitmap_deletes == 4,
          "recovery releases every acquired resource exactly once");
}
void incomplete_color() {
  fake_resources api; cursor_resource_owner resources(api);
  api.null_pixels = api.fail_bitmap_delete = true;
  require(!resources.create(custom()) && resources.pending_bitmaps() == 1, "non-null color with no pixels retained");
  for (unsigned i = 0; i < 10; ++i) require(!resources.create(custom()), "color cleanup admission gate");
  require(api.bitmap_creates == 1 && api.cursor_creates == 0, "no replacement allocation while color remains");
  api.fail_bitmap_delete = false;
  require(resources.cleanup() && api.bitmap_deletes == 1, "incomplete color closed exactly once");
}
void mask_admission_failure() {
  fake_resources api; cursor_resource_owner resources(api);
  api.fail_mask = api.fail_bitmap_delete = true;
  require(!resources.create(custom()) && resources.pending_bitmaps() == 1, "failed mask retains its color");
  require(!resources.create(custom()) && api.bitmap_creates == 1, "failed mask cleanup blocks replacement");
  api.fail_bitmap_delete = false;
  require(resources.cleanup() && api.bitmaps.empty(), "failed mask cleanup recovers");
}
void cursor_factory_failure() {
  fake_resources api; cursor_resource_owner resources(api);
  api.fail_cursor = api.fail_bitmap_delete = true;
  require(!resources.create(custom()) && resources.pending_bitmaps() == 2 && !resources.unpublished_cursors(),
          "factory failure preserves both construction bitmaps");
  require(!resources.create(custom()) && api.bitmap_creates == 2, "factory cleanup gates all later allocation");
  api.fail_bitmap_delete = false;
  require(resources.cleanup() && api.bitmap_deletes == 2 && api.cursors.empty(), "factory failure has no cursor transfer");
}
}  // namespace
int main() {
  try {
    failed_bitmap_and_cursor_cleanup(); incomplete_color(); mask_admission_failure(); cursor_factory_failure();
    std::cout << "Cursor resources: 4/4 passed\n"; return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
