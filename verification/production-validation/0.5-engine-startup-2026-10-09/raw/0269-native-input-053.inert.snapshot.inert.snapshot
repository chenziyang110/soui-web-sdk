#pragma once

// Private fault-injection seam. This file is not part of the installed SDK.
#include <cstdint>
#include <memory>
#include <string>

#include <soui/runtime/services/desktop/tray_service.h>

namespace soui::runtime::detail {

struct tray_window_identity {
  bool valid{false};
  bool child{false};
  bool message_only{false};
  std::uint32_t process_id{0};
  std::uint32_t thread_id{0};
};

enum class tray_native_operation { add, set_version, update_tooltip, return_focus, remove };

struct tray_native_data {
  void* window{nullptr};
  void* icon{nullptr};
  std::uint32_t id{0};
  std::uint32_t callback_message{0};
  std::wstring tooltip;
};

class tray_platform {
 public:
  virtual ~tray_platform() = default;
  [[nodiscard]] virtual std::uint32_t current_thread_id() const noexcept = 0;
  [[nodiscard]] virtual std::uint32_t current_process_id() const noexcept = 0;
  [[nodiscard]] virtual tray_window_identity window_identity(void* window) const noexcept = 0;
  [[nodiscard]] virtual std::uint32_t taskbar_created_message() noexcept = 0;
  [[nodiscard]] virtual void* copy_icon(void* icon) noexcept = 0;
  virtual void destroy_icon(void* icon) noexcept = 0;
  [[nodiscard]] virtual bool notify(tray_native_operation operation,
                                    const tray_native_data& data) noexcept = 0;
};

struct tray_service_test_access {
  // The borrowed test platform must outlive the service.
  [[nodiscard]] static std::unique_ptr<tray_service> create(tray_platform& platform) {
    return std::unique_ptr<tray_service>(new tray_service(platform));
  }
};

}  // namespace soui::runtime::detail
