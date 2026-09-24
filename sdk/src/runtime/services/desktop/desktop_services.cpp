#include <soui/runtime/services/desktop/desktop_services.h>
#include <soui/runtime/services/desktop/window_commands.h>

#include "unicode_conversion.h"

#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace soui::runtime {
namespace {

desktop_response denied(const char* code, const char* message) {
  return {result_kind::failure, code, message, {}};
}

bool authorized(const permission_store& permissions, const desktop_request_context& context,
                const char* capability) {
  return permissions.authorize(context.session_id, context.origin, context.token, capability);
}

desktop_response require_capability(const permission_store& permissions,
                                    const desktop_request_context& context,
                                    const char* capability) {
  if (!authorized(permissions, context, capability)) {
    return denied("E_PERMISSION_DENIED", "The current session does not hold this capability.");
  }
  return {result_kind::success, {}, {}, {}};
}

desktop_response require_gesture(const desktop_request_context& context) {
  if (!context.user_gesture) {
    return denied("E_GESTURE_REQUIRED", "This desktop operation requires a user gesture.");
  }
  return {result_kind::success, {}, {}, {}};
}

}  // namespace

desktop_response directory_picker::open(void* owner, const desktop_request_context& context,
                                        const std::string& initial_directory) const {
  const auto capability = require_capability(permissions_, context, "desktop.dialog.pickDirectory");
  if (!capability.ok()) return capability;
  const auto gesture = require_gesture(context);
  if (!gesture.ok()) return gesture;
#ifdef _WIN32
  (void)initial_directory;
  BROWSEINFOW browse{};
  browse.hwndOwner = static_cast<HWND>(owner);
  browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  const std::wstring title = L"Select a folder";
  browse.lpszTitle = title.c_str();
  PIDLIST_ABSOLUTE selected = SHBrowseForFolderW(&browse);
  if (selected == nullptr) {
    return {result_kind::user_cancelled, "E_USER_CANCELLED", "The user closed the folder picker.", {}};
  }
  wchar_t path[MAX_PATH]{};
  const bool resolved = SHGetPathFromIDListW(selected, path) != FALSE;
  CoTaskMemFree(selected);
  if (!resolved) return denied("E_NATIVE_FAILURE", "The selected folder could not be resolved.");
  const auto utf8_path = detail::wide_to_utf8(path);
  if (!utf8_path) return denied("E_NATIVE_FAILURE", "The selected folder path is not valid Unicode.");
  return {result_kind::success, {}, {}, *utf8_path};
#else
  (void)owner;
  (void)initial_directory;
  return denied("E_UNSUPPORTED", "The native directory picker is available on Windows hosts only.");
#endif
}

desktop_response window_service::set_title(void* window, const desktop_request_context& context,
                                           const std::string& title) const {
  const auto capability = require_capability(permissions_, context, "desktop.window.setTitle");
  if (!capability.ok()) return capability;
#ifdef _WIN32
  if (window == nullptr) return denied("E_INVALID_ARGUMENT", "A window handle is required.");
  const auto wide_title = detail::utf8_to_wide(title);
  if (!wide_title) return denied("E_INVALID_ARGUMENT", "The window title must be valid UTF-8 text.");
  if (!SetWindowTextW(static_cast<HWND>(window), wide_title->c_str())) {
    return denied("E_NATIVE_FAILURE", "The native window title could not be updated.");
  }
  return {result_kind::success, {}, {}, {}};
#else
  (void)window;
  (void)title;
  return denied("E_UNSUPPORTED", "Window services are available on Windows hosts only.");
#endif
}

desktop_response window_service::begin_drag(void* window, const desktop_request_context& context) const {
  const auto capability = require_capability(permissions_, context, "desktop.window.beginDrag");
  if (!capability.ok()) return capability;
  const auto gesture = require_gesture(context);
  if (!gesture.ok()) return gesture;
#ifdef _WIN32
  const auto hwnd = static_cast<HWND>(window);
  if (hwnd == nullptr || !IsWindow(hwnd)) return denied("E_INVALID_ARGUMENT", "A valid window handle is required.");
  if (!PostMessageW(hwnd, window_command_message,
                    static_cast<WPARAM>(window_command::begin_drag), 0))
    return denied("E_NATIVE_FAILURE", "The native window drag was not accepted.");
  return {result_kind::success, {}, {}, {}};
#else
  (void)window;
  return denied("E_UNSUPPORTED", "Window services are available on Windows hosts only.");
#endif
}

