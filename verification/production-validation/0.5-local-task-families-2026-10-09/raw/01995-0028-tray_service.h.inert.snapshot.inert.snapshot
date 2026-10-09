#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <soui/runtime/services/desktop/desktop_services.h>

namespace soui::runtime {
namespace detail {
class tray_platform;
struct tray_service_test_access;
}

struct tray_options {
  void* owner_window{nullptr};
  void* icon{nullptr};
  std::uint32_t icon_id{0};
  std::uint32_t callback_message{0};
  std::string tooltip;
};

enum class tray_event { none, activated, context_menu };

struct tray_message_result {
  bool handled{false};
  tray_event event{tray_event::none};
  bool position_valid{false};
  int x{0};
  int y{0};
  desktop_response response{result_kind::success, {}, {}, {}};
};

// Application-owned, optional native service; not a Web permission or gesture.
// Construct/use/destroy on the owner HWND's thread. The application owns a
// top-level HWND and its message loop, and must remove/destroy this service
// before destroying that HWND. It exclusively reserves the HWND/id/message
// identity. The service copies HICON; callers retain ownership of the input.
// Nested operations caused by synchronous Shell messages return E_BUSY. The
// application must not destroy the service while any member call is active.
class tray_service final {
 public:
  tray_service();
  ~tray_service();
  tray_service(const tray_service&) = delete;
  tray_service& operator=(const tray_service&) = delete;
  tray_service(tray_service&&) = delete;
  tray_service& operator=(tray_service&&) = delete;

  [[nodiscard]] desktop_response install(const tray_options& options);
  [[nodiscard]] desktop_response update_tooltip(const std::string& tooltip);
  // Return keyboard focus to the notification area after dismissing native
  // UI such as a context menu. This does not activate a business window.
  [[nodiscard]] desktop_response return_focus();
  // Revokes desired residency before attempting Shell removal. A failed
  // removal is not confirmed cleanup; retry on the owner thread while alive.
  [[nodiscard]] desktop_response remove();
  [[nodiscard]] tray_message_result dispatch_message(std::uint32_t message,
                                                    std::uintptr_t wparam,
                                                    std::intptr_t lparam);

  // Thread-affine snapshots. installed means ADD + VERSION_4 succeeded;
  // desired means a TaskbarCreated notification may retry installation.
  [[nodiscard]] bool installed() const noexcept;
  [[nodiscard]] bool desired() const noexcept;
  [[nodiscard]] bool cleanup_pending() const noexcept;

 private:
  friend struct detail::tray_service_test_access;
  explicit tray_service(detail::tray_platform& platform);
  struct implementation;
  std::unique_ptr<implementation> impl_;
};

}  // namespace soui::runtime
