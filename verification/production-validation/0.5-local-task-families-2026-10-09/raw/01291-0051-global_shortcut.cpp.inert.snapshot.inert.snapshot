#include <soui/runtime/services/desktop/global_shortcut.h>

#include "shortcut_platform.h"

#include <windows.h>

namespace soui::runtime {
namespace {

global_shortcut_response success() { return {result_kind::success, {}, {}, 0}; }

global_shortcut_response failure(const char* code, const char* message,
                                 std::uint32_t native_error = 0) {
  return {result_kind::failure, code, message, native_error};
}

class operation_guard final {
 public:
  explicit operation_guard(bool& active) : active_(active) { active_ = true; }
  ~operation_guard() { active_ = false; }
  operation_guard(const operation_guard&) = delete;
  operation_guard& operator=(const operation_guard&) = delete;
 private:
  bool& active_;
};

class win32_shortcut_platform final : public detail::shortcut_platform {
 public:
  std::uint32_t current_thread_id() const noexcept override { return GetCurrentThreadId(); }
  std::uint32_t current_process_id() const noexcept override { return GetCurrentProcessId(); }
  detail::shortcut_window_identity window_identity(void* window) const noexcept override {
    const auto hwnd = static_cast<HWND>(window);
    if (!hwnd || !IsWindow(hwnd)) return {};
    DWORD process = 0;
    const DWORD thread = GetWindowThreadProcessId(hwnd, &process);
    return {thread != 0, process, thread};
  }
  detail::shortcut_native_result register_key(void* window, std::uint32_t id,
                                              std::uint32_t modifiers,
                                              std::uint32_t key) noexcept override {
    SetLastError(ERROR_SUCCESS);
    const bool registered = RegisterHotKey(static_cast<HWND>(window), static_cast<int>(id), modifiers, key) != FALSE;
    const DWORD error = registered ? ERROR_SUCCESS : GetLastError();
    return {registered, error};
  }
  detail::shortcut_native_result unregister_key(void* window, std::uint32_t id) noexcept override {
    SetLastError(ERROR_SUCCESS);
    const bool removed = UnregisterHotKey(static_cast<HWND>(window), static_cast<int>(id)) != FALSE;
    const DWORD error = removed ? ERROR_SUCCESS : GetLastError();
    return {removed, error};
  }
};

detail::shortcut_platform& native_platform() {
  static win32_shortcut_platform platform;
  return platform;
}

}  // namespace

struct global_shortcut::implementation {
  explicit implementation(detail::shortcut_platform& native)
      : platform(native), owner_thread(native.current_thread_id()) {}

  ~implementation() {
    const bool needs_cleanup = active || cleanup_pending;
    active = false;
    operating = true;
    if (!needs_cleanup || !on_owner_thread()) return;
    const auto identity = platform.window_identity(options.owner_window);
    if (identity.valid && identity.process_id == platform.current_process_id() &&
        identity.thread_id == owner_thread)
      (void)platform.unregister_key(options.owner_window, options.id);
    // Best effort cannot report or establish confirmed unregistration.
  }

  bool on_owner_thread() const noexcept { return platform.current_thread_id() == owner_thread; }

  global_shortcut_response validate_owner(void* owner) const {
    const auto identity = platform.window_identity(owner);
    if (!owner || !identity.valid || identity.process_id != platform.current_process_id())
      return failure("E_INVALID_ARGUMENT", "Shortcut owner must be a valid current-process window.");
    if (identity.thread_id != owner_thread)
      return failure("E_THREAD_AFFINITY", "Shortcut owner must belong to the service creation thread.");
    return success();
  }