desktop_response window_service::minimize(void* window, const desktop_request_context& context) const {
  const auto capability = require_capability(permissions_, context, "desktop.window.minimize");
  if (!capability.ok()) return capability;
  const auto gesture = require_gesture(context);
  if (!gesture.ok()) return gesture;
#ifdef _WIN32
  const auto hwnd = static_cast<HWND>(window);
  if (hwnd == nullptr || !IsWindow(hwnd)) return denied("E_INVALID_ARGUMENT", "A valid window handle is required.");
  DWORD_PTR applied = 0;
  if (!SendMessageTimeoutW(hwnd, window_command_message,
                          static_cast<WPARAM>(window_command::minimize), 0,
                          SMTO_ABORTIFHUNG | SMTO_BLOCK, 250, &applied) || !applied)
    return denied("E_NATIVE_FAILURE", "The native window minimize was not accepted.");
  return {result_kind::success, {}, {}, R"({"minimized":true})"};
#else
  (void)window;
  return denied("E_UNSUPPORTED", "Window services are available on Windows hosts only.");
#endif
}

desktop_response window_service::toggle_maximize(void* window, const desktop_request_context& context) const {
  const auto capability = require_capability(permissions_, context, "desktop.window.toggleMaximize");
  if (!capability.ok()) return capability;
  const auto gesture = require_gesture(context);
  if (!gesture.ok()) return gesture;
#ifdef _WIN32
  const auto hwnd = static_cast<HWND>(window);
  if (hwnd == nullptr || !IsWindow(hwnd)) return denied("E_INVALID_ARGUMENT", "A valid window handle is required.");
  DWORD_PTR applied = 0;
  if (!SendMessageTimeoutW(hwnd, window_command_message,
                          static_cast<WPARAM>(window_command::toggle_maximize), 0,
                          SMTO_ABORTIFHUNG | SMTO_BLOCK, 250, &applied) || !applied)
    return denied("E_NATIVE_FAILURE", "The native window maximize was not accepted.");
  return {result_kind::success, {}, {}, std::string{"{\"maximized\":"} + (IsZoomed(hwnd) ? "true}" : "false}")};
#else
  (void)window;
  return denied("E_UNSUPPORTED", "Window services are available on Windows hosts only.");
#endif
}

desktop_response window_service::close(void* window, const desktop_request_context& context) const {
  const auto capability = require_capability(permissions_, context, "desktop.window.close");
  if (!capability.ok()) return capability;
  const auto gesture = require_gesture(context);
  if (!gesture.ok()) return gesture;
#ifdef _WIN32
  const auto hwnd = static_cast<HWND>(window);
  if (hwnd == nullptr || !IsWindow(hwnd)) return denied("E_INVALID_ARGUMENT", "A valid window handle is required.");
  if (!PostMessageW(hwnd, WM_CLOSE, 0, 0)) return denied("E_NATIVE_FAILURE", "The native window close was not accepted.");
  return {result_kind::success, {}, {}, R"({"accepted":true})"};
#else
  (void)window;
  return denied("E_UNSUPPORTED", "Window services are available on Windows hosts only.");
#endif
}

desktop_response window_service::set_foreground(void* window,
                                                 const desktop_request_context& context) const {
  const auto capability = require_capability(permissions_, context, "window.control");
  if (!capability.ok()) return capability;
#ifdef _WIN32
  if (window == nullptr) return denied("E_INVALID_ARGUMENT", "A window handle is required.");
  if (!SetForegroundWindow(static_cast<HWND>(window))) {
    return denied("E_NATIVE_FAILURE", "The native window could not be focused.");
  }
  return {result_kind::success, {}, {}, {}};
#else
  (void)window;
  return denied("E_UNSUPPORTED", "Window services are available on Windows hosts only.");
#endif
}

desktop_response clipboard_service::write_text(const desktop_request_context& context,
                                               const std::string& text) const {
  const auto capability = require_capability(permissions_, context, "desktop.clipboard.writeText");
  if (!capability.ok()) return capability;
  const auto gesture = require_gesture(context);
  if (!gesture.ok()) return gesture;
#ifdef _WIN32
  const auto wide_text = detail::utf8_to_wide(text);
  if (!wide_text) return denied("E_INVALID_ARGUMENT", "Clipboard text must be valid UTF-8 text.");
  if (!OpenClipboard(nullptr)) return denied("E_NATIVE_FAILURE", "The clipboard is busy.");
  const SIZE_T bytes = (wide_text->size() + 1U) * sizeof(wchar_t);
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (memory == nullptr) {
    CloseClipboard();
    return denied("E_NATIVE_FAILURE", "Clipboard memory could not be allocated.");
  }
  void* target = GlobalLock(memory);
  if (target == nullptr) {
    GlobalFree(memory);
    CloseClipboard();
    return denied("E_NATIVE_FAILURE", "Clipboard memory could not be locked.");
  }
  std::memcpy(target, wide_text->c_str(), bytes);
  GlobalUnlock(memory);
  if (!EmptyClipboard() || SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
    GlobalFree(memory);
    CloseClipboard();
    return denied("E_NATIVE_FAILURE", "Clipboard data could not be committed.");
  }
  CloseClipboard();
  return {result_kind::success, {}, {}, {}};
#else
  (void)text;
  return denied("E_UNSUPPORTED", "Clipboard services are available on Windows hosts only.");
#endif
}

}  // namespace soui::runtime
