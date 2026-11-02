#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <winver.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using clock_type = std::chrono::steady_clock;
constexpr wchar_t owner_class[] = L"SouiTrayFocusDiagnosticOwner";
constexpr UINT callback_message = WM_APP + 19;
constexpr UINT completion_message = WM_APP + 20;
constexpr UINT_PTR timer_id = 1;
constexpr unsigned icon_id = 19;
constexpr long long whole_ms = 60000;
constexpr std::array<long long, 5> offsets{{0, 16, 50, 200, 1000}};
constexpr std::size_t ancestor_limit = 8;
constexpr std::size_t ancestor_node_bytes = 6144;
bool observation_handles_closed = true;
bool observation_data_truncated = false;

std::string utf8(const std::wstring& value) {
  if (value.empty()) return {};
  const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (count <= 0) throw std::runtime_error("UTF-16 diagnostic text could not be encoded");
  std::string result(static_cast<std::size_t>(count), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), result.data(), count, nullptr, nullptr) != count)
    throw std::runtime_error("UTF-16 diagnostic text encoding changed");
  return result;
}
std::string quote(const std::string& value) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result = "\"";
  for (const unsigned char ch : value) {
    if (ch == '"' || ch == '\\') { result += '\\'; result += static_cast<char>(ch); }
    else if (ch < 32) { result += "\\u00"; result += digits[ch >> 4]; result += digits[ch & 15]; }
    else result += static_cast<char>(ch);
  }
  return result + '"';
}
const char* boolean(bool value) { return value ? "true" : "false"; }
std::string handle_value(const void* value) {
  char buffer[32]{};
  std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(value)));
  return quote(buffer);
}
std::string guid_text(const GUID& value) {
  wchar_t buffer[40]{};
  if (!StringFromGUID2(value, buffer, static_cast<int>(std::size(buffer))))
    throw std::runtime_error("GUID formatting failed");
  return utf8(buffer);
}
std::string filetime_text(const FILETIME& value) {
  const auto number = (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
  return quote(std::to_string(number));
}
struct object {
  std::string text{"{"};
  bool first{true};
  object& add(const char* key, const std::string& raw) {
    if (!first) text += ',';
    first = false; text += quote(key) + ':' + raw; return *this;
  }
  std::string done() const { return text + '}'; }
};
std::string unknown(DWORD error) {
  return object{}.add("known", "false").add("win32Error", std::to_string(error)).done();
}
std::string rect_value(const RECT& rect) {
  return object{}.add("left", std::to_string(rect.left)).add("top", std::to_string(rect.top))
      .add("right", std::to_string(rect.right)).add("bottom", std::to_string(rect.bottom))
      .add("nonempty", boolean(rect.right > rect.left && rect.bottom > rect.top)).done();
}
struct window_stamp {
  HWND window{};
  DWORD pid{}, thread{};
  FILETIME birth{};
  bool known{};
  DWORD error{};
};
window_stamp stamp_window(HWND window) {
  window_stamp result{}; result.window = window;
  if (!window) { result.error = ERROR_INVALID_WINDOW_HANDLE; return result; }
  result.thread = GetWindowThreadProcessId(window, &result.pid);
  if (!result.thread || !result.pid) { result.error = GetLastError(); return result; }
  const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, result.pid);
  if (!process) { result.error = GetLastError(); return result; }
  FILETIME exit{}, kernel{}, user{};
  result.known = GetProcessTimes(process, &result.birth, &exit, &kernel, &user) != FALSE;
  result.error = result.known ? ERROR_SUCCESS : GetLastError();
  const bool closed = CloseHandle(process) != FALSE;
  observation_handles_closed = observation_handles_closed && closed;
  return result;
}
std::string stamp_value(const window_stamp& value) {
  return object{}.add("hwnd", handle_value(value.window)).add("pid", std::to_string(value.pid))
      .add("threadId", std::to_string(value.thread)).add("known", boolean(value.known))
      .add("creationFileTime", value.known ? filetime_text(value.birth) : "null")
      .add("win32Error", std::to_string(value.error)).done();
}
bool same_stamp(const window_stamp& left, const window_stamp& right) {
  return left.known && right.known && left.window == right.window && left.pid == right.pid && left.thread == right.thread &&
      left.birth.dwLowDateTime == right.birth.dwLowDateTime && left.birth.dwHighDateTime == right.birth.dwHighDateTime;
}
std::string geometry(HWND window) {
  RECT outer{}, client{}, screen{};
  const bool outer_ok = GetWindowRect(window, &outer) != FALSE;
  const auto outer_error = outer_ok ? ERROR_SUCCESS : GetLastError();
  const bool client_ok = GetClientRect(window, &client) != FALSE;
  const auto client_error = client_ok ? ERROR_SUCCESS : GetLastError();
  bool mapped{}; DWORD map_error{};
  if (client_ok) {
    screen = client; SetLastError(ERROR_SUCCESS);
    const auto displacement = MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&screen), 2);
    map_error = GetLastError(); mapped = displacement != 0 || map_error == ERROR_SUCCESS;
  }
  return object{}.add("windowRect", outer_ok ? rect_value(outer) : "null").add("windowRectError", std::to_string(outer_error))
      .add("clientRect", client_ok ? rect_value(client) : "null").add("clientRectError", std::to_string(client_error))
      .add("clientMappedToScreen", mapped ? rect_value(screen) : "null").add("mapError", std::to_string(map_error))
      .add("windowRectMayBeDpiVirtualized", "true").add("iconRectCoordinateComparisonQualified", "false").done();
}
std::string dpi_context() {
  const auto user32 = GetModuleHandleW(L"user32.dll");
  using get_context = HANDLE (WINAPI*)();
  using get_awareness = int (WINAPI*)(HANDLE);
  const auto context_fn = reinterpret_cast<get_context>(GetProcAddress(user32, "GetThreadDpiAwarenessContext"));
  const auto awareness_fn = reinterpret_cast<get_awareness>(GetProcAddress(user32, "GetAwarenessFromDpiAwarenessContext"));
  const auto context = context_fn ? context_fn() : nullptr;
  return object{}.add("processDpiAware", boolean(IsProcessDPIAware() != FALSE))
      .add("threadContextApiAvailable", boolean(context_fn && awareness_fn))
      .add("threadAwareness", context && awareness_fn ? std::to_string(awareness_fn(context)) : "null")
      .add("awarenessChangedByProbe", "false").add("rectanglesNormalizedToCommonDpi", "false")
      .add("externalDpiAndMonitorEvidenceRequired", "true").done();
}
std::string ancestor_chain(HWND window, clock_type::time_point deadline) {
  std::array<HWND, ancestor_limit> seen{};
  SetLastError(ERROR_SUCCESS); const auto expected_root = GetAncestor(window, GA_ROOT);
  const auto root_error = expected_root ? ERROR_SUCCESS : GetLastError();
  std::string nodes = "["; std::size_t count{};
  bool complete{}, root_reached{}, repeated{}, expired{}, byte_limit{}, identities_known_unchanged{true};
  while (window && count < ancestor_limit) {
    if (clock_type::now() >= deadline) { expired = true; break; }
    for (std::size_t index = 0; index < count; ++index) repeated = repeated || seen[index] == window;
    if (repeated) break;
    seen[count] = window;
    const auto before = stamp_window(window);
    std::array<wchar_t, 64> name{};
    const auto length = GetClassNameW(window, name.data(), static_cast<int>(name.size()));
    const auto name_error = length ? ERROR_SUCCESS : GetLastError();
    SetLastError(ERROR_SUCCESS); const auto parent = GetAncestor(window, GA_PARENT);
    const auto parent_error = parent ? ERROR_SUCCESS : GetLastError();
    const auto position = geometry(window);
    const auto after = stamp_window(window);
    identities_known_unchanged = identities_known_unchanged && same_stamp(before, after) && length > 0 &&
        length < static_cast<int>(name.size()) - 1;
    const auto node = object{}.add("identityBefore", stamp_value(before)).add("identityAfter", stamp_value(after))
        .add("className", length ? quote(utf8(std::wstring(name.data(), length))) : "null")
        .add("classError", std::to_string(name_error)).add("classMayBeTruncated", boolean(length == static_cast<int>(name.size()) - 1))
        .add("visible", boolean(IsWindowVisible(window) != FALSE)).add("parentByGaParent", handle_value(parent))
        .add("parentError", std::to_string(parent_error))
        .add("geometry", position).add("identityUnchangedDuringQuery", boolean(same_stamp(before, after))).done();
    if (nodes.size() + node.size() + 1 > ancestor_node_bytes) {
      byte_limit = true; observation_data_truncated = true; break;
    }
    if (count++) nodes += ',';
    nodes += node;
    if (window == expected_root) { root_reached = true; complete = identities_known_unchanged; break; }
    window = parent;
  }
  return object{}.add("nodes", nodes + ']').add("limit", std::to_string(ancestor_limit)).add("complete", boolean(complete))
      .add("expectedRoot", handle_value(expected_root)).add("expectedRootError", std::to_string(root_error))
      .add("rootReached", boolean(root_reached)).add("identitiesKnownUnchanged", boolean(identities_known_unchanged))
      .add("nodeByteLimit", std::to_string(ancestor_node_bytes)).add("nodeByteLimitReached", boolean(byte_limit))
      .add("repeatedHandle", boolean(repeated)).add("deadlineReached", boolean(expired))
      .add("windowReuseWithinProcessNotExcluded", "true").done();
}
std::string desktop_name(HDESK desktop) {
  if (!desktop) return unknown(GetLastError());
  std::array<wchar_t, 512> buffer{};
  DWORD required{};
  if (!GetUserObjectInformationW(desktop, UOI_NAME, buffer.data(),
      static_cast<DWORD>(sizeof(buffer)), &required)) return unknown(GetLastError());
  return object{}.add("known", "true").add("name", quote(utf8(buffer.data()))).done();
}
std::string thread_desktop(DWORD thread) {
  if (!thread) return unknown(ERROR_INVALID_PARAMETER);
  SetLastError(ERROR_SUCCESS);
  // GetThreadDesktop returns a borrowed handle; never close it.
  return desktop_name(GetThreadDesktop(thread));
}
std::string input_desktop() {
  const auto desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
  if (!desktop) return unknown(GetLastError());
  const auto result = desktop_name(desktop);
  const bool closed = CloseDesktop(desktop) != FALSE;
  observation_handles_closed = observation_handles_closed && closed;
  const auto error = closed ? ERROR_SUCCESS : GetLastError();
  return object{}.add("identity", result).add("handleClosed", boolean(closed))
      .add("closeError", std::to_string(error)).done();
}
std::string token_info(HANDLE process) {
  HANDLE token{};
  if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return unknown(GetLastError());
  object result;
  DWORD required{};
  TOKEN_ELEVATION elevation{};
  const bool elevation_ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &required) != FALSE;
  const auto elevation_error = elevation_ok ? ERROR_SUCCESS : GetLastError();
  result.add("elevated", elevation_ok ? boolean(elevation.TokenIsElevated != 0) : "null")
      .add("elevationError", std::to_string(elevation_error));
  TOKEN_ELEVATION_TYPE type{};
  const bool type_ok = GetTokenInformation(token, TokenElevationType, &type, sizeof(type), &required) != FALSE;
  const auto type_error = type_ok ? ERROR_SUCCESS : GetLastError();
  result.add("elevationType", type_ok ? std::to_string(static_cast<unsigned>(type)) : "null")
      .add("elevationTypeError", std::to_string(type_error));
  DWORD session{}, ui_access{};
  const bool session_ok = GetTokenInformation(token, TokenSessionId, &session, sizeof(session), &required) != FALSE;
  const auto session_error = session_ok ? ERROR_SUCCESS : GetLastError();
  result.add("sessionId", session_ok ? std::to_string(session) : "null").add("sessionError", std::to_string(session_error));
  const bool ui_ok = GetTokenInformation(token, TokenUIAccess, &ui_access, sizeof(ui_access), &required) != FALSE;
  const auto ui_error = ui_ok ? ERROR_SUCCESS : GetLastError();
  result.add("uiAccess", ui_ok ? boolean(ui_access != 0) : "null").add("uiAccessError", std::to_string(ui_error));
  alignas(TOKEN_MANDATORY_LABEL) std::array<unsigned char, 4096> label_bytes{};
  const bool label_ok = GetTokenInformation(token, TokenIntegrityLevel, label_bytes.data(),
      static_cast<DWORD>(label_bytes.size()), &required) != FALSE;
  const auto label_error = label_ok ? ERROR_SUCCESS : GetLastError();
  const auto label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(label_bytes.data());
  const bool sid_ok = label_ok && label->Label.Sid && IsValidSid(label->Label.Sid) && *GetSidSubAuthorityCount(label->Label.Sid) != 0;
  const auto rid = sid_ok ? *GetSidSubAuthority(label->Label.Sid, *GetSidSubAuthorityCount(label->Label.Sid) - 1) : 0;
  result.add("integrityRid", sid_ok ? std::to_string(rid) : "null").add("integrityError", std::to_string(label_error));
  const bool closed = CloseHandle(token) != FALSE;
  observation_handles_closed = observation_handles_closed && closed;
  return result.add("tokenHandleClosed", boolean(closed)).done();
}
std::string process_info(DWORD pid) {
  const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
  if (!process) return unknown(GetLastError());
  object result;
  FILETIME birth{}, exit{}, kernel{}, user{};
  const bool time_ok = GetProcessTimes(process, &birth, &exit, &kernel, &user) != FALSE;
  const auto time_error = time_ok ? ERROR_SUCCESS : GetLastError();
  std::array<wchar_t, 32768> image{};
  DWORD length = static_cast<DWORD>(image.size());
  const bool image_ok = QueryFullProcessImageNameW(process, 0, image.data(), &length) != FALSE;
  const auto image_error = image_ok ? ERROR_SUCCESS : GetLastError();
  result.add("creationFileTime", time_ok ? filetime_text(birth) : "null").add("birthError", std::to_string(time_error))
      .add("image", image_ok ? quote(utf8(std::wstring(image.data(), length))) : "null")
      .add("imageError", std::to_string(image_error)).add("token", token_info(process));
  const auto wait = WaitForSingleObject(process, 0);
  const auto wait_error = wait == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
  result.add("waitResult", std::to_string(wait)).add("waitError", std::to_string(wait_error));
  const bool closed = CloseHandle(process) != FALSE;
  observation_handles_closed = observation_handles_closed && closed;
  return result.add("processHandleClosed", boolean(closed)).done();
}
std::string window_info(HWND window, clock_type::time_point deadline) {
  object result;
  result.add("hwnd", handle_value(window));
  if (clock_type::now() >= deadline) return result.add("known", "false").add("deadlineReached", "true").done();
  if (!window || !IsWindow(window)) return result.add("known", "false").done();
  DWORD pid{};
  const auto thread = GetWindowThreadProcessId(window, &pid);
  std::array<wchar_t, 256> name{};
  const auto name_length = GetClassNameW(window, name.data(), static_cast<int>(name.size()));
  const auto name_error = name_length ? ERROR_SUCCESS : GetLastError();
  result.add("known", "true").add("className", name_length ? quote(utf8(std::wstring(name.data(), name_length))) : "null")
      .add("classError", std::to_string(name_error)).add("visible", boolean(IsWindowVisible(window) != FALSE))
      .add("parent", handle_value(GetParent(window))).add("root", handle_value(GetAncestor(window, GA_ROOT)))
      .add("rootOwner", handle_value(GetAncestor(window, GA_ROOTOWNER)))
      .add("pid", std::to_string(pid)).add("threadId", std::to_string(thread))
      .add("process", pid ? process_info(pid) : "null").add("desktop", thread_desktop(thread))
      .add("geometry", geometry(window)).add("ancestorsByGaParent", ancestor_chain(window, deadline));
  DWORD after_pid{};
  const auto after_thread = GetWindowThreadProcessId(window, &after_pid);
  return result.add("identityUnchangedDuringQuery", boolean(IsWindow(window) && thread == after_thread && pid == after_pid)).done();
}
std::string gui_thread(DWORD thread, clock_type::time_point deadline, HWND expected_foreground = nullptr) {
  if (clock_type::now() >= deadline) return object{}.add("known", "false").add("deadlineReached", "true").done();
  GUITHREADINFO info{}; info.cbSize = sizeof(info);
  if (!GetGUIThreadInfo(thread, &info)) return unknown(GetLastError());
  const auto expected_root = expected_foreground ? GetAncestor(expected_foreground, GA_ROOT) : nullptr;
  const auto active_root = info.hwndActive ? GetAncestor(info.hwndActive, GA_ROOT) : nullptr;
  const auto focus_root = info.hwndFocus ? GetAncestor(info.hwndFocus, GA_ROOT) : nullptr;
  DWORD focus_pid{}; const auto focus_thread = info.hwndFocus ? GetWindowThreadProcessId(info.hwndFocus, &focus_pid) : 0;
  return object{}.add("known", "true").add("requestedThreadId", std::to_string(thread))
      .add("flags", std::to_string(info.flags)).add("active", handle_value(info.hwndActive))
      .add("activeRoot", handle_value(active_root)).add("focusRoot", handle_value(focus_root))
      .add("expectedForeground", handle_value(expected_foreground)).add("expectedForegroundRoot", handle_value(expected_root))
      .add("activeRootMatchesExpectedForegroundRoot", expected_root && active_root ? boolean(active_root == expected_root) : "null")
      .add("activeMatchesExpectedForeground", expected_foreground ? boolean(info.hwndActive == expected_foreground) : "null")
      .add("focusThreadMatchesRequestedThread", focus_thread ? boolean(focus_thread == thread) : "null")
      .add("focusRootMatchesExpectedForegroundRoot", expected_root && focus_root ? boolean(focus_root == expected_root) : "null")
      .add("focus", handle_value(info.hwndFocus)).add("focusWindow", window_info(info.hwndFocus, deadline))
      .add("capture", handle_value(info.hwndCapture)).add("menuOwner", handle_value(info.hwndMenuOwner)).done();
}
std::string shell_version() {
  std::array<wchar_t, 32768> path{};
  const auto module = GetModuleHandleW(L"shell32.dll");
  const auto length = module ? GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size())) : 0;
  if (!length || length >= path.size()) return unknown(GetLastError());
  object result;
  result.add("loadedPath", quote(utf8(std::wstring(path.data(), length))));
  DWORD ignored{};
  const auto bytes = GetFileVersionInfoSizeW(path.data(), &ignored);
  if (!bytes || bytes > 1024 * 1024) return result.add("version", "null").add("versionError", std::to_string(GetLastError())).done();
  std::vector<unsigned char> buffer(bytes);
  if (!GetFileVersionInfoW(path.data(), 0, bytes, buffer.data()))
    return result.add("version", "null").add("versionError", std::to_string(GetLastError())).done();
  VS_FIXEDFILEINFO* version{}; UINT size{};
  if (!VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<void**>(&version), &size) ||
      size < sizeof(*version) || version->dwSignature != 0xFEEF04BD)
    return result.add("version", "null").done();
  const auto text = std::to_string(HIWORD(version->dwFileVersionMS)) + '.' + std::to_string(LOWORD(version->dwFileVersionMS)) + '.' +
      std::to_string(HIWORD(version->dwFileVersionLS)) + '.' + std::to_string(LOWORD(version->dwFileVersionLS));
  return result.add("version", quote(text)).add("externalFileHashRequired", "true").done();
}

