#pragma once

// Private fault-injection seam. This file is not part of the installed SDK.
#include <cstdint>
#include <atomic>
#include <limits>
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

// Individual native observations, not an overall "preparation succeeded" mock.
// unavailable before activation may permit the unchanged NIM path; unsafe is
// an observed identity race and must never be treated as missing metadata.
enum class tray_focus_query { known, unavailable, unsafe };

struct tray_focus_identity {
  void* window{nullptr};
  std::uint32_t process_id{0};
  std::uint32_t thread_id{0};
  std::uint64_t creation_time{0};
};

struct tray_focus_target {
  tray_focus_query query{tray_focus_query::unavailable};
  tray_focus_identity identity;
  // Owned metadata-only process lease. release_focus_target releases it even
  // when acquisition returned unavailable/unsafe after partially acquiring it.
  void* process_lease{nullptr};
};

struct tray_focus_environment {
  tray_focus_query query{tray_focus_query::unavailable};
  tray_window_identity owner;
  tray_focus_identity target;
  void* foreground{nullptr};
  bool target_alive{false};
  bool same_session{false};
  bool same_input_desktop{false};
  bool owner_menu_clear{false};
};

struct tray_focus_gui {
  tray_focus_query query{tray_focus_query::unavailable};
  void* active{nullptr};
  void* focus{nullptr};
};

struct tray_focus_barrier {
  bool sent{false};
  std::uintptr_t message_result{0}; // WM_NULL may successfully return zero.
};

// Reuses TaskbarCreated so existing callers keep their current routing. This
// scalar tag is not a security credential; stale tickets are consumed/ignored.
inline constexpr std::uintptr_t tray_taskbar_wake_tag = 0x534f5549U;

class tray_taskbar_ticket_source final {
 public:
  constexpr explicit tray_taskbar_ticket_source(std::uintptr_t first = 1) noexcept : next_(first) {}
  [[nodiscard]] std::uintptr_t reserve() noexcept {
    auto value = next_.load(std::memory_order_relaxed);
    const auto limit = static_cast<std::uintptr_t>((std::numeric_limits<std::intptr_t>::max)());
    // A busy UI guard tail must not spin indefinitely under contention.
    for (unsigned attempt = 0; attempt < 16 && value && value < limit; ++attempt)
      if (next_.compare_exchange_strong(value, value + 1, std::memory_order_relaxed)) return value;
    return 0;
  }
 private:
  std::atomic<std::uintptr_t> next_;
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
  [[nodiscard]] virtual std::uint64_t focus_now_ms() const noexcept = 0;
  [[nodiscard]] virtual void* focus_foreground() const noexcept = 0;
  [[nodiscard]] virtual tray_focus_target acquire_focus_target() noexcept = 0;
  [[nodiscard]] virtual tray_focus_environment observe_focus_environment(
      void* owner, const tray_focus_target& target) const noexcept = 0;
  virtual void release_focus_target(tray_focus_target& target) noexcept = 0;
  [[nodiscard]] virtual bool activate_focus_target(void* window) noexcept = 0;
  [[nodiscard]] virtual tray_focus_barrier send_focus_barrier(
      void* window, std::uint32_t timeout_ms) noexcept = 0;
  [[nodiscard]] virtual tray_focus_gui focus_gui(std::uint32_t thread_id) const noexcept = 0;
  virtual void wait_focus(std::uint32_t requested_ms) noexcept = 0;
  [[nodiscard]] virtual bool post_taskbar_wake(void* owner, std::uint32_t message,
                                               std::uintptr_t ticket) noexcept = 0;
};

struct tray_service_test_access {
  // The borrowed test platform must outlive the service.
  [[nodiscard]] static std::unique_ptr<tray_service> create(tray_platform& platform) {
    return std::unique_ptr<tray_service>(new tray_service(platform));
  }
};

}  // namespace soui::runtime::detail
