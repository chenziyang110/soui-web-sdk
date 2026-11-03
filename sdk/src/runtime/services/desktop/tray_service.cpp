#include <soui/runtime/services/desktop/tray_service.h>

#include "tray_platform.h"
#include "unicode_conversion.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cwchar>
#include <iterator>
#include <limits>
#include <utility>

namespace soui::runtime {
namespace {

desktop_response failure(const char* code, const char* message) {
  return {result_kind::failure, code, message, {}};
}

desktop_response success() { return {result_kind::success, {}, {}, {}}; }

std::uint64_t creation_time(const FILETIME& time) noexcept {
  return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32U) | time.dwLowDateTime;
}

bool input_desktop_name(DWORD thread, std::array<wchar_t, 512>& name) noexcept {
  const auto desktop = GetThreadDesktop(thread); // Borrowed handle.
  DWORD needed{}; BOOL input{};
  return desktop && GetUserObjectInformationW(desktop, UOI_NAME, name.data(),
      static_cast<DWORD>(sizeof(name)), &needed) && needed >= sizeof(wchar_t) &&
      needed <= sizeof(name) && needed % sizeof(wchar_t) == 0 &&
      name[needed / sizeof(wchar_t) - 1] == L'\0' &&
      GetUserObjectInformationW(desktop, UOI_IO, &input, sizeof(input), &needed) && input;
}

std::uintptr_t next_taskbar_ticket() noexcept {
  // Shared by every service instance. Never wrap or reuse a posted scalar.
  static detail::tray_taskbar_ticket_source tickets;
  return tickets.reserve();
}

class focus_budget final {
 public:
  explicit focus_budget(detail::tray_platform& platform) noexcept : platform_(platform), last_(platform.focus_now_ms()) {
    valid_ = last_ <= (std::numeric_limits<std::uint64_t>::max)() - 1000;
    if (valid_) deadline_ = last_ + 1000;
  }
  std::uint32_t remaining() noexcept {
    if (!valid_) return 0;
    const auto now = platform_.focus_now_ms();
    if (now < last_ || now >= deadline_) { valid_ = false; return 0; }
    last_ = now;
    return static_cast<std::uint32_t>(deadline_ - now);
  }
 private:
  detail::tray_platform& platform_;
  std::uint64_t last_{0}, deadline_{0};
  bool valid_{false};
};

bool same_focus_identity(const detail::tray_focus_identity& a, const detail::tray_focus_identity& b) noexcept {
  return a.window == b.window && a.process_id == b.process_id && a.thread_id == b.thread_id &&
         a.creation_time == b.creation_time;
}

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
  std::uint64_t focus_now_ms() const noexcept override {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
  }
  void* focus_foreground() const noexcept override { return GetForegroundWindow(); }
  detail::tray_focus_target acquire_focus_target() noexcept override {
    detail::tray_focus_target target;
    const auto window = FindWindowW(L"Shell_TrayWnd", nullptr);
    DWORD pid{}; const auto thread = window ? GetWindowThreadProcessId(window, &pid) : 0;
    if (!window || !pid || !thread) return target;
    target.identity = {window, pid, thread, 0};
    target.process_lease = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    FILETIME birth{}, exit{}, kernel{}, user{};
    const bool time_known = target.process_lease && GetProcessTimes(target.process_lease, &birth, &exit, &kernel, &user);
    if (time_known) target.identity.creation_time = creation_time(birth);
    std::array<wchar_t, 32768> image{}, windows{};
    DWORD length = static_cast<DWORD>(image.size());
    const bool image_known = target.process_lease && QueryFullProcessImageNameW(target.process_lease, 0, image.data(), &length) &&
        length && length < image.size();
    const auto windows_length = GetSystemWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
    constexpr wchar_t suffix[] = L"\\explorer.exe";
    bool expected_image = false;
    if (image_known && windows_length && windows_length < windows.size() &&
        std::size(suffix) <= windows.size() - windows_length) {
      std::copy(std::begin(suffix), std::end(suffix), windows.begin() + windows_length);
      expected_image = _wcsicmp(image.data(), windows.data()) == 0;
    }
    DWORD after_pid{}, shell_pid{};
    const auto after_thread = GetWindowThreadProcessId(window, &after_pid);
    const auto shell_thread = GetWindowThreadProcessId(GetShellWindow(), &shell_pid);
    if (!IsWindow(window) || FindWindowW(L"Shell_TrayWnd", nullptr) != window ||
        after_pid != pid || after_thread != thread || !shell_thread || shell_pid != pid ||
        pid == GetCurrentProcessId() || thread == GetCurrentThreadId()) {
      target.query = detail::tray_focus_query::unsafe;
      return target;
    }
    if (target.process_lease && WaitForSingleObject(target.process_lease, 0) != WAIT_TIMEOUT) {
      target.query = detail::tray_focus_query::unsafe;
      return target;
    }
    if (time_known && image_known && windows_length && expected_image)
      target.query = detail::tray_focus_query::known;
    return target;
  }
  detail::tray_focus_environment observe_focus_environment(
      void* owner, const detail::tray_focus_target& target) const noexcept override {
    detail::tray_focus_environment observed;
    observed.owner = window_identity(owner);
    observed.foreground = GetForegroundWindow();
    const auto window = static_cast<HWND>(target.identity.window);
    DWORD pid{}, shell_pid{};
    const auto thread = window ? GetWindowThreadProcessId(window, &pid) : 0;
    const auto shell_thread = GetWindowThreadProcessId(GetShellWindow(), &shell_pid);
    observed.target = {window, pid, thread, 0};
    observed.target_alive = target.process_lease && WaitForSingleObject(target.process_lease, 0) == WAIT_TIMEOUT;
    if (!observed.owner.valid || !window || !IsWindow(window) || !observed.target_alive ||
        FindWindowW(L"Shell_TrayWnd", nullptr) != window || pid != target.identity.process_id ||
        thread != target.identity.thread_id || !shell_thread || shell_pid != pid) {
      observed.query = detail::tray_focus_query::unsafe;
      return observed;
    }
    FILETIME birth{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(target.process_lease, &birth, &exit, &kernel, &user)) return observed;
    observed.target.creation_time = creation_time(birth);
    if (observed.target.creation_time != target.identity.creation_time) {
      observed.query = detail::tray_focus_query::unsafe;
      return observed;
    }
    DWORD owner_session{}, target_session{};
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &owner_session) || !ProcessIdToSessionId(pid, &target_session)) return observed;
    observed.same_session = owner_session == target_session;
    std::array<wchar_t, 512> owner_desktop{}, target_desktop{};
    USEROBJECTFLAGS station{}; DWORD needed{};
    if (!input_desktop_name(GetCurrentThreadId(), owner_desktop) || !input_desktop_name(thread, target_desktop) ||
        !GetUserObjectInformationW(GetProcessWindowStation(), UOI_FLAGS, &station, sizeof(station), &needed)) return observed;
    observed.same_input_desktop = (station.dwFlags & WSF_VISIBLE) != 0 &&
        _wcsicmp(owner_desktop.data(), target_desktop.data()) == 0;
    GUITHREADINFO gui{}; gui.cbSize = sizeof(gui);
    if (!GetGUIThreadInfo(GetCurrentThreadId(), &gui)) return observed;
    observed.owner_menu_clear = !(gui.flags & (GUI_INMENUMODE | GUI_SYSTEMMENUMODE | GUI_POPUPMENUMODE));
    observed.foreground = GetForegroundWindow();
    observed.query = detail::tray_focus_query::known;
    return observed;
  }
  void release_focus_target(detail::tray_focus_target& target) noexcept override {
    if (target.process_lease) (void)CloseHandle(std::exchange(target.process_lease, nullptr));
  }
  bool activate_focus_target(void* window) noexcept override {
    return SetForegroundWindow(static_cast<HWND>(window)) != FALSE;
  }
  detail::tray_focus_barrier send_focus_barrier(void* window, std::uint32_t timeout_ms) noexcept override {
    DWORD_PTR result{};
    const bool sent = SendMessageTimeoutW(static_cast<HWND>(window), WM_NULL, 0, 0,
        SMTO_NORMAL | SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, timeout_ms, &result) != 0;
    return {sent, result};
  }
  detail::tray_focus_gui focus_gui(std::uint32_t thread_id) const noexcept override {
    GUITHREADINFO gui{}; gui.cbSize = sizeof(gui);
    if (!GetGUIThreadInfo(thread_id, &gui)) return {};
    return {detail::tray_focus_query::known, gui.hwndActive, gui.hwndFocus};
  }
  void wait_focus(std::uint32_t requested_ms) noexcept override { Sleep(requested_ms); }
  bool post_taskbar_wake(void* owner, std::uint32_t message, std::uintptr_t ticket) noexcept override {
    return PostMessageW(static_cast<HWND>(owner), message, detail::tray_taskbar_wake_tag,
                        static_cast<LPARAM>(ticket)) != FALSE;
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
    closing = true;
    cancel_taskbar_pending();
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

  struct operation_guard final {
    explicit operation_guard(implementation& value, bool schedule = true) noexcept : state(value), may_post(schedule) {
      state.operating = true;
    }
    ~operation_guard() {
      state.operating = false;
      if (may_post) state.post_taskbar_pending(); // Only enqueue; never ADD in a guard/destructor.
    }
    operation_guard(const operation_guard&) = delete;
    operation_guard& operator=(const operation_guard&) = delete;
    implementation& state;
    bool may_post;
  };

  void cancel_taskbar_pending() noexcept {
    pending_taskbar = false;
    wake_queued = false;
    wake_ticket = 0;
  }
  void post_taskbar_pending() noexcept {
    if (closing || !pending_taskbar || !desired || wake_queued || !on_owner_thread()) return;
    const auto owner = platform.window_identity(data.window);
    if (!owner.valid || owner.child || owner.message_only || owner.thread_id != owner_thread ||
        owner.process_id != platform.current_process_id()) return;
    const auto ticket = next_taskbar_ticket();
    if (!ticket) return; // Exhaustion leaves pending for a later native hint.
    wake_ticket = ticket;
    wake_queued = true;
    if (!platform.post_taskbar_wake(data.window, taskbar_message, ticket)) {
      wake_queued = false;
      wake_ticket = 0;
    }
  }
  bool focus_cancelled() const noexcept {
    return focus_abort_requested || pending_taskbar || closing || !desired || !installed;
  }
  bool focus_owner_valid(const detail::tray_window_identity& owner) const noexcept {
    return owner.valid && !owner.child && !owner.message_only && owner.thread_id == owner_thread &&
           owner.process_id == platform.current_process_id();
  }
  enum class focus_route { original_call, prepared_call, reject };
  enum class focus_state { unsafe, pending, accepted };
  focus_state focus_check(focus_budget& budget, const detail::tray_focus_target& target, unsigned foreground_kind) const noexcept {
    if (focus_cancelled() || !budget.remaining()) return focus_state::unsafe;
    const auto observed = platform.observe_focus_environment(data.window, target);
    if (focus_cancelled() || !budget.remaining() || observed.query != detail::tray_focus_query::known ||
        !focus_owner_valid(observed.owner) || !same_focus_identity(target.identity, observed.target) ||
        !observed.target_alive || !observed.same_session || !observed.same_input_desktop || !observed.owner_menu_clear) return focus_state::unsafe;
    // Only post-activation checks may tolerate a known-safe activation gap.
    // NULL is never a foreground match and cannot authorize NIM.
    if (foreground_kind != 0 && !observed.foreground) return focus_state::pending;
    const bool accepted = foreground_kind == 0 ? observed.foreground == data.window : foreground_kind == 1
        ? observed.foreground == data.window || observed.foreground == target.identity.window
        : observed.foreground == target.identity.window;
    return accepted ? focus_state::accepted : focus_state::unsafe;
  }
  focus_route prepare_focus(detail::tray_focus_target& target, focus_budget& budget) noexcept {
    if (focus_cancelled() || !budget.remaining()) return focus_route::reject;
    target = platform.acquire_focus_target();
    if (focus_cancelled() || !budget.remaining() || target.query == detail::tray_focus_query::unsafe)
      return focus_route::reject;
    if (target.query == detail::tray_focus_query::unavailable) {
      // No SFW has been attempted. Only missing metadata can retain the old
      // path; an observed identity change is never this fallback.
      const auto owner = platform.window_identity(data.window);
      const auto foreground = platform.focus_foreground();
      return !focus_cancelled() && budget.remaining() && focus_owner_valid(owner) && foreground == data.window
          ? focus_route::original_call : focus_route::reject;
    }
    if (!target.process_lease || !target.identity.window || target.identity.window == data.window ||
        !target.identity.process_id || !target.identity.thread_id || !target.identity.creation_time ||
        target.identity.process_id == platform.current_process_id() || target.identity.thread_id == owner_thread ||
        focus_check(budget, target, 0) != focus_state::accepted) return focus_route::reject;
    // From the attempted activation onward every failure is a rejection;
    // there is no second activation, barrier, or fallback NIM.
    const bool activated = platform.activate_focus_target(target.identity.window);
    if (focus_check(budget, target, 1) == focus_state::unsafe || !activated) return focus_route::reject;
    const auto timeout = budget.remaining();
    if (!timeout || focus_cancelled()) return focus_route::reject;
    const auto barrier = platform.send_focus_barrier(target.identity.window, timeout);
    if (focus_check(budget, target, 2) == focus_state::unsafe || !barrier.sent) return focus_route::reject;
    unsigned polls = 0;
    enum class readiness { unsafe, foreground_pending, not_ready, ready };
    auto ready = [&]() noexcept {
      if (polls >= 256) return readiness::unsafe;
      const auto before = focus_check(budget, target, 2);
      if (before == focus_state::unsafe) return readiness::unsafe;
      ++polls;
      const auto gui = platform.focus_gui(target.identity.thread_id);
      const auto after = focus_check(budget, target, 2);
      if (after == focus_state::unsafe || gui.query != detail::tray_focus_query::known) return readiness::unsafe;
      if (before == focus_state::pending || after == focus_state::pending) return readiness::foreground_pending;
      return gui.active == target.identity.window && gui.focus == target.identity.window ? readiness::ready : readiness::not_ready;
    };
    for (;;) {
      const auto state = ready();
      if (state == readiness::unsafe) return focus_route::reject;
      if (state == readiness::ready) {
        // Recheck the actual predicate at the final boundary. Only a safe
        // foreground NULL returns to waiting; ordinary GUI match loss still
        // rejects. A later attempt must establish a new complete ready pair.
        const auto final = ready();
        if (final == readiness::ready)
          return !focus_cancelled() && budget.remaining() ? focus_route::prepared_call : focus_route::reject;
        if (final != readiness::foreground_pending) return focus_route::reject;
      }
      if (polls >= 256) return focus_route::reject;
      const auto remaining = budget.remaining();
      if (!remaining || focus_cancelled()) return focus_route::reject;
      platform.wait_focus((std::min)(remaining, std::uint32_t{5}));
      if (focus_check(budget, target, 2) == focus_state::unsafe) return focus_route::reject;
    }
  }

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
  bool pending_taskbar{false}, wake_queued{false}, closing{false};
  bool focus_preparing{false}, focus_abort_requested{false};
  std::uintptr_t wake_ticket{0};
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
  implementation::operation_guard guard(state);
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
  state.cancel_taskbar_pending();
  state.taskbar_message = taskbar_message;
  state.desired = true;
  return state.add();
}