class probe {
 public:
  probe(bool guid_identity, bool observe_keyboard, std::wstring output, clock_type::time_point started)
      : guid_identity_(guid_identity), observe_keyboard_(observe_keyboard), output_(std::move(output)), started_(started) {}
  int run() {
    try {
      open_output();
      if (FAILED(CoCreateGuid(&run_guid_))) throw std::runtime_error("CoCreateGuid failed");
      guid_ = guid_identity_ ? run_guid_ : GUID{};
      emit("run.started", object{}.add("architecture", quote(sizeof(void*) == 8 ? "x64" : "x86"))
          .add("pid", std::to_string(GetCurrentProcessId())).add("process", process_info(GetCurrentProcessId()))
          .add("wholeBudgetMs", std::to_string(whole_ms)).add("output", quote(utf8(output_)))
          .add("observeKeyboard", boolean(observe_keyboard_)).add("experimentMode", quote(observe_keyboard_ ? "keyboard-route-observation" : "api-single-call"))
          .add("notifyIconBytes", std::to_string(sizeof(NOTIFYICONDATAW)))
          .add("notifyIdentifierBytes", std::to_string(sizeof(NOTIFYICONIDENTIFIER)))
          .add("shell32", shell_version()).add("sdkExecuted", "false").add("uiAutomationExecuted", "false"));
      check_deadline();
      instance_ = GetModuleHandleW(nullptr);
      WNDCLASSW wc{}; wc.lpfnWndProc = window_proc; wc.hInstance = instance_; wc.lpszClassName = owner_class;
      if (!RegisterClassW(&wc)) throw std::runtime_error("RegisterClassW failed");
      registered_ = true;
      owner_ = CreateWindowExW(0, owner_class, L"SOUI tray focus diagnostic", WS_OVERLAPPEDWINDOW,
          CW_USEDEFAULT, CW_USEDEFAULT, 360, 200, nullptr, nullptr, instance_, this);
      if (!owner_) throw std::runtime_error("CreateWindowExW failed");
      taskbar_message_ = RegisterWindowMessageW(L"TaskbarCreated");
      if (taskbar_message_ < 0xC000) throw std::runtime_error("TaskbarCreated registration failed");
      icon_ = CopyIcon(LoadIconW(nullptr, IDI_INFORMATION));
      if (!icon_) throw std::runtime_error("CopyIcon failed");
      auto data = identity();
      data.uFlags |= NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
      data.uCallbackMessage = callback_message; data.hIcon = icon_;
      wcscpy_s(data.szTip, guid_identity_ ? L"SOUI focus diagnostic GUID" : L"SOUI focus diagnostic HWND");
      check_deadline(); added_ = notify(NIM_ADD, "tray.add", data);
      if (!added_) throw std::runtime_error("NIM_ADD returned FALSE");
      data = identity(); data.uVersion = NOTIFYICON_VERSION_4;
      check_deadline(); version_set_ = notify(NIM_SETVERSION, "tray.version", data);
      if (!version_set_) throw std::runtime_error("NIM_SETVERSION returned FALSE");
      emit("initial.snapshot", object{}.add("snapshot", snapshot()));
      check_deadline();
      if (!SetTimer(owner_, timer_id, 10, nullptr)) throw std::runtime_error("SetTimer failed");
      emit("interactive.ready", object{}.add("instructions", quote("Right-click this diagnostic icon once; when its menu is visible press Escape once. Do not select an item."))
          .add("externalRealInputEvidenceRequired", "true").add("escapeObservedByProbe", "false"));
      MSG message{};
      int received{};
      while ((received = static_cast<int>(GetMessageW(&message, nullptr, 0, 0))) > 0) {
        if (expired()) { invalidate("whole-deadline"); break; }
        ++dequeue_order_; queued_message_ = message.message; queued_window_ = message.hwnd; queued_dispatch_ = true;
        TranslateMessage(&message); DispatchMessageW(&message); queued_dispatch_ = false;
        if (quit_) break;
      }
      if (received < 0) throw std::runtime_error("GetMessageW failed");
      if (!focus_called_ && !invalid_) invalidate("focus-call-not-observed");
    } catch (const std::exception& error) {
      invalid_ = true;
      safe_emit("run.error", object{}.add("message", quote(error.what())).add("win32Diagnostic", std::to_string(GetLastError())));
      if (log_ == INVALID_HANDLE_VALUE)
        MessageBoxW(nullptr, L"The requested fresh output directory could not be opened. No probe result was recorded. Check --output and retain this invocation failure.",
            L"SOUI tray diagnostic: output error", MB_OK | MB_ICONERROR);
    }
    cleanup();
    cleanup_ok_ = cleanup_ok_ && observation_handles_closed;
    if (expired()) deadline_ = true;
    const int result = invalid_ || deadline_ || !cleanup_ok_ || observation_data_truncated || !focus_called_ || next_observation_ != offsets.size() ||
        (observe_keyboard_ && !keyboard_menu_complete_)
        ? 2 : focus_accepted_ ? 0 : 1;
    safe_emit("run.finished", object{}.add("exitCode", std::to_string(result)).add("focusCalls", std::to_string(focus_calls_))
        .add("focusAccepted", focus_called_ ? boolean(focus_accepted_) : "null").add("observations", std::to_string(next_observation_))
        .add("observeKeyboard", boolean(observe_keyboard_)).add("keyboardCandidateReceived", boolean(keyboard_menu_attempted_))
        .add("keyboardMenuNoSelectionObserved", boolean(keyboard_menu_complete_)).add("keyboardInputSourceQualifiedByProbe", "false")
        .add("diagnosticDataTruncated", boolean(observation_data_truncated))
        .add("invalid", boolean(invalid_)).add("deadlineReached", boolean(deadline_)).add("cleanupConfirmed", boolean(cleanup_ok_))
        .add("actualKeyboardFocusQualified", "false").add("escapeObservedByProbe", "false")
        .add("wholeJobClosureObservedByProbe", "false").add("externalOwnedEvidenceRequired", "true")
        .add("productionQualificationComplete", "false").add("releaseReady", "false"));
    if (log_ != INVALID_HANDLE_VALUE) { if (!CloseHandle(log_)) return 2; log_ = INVALID_HANDLE_VALUE; }
    return log_failed_ ? 2 : result;
  }
 private:
  long long elapsed() const { return std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - started_).count(); }
  bool expired() const { return elapsed() >= whole_ms; }
  void check_deadline() { if (expired()) { deadline_ = true; throw std::runtime_error("Original 60000ms deadline reached"); } }
  void open_output() {
    std::array<wchar_t, 32768> full{}, current{};
    if (output_.size() < 3 || output_[1] != L':' || (output_[2] != L'\\' && output_[2] != L'/'))
      throw std::runtime_error("--output must be an absolute local drive directory");
    const auto count = GetFullPathNameW(output_.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
    const auto cwd_count = GetCurrentDirectoryW(static_cast<DWORD>(current.size()), current.data());
    if (!count || count >= 240 || !cwd_count || cwd_count >= current.size()) throw std::runtime_error("Output path resolution failed or exceeds bounded length");
    output_.assign(full.data(), count);
    std::wstring cwd(current.data(), cwd_count);
    if (!cwd.empty() && cwd.back() != L'\\') cwd += L'\\';
    if (_wcsicmp(output_.c_str(), current.data()) == 0 || _wcsnicmp(output_.c_str(), cwd.c_str(), cwd.size()) == 0)
      throw std::runtime_error("--output must be outside the working directory; never write the launch medium");
    for (auto ancestor = output_; ancestor.size() > 3;) {
      const auto attributes = GetFileAttributesW(ancestor.c_str());
      if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("--output must not traverse a reparse point");
      const auto slash = ancestor.find_last_of(L"\\/");
      if (slash == std::wstring::npos || slash < 3) break;
      ancestor.resize(slash);
    }
    if (!CreateDirectoryW(output_.c_str(), nullptr)) throw std::runtime_error("--output must be fresh and its parent must already exist");
    log_ = CreateFileW((output_ + L"\\events.ndjson").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log_ == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create fresh events.ndjson");
  }
  void emit(const char* name, object fields) {
    if (log_ == INVALID_HANDLE_VALUE || ++records_ > 128) throw std::runtime_error("Diagnostic log unavailable or record bound reached");
    FILETIME utc{}; GetSystemTimeAsFileTime(&utc);
    const auto body = object{}.add("schema", quote("soui.native-tray-focus-diagnostic/v1"))
        .add("runId", quote(guid_text(run_guid_))).add("identityMode", quote(guid_identity_ ? "guid" : "hwnd"))
        .add("sequence", std::to_string(records_)).add("elapsedMs", std::to_string(elapsed())).add("utcFileTime", filetime_text(utc))
        .add("event", quote(name)).add("data", fields.done()).done() + '\n';
    if (body.size() > 65536 || bytes_ + body.size() > 1048576) throw std::runtime_error("Diagnostic byte bound reached");
    DWORD written{};
    if (!WriteFile(log_, body.data(), static_cast<DWORD>(body.size()), &written, nullptr) || written != body.size())
      throw std::runtime_error("Diagnostic log write failed");
    bytes_ += written;
  }
  void safe_emit(const char* name, object fields) noexcept {
    try { emit(name, std::move(fields)); } catch (...) { log_failed_ = true; }
  }
  NOTIFYICONDATAW identity() const {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data); data.hWnd = owner_; data.uID = icon_id;
    if (guid_identity_) { data.uFlags = NIF_GUID; data.guidItem = guid_; }
    return data;
  }
  bool notify(DWORD operation, const char* event, NOTIFYICONDATAW& data) {
    SetLastError(ERROR_SUCCESS);
    const bool accepted = Shell_NotifyIconW(operation, &data) != FALSE;
    const auto error = GetLastError(); // Diagnostic only: Shell_NotifyIcon does not specify this contract.
    if (accepted && operation == NIM_ADD) added_ = true;
    if (accepted && operation == NIM_DELETE) added_ = false;
    emit(event, object{}.add("operation", std::to_string(operation)).add("accepted", boolean(accepted))
        .add("lastErrorDiagnostic", std::to_string(error)).add("extendedErrorContractDocumented", "false")
        .add("cbSize", std::to_string(data.cbSize)).add("hWnd", handle_value(data.hWnd)).add("uID", std::to_string(data.uID))
        .add("uIdIsGuidIdentity", "false").add("uFlags", std::to_string(data.uFlags)).add("guidItem", quote(guid_text(data.guidItem)))
        .add("callbackMessage", std::to_string(data.uCallbackMessage)).add("hIcon", handle_value(data.hIcon))
        .add("versionOrTimeout", std::to_string(data.uVersion)).add("dwState", std::to_string(data.dwState))
        .add("dwStateMask", std::to_string(data.dwStateMask)).add("dwInfoFlags", std::to_string(data.dwInfoFlags))
        .add("hBalloonIcon", handle_value(data.hBalloonIcon)));
    return accepted;
  }
  std::string icon_rect() const {
    NOTIFYICONIDENTIFIER id{}; id.cbSize = sizeof(id); id.hWnd = owner_; id.uID = icon_id; id.guidItem = guid_;
    RECT rect{};
    const auto result = Shell_NotifyIconGetRect(&id, &rect);
    object value;
    value.add("hresult", std::to_string(static_cast<long>(result))).add("known", boolean(result == S_OK))
        .add("hWnd", handle_value(id.hWnd)).add("uID", std::to_string(id.uID)).add("guidItem", quote(guid_text(id.guidItem)));
    if (result == S_OK) value.add("left", std::to_string(rect.left)).add("top", std::to_string(rect.top))
        .add("right", std::to_string(rect.right)).add("bottom", std::to_string(rect.bottom))
        .add("nonempty", boolean(rect.right > rect.left && rect.bottom > rect.top));
    value.add("coordinateSpace", quote("Shell_NotifyIconGetRect screen coordinates; not DPI-normalized by probe"));
    return value.done();
  }
  std::string snapshot() const {
    if (expired()) throw std::runtime_error("Snapshot admission reached original deadline");
    const auto deadline = started_ + std::chrono::milliseconds(whole_ms);
    const auto sample_started = elapsed();
    const auto shell = FindWindowW(L"Shell_TrayWnd", nullptr);
    const auto foreground = GetForegroundWindow();
    const auto before = stamp_window(foreground);
    DWORD pid{}; const auto shell_thread = shell ? GetWindowThreadProcessId(shell, &pid) : 0;
    const auto foreground_thread = foreground ? GetWindowThreadProcessId(foreground, &pid) : 0;
    const auto current_foreground_gui = foreground_thread ? gui_thread(foreground_thread, deadline, foreground) : "null";
    object result;
    result.add("owner", window_info(owner_, deadline)).add("shellTray", window_info(shell, deadline))
        .add("foreground", window_info(foreground, deadline)).add("ownerGuiThread", gui_thread(GetCurrentThreadId(), deadline))
        .add("shellGuiThread", shell_thread ? gui_thread(shell_thread, deadline) : "null")
        .add("foregroundGuiThread", current_foreground_gui);
    if (!expired()) result.add("inputDesktop", input_desktop()).add("iconRect", icon_rect()).add("dpiContext", dpi_context());
    else result.add("additionalQueriesOmittedAtDeadline", "true");
    const auto after = expired() ? window_stamp{} : stamp_window(GetForegroundWindow());
    return result.add("foregroundIdentityBefore", stamp_value(before)).add("foregroundIdentityAfter", stamp_value(after))
        .add("foregroundIdentityUnchangedDuringSnapshot", boolean(same_stamp(before, after)))
        .add("sampleStartedElapsedMs", std::to_string(sample_started)).add("sampleCompletedElapsedMs", std::to_string(elapsed()))
        .add("snapshotAtomic", "false").add("windowReuseWithinProcessNotExcluded", "true")
        .add("notificationToolbarFocusQualifiedByProbe", "false").done();
  }
  void invalidate(const char* reason) {
    if (!invalid_) emit("control.invalid", object{}.add("reason", quote(reason)));
    invalid_ = true; deadline_ = deadline_ || expired();
    if (tracking_) (void)EndMenu();
    quit_ = true; PostQuitMessage(2);
  }
  void show_menu() {
    if (menu_attempted_ || focus_called_) { invalidate("duplicate-menu"); return; }
    menu_attempted_ = true;
    check_deadline();
    emit("menu.before", object{}.add("snapshot", snapshot()).add("sourceVerified", "false"));
    menu_ = CreatePopupMenu();
    if (!menu_ || !AppendMenuW(menu_, MF_STRING | MF_DISABLED, 10, L"SOUI tray focus diagnostic") ||
        !AppendMenuW(menu_, MF_SEPARATOR, 0, nullptr) || !AppendMenuW(menu_, MF_STRING, 1, L"Do not select: press Escape"))
      throw std::runtime_error("Popup menu creation failed");
    if (GetForegroundWindow() != owner_) {
      SetLastError(ERROR_SUCCESS); const bool accepted = SetForegroundWindow(owner_) != FALSE;
      const auto error = GetLastError();
      emit("foreground.attempt", object{}.add("accepted", boolean(accepted)).add("lastErrorDiagnostic", std::to_string(error)));
    }
    if (GetForegroundWindow() != owner_) { invalidate("owner-not-foreground"); return; }
    POINT cursor{}; if (!GetCursorPos(&cursor)) throw std::runtime_error("GetCursorPos failed");
    check_deadline(); tracking_ = true;
    SetLastError(ERROR_SUCCESS);
    const auto command = TrackPopupMenuEx(menu_, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, cursor.x, cursor.y, owner_, nullptr);
    const auto error = GetLastError(); tracking_ = false;
    emit("menu.returned", object{}.add("command", std::to_string(command)).add("lastErrorDiagnostic", std::to_string(error))
        .add("escapeObservedByProbe", "false"));
    if (invalid_ || expired()) { invalidate("deadline-or-abort-after-menu"); return; }
    emit("menu.after", object{}.add("snapshot", snapshot()));
    check_deadline();
    if (command != 0 || error != ERROR_SUCCESS) { invalidate("expected-no-selection"); return; }
    if (!PostMessageW(owner_, WM_NULL, 0, 0)) throw std::runtime_error("Menu WM_NULL could not be posted");
    if (!DestroyMenu(menu_)) throw std::runtime_error("Menu destruction failed");
    menu_ = nullptr;
    if (!PostMessageW(owner_, completion_message, 0, 0)) throw std::runtime_error("Focus completion could not be posted");
    completion_posted_ = true;
  }
  void show_keyboard_menu() {
    if (!observe_keyboard_ || !keyboard_waiting_ || keyboard_menu_attempted_ || !menu_attempted_ || !completion_posted_ ||
        !focus_called_ || focus_calls_ != 1 || next_observation_ != offsets.size() || tracking_ || menu_ || context_depth_ != 1) {
      invalidate("keyboard-second-menu-preconditions"); return;
    }
    check_deadline(); keyboard_menu_attempted_ = true; keyboard_waiting_ = false;
    emit("keyboard.candidate", object{}.add("phase", quote("keyboard-second-menu")).add("v4NotificationId", std::to_string(icon_id))
        .add("notification", std::to_string(WM_CONTEXTMENU)).add("sourceVerified", "false")
        .add("actualKeyboardInputQualifiedByProbe", "false").add("focusCalls", std::to_string(focus_calls_))
        .add("snapshotBeforeAnySecondMenuActivation", snapshot()));
    check_deadline();
    menu_ = CreatePopupMenu();
    if (!menu_ || !AppendMenuW(menu_, MF_STRING | MF_DISABLED, 10, L"SECOND MENU: keyboard-route candidate") ||
        !AppendMenuW(menu_, MF_SEPARATOR, 0, nullptr) || !AppendMenuW(menu_, MF_STRING, 1, L"External screenshot, then Escape"))
      throw std::runtime_error("Keyboard observation popup creation failed");
    // A real second callback is already recorded. This activation serves only
    // the second visible menu; it is never part of the first focus evidence.
    if (GetForegroundWindow() != owner_) {
      SetLastError(ERROR_SUCCESS); const bool accepted = SetForegroundWindow(owner_) != FALSE;
      const auto error = GetLastError();
      emit("foreground.attempt", object{}.add("phase", quote("keyboard-second-menu"))
          .add("accepted", boolean(accepted)).add("lastErrorDiagnostic", std::to_string(error))
          .add("excludedFromFirstFocusEvidence", "true"));
    }
    if (GetForegroundWindow() != owner_) { invalidate("keyboard-second-menu-owner-not-foreground"); return; }
    POINT cursor{}; if (!GetCursorPos(&cursor)) throw std::runtime_error("Keyboard observation cursor query failed");
    check_deadline(); tracking_ = true;
    emit("keyboard.menu-tracking", object{}.add("phase", quote("keyboard-second-menu"))
        .add("displayCoordinatesFromCursorOnly", "true").add("visibleMenuRequiresExternalEvidence", "true"));
    SetLastError(ERROR_SUCCESS);
    const auto command = TrackPopupMenuEx(menu_, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, cursor.x, cursor.y, owner_, nullptr);
    const auto error = GetLastError(); tracking_ = false;
    emit("keyboard.menu-returned", object{}.add("command", std::to_string(command)).add("lastErrorDiagnostic", std::to_string(error))
        .add("escapeObservedByProbe", "false").add("sourceVerified", "false"));
    if (invalid_ || expired()) { invalidate("deadline-or-abort-after-keyboard-menu"); return; }
    if (command != 0 || error != ERROR_SUCCESS) { invalidate("keyboard-menu-expected-no-selection"); return; }
    if (!DestroyMenu(menu_)) throw std::runtime_error("Keyboard observation menu destruction failed");
    menu_ = nullptr; keyboard_menu_complete_ = true;
    emit("keyboard.observation-complete", object{}.add("focusCalls", std::to_string(focus_calls_))
        .add("apiAccepted", boolean(focus_accepted_)).add("sourceVerified", "false"));
    quit_ = true; PostQuitMessage(focus_accepted_ ? 0 : 1);
  }
  void complete_focus() {
    if (!completion_posted_ || focus_called_ || context_depth_ || tracking_ || !null_order_ || null_order_ >= dequeue_order_ ||
        !queued_dispatch_ || queued_window_ != owner_ || queued_message_ != completion_message || InSendMessageEx(nullptr) != ISMEX_NOSEND ||
        !added_ || !version_set_ || GetForegroundWindow() != owner_) { invalidate("completion-preconditions"); return; }
    check_deadline();
    emit("focus.before", object{}.add("snapshot", snapshot()).add("dequeueOrder", std::to_string(dequeue_order_))
        .add("nullOrder", std::to_string(null_order_)).add("callbackDepth", std::to_string(context_depth_))
        .add("inSendMessageEx", std::to_string(InSendMessageEx(nullptr))));
    check_deadline(); focus_called_ = true; ++focus_calls_;
    auto data = identity();
    SetLastError(ERROR_SUCCESS);
    focus_accepted_ = Shell_NotifyIconW(NIM_SETFOCUS, &data) != FALSE;
    const auto error = GetLastError();
    focus_started_ = clock_type::now();
    emit("tray.focus", object{}.add("accepted", boolean(focus_accepted_)).add("calls", std::to_string(focus_calls_))
        .add("returnedElapsedMs", std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(focus_started_ - started_).count()))
        .add("operation", std::to_string(NIM_SETFOCUS)).add("cbSize", std::to_string(data.cbSize))
        .add("hWnd", handle_value(data.hWnd)).add("uID", std::to_string(data.uID)).add("uFlags", std::to_string(data.uFlags))
        .add("guidItem", quote(guid_text(data.guidItem))).add("versionOrTimeout", std::to_string(data.uVersion))
        .add("callbackMessage", std::to_string(data.uCallbackMessage)).add("hIcon", handle_value(data.hIcon))
        .add("dwState", std::to_string(data.dwState)).add("dwStateMask", std::to_string(data.dwStateMask))
        .add("dwInfoFlags", std::to_string(data.dwInfoFlags)).add("hBalloonIcon", handle_value(data.hBalloonIcon))
        .add("lastErrorDiagnostic", std::to_string(error)).add("extendedErrorContractDocumented", "false"));
    observe_due();
  }
  void observe_due() {
    while (focus_called_ && next_observation_ < offsets.size()) {
      check_deadline();
      const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - focus_started_).count();
      if (age < offsets[next_observation_]) return;
      const auto observed = snapshot();
      const auto end_age = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - focus_started_).count();
      emit("focus.observation", object{}.add("scheduledOffsetMs", std::to_string(offsets[next_observation_]))
          .add("actualOffsetMs", std::to_string(age)).add("latenessMs", std::to_string(age - offsets[next_observation_]))
          .add("observationFinishedOffsetMs", std::to_string(end_age)).add("snapshotAtomic", "false")
          .add("snapshot", observed).add("apiAccepted", boolean(focus_accepted_))
          .add("actualKeyboardFocusQualified", "false"));
      ++next_observation_;
    }
    if (focus_called_ && next_observation_ == offsets.size()) {
      if (observe_keyboard_) {
        if (!keyboard_waiting_ && !keyboard_menu_attempted_) {
          check_deadline(); keyboard_waiting_ = true;
          emit("keyboard.ready", object{}.add("instructions", quote("Without Win+B, Tab, mouse repositioning or a click, press Shift+F10 once; retain the second visible menu, then press Escape."))
              .add("sourceVerified", "false").add("remainingWholeBudgetMs", std::to_string(whole_ms - elapsed()))
              .add("focusCalls", std::to_string(focus_calls_)).add("apiAccepted", boolean(focus_accepted_)));
        }
      } else { quit_ = true; PostQuitMessage(focus_accepted_ ? 0 : 1); }
    }
  }
  LRESULT dispatch(HWND window, UINT message, WPARAM first, LPARAM second) {
    if (message == WM_CLOSE) { invalidate("owner-close"); return 0; }
    if (taskbar_message_ && message == taskbar_message_) { invalidate("unexpected-taskbar-restart"); return 0; }
    if (message == WM_TIMER && first == timer_id) {
      if (expired()) { invalidate("whole-deadline"); return 0; }
      if (!tracking_) observe_due();
      return 0;
    }
    if (message == WM_NULL && completion_posted_) {
      if (context_depth_ || !queued_dispatch_ || queued_window_ != owner_ || queued_message_ != WM_NULL ||
          InSendMessageEx(nullptr) != ISMEX_NOSEND || null_order_) { invalidate("null-preconditions"); return 0; }
      null_order_ = dequeue_order_; return 0;
    }
    if (message == completion_message) { complete_focus(); return 0; }
    if (message == callback_message && added_ && version_set_) {
      const auto packed = static_cast<std::uint32_t>(second);
      if ((packed >> 16) == icon_id && (packed & 0xffff) == WM_CONTEXTMENU) {
        ++context_depth_;
        try { emit("tray.context-menu", object{}.add("notification", std::to_string(packed & 0xffff))
            .add("v4NotificationId", std::to_string(packed >> 16)).add("sourceVerified", "false")
            .add("coordinatesDefined", "false").add("inSendMessageEx", std::to_string(InSendMessageEx(nullptr)))
            .add("phase", quote(menu_attempted_ ? "keyboard-second-menu-candidate" : "first-menu")));
          if (observe_keyboard_ && menu_attempted_) show_keyboard_menu(); else show_menu(); }
        catch (...) { --context_depth_; throw; }
        --context_depth_; return 0;
      }
    }
    return DefWindowProcW(window, message, first, second);
  }
  static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM first, LPARAM second) noexcept {
    auto* self = reinterpret_cast<probe*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<probe*>(reinterpret_cast<CREATESTRUCTW*>(second)->lpCreateParams);
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, first, second);
    try { return self->dispatch(window, message, first, second); }
    catch (const std::exception& error) {
      self->safe_emit("dispatch.error", object{}.add("message", quote(error.what())));
      self->invalid_ = true; self->quit_ = true; if (self->tracking_) (void)EndMenu(); PostQuitMessage(2); return 0;
    } catch (...) { self->invalid_ = true; self->quit_ = true; if (self->tracking_) (void)EndMenu(); PostQuitMessage(2); return 0; }
  }
  void cleanup() noexcept {
    if (owner_) (void)KillTimer(owner_, timer_id);
    if (menu_) { const bool ok = DestroyMenu(menu_) != FALSE; cleanup_ok_ = cleanup_ok_ && ok;
      safe_emit("menu.cleanup", object{}.add("accepted", boolean(ok))); if (ok) menu_ = nullptr; }
    if (added_) {
      auto data = identity();
      try { const bool ok = notify(NIM_DELETE, "tray.delete", data); cleanup_ok_ = cleanup_ok_ && ok; }
      catch (...) { log_failed_ = true; cleanup_ok_ = false; }
    }
    if (icon_) { const bool ok = DestroyIcon(icon_) != FALSE; cleanup_ok_ = cleanup_ok_ && ok;
      safe_emit("icon.cleanup", object{}.add("accepted", boolean(ok))); icon_ = nullptr; }
    if (owner_) { const bool ok = DestroyWindow(owner_) != FALSE; cleanup_ok_ = cleanup_ok_ && ok;
      safe_emit("owner.cleanup", object{}.add("accepted", boolean(ok))); owner_ = nullptr; }
    if (registered_) { const bool ok = UnregisterClassW(owner_class, instance_) != FALSE; cleanup_ok_ = cleanup_ok_ && ok;
      safe_emit("class.cleanup", object{}.add("accepted", boolean(ok))); registered_ = false; }
  }
  bool guid_identity_{}, observe_keyboard_{}, keyboard_waiting_{}, keyboard_menu_attempted_{}, keyboard_menu_complete_{};
  std::wstring output_;
  clock_type::time_point started_, focus_started_{};
  GUID run_guid_{}, guid_{};
  HANDLE log_{INVALID_HANDLE_VALUE};
  HINSTANCE instance_{};
  HWND owner_{}; HICON icon_{}; HMENU menu_{};
  UINT taskbar_message_{}, queued_message_{}; HWND queued_window_{};
  bool registered_{}, added_{}, version_set_{}, menu_attempted_{}, tracking_{}, completion_posted_{}, queued_dispatch_{};
  bool focus_called_{}, focus_accepted_{}, invalid_{}, deadline_{}, quit_{}, log_failed_{}, cleanup_ok_{true};
  unsigned context_depth_{}, focus_calls_{}, records_{};
  std::uint64_t dequeue_order_{}, null_order_{};
  std::size_t next_observation_{}, bytes_{};
};
constexpr wchar_t usage[] = L"SouiTrayFocusDiagnostic --identity hwnd|guid --output C:\\fresh\\guest-directory [--observe-keyboard]\n"
    L"Only the named GUI operator may launch in the task VM. Output must be a fresh absolute directory outside the working directory.\n"
    L"One real tray right-click/Escape, one NIM_SETFOCUS, fixed60000ms. No synthetic input, SDK or UIA.\n"
    L"--observe-keyboard retains the icon after five snapshots for one real Shift+F10 / second visible menu / Escape. BOOL FALSE still exits1.\n";
}  // namespace

