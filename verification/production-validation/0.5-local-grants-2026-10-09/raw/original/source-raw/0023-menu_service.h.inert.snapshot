#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <soui/runtime/services/desktop/desktop_services.h>

namespace soui::runtime {
namespace detail {
class menu_platform;
struct menu_service_test_access;
}

enum class menu_item_kind { command, separator, submenu };

struct menu_item {
  menu_item_kind kind{menu_item_kind::command};
  std::uint32_t id{0};
  std::string label;
  bool enabled{true};
  bool checked{false};
  std::vector<menu_item> children;
};

struct menu_result {
  desktop_response response{result_kind::success, {}, {}, {}};
  std::uint32_t command_id{0};
  bool has_selection{false};
  // Raw diagnostic captured immediately after the native operation. With a
  // zero tracking result it does not establish that the user cancelled.
  std::uint32_t native_error{0};
};

// Native application API. Construct/use/destroy on one thread; the borrowed
// owner HWND must belong to that process/thread and outlive the show call.
// show is modal and may dispatch application messages. Nested show/cleanup
// return E_BUSY; cancel is permitted while this service tracks its own menu.
// Do not destroy this service or the owner HWND while a member call is active.
// No application callback or HMENU is retained or exposed by the public API.
// The service establishes foreground on the owner's top-level ancestor and
// posts WM_NULL to the original owner after tracking. A tray caller remains
// responsible for tray_service::return_focus() after its UI is dismissed.
class menu_service final {
 public:
  menu_service();
  ~menu_service();
  menu_service(const menu_service&) = delete;
  menu_service& operator=(const menu_service&) = delete;
  menu_service(menu_service&&) = delete;
  menu_service& operator=(menu_service&&) = delete;

  [[nodiscard]] menu_result show(void* owner_window, const std::vector<menu_item>& items,
                                 int screen_x, int screen_y);
  [[nodiscard]] desktop_response cancel();
  [[nodiscard]] desktop_response cleanup();
  // Thread-affine snapshot. Failed destruction blocks another show until a
  // cleanup retry succeeds; the destructor is only best-effort cleanup.
  [[nodiscard]] bool cleanup_pending() const noexcept;

 private:
  friend struct detail::menu_service_test_access;
  explicit menu_service(detail::menu_platform& platform);
  struct implementation;
  std::unique_ptr<implementation> impl_;
};

}  // namespace soui::runtime
