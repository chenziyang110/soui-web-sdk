#include <soui/runtime/services/desktop/tray_service.h>

#include "tray_platform.h"
#include "unicode_conversion.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <utility>

namespace soui::runtime {
namespace {

desktop_response failure(const char* code, const char* message) {
  return {result_kind::failure, code, message, {}};
}

desktop_response success() { return {result_kind::success, {}, {}, {}}; }

class operation_guard final {
 public:
  explicit operation_guard(bool& active) : active_(active) { active_ = true; }
  ~operation_guard() { active_ = false; }
  operation_guard(const operation_guard&) = delete;
  operation_guard& operator=(const operation_guard&) = delete;
 private:
  bool& active_;
};

class win32_tray_platform final : public detail::tray_platform {
 public:
  std::uint32_t current_thread_id() const noexcept override { return GetCurrentThreadId(); }
  std::uint32_t current_process_id() const noexcept override { return GetCurrentProcessId(); }
  detail::tray_window_identity window_identity(void* window) const noexcept override {
    const auto hwnd = static_cast<HWND>(window);
    detail::tray_window_identity identity;
    if (!hwnd || !IsWindow(hwnd)) return identity;
    DWORD process_id = 0;
    identity.thread_id = GetWindowThreadProcessId(hwnd, &process_id);
    identity.process_id = process_id;
    identity.valid = identity.thread_id != 0;
    identity.child = (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD) != 0 ||
                     GetAncestor(hwnd, GA_ROOT) != hwnd;
    identity.message_only = GetParent(hwnd) == HWND_MESSAGE;
    return identity;
  }
  std::uint32_t taskbar_created_message() noexcept override {
    return RegisterWindowMessageW(L"TaskbarCreated");
  }
  void* copy_icon(void* icon) noexcept override { return CopyIcon(static_cast<HICON>(icon)); }
  void destroy_icon(void* icon) noexcept override {
    if (icon) (void)DestroyIcon(static_cast<HICON>(icon));
  }
  bool notify(detail::tray_native_operation operation,
              const detail::tray_native_data& data) noexcept override {
    NOTIFYICONDATAW native{};
    native.cbSize = sizeof(native);
    native.hWnd = static_cast<HWND>(data.window);
    native.uID = data.id;
    DWORD command = NIM_DELETE;
    switch (operation) {
      case detail::tray_native_operation::add:
        command = NIM_ADD;
        native.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        native.uCallbackMessage = data.callback_message;
        native.hIcon = static_cast<HICON>(data.icon);
        std::copy(data.tooltip.begin(), data.tooltip.end(), native.szTip);
        break;
      case detail::tray_native_operation::set_version:
        command = NIM_SETVERSION;
        native.uVersion = NOTIFYICON_VERSION_4;
        break;
      case detail::tray_native_operation::update_tooltip:
        command = NIM_MODIFY;
        native.uFlags = NIF_TIP | NIF_SHOWTIP;
        std::copy(data.tooltip.begin(), data.tooltip.end(), native.szTip);
        break;
      case detail::tray_native_operation::return_focus:
        command = NIM_SETFOCUS;
        break;
      case detail::tray_native_operation::remove:
        break;
    }
    return Shell_NotifyIconW(command, &native) != FALSE;
  }
};

detail::tray_platform& native_platform() {
  static win32_tray_platform platform;
  return platform;
}

}  // namespace

struct tray_service::implementation {
  explicit implementation(detail::tray_platform& native)
      : platform(native), owner_thread(native.current_thread_id()) {}

  ~implementation() {
    // A destructor cannot report a failed Shell cleanup. Explicit remove on
    // the owner thread is the application's confirmation boundary.
    const bool needs_cleanup = installed || cleanup_pending;
    operating = true;
    desired = false;
    installed = false;
    if (on_owner_thread() && needs_cleanup) {
      const auto identity = platform.window_identity(data.window);
      if (identity.valid && identity.thread_id == owner_thread &&
          identity.process_id == platform.current_process_id())
        (void)platform.notify(detail::tray_native_operation::remove, data);
    }
    release_icon();
  }

  bool on_owner_thread() const noexcept { return platform.current_thread_id() == owner_thread; }

