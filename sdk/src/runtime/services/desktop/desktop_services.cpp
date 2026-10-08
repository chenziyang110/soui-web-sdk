#include <soui/runtime/services/desktop/desktop_services.h>
#include <soui/runtime/services/desktop/window_commands.h>

#include "unicode_conversion.h"

#include <cstring>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
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

#ifdef _WIN32
bool owned_window(void* window) {
  const auto hwnd = static_cast<HWND>(window);
  DWORD process_id = 0;
  return hwnd != nullptr && IsWindow(hwnd) &&
         GetWindowThreadProcessId(hwnd, &process_id) != 0 &&
         process_id == GetCurrentProcessId();
}

struct pending_title {
  HWND window{nullptr};
  std::wstring title;
  std::mutex mutex;
  std::condition_variable finished;
  bool completed{false};
  bool cancelled{false};
  bool applied{false};
};

struct title_dispatcher {
  std::mutex mutex;
  std::unordered_map<std::uintptr_t, std::shared_ptr<pending_title>> pending;
  std::uintptr_t next_id{1};
};

title_dispatcher& titles() {
  static title_dispatcher dispatcher;
  return dispatcher;
}

void remove_pending_title(std::uintptr_t id) {
  auto& dispatcher = titles();
  std::lock_guard<std::mutex> lock(dispatcher.mutex);
  dispatcher.pending.erase(id);
}
#endif

}  // namespace

#ifdef _WIN32
bool execute_pending_title_command(HWND window, std::uintptr_t request_id) {
  if (!IsWindow(window) || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId())
    return false;
  std::shared_ptr<pending_title> request;
  {
    auto& dispatcher = titles();
    std::lock_guard<std::mutex> lock(dispatcher.mutex);
    const auto found = dispatcher.pending.find(request_id);
    if (found == dispatcher.pending.end() || found->second->window != window) return false;
    request = std::move(found->second);
    dispatcher.pending.erase(found);
  }
  {
    std::lock_guard<std::mutex> lock(request->mutex);
    if (request->cancelled) return false;
  }
  const bool applied = SetWindowTextW(window, request->title.c_str()) != FALSE;
  {
    std::lock_guard<std::mutex> lock(request->mutex);
    request->applied = applied;
    request->completed = true;
  }
  request->finished.notify_one();
  return applied;
}
#endif