desktop_response tray_service::update_tooltip(const std::string& value) {
  auto& state = *impl_;
  if (!state.on_owner_thread())
    return failure("E_THREAD_AFFINITY", "Tray operations must run on the service creation thread.");
  if (state.operating) return failure("E_BUSY", "Another tray operation is in progress.");
  implementation::operation_guard guard(state);
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
  if (state.operating) {
    if (state.focus_preparing) state.focus_abort_requested = true;
    return failure("E_BUSY", "Another tray operation is in progress.");
  }
  implementation::operation_guard guard(state);
  state.cancel_taskbar_pending();
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
  implementation::operation_guard guard(state);
  if (!state.installed)
    return failure("E_TRAY_NOT_INSTALLED", "Returning tray focus requires a confirmed installation.");
  const auto owner = state.validate_owner(state.data.window);
  if (!owner.ok()) return owner;
  detail::tray_focus_target target;
  struct target_scope {
    detail::tray_platform& platform;
    detail::tray_focus_target& target;
    ~target_scope() { if (target.process_lease) platform.release_focus_target(target); }
  } lease{state.platform, target};
  struct preparation_scope {
    implementation& state;
    ~preparation_scope() { state.focus_preparing = false; }
  } preparation{state};
  state.focus_abort_requested = false;
  // Compatibility selection is first: background/NULL callers retain exactly
  // the old NIM path, including when a rebuild hint is pending.
  if (state.platform.focus_foreground() == state.data.window) {
    state.focus_preparing = true;
    focus_budget budget(state.platform);
    if (state.prepare_focus(target, budget) == implementation::focus_route::reject ||
        state.focus_cancelled() || !budget.remaining())
      return failure("E_TRAY_FOCUS", "Tray focus preparation was rejected; NIM_SETFOCUS was not attempted.");
  }
  state.focus_preparing = false;
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
  const bool taskbar = state.taskbar_message != 0 && message == state.taskbar_message;
  if (taskbar && wparam == detail::tray_taskbar_wake_tag) {
    result.handled = true;
    const auto ticket = static_cast<std::uintptr_t>(lparam);
    if (!state.wake_queued || !state.pending_taskbar || !ticket || ticket != state.wake_ticket)
      return result; // Stale tagged wake is not a fresh native restart hint.
    state.wake_queued = false;
    state.wake_ticket = 0; // This queued message has actually been consumed.
  }
  if (state.operating) {
    if (taskbar && state.desired) {
      state.pending_taskbar = true;
      if (state.focus_preparing) state.focus_abort_requested = true;
      result.handled = true;
    }
    result.response = failure("E_BUSY", "Another tray operation is in progress.");
    return result;
  }
  // A recovery handler cannot schedule itself again. Hints received inside
  // its ADD/VERSION calls remain pending until a later external drive.
  implementation::operation_guard guard(state, !taskbar);
  if (taskbar) {
    result.handled = true;
    state.cancel_taskbar_pending();
    if (!state.desired) return result;
    // A rebuild notification is a hint, not proof that the old registration
    // disappeared. If ADD fails (including a duplicate/spoofed notification),
    // retain the possible old registration so remove still attempts DELETE.
    state.cleanup_pending = state.installed || state.cleanup_pending;
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
