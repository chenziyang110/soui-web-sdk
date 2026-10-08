#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <soui/runtime/core/result/result.h>

namespace soui::runtime {
namespace detail {
class shortcut_platform;
struct global_shortcut_test_access;
}

namespace shortcut_modifiers {
inline constexpr std::uint32_t alt = 0x0001;
inline constexpr std::uint32_t control = 0x0002;
inline constexpr std::uint32_t shift = 0x0004;
inline constexpr std::uint32_t windows = 0x0008;
}

struct global_shortcut_options {
  void* owner_window{nullptr};
  std::uint32_t id{0};
  std::uint32_t modifiers{0};
  std::uint32_t virtual_key{0};
  bool no_repeat{true};
};

struct global_shortcut_response {
  result_kind kind{result_kind::failure};
  std::string code;
  std::string message;
  std::uint32_t native_error{0};
  [[nodiscard]] bool ok() const noexcept { return kind == result_kind::success; }
};

struct global_shortcut_message_result {
  bool handled{false};
  bool activated{false};
  global_shortcut_response response{result_kind::success, {}, {}, 0};
};

// One native registration owned by the creating thread. The application owns
// the current-process/thread HWND and exclusively reserves its ID (0..0xBFFF).
// Do not reuse an unregistered ID during that HWND's lifetime: a queued old
// WM_HOTKEY has no registration generation. Message matches are not proof of
// trusted input, permission or a user gesture.
// Unregister and destroy before destroying the owner HWND. All operations and
// state snapshots are thread-affine. Nested operations return E_BUSY; never
// destroy the service while one of its members is executing.
class global_shortcut final {
 public:
  global_shortcut();
  ~global_shortcut();
  global_shortcut(const global_shortcut&) = delete;
  global_shortcut& operator=(const global_shortcut&) = delete;
  global_shortcut(global_shortcut&&) = delete;
  global_shortcut& operator=(global_shortcut&&) = delete;

  // Failed registration may be retried. After a successful registration and
  // successful unregister this object is retired and cannot register again.
  [[nodiscard]] global_shortcut_response register_shortcut(const global_shortcut_options& options);
  // Stops activation immediately on the owner thread; failure retains the
  // registration for cleanup retry and is not confirmed native removal.
  [[nodiscard]] global_shortcut_response unregister_shortcut();
  [[nodiscard]] global_shortcut_message_result dispatch_message(std::uint32_t message,
                                                                std::uintptr_t wparam,
                                                                std::intptr_t lparam);
  [[nodiscard]] bool active() const noexcept;
  [[nodiscard]] bool cleanup_pending() const noexcept;
  [[nodiscard]] bool retired() const noexcept;

 private:
  friend struct detail::global_shortcut_test_access;
  explicit global_shortcut(detail::shortcut_platform& platform);
  struct implementation;
  std::unique_ptr<implementation> impl_;
};

}  // namespace soui::runtime