  desktop_response validate_owner(void* window) const {
    const auto identity = platform.window_identity(window);
    if (!window || !identity.valid || identity.child || identity.message_only ||
        identity.process_id != platform.current_process_id())
      return failure("E_INVALID_ARGUMENT", "Tray owner must be a current-process top-level window.");
    if (identity.thread_id != owner_thread)
      return failure("E_THREAD_AFFINITY", "Tray owner must belong to the service creation thread.");
    return success();
  }

  void release_icon() noexcept {
    if (data.icon) platform.destroy_icon(std::exchange(data.icon, nullptr));
  }

  desktop_response add() {
    installed = false;
    if (!platform.notify(detail::tray_native_operation::add, data))
      return failure("E_TRAY_ADD", "Shell did not accept the tray icon; residency remains desired.");
    if (!platform.notify(detail::tray_native_operation::set_version, data)) {
      cleanup_pending = !platform.notify(detail::tray_native_operation::remove, data);
      return cleanup_pending
          ? failure("E_TRAY_CLEANUP_PENDING", "Tray version setup and rollback failed; explicit cleanup is required.")
          : failure("E_TRAY_VERSION", "Tray version setup failed; the added icon was removed.");
    }
    installed = true;
    cleanup_pending = false;
    return success();
  }

  detail::tray_platform& platform;
  const std::uint32_t owner_thread;
  detail::tray_native_data data;
  std::uint32_t taskbar_message{0};
  bool installed{false};
  bool desired{false};
  bool cleanup_pending{false};
  bool operating{false};
};

tray_service::tray_service() : tray_service(native_platform()) {}
tray_service::tray_service(detail::tray_platform& platform)
    : impl_(std::make_unique<implementation>(platform)) {}
tray_service::~tray_service() = default;

desktop_response tray_service::install(const tray_options& options) {
  auto& state = *impl_;
  if (!state.on_owner_thread())
    return failure("E_THREAD_AFFINITY", "Tray operations must run on the service creation thread.");
  if (state.operating) return failure("E_BUSY", "Another tray operation is in progress.");
  operation_guard guard(state.operating);
  if (state.desired || state.installed || state.cleanup_pending)
    return failure("E_TRAY_ACTIVE", "Remove the existing tray identity before installing another.");
  if (!options.icon || options.icon_id == 0 || options.icon_id > 65535 ||
      options.callback_message < WM_APP || options.callback_message > 0xBFFF)
    return failure("E_INVALID_ARGUMENT", "Tray requires an icon, a 1..65535 ID and a WM_APP callback message.");
  if (options.tooltip.size() > 127U * 4U)
    return failure("E_INVALID_ARGUMENT", "Tray tooltip exceeds the bounded UTF-8 input size.");
  const auto tooltip = detail::utf8_to_wide(options.tooltip);
  if (!tooltip || tooltip->size() > 127)
    return failure("E_INVALID_ARGUMENT", "Tray tooltip must be valid UTF-8 without NUL and at most 127 UTF-16 units.");
  const auto owner = state.validate_owner(options.owner_window);
  if (!owner.ok()) return owner;
  const auto taskbar_message = state.platform.taskbar_created_message();
  if (taskbar_message < 0xC000 || taskbar_message > 0xFFFF)
    return failure("E_TRAY_MESSAGE", "Cannot register the TaskbarCreated notification.");
  // Finish allocation before acquiring the copied native resource.
  detail::tray_native_data candidate{options.owner_window, nullptr, options.icon_id,
                                     options.callback_message, *tooltip};
  candidate.icon = state.platform.copy_icon(options.icon);
  if (!candidate.icon) return failure("E_TRAY_ICON", "Cannot copy the tray icon.");
  state.data = std::move(candidate);
  state.taskbar_message = taskbar_message;
  state.desired = true;
  return state.add();
}

desktop_response tray_service::update_tooltip(const std::string& value) {
  auto& state = *impl_;
  if (!state.on_owner_thread())
    return failure("E_THREAD_AFFINITY", "Tray operations must run on the service creation thread.");
  if (state.operating) return failure("E_BUSY", "Another tray operation is in progress.");
  operation_guard guard(state.operating);
  if (value.size() > 127U * 4U)
    return failure("E_INVALID_ARGUMENT", "Tray tooltip exceeds the bounded UTF-8 input size.");
  const auto tooltip = detail::utf8_to_wide(value);
  if (!tooltip || tooltip->size() > 127)
    return failure("E_INVALID_ARGUMENT", "Tray tooltip must be valid UTF-8 without NUL and at most 127 UTF-16 units.");
  if (!state.installed)
    return failure("E_TRAY_NOT_INSTALLED", "Tray tooltip updates require a confirmed installation.");
  const auto owner = state.validate_owner(state.data.window);
  if (!owner.ok()) return owner;
  auto candidate = state.data;
  candidate.tooltip = *tooltip;
  if (!state.platform.notify(detail::tray_native_operation::update_tooltip, candidate))
    return failure("E_TRAY_MODIFY", "Shell did not confirm the tooltip update; previous text is retained.");
  state.data.tooltip = std::move(candidate.tooltip);
  return success();
}

desktop_response tray_service::remove() {
  auto& state = *impl_;
  if (!state.on_owner_thread())
    return failure("E_THREAD_AFFINITY", "Tray operations must run on the service creation thread.");
  if (state.operating) return failure("E_BUSY", "Another tray operation is in progress.");
  operation_guard guard(state.operating);
  state.desired = false;
  const bool needs_cleanup = state.installed || state.cleanup_pending;
  state.installed = false;
  if (needs_cleanup) {
    state.cleanup_pending = true;
    const auto owner = state.validate_owner(state.data.window);
    if (!owner.ok()) return owner;
    if (!state.platform.notify(detail::tray_native_operation::remove, state.data))
      return failure("E_TRAY_DELETE", "Shell did not confirm tray removal; cleanup must be retried.");
  }
  state.cleanup_pending = false;
  state.release_icon();
  return success();
}

desktop_response tray_service::return_focus() {
  auto& state = *impl_;
  if (!state.on_owner_thread())
    return failure("E_THREAD_AFFINITY", "Tray operations must run on the service creation thread.");
  if (state.operating) return failure("E_BUSY", "Another tray operation is in progress.");
  operation_guard guard(state.operating);
  if (!state.installed)
    return failure("E_TRAY_NOT_INSTALLED", "Returning tray focus requires a confirmed installation.");
  const auto owner = state.validate_owner(state.data.window);
  if (!owner.ok()) return owner;
  if (!state.platform.notify(detail::tray_native_operation::return_focus, state.data))
    return failure("E_TRAY_FOCUS", "Shell did not confirm keyboard focus returned to the notification area.");
  return success();
}

tray_message_result tray_service::dispatch_message(std::uint32_t message,
                                                   std::uintptr_t wparam,
                                                   std::intptr_t lparam) {
  auto& state = *impl_;
  tray_message_result result;
  if (!state.on_owner_thread()) {
    result.response = failure("E_THREAD_AFFINITY", "Tray messages must be dispatched on the service creation thread.");
    return result;
  }
  if (state.operating) {
    result.response = failure("E_BUSY", "Another tray operation is in progress.");
    return result;
  }
  operation_guard guard(state.operating);
  if (state.taskbar_message != 0 && message == state.taskbar_message) {
    result.handled = true;
    if (!state.desired) return result;
    // A rebuild notification is a hint, not proof that the old registration
    // disappeared. If ADD fails (including a duplicate/spoofed notification),
    // retain the possible old registration so remove still attempts DELETE.
    state.cleanup_pending = false;
    state.installed = false;
    result.response = state.validate_owner(state.data.window);
    if (result.response.ok()) result.response = state.add();
    return result;
  }
  const auto notification = static_cast<std::uint32_t>(lparam);
  if (!state.installed || message != state.data.callback_message ||
      (notification >> 16U) != state.data.id) return result;
  result.handled = true;
  result.response = state.validate_owner(state.data.window);
  if (!result.response.ok()) return result;
  const auto kind = notification & 0xFFFFU;
  if (kind == NIN_SELECT || kind == NIN_KEYSELECT) {
    result.event = tray_event::activated;
    result.position_valid = true;
    result.x = static_cast<std::int16_t>(wparam & 0xFFFFU);
    result.y = static_cast<std::int16_t>((wparam >> 16U) & 0xFFFFU);
  } else if (kind == WM_CONTEXTMENU) {
    result.event = tray_event::context_menu;
    // NOTIFYICON_VERSION_4 does not define wParam coordinates for this event.
  }
  return result;
}

bool tray_service::installed() const noexcept { return impl_->installed; }
bool tray_service::desired() const noexcept { return impl_->desired; }
bool tray_service::cleanup_pending() const noexcept { return impl_->cleanup_pending; }

}  // namespace soui::runtime
