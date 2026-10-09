#include <soui/runtime/services/desktop/menu_service.h>

#include "menu_platform.h"
#include "unicode_conversion.h"

#include <windows.h>

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace soui::runtime {
namespace {

constexpr unsigned maximum_items = 256;
constexpr unsigned maximum_depth = 8;
constexpr std::size_t maximum_label_units = 1024;
constexpr std::size_t maximum_label_bytes = 64 * 1024;
// EndMenu acts on the calling thread. A nested service must not take over
// tracking while another public menu_service is inside TrackPopupMenuEx.
thread_local const void* active_menu_service = nullptr;

desktop_response success() { return {result_kind::success, {}, {}, {}}; }
desktop_response failure(const char* code, const char* message) {
  return {result_kind::failure, code, message, {}};
}
desktop_response native_failure(const char* code, const char* message, std::uint32_t error) {
  return {result_kind::failure, code, message, std::to_string(error)};
}
menu_result rejected(desktop_response response) { return {std::move(response), 0, false, 0}; }

class operation_guard final {
 public:
  explicit operation_guard(bool& state) : state_(state) { state_ = true; }
  ~operation_guard() { state_ = false; }
  operation_guard(const operation_guard&) = delete;
  operation_guard& operator=(const operation_guard&) = delete;
 private:
  bool& state_;
};

class win32_menu_platform final : public detail::menu_platform {
 public:
  std::uint32_t current_thread_id() const noexcept override { return GetCurrentThreadId(); }
  std::uint32_t current_process_id() const noexcept override { return GetCurrentProcessId(); }
  detail::menu_window_identity window_identity(void* window) const noexcept override {
    const auto hwnd = static_cast<HWND>(window);
    if (!hwnd || !IsWindow(hwnd)) return {};
    DWORD process = 0;
    const auto thread = GetWindowThreadProcessId(hwnd, &process);
    return {thread != 0, process, thread};
  }
  detail::menu_create_result create_popup() noexcept override {
    SetLastError(ERROR_SUCCESS);
    const auto handle = CreatePopupMenu();
    return {handle, handle ? 0U : GetLastError()};
  }
  detail::menu_native_status insert(void* parent, unsigned position,
                                    const detail::menu_native_item& item) noexcept override {
    MENUITEMINFOW data{};
    data.cbSize = sizeof(data);
    data.fMask = MIIM_FTYPE | MIIM_STATE;
    data.fType = item.kind == menu_item_kind::separator ? MFT_SEPARATOR : MFT_STRING;
    data.fState = (item.enabled ? MFS_ENABLED : MFS_DISABLED) | (item.checked ? MFS_CHECKED : MFS_UNCHECKED);
    if (item.kind != menu_item_kind::separator) {
      data.fMask |= MIIM_STRING;
      data.dwTypeData = const_cast<wchar_t*>(item.label.data());
      data.cch = static_cast<UINT>(item.label.size());
    }
    if (item.kind == menu_item_kind::command) { data.fMask |= MIIM_ID; data.wID = item.id; }
    if (item.kind == menu_item_kind::submenu) { data.fMask |= MIIM_SUBMENU; data.hSubMenu = static_cast<HMENU>(item.submenu); }
    SetLastError(ERROR_SUCCESS);
    const bool ok = InsertMenuItemW(static_cast<HMENU>(parent), position, TRUE, &data) != FALSE;
    return {ok, ok ? 0U : GetLastError()};
  }
  detail::menu_native_status destroy(void* root) noexcept override {
    SetLastError(ERROR_SUCCESS);
    const bool ok = DestroyMenu(static_cast<HMENU>(root)) != FALSE;
    return {ok, ok ? 0U : GetLastError()};
  }
  detail::menu_native_status foreground(void* owner) noexcept override {
    // TrackPopupMenu permits a child owner, but its top-level ancestor must
    // receive foreground activation for outside-click dismissal to work.
    const auto hwnd = GetAncestor(static_cast<HWND>(owner), GA_ROOT);
    DWORD process = 0;
    if (!hwnd || !GetWindowThreadProcessId(hwnd, &process) || process != GetCurrentProcessId())
      return {false, ERROR_INVALID_WINDOW_HANDLE};
    if (GetForegroundWindow() == hwnd) return {true, 0};
    SetLastError(ERROR_SUCCESS);
    const bool accepted = SetForegroundWindow(hwnd) != FALSE;
    const auto diagnostic = accepted ? 0U : GetLastError();
    return accepted || GetForegroundWindow() == hwnd
        ? detail::menu_native_status{true, 0} : detail::menu_native_status{false, diagnostic};
  }
  detail::menu_native_status finish(void* owner) noexcept override {
    SetLastError(ERROR_SUCCESS);
    const bool ok = PostMessageW(static_cast<HWND>(owner), WM_NULL, 0, 0) != FALSE;
    return {ok, ok ? 0U : GetLastError()};
  }
  detail::menu_track_result track(void* root, void* owner, int x, int y,
                                  std::uint32_t flags) noexcept override {
    SetLastError(ERROR_SUCCESS);
    const auto command = TrackPopupMenuEx(static_cast<HMENU>(root), flags, x, y,
                                          static_cast<HWND>(owner), nullptr);
    // RETURNCMD's zero is ambiguous. Preserve the immediately available
    // diagnostic without turning no selection into confirmed user cancellation.
    return {static_cast<std::uint32_t>(command), command ? 0U : GetLastError()};
  }
  detail::menu_native_status end_menu() noexcept override {
    SetLastError(ERROR_SUCCESS);
    const bool ok = EndMenu() != FALSE;
    return {ok, ok ? 0U : GetLastError()};
  }
};

detail::menu_platform& native_platform() {
  static win32_menu_platform platform;
  return platform;
}

struct normalized_item {
  menu_item_kind kind;
  std::uint32_t id;
  std::wstring label;
  bool enabled;
  bool checked;
  std::vector<normalized_item> children;
};

struct menu_validation {
  unsigned count{0};
  std::size_t label_bytes{0};
  std::unordered_map<std::uint32_t, bool> commands;

  desktop_response visit(const std::vector<menu_item>& input, std::vector<normalized_item>& output,
                          unsigned depth, bool ancestors_enabled) {
    if (depth > maximum_depth || input.size() > maximum_items - count)
      return failure("E_MENU_LIMIT", "Menu exceeds 256 items or eight levels.");
    output.reserve(input.size());
    for (const auto& item : input) {
      if (++count > maximum_items) return failure("E_MENU_LIMIT", "Menu exceeds 256 items.");
      if (item.kind == menu_item_kind::command) {
        if (item.id == 0 || item.id > 65535 || !item.children.empty() || commands.count(item.id) != 0)
          return failure("E_INVALID_ARGUMENT", "Commands require a unique 1..65535 ID and no children.");
        commands.emplace(item.id, ancestors_enabled && item.enabled);
      } else if (item.kind == menu_item_kind::separator) {
        if (item.id != 0 || !item.label.empty() || !item.children.empty() || item.checked || !item.enabled)
          return failure("E_INVALID_ARGUMENT", "Separators cannot carry an ID, label, children or state.");
      } else if (item.kind == menu_item_kind::submenu) {
        if (item.id != 0 || item.children.empty())
          return failure("E_INVALID_ARGUMENT", "Submenus require children and cannot carry command IDs.");
      } else return failure("E_INVALID_ARGUMENT", "Unknown menu item kind.");
      if (item.label.size() > maximum_label_units * 4)
        return failure("E_MENU_LIMIT", "Menu label exceeds the bounded UTF-8 input size.");
      auto label = detail::utf8_to_wide(item.label);
      if (!label) return failure("E_INVALID_ARGUMENT", "Menu labels require strict UTF-8 without embedded NUL.");
      if (label->size() > maximum_label_units || label->size() * sizeof(wchar_t) > maximum_label_bytes - label_bytes)
        return failure("E_MENU_LIMIT", "Menu labels exceed 1024 UTF-16 units each or 64 KiB total.");
      label_bytes += label->size() * sizeof(wchar_t);
      normalized_item normalized{item.kind, item.id, std::move(*label), item.enabled, item.checked, {}};
      if (item.kind == menu_item_kind::submenu) {
        const auto result = visit(item.children, normalized.children, depth + 1, ancestors_enabled && item.enabled);
        if (!result.ok()) return result;
      }
      output.push_back(std::move(normalized));
    }
    return success();
  }
};

}  // namespace

struct menu_service::implementation {
  explicit implementation(detail::menu_platform& value)
      : platform(value), owner_thread(value.current_thread_id()) {}
  ~implementation() {
    if (on_owner_thread()) { operating = true; (void)destroy_roots(); }
  }
  bool on_owner_thread() const noexcept { return platform.current_thread_id() == owner_thread; }
  desktop_response validate_owner(void* owner) const {
    const auto identity = platform.window_identity(owner);
    if (!owner || !identity.valid || identity.process_id != platform.current_process_id())
      return failure("E_INVALID_ARGUMENT", "Menu owner must be a valid current-process window.");
    if (identity.thread_id != owner_thread)
      return failure("E_THREAD_AFFINITY", "Menu owner must belong to the service creation thread.");
    return success();
  }

  detail::menu_native_status destroy_roots() noexcept {
    detail::menu_native_status first{true, 0};
    // Only independent ownership roots are retained: attached children belong
    // to their parent and must never receive a separate DestroyMenu call.
    for (std::size_t index = roots.size(); index != 0;) {
      --index;
      const auto result = platform.destroy(roots[index]);
      if (result.ok) roots.erase(roots.begin() + static_cast<std::ptrdiff_t>(index));
      else if (first.ok) first = result;
    }
    return first;
  }

  detail::menu_native_status build(const std::vector<normalized_item>& items, void*& root) {
    const auto created = platform.create_popup();
    if (!created.handle) return {false, created.error};
    root = created.handle;
    roots.push_back(root); // capacity is reserved before acquiring any HMENU
    unsigned position = 0;
    for (const auto& item : items) {
      void* child = nullptr;
      if (item.kind == menu_item_kind::submenu) {
        const auto nested = build(item.children, child);
        if (!nested.ok) return nested;
      }
      const detail::menu_native_item native{item.kind, item.id, item.label, item.enabled, item.checked, child};
      const auto appended = platform.insert(root, position++, native);
      if (!appended.ok) return appended;
      if (child) roots.erase(std::find(roots.begin(), roots.end(), child));
    }
    return {true, 0};
  }

  detail::menu_platform& platform;
  const std::uint32_t owner_thread;
  std::vector<void*> roots;
  bool operating{false};
  bool tracking{false};
  bool cancelling{false};
};

menu_service::menu_service() : menu_service(native_platform()) {}
menu_service::menu_service(detail::menu_platform& platform)
    : impl_(std::make_unique<implementation>(platform)) {}
menu_service::~menu_service() = default;

menu_result menu_service::show(void* owner, const std::vector<menu_item>& items, int x, int y) {
  auto& state = *impl_;
  if (!state.on_owner_thread())
    return rejected(failure("E_THREAD_AFFINITY", "Menu operations must run on the creation thread."));
  if (state.operating || active_menu_service)
    return rejected(failure("E_BUSY", "A menu operation is already in progress on this thread."));
  operation_guard operation(state.operating);
  if (!state.roots.empty())
    return rejected(failure("E_MENU_CLEANUP_PENDING", "Retry menu cleanup before showing another menu."));
  if (items.empty()) return rejected(failure("E_INVALID_ARGUMENT", "A popup menu requires at least one item."));
  menu_validation validated;
  std::vector<normalized_item> normalized;
  const auto validation = validated.visit(items, normalized, 1, true);
  if (!validation.ok()) return rejected(validation);
  const auto owner_validation = state.validate_owner(owner);
  if (!owner_validation.ok()) return rejected(owner_validation);
  state.roots.reserve(validated.count + 1);
  struct exception_cleanup {
    implementation& state;
    bool finished{false};
    ~exception_cleanup() { if (!finished) (void)state.destroy_roots(); }
  } cleanup_on_exception{state};
  void* root = nullptr;
  const auto built = state.build(normalized, root);
  menu_result result;
  if (!built.ok) {
    result.response = native_failure("E_MENU_BUILD", "Cannot create or attach the native menu tree.", built.error);
    result.native_error = built.error;
  } else {
    const auto foreground = state.platform.foreground(owner);
    if (!foreground.ok) {
      result.response = native_failure("E_MENU_FOREGROUND", "The menu owner could not become the foreground window.", foreground.error);
      result.native_error = foreground.error;
    } else {
      state.tracking = true;
      active_menu_service = &state;
      const auto tracked = state.platform.track(root, owner, x, y, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON);
      active_menu_service = nullptr;
      state.tracking = false;
      result.native_error = tracked.error;
      if (tracked.command != 0) {
        const auto command = validated.commands.find(tracked.command);
        if (command == validated.commands.end() || !command->second)
          result.response = failure("E_MENU_SELECTION", "Native selection is not an enabled command in this menu tree.");
        else { result.command_id = tracked.command; result.has_selection = true; }
      } else if (tracked.error != 0) {
        result.response = native_failure("E_MENU_TRACK", "Menu returned no command; a native last-error diagnostic is available.", tracked.error);
      } else result.response.message = "No command was selected; cancellation or successful display is not established.";
      const auto finished = state.platform.finish(owner);
      if (!finished.ok) {
        const auto previous = result.response.code;
        const auto previous_error = result.native_error;
        result.response = native_failure("E_MENU_FINISH", "The menu completion message could not be posted to its owner.", finished.error);
        if (!previous.empty()) result.response.message += " Previous result: " + previous + ", native_error=" + std::to_string(previous_error) + ".";
        result.native_error = finished.error;
        result.command_id = 0;
        result.has_selection = false;
      }
    }
  }
  const auto destroyed = state.destroy_roots();
  cleanup_on_exception.finished = true;
  if (!destroyed.ok) {
    const auto previous = result.response.code;
    const auto previous_error = result.native_error;
    result.response = native_failure("E_MENU_CLEANUP_PENDING", "Menu destruction failed; retained resources require cleanup().", destroyed.error);
    if (!previous.empty()) result.response.message += " Previous result: " + previous + ", native_error=" + std::to_string(previous_error) + ".";
    result.native_error = destroyed.error;
    result.command_id = 0;
    result.has_selection = false;
  }
  return result;
}

desktop_response menu_service::cancel() {
  auto& state = *impl_;
  if (!state.on_owner_thread()) return failure("E_THREAD_AFFINITY", "Menu cancellation must run on the creation thread.");
  if (!state.tracking || active_menu_service != &state)
    return failure("E_MENU_NOT_TRACKING", "This service is not tracking a menu; no other menu was cancelled.");
  if (state.cancelling) return failure("E_BUSY", "Menu cancellation is already in progress.");
  operation_guard cancellation(state.cancelling);
  const auto ended = state.platform.end_menu();
  return ended.ok ? success() : native_failure("E_MENU_CANCEL", "Native menu cancellation was not confirmed.", ended.error);
}

desktop_response menu_service::cleanup() {
  auto& state = *impl_;
  if (!state.on_owner_thread()) return failure("E_THREAD_AFFINITY", "Menu cleanup must run on the creation thread.");
  if (state.operating) return failure("E_BUSY", "Cannot destroy a menu while its operation is active.");
  operation_guard operation(state.operating);
  const auto destroyed = state.destroy_roots();
  return destroyed.ok ? success() : native_failure("E_MENU_CLEANUP_PENDING", "Menu destruction failed; retry cleanup().", destroyed.error);
}

bool menu_service::cleanup_pending() const noexcept { return !impl_->roots.empty(); }

}  // namespace soui::runtime