desktop_response directory_picker::open(void* owner, const desktop_request_context& context,
                                        const std::string& initial_directory) const {
  const auto capability = require_capability(permissions_, context, "desktop.dialog.pickDirectory");
  if (!capability.ok()) return capability;
  const auto gesture = require_gesture(context);
  if (!gesture.ok()) return gesture;
#ifdef _WIN32
  (void)initial_directory;
  if (!owned_window(owner))
    return denied("E_INVALID_ARGUMENT", "A live application window is required for the directory picker.");
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(com))
    return denied("E_THREAD_APARTMENT", "The directory picker requires an STA thread.");
  struct com_scope final { ~com_scope() { CoUninitialize(); } } apartment;
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
  if (title.size() > 512U)
    return denied("E_INVALID_ARGUMENT", "The window title exceeds the 512-byte limit.");
  const auto wide_title = detail::utf8_to_wide(title);
  if (!wide_title) return denied("E_INVALID_ARGUMENT", "The window title must be valid UTF-8 text.");
  if (!owned_window(window))
    return denied("E_INVALID_ARGUMENT", "A live application window is required.");
  const auto hwnd = static_cast<HWND>(window);
  if (GetWindowThreadProcessId(hwnd, nullptr) == GetCurrentThreadId()) {
    if (!SetWindowTextW(hwnd, wide_title->c_str()))
      return denied("E_NATIVE_FAILURE", "The native window title could not be updated.");
    return {result_kind::success, {}, {}, {}};
  }

  auto request = std::make_shared<pending_title>();
  request->window = hwnd;
  request->title = *wide_title;
  std::uintptr_t id = 0;
  {
    auto& dispatcher = titles();
    std::lock_guard<std::mutex> lock(dispatcher.mutex);
    if (dispatcher.pending.size() >= 64U)
      return denied("E_LIMIT_PENDING", "Too many window title updates are pending.");
    do {
      id = dispatcher.next_id++;
      if (dispatcher.next_id == 0) dispatcher.next_id = 1;
    } while (id == 0 || dispatcher.pending.find(id) != dispatcher.pending.end());
    dispatcher.pending.emplace(id, request);
  }
  if (!PostMessageW(hwnd, window_command_message, static_cast<WPARAM>(window_command::set_title),
                    static_cast<LPARAM>(id))) {
    remove_pending_title(id);
    return denied("E_NATIVE_FAILURE", "The native window title update was not accepted.");
  }
  std::unique_lock<std::mutex> lock(request->mutex);
  if (!request->finished.wait_for(lock, std::chrono::milliseconds(250),
                                  [&request] { return request->completed; })) {
    request->cancelled = true;
    lock.unlock();
    remove_pending_title(id);
    return denied("E_WINDOW_TIMEOUT", "The native window did not confirm the title update in time.");
  }
  if (!request->applied)
    return denied("E_NATIVE_FAILURE", "The native window title could not be updated.");
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
  if (!owned_window(window)) return denied("E_INVALID_ARGUMENT", "A live application window is required.");
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
  if (!owned_window(window)) return denied("E_INVALID_ARGUMENT", "A live application window is required.");
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
  if (!owned_window(window)) return denied("E_INVALID_ARGUMENT", "A live application window is required.");
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
  if (!owned_window(window)) return denied("E_INVALID_ARGUMENT", "A live application window is required.");
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
  if (!owned_window(window))
    return denied("E_INVALID_ARGUMENT", "A live application window is required.");
  if (!SetForegroundWindow(static_cast<HWND>(window))) {
    return denied("E_NATIVE_FAILURE", "The native window could not be focused.");
  }
  return {result_kind::success, {}, {}, {}};
#else
  (void)window;
  return denied("E_UNSUPPORTED", "Window services are available on Windows hosts only.");
#endif
}

desktop_response clipboard_service::write_text(void* owner, const desktop_request_context& context,
                                               const std::string& text) const {
  const auto capability = require_capability(permissions_, context, "desktop.clipboard.writeText");
  if (!capability.ok()) return capability;
  const auto gesture = require_gesture(context);
  if (!gesture.ok()) return gesture;
#ifdef _WIN32
  const auto wide_text = detail::utf8_to_wide(text);
  if (!wide_text) return denied("E_INVALID_ARGUMENT", "Clipboard text must be valid UTF-8 text.");
  const auto window = static_cast<HWND>(owner);
  if (!owned_window(owner))
    return denied("E_INVALID_ARGUMENT", "A live application window is required for clipboard ownership.");
  const SIZE_T bytes = (wide_text->size() + 1U) * sizeof(wchar_t);
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (memory == nullptr) {
    return denied("E_NATIVE_FAILURE", "Clipboard memory could not be allocated.");
  }
  void* target = GlobalLock(memory);
  if (target == nullptr) {
    GlobalFree(memory);
    return denied("E_NATIVE_FAILURE", "Clipboard memory could not be locked.");
  }
  std::memcpy(target, wide_text->c_str(), bytes);
  GlobalUnlock(memory);
  if (!OpenClipboard(window)) {
    GlobalFree(memory);
    return denied("E_NATIVE_FAILURE", "The clipboard is busy.");
  }
  if (!EmptyClipboard() || SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
    GlobalFree(memory);
    CloseClipboard();
    return denied("E_NATIVE_FAILURE", "Clipboard data could not be committed.");
  }
  CloseClipboard();
  return {result_kind::success, {}, {}, {}};
#else
  (void)owner;
  (void)text;
  return denied("E_UNSUPPORTED", "Clipboard services are available on Windows hosts only.");
#endif
}

}  // namespace soui::runtime