  detail::shortcut_platform& platform;
  const std::uint32_t owner_thread;
  global_shortcut_options options;
  bool active{false};
  bool cleanup_pending{false};
  bool retired{false};
  bool operating{false};
};

global_shortcut::global_shortcut() : global_shortcut(native_platform()) {}
global_shortcut::global_shortcut(detail::shortcut_platform& platform)
    : impl_(std::make_unique<implementation>(platform)) {}
global_shortcut::~global_shortcut() = default;

global_shortcut_response global_shortcut::register_shortcut(const global_shortcut_options& options) {
  auto& state = *impl_;
  if (!state.on_owner_thread())
    return failure("E_THREAD_AFFINITY", "Shortcut operations must run on the service creation thread.");
  if (state.operating) return failure("E_BUSY", "Another shortcut operation is in progress.");
  operation_guard guard(state.operating);
  if (state.retired)
    return failure("E_SHORTCUT_RETIRED", "This shortcut has been retired; allocate a new object and a new window ID.");
  if (state.active || state.cleanup_pending)
    return failure("E_SHORTCUT_ACTIVE", "Unregister the existing shortcut before releasing this object.");
  constexpr std::uint32_t allowed_modifiers = MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;
  if (options.id > 0xBFFF || (options.modifiers & ~allowed_modifiers) != 0 ||
      options.virtual_key == 0 || options.virtual_key > 254 || options.virtual_key == VK_F12)
    return failure("E_INVALID_ARGUMENT", "Shortcut requires an application ID, supported modifiers and a 1..254 key other than F12.");
  const auto owner = state.validate_owner(options.owner_window);
  if (!owner.ok()) return owner;
  const auto native_modifiers = options.modifiers | (options.no_repeat ? MOD_NOREPEAT : 0U);
  const auto registered = state.platform.register_key(options.owner_window, options.id, native_modifiers,
                                                     options.virtual_key);
  if (!registered.succeeded)
    return failure("E_SHORTCUT_REGISTER", "Windows did not register the shortcut; inspect native_error.", registered.error);
  state.options = options;
  state.active = true;
  return success();
}

global_shortcut_response global_shortcut::unregister_shortcut() {
  auto& state = *impl_;
  if (!state.on_owner_thread())
    return failure("E_THREAD_AFFINITY", "Shortcut operations must run on the service creation thread.");
  if (state.operating) return failure("E_BUSY", "Another shortcut operation is in progress.");
  operation_guard guard(state.operating);
  const bool needs_cleanup = state.active || state.cleanup_pending;
  state.active = false;
  if (!needs_cleanup) return success();
  state.cleanup_pending = true;
  const auto owner = state.validate_owner(state.options.owner_window);
  if (!owner.ok()) return owner;
  const auto removed = state.platform.unregister_key(state.options.owner_window, state.options.id);
  if (!removed.succeeded)
    return failure("E_SHORTCUT_UNREGISTER", "Windows did not confirm shortcut removal; cleanup must be retried.", removed.error);
  state.cleanup_pending = false;
  state.retired = true;
  return success();
}

global_shortcut_message_result global_shortcut::dispatch_message(std::uint32_t message,
                                                                std::uintptr_t wparam,
                                                                std::intptr_t lparam) {
  auto& state = *impl_;
  global_shortcut_message_result result;
  if (!state.on_owner_thread()) {
    result.response = failure("E_THREAD_AFFINITY", "Shortcut messages must be dispatched on the service creation thread.");
    return result;
  }
  if (state.operating) {
    result.response = failure("E_BUSY", "Another shortcut operation is in progress.");
    return result;
  }
  operation_guard guard(state.operating);
  const std::uintptr_t expected = (static_cast<std::uintptr_t>(state.options.virtual_key) << 16U) |
                                state.options.modifiers;
  if (!state.active || message != WM_HOTKEY || wparam != state.options.id ||
      static_cast<std::uintptr_t>(lparam) != expected) return result;
  result.handled = true;
  result.response = state.validate_owner(state.options.owner_window);
  if (result.response.ok()) result.activated = true;
  return result;
}

bool global_shortcut::active() const noexcept { return impl_->active; }
bool global_shortcut::cleanup_pending() const noexcept { return impl_->cleanup_pending; }
bool global_shortcut::retired() const noexcept { return impl_->retired; }

}  // namespace soui::runtime
