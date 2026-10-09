#pragma once

// Private, uninstalled Win32 boundary for deterministic no-desktop tests.
#include <cstdint>
#include <memory>
#include <string_view>

#include <soui/runtime/services/desktop/menu_service.h>

namespace soui::runtime::detail {

struct menu_window_identity {
  bool valid{false};
  std::uint32_t process_id{0};
  std::uint32_t thread_id{0};
};
struct menu_native_status { bool ok{false}; std::uint32_t error{0}; };
struct menu_create_result { void* handle{nullptr}; std::uint32_t error{0}; };
struct menu_track_result { std::uint32_t command{0}; std::uint32_t error{0}; };
struct menu_native_item {
  menu_item_kind kind{menu_item_kind::command};
  std::uint32_t id{0};
  std::wstring_view label;
  bool enabled{true};
  bool checked{false};
  void* submenu{nullptr};
};

class menu_platform {
 public:
  virtual ~menu_platform() = default;
  virtual std::uint32_t current_thread_id() const noexcept = 0;
  virtual std::uint32_t current_process_id() const noexcept = 0;
  virtual menu_window_identity window_identity(void* window) const noexcept = 0;
  virtual menu_create_result create_popup() noexcept = 0;
  // Child ownership transfers to parent only when insertion succeeds.
  virtual menu_native_status insert(void* parent, unsigned position,
                                    const menu_native_item& item) noexcept = 0;
  virtual menu_native_status destroy(void* root) noexcept = 0;
  virtual menu_native_status foreground(void* owner) noexcept = 0;
  virtual menu_native_status finish(void* owner) noexcept = 0;
  virtual menu_track_result track(void* root, void* owner, int x, int y,
                                  std::uint32_t flags) noexcept = 0;
  virtual menu_native_status end_menu() noexcept = 0;
};

struct menu_service_test_access {
  static std::unique_ptr<menu_service> create(menu_platform& platform) {
    return std::unique_ptr<menu_service>(new menu_service(platform));
  }
};

}  // namespace soui::runtime::detail