int wmain(int argc, wchar_t** argv) {
  const auto started = clock_type::now();
  if (argc == 2 && std::wcscmp(argv[1], L"--help") == 0) {
    std::fputws(usage, stdout);
    MessageBoxW(nullptr, usage, L"SOUI tray diagnostic: help", MB_OK | MB_ICONINFORMATION);
    return 0;
  }
  std::wstring mode, output;
  bool observe_keyboard{};
  bool valid = argc == 5 || argc == 6;
  for (int index = 1; valid && index < argc; ++index) {
    const std::wstring option = argv[index];
    if (option == L"--observe-keyboard" && !observe_keyboard) observe_keyboard = true;
    else if (option == L"--identity" && mode.empty() && index + 1 < argc) mode = argv[++index];
    else if (option == L"--output" && output.empty() && index + 1 < argc) output = argv[++index];
    else valid = false;
  }
  if (!valid || (mode != L"hwnd" && mode != L"guid") || output.empty()) {
    std::fputws(L"Invalid arguments; no diagnostic started.\n", stderr); std::fputws(usage, stderr);
    MessageBoxW(nullptr, usage, L"SOUI tray diagnostic: invalid arguments", MB_OK | MB_ICONERROR);
    return 64;
  }
  probe diagnostic(mode == L"guid", observe_keyboard, std::move(output), started);
  return diagnostic.run();
}
