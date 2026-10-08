#pragma once

// Private, uninstalled fault-injection boundary. No desktop objects are
// required by the global_shortcut contract tests.
#include <cstdint>
#include <memory>

#include <soui/runtime/services/desktop/global_shortcut.h>

namespace soui::runtime::detail {

struct shortcut_window_identity {
  bool valid{false};
  std::uint32_t process_id{0};
  std::uint32_t thread_id{0};
};

struct shortcut_native_result {
  bool succeeded{false};
  std::uint32_t error{0};
};

class shortcut_platform {
 public:
  virtual ~shortcut_platform() = default;
  [[nodiscard]] virtual std::uint32_t current_thread_id() const noexcept = 0;
  [[nodiscard]] virtual std::uint32_t current_process_id() const noexcept = 0;
  [[nodiscard]] virtual shortcut_window_identity window_identity(void* window) const noexcept = 0;
  [[nodiscard]] virtual shortcut_native_result register_key(void* window, std::uint32_t id,
                                                          std::uint32_t native_modifiers,
                                                          std::uint32_t virtual_key) noexcept = 0;
  [[nodiscard]] virtual shortcut_native_result unregister_key(void* window,
                                                            std::uint32_t id) noexcept = 0;
};

struct global_shortcut_test_access {
  // The borrowed platform must outlive the service.
  [[nodiscard]] static std::unique_ptr<global_shortcut> create(shortcut_platform& platform) {
    return std::unique_ptr<global_shortcut>(new global_shortcut(platform));
  }
};

}  // namespace soui::runtime::detail
