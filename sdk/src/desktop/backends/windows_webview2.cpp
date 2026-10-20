#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl.h>
#include <WebView2.h>

#include "../backend.h"
#include "../resource_uri.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <utility>

namespace soui::desktop::detail {
namespace {

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

constexpr wchar_t window_class[] = L"Soui.UnifiedDesktop.WebView2.v1";
constexpr UINT dispatch_message = WM_APP + 0x491;
constexpr UINT failure_message = WM_APP + 0x492;
constexpr UINT tray_message = WM_APP + 0x493;
constexpr UINT tray_icon_id = 1;
constexpr std::size_t max_dispatch_queue = 1024;
constexpr std::size_t max_message_characters = 65536;
constexpr std::size_t max_clipboard_bytes = 1024 * 1024;
// EndMenu operates on the calling thread. A nested popup belonging to another
// window must not steal ownership of the native menu currently being tracked.
thread_local HWND active_menu_owner = nullptr;

result hresult_failure(const char* operation, HRESULT code) {
  char number[16]{};
  std::snprintf(number, sizeof(number), "0x%08lX", static_cast<unsigned long>(code));
  return result::failure("E_WINDOWS", std::string(operation) + " failed (" + number + ")");
}

// Loader discovery and asynchronous environment creation can both report a
// missing runtime. Never expose browser/profile paths or environment overrides.
result webview_startup_failure(const char* operation, HRESULT code) {
  auto failure = hresult_failure(operation, code);
  if (code == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
    failure.code = "E_WEBVIEW2_RUNTIME";
    failure.message += ". No compatible WebView2 runtime was found. Install or repair the "
      "official Microsoft Edge WebView2 Runtime for this application architecture, then retry.";
  } else {
    failure.message += ". Check the WebView2 Runtime installation, application architecture "
      "and user-data-directory permissions. Run the staged runtime doctor for deployment guidance.";
  }
  return failure;
}

result win32_failure(const char* operation) {
  const DWORD error = GetLastError();
  return hresult_failure(operation, HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE));
}

result closed_error() {
  return result::failure("E_CLOSED", "The desktop window is closed or unavailable");
}

bool to_wide(std::string_view input, std::wstring& output) {
  if (input.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    return false;
  if (input.empty()) {
    output.clear();
    return true;
  }
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                                       static_cast<int>(input.size()), nullptr, 0);
  if (!length) return false;
  output.resize(static_cast<std::size_t>(length));
  return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                            static_cast<int>(input.size()), output.data(), length) == length;
}

bool to_utf8(std::wstring_view input, std::string& output) {
  if (input.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    return false;
  if (input.empty()) {
    output.clear();
    return true;
  }
  const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(),
                                       static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
  if (!length) return false;
  output.resize(static_cast<std::size_t>(length));
  return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(),
                            static_cast<int>(input.size()), output.data(), length,
                            nullptr, nullptr) == length;
}

bool bounded_utf8(const wchar_t* text, std::size_t limit, std::string& output) {
  if (!text) return false;
  std::size_t length = 0;
  while (length <= limit && text[length]) ++length;
  return length <= limit && to_utf8(std::wstring_view(text, length), output);
}

struct com_string {
  LPWSTR value{nullptr};
  ~com_string() { CoTaskMemFree(value); }
  com_string() = default;
  com_string(const com_string&) = delete;
  com_string& operator=(const com_string&) = delete;
};

struct clipboard_lock {
  explicit clipboard_lock(HWND owner) : opened(OpenClipboard(owner) != FALSE) {}
  ~clipboard_lock() { if (opened) CloseClipboard(); }
  bool opened;
};

struct global_lock {
  explicit global_lock(HGLOBAL value) : handle(value), data(GlobalLock(value)) {}
  ~global_lock() { if (data) GlobalUnlock(handle); }
  HGLOBAL handle;
  void* data;
};

struct popup_handle {
  HMENU value{CreatePopupMenu()};
  ~popup_handle() { if (value) DestroyMenu(value); }
  popup_handle() = default;
  popup_handle(const popup_handle&) = delete;
  popup_handle& operator=(const popup_handle&) = delete;
};

struct state : std::enable_shared_from_this<state> {
  HWND window{nullptr};
  bool closed{false};
  bool created{false};
  bool dispatching{false};
  bool visible{true};
  bool enable_resource_server{false};
  bool installing_script{false};
  bool has_load{false};
  bool navigation_ticket{false};
  std::uint64_t generation{0};
  std::uint64_t installed_generation{0};
  UINT64 navigation_id{0};
  std::string target_uri;
  std::wstring target_preload;
  std::wstring script_id;
  backend_events events;
  result terminal_status{result::success()};
  ComPtr<ICoreWebView2Environment> environment;
  ComPtr<ICoreWebView2Controller> controller;
  ComPtr<ICoreWebView2> webview;
  ComPtr<ICoreWebView2_18> secured_webview;
  ComPtr<IFileOpenDialog> directory_dialog;
  UINT taskbar_created_message{0};
  bool tray_desired{false};
  bool tray_present{false};
  bool tray_ready{false};
  std::uint64_t tray_revision{0};
  std::uint64_t popup_revision{0};
  std::wstring tray_tooltip;
  std::vector<menu_item> tray_items;

  // Workers touch only these fields under queue_mutex. Posted messages carry no
  // pointer/ownership payload, so DestroyWindow cannot strand allocated work.
  std::mutex queue_mutex;
  HWND dispatch_window{nullptr};
  bool accepting_work{false};
  bool dispatch_posted{false};
  std::deque<std::function<void()>> queue;

  bool active() const { return created && !closed && terminal_status.ok; }

  NOTIFYICONDATAW tray_identity() const {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    // HWND + uID is the Shell identity; equal IDs on different windows do not
    // share icons, command handlers, or mutable menu configuration.
    data.uID = tray_icon_id;
    return data;
  }

  result remove_tray() {
    tray_desired = false;
    tray_ready = false;
    ++tray_revision;
    tray_items.clear();
    tray_tooltip.clear();
    if (!tray_present) return result::success();
    auto data = tray_identity();
    if (!Shell_NotifyIconW(NIM_DELETE, &data))
      return result::failure("E_TRAY_REMOVE", "Windows Shell did not confirm tray icon removal");
    tray_present = false;
    return result::success();
  }

  result publish_tray(const std::wstring& tooltip) {
    auto data = tray_identity();
    // A failed version negotiation may have left an unconfigured icon.
    if (tray_present && !tray_ready) {
      if (!Shell_NotifyIconW(NIM_DELETE, &data))
        return result::failure("E_TRAY_CLEANUP", "Windows Shell refused stale tray cleanup");
      tray_present = false;
    }
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = tray_message;
    // This is a shared system icon. It is never destroyed by an individual
    // backend, and no icon file/parser or SOUI legacy dependency is required.
    data.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    if (!data.hIcon) return win32_failure("LoadIconW");
    if (tooltip.size() >= sizeof(data.szTip) / sizeof(data.szTip[0]))
      return result::failure("E_TRAY_TOOLTIP", "Windows tray tooltips allow 127 UTF-16 units");
    std::copy(tooltip.begin(), tooltip.end(), data.szTip);
    if (tray_present) {
      if (!Shell_NotifyIconW(NIM_MODIFY, &data))
        return result::failure("E_TRAY_UPDATE", "Windows Shell did not update the tray icon");
      return result::success();
    }
    if (!Shell_NotifyIconW(NIM_ADD, &data))
      return result::failure("E_TRAY_UNAVAILABLE",
                             "Windows Shell notification area is unavailable or rejected the icon");
    tray_present = true;
    data.uVersion = NOTIFYICON_VERSION_4;
    if (!Shell_NotifyIconW(NIM_SETVERSION, &data)) {
      tray_present = !Shell_NotifyIconW(NIM_DELETE, &data);
      return result::failure(tray_present ? "E_TRAY_CLEANUP" : "E_TRAY_VERSION",
          tray_present ? "Tray version negotiation and rollback both failed" :
                         "Windows Shell does not support the required tray event format");
    }
    tray_ready = true;
    return result::success();
  }

  result update_tray(const tray_options& settings) {
    if (!active()) return closed_error();
    std::wstring tooltip;
    if (settings.tooltip.find('\0') != std::string::npos ||
        !to_wide(settings.tooltip, tooltip) || tooltip.size() > 127)
      return result::failure("E_TRAY_TOOLTIP",
                             "Windows tray tooltip must be UTF-8 and at most 127 UTF-16 units");
    // Copy before calling the Shell so allocation failure cannot leave native
    // and stored command configurations describing different successful updates.
    auto items = settings.menu;
    if (!taskbar_created_message) {
      taskbar_created_message = RegisterWindowMessageW(L"TaskbarCreated");
      if (!taskbar_created_message) return win32_failure("RegisterWindowMessageW");
    }
    const auto published = publish_tray(tooltip);
    if (!published.ok) return published;
    tray_tooltip = std::move(tooltip);
    tray_items = std::move(items);
    tray_desired = true;
    ++tray_revision;
    return result::success();
  }

  void restore_tray() {
    // Explorer discards notification icons on restart. Keep the user's desired
    // state, but do not pretend recovery succeeded when no Shell host accepts it.
    tray_present = false;
    tray_ready = false;
    if (!active() || !tray_desired) return;
    const auto restored = publish_tray(tray_tooltip);
    if (!restored.ok) report(restored);
  }

  result show_menu(const std::vector<menu_item>& items, POINT position,
                   std::uint64_t expected_tray_revision = 0) {
    if (!active()) return closed_error();
    if (items.empty() || items.size() > 64)
      return result::failure("E_MENU_ITEMS", "A popup menu requires between 1 and 64 items");
    if (active_menu_owner)
      return result::failure("E_BUSY", "Another native popup menu is already active");
    const auto snapshot = items;
    popup_handle menu;
    if (!menu.value) return win32_failure("CreatePopupMenu");
    for (std::size_t i = 0; i < snapshot.size(); ++i) {
      const auto& item = snapshot[i];
      if (item.separator) {
        if (!AppendMenuW(menu.value, MF_SEPARATOR, 0, nullptr))
          return win32_failure("AppendMenuW");
        continue;
      }
      std::wstring label;
      if (item.label.find('\0') != std::string::npos || !to_wide(item.label, label))
        return result::failure("E_MENU_LABEL", "A menu label is not valid UTF-8");
      std::wstring literal;
      literal.reserve(label.size());
      for (const wchar_t character : label) {
        literal += character;
        if (character == L'&') literal += L'&';
      }
      const UINT flags = MF_STRING | (item.enabled ? MF_ENABLED : MF_GRAYED) |
                         (item.checked ? MF_CHECKED : MF_UNCHECKED);
      if (!AppendMenuW(menu.value, flags, static_cast<UINT_PTR>(i + 1), literal.c_str()))
        return win32_failure("AppendMenuW");
    }
    const HWND owner = window;
    if (GetForegroundWindow() != owner && !SetForegroundWindow(owner) &&
        GetForegroundWindow() != owner)
      return result::failure("E_FOCUS", "Windows refused foreground activation for the popup");
    // TrackPopupMenuEx enters a nested message loop. Invalidate a previously
    // selected popup command before that loop can dispatch it into this newer
    // menu registration, even when both registrations reuse the same ID.
    const auto expected_popup_revision = expected_tray_revision ? 0 : ++popup_revision;
    active_menu_owner = owner;
    SetLastError(ERROR_SUCCESS);
    const UINT command = static_cast<UINT>(TrackPopupMenuEx(
        menu.value, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON |
        (GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN),
        position.x, position.y, owner, nullptr));
    const DWORD track_error = command ? ERROR_SUCCESS : GetLastError();
    active_menu_owner = nullptr;
    // Required for reliable dismissal when opened from a notification icon.
    if (IsWindow(owner)) PostMessageW(owner, WM_NULL, 0, 0);
    if (!active()) return closed_error();
    if (expected_tray_revision &&
        (!tray_ready || !tray_desired || tray_revision != expected_tray_revision))
      return result::success();
    if (expected_tray_revision) {
      auto data = tray_identity();
      Shell_NotifyIconW(NIM_SETFOCUS, &data);
    }
    if (!command) {
      // TPM_RETURNCMD uses zero for both dismissal and failure. Do not claim a
      // command ran; preserve a supplied error and otherwise finish with none.
      return track_error ? hresult_failure("TrackPopupMenuEx", HRESULT_FROM_WIN32(track_error)) :
                           result::success();
    }
    if (command > snapshot.size() || !snapshot[command - 1].enabled ||
        snapshot[command - 1].separator)
      return result::failure("E_MENU_COMMAND", "Windows returned an invalid menu command");
    const std::weak_ptr<state> weak = shared_from_this();
    return enqueue([weak, expected_tray_revision, expected_popup_revision,
                    id = snapshot[command - 1].id] {
      if (const auto self = weak.lock(); self && self->active() && self->events.command &&
          (!expected_tray_revision || (self->tray_desired && self->tray_ready &&
                                      self->tray_revision == expected_tray_revision)) &&
          (expected_tray_revision || self->popup_revision == expected_popup_revision))
        self->events.command(id);
    });
  }

  void open_tray_menu() {
    if (!active() || !tray_desired || !tray_ready || tray_items.empty()) return;
    POINT position{};
    NOTIFYICONIDENTIFIER identity{};
    identity.cbSize = sizeof(identity);
    identity.hWnd = window;
    identity.uID = tray_icon_id;
    RECT icon_bounds{};
    if (SUCCEEDED(Shell_NotifyIconGetRect(&identity, &icon_bounds))) {
      position = {icon_bounds.left, icon_bounds.bottom};
    } else if (!GetCursorPos(&position)) {
      report(win32_failure("GetCursorPos"));
      return;
    }
    const auto revision = tray_revision;
    const std::weak_ptr<state> weak = shared_from_this();
    const auto queued = enqueue([weak, revision, position] {
      if (const auto self = weak.lock(); self && self->active() && self->tray_ready &&
          self->tray_desired && self->tray_revision == revision) {
        const auto displayed = self->show_menu(self->tray_items, position, revision);
        if (!displayed.ok && self->active()) self->report(displayed);
      }
    });
    if (!queued.ok) report(queued);
  }

  result enqueue(std::function<void()> work) {
    if (!work) return result::failure("E_ARGUMENT", "The dispatched callback is empty");
    std::lock_guard<std::mutex> lock(queue_mutex);
    if (!accepting_work || !dispatch_window) return closed_error();
    if (queue.size() >= max_dispatch_queue)
      return result::failure("E_BUSY", "The desktop dispatch queue is full");
    queue.push_back(std::move(work));
    if (!dispatch_posted) {
      if (!PostMessageW(dispatch_window, dispatch_message, 0, 0)) {
        queue.pop_back();
        return win32_failure("PostMessageW");
      }
      dispatch_posted = true;
    }
    return result::success();
  }

  void invoke_next() {
    std::function<void()> work;
    {
      std::lock_guard<std::mutex> lock(queue_mutex);
      dispatch_posted = false;
      // A modal directory picker may run a nested native message pump. Keep
      // application callbacks serialized until the outer callback has returned.
      if (dispatching || !accepting_work || queue.empty()) return;
      work = std::move(queue.front());
      queue.pop_front();
    }
    dispatching = true;
    try {
      if (active()) work();
    } catch (...) {
      fail(result::failure("E_CALLBACK", "A dispatched desktop callback threw an exception"));
    }
    dispatching = false;
    bool post_failed = false;
    {
      std::lock_guard<std::mutex> lock(queue_mutex);
      if (accepting_work && !queue.empty() && !dispatch_posted) {
        dispatch_posted = PostMessageW(dispatch_window, dispatch_message, 0, 0) != FALSE;
        post_failed = !dispatch_posted;
      }
    }
    if (post_failed) fail(win32_failure("PostMessageW"));
  }

  bool allowed(std::string_view uri) const {
    // Opting in permits the native-selected loopback document, not arbitrary
    // HTTP pages or even other documents served by the same resource server.
    const bool local_uri = uri.compare(0, 7, "file://") == 0 ||
        (enable_resource_server && is_resource_uri(uri));
    return active() && local_uri && events.allow_navigation && events.allow_navigation(uri);
  }

  bool current_source(std::string& source) const {
    if (!webview || installed_generation != generation || !installed_generation) return false;
    com_string native_source;
    return SUCCEEDED(webview->get_Source(&native_source.value)) &&
           bounded_utf8(native_source.value, 32768, source) && allowed(source);
  }

  void report(result error) {
    const std::weak_ptr<state> weak = shared_from_this();
    enqueue([weak, error = std::move(error)] {
      if (const auto self = weak.lock(); self && self->active() && self->events.error)
        self->events.error(error);
    });
  }

  void fail(result error) {
    if (closed || !terminal_status.ok) return;
    terminal_status = std::move(error);
    {
      std::lock_guard<std::mutex> lock(queue_mutex);
      accepting_work = false;
    }
    // Finish COM event handlers before notifying the application or closing its
    // controller. In particular, never run a modal user callback inside WebView2.
    if (!window || !PostMessageW(window, failure_message, 0, 0)) finish_failure();
  }

  void finish_failure() {
    if (closed) return;
    const auto error_callback = events.error;
    if (error_callback) {
      try { error_callback(terminal_status); } catch (...) {}
    }
    shutdown(true);
  }

  result shutdown(bool notify, bool destroy_window = true) {
    if (closed) return result::success();
    closed = true;  // Revoke all async completions before releasing COM objects.
    ++generation;
    std::deque<std::function<void()>> discarded;
    {
      std::lock_guard<std::mutex> lock(queue_mutex);
      accepting_work = false;
      dispatch_window = nullptr;
      dispatch_posted = false;
      discarded.swap(queue);
    }
    auto on_closed = notify ? std::move(events.closed) : std::function<void()>{};
    auto on_error = notify ? std::move(events.error) : std::function<void(result)>{};
    events = {};
    if (active_menu_owner == window) EndMenu();
    const auto tray_removed = remove_tray();
    if (directory_dialog) directory_dialog->Close(HRESULT_FROM_WIN32(ERROR_CANCELLED));
    directory_dialog.Reset();
    if (webview) webview->Stop();
    if (controller) controller->Close();
    secured_webview.Reset();
    webview.Reset();
    controller.Reset();
    environment.Reset();
    const HWND old_window = window;
    window = nullptr;
    if (destroy_window && old_window && IsWindow(old_window)) DestroyWindow(old_window);
    if (!tray_removed.ok && on_error) {
      try { on_error(tray_removed); } catch (...) {}
    }
    if (on_closed) {
      try { on_closed(); } catch (...) {}
    }
    return tray_removed;
  }

  void update_bounds() {
    if (!controller || !window || closed) return;
    RECT bounds{};
    if (!GetClientRect(window, &bounds)) {
      fail(win32_failure("GetClientRect"));
      return;
    }
    const HRESULT hr = controller->put_Bounds(bounds);
    if (FAILED(hr)) fail(hresult_failure("WebView2 put_Bounds", hr));
  }

  // Only one registration is outstanding. An obsolete completion removes its
  // script and starts the newest request; it can never Navigate the old URI.
  void install_latest_script() {
    if (!active() || !webview || !has_load || installing_script) return;
    if (!script_id.empty()) {
      const HRESULT removed = webview->RemoveScriptToExecuteOnDocumentCreated(script_id.c_str());
      if (FAILED(removed)) {
        fail(hresult_failure("WebView2 RemoveScriptToExecuteOnDocumentCreated", removed));
        return;
      }
      script_id.clear();
    }
    installed_generation = 0;
    installing_script = true;
    const auto requested_generation = generation;
    const std::weak_ptr<state> weak = shared_from_this();
    const HRESULT hr = webview->AddScriptToExecuteOnDocumentCreated(
        target_preload.c_str(),
        Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
            [weak, requested_generation](HRESULT error, LPCWSTR id) -> HRESULT {
              const auto self = weak.lock();
              if (!self || !self->active()) return S_OK;
              try {
                self->installing_script = false;
                if (FAILED(error) || !id || !*id) {
                  self->fail(hresult_failure("WebView2 document-start preload",
                                            FAILED(error) ? error : E_UNEXPECTED));
                  return S_OK;
                }
                if (requested_generation != self->generation) {
                  const HRESULT removed =
                      self->webview->RemoveScriptToExecuteOnDocumentCreated(id);
                  if (FAILED(removed)) {
                    self->fail(hresult_failure("WebView2 remove stale preload", removed));
                  } else {
                    self->install_latest_script();
                  }
                  return S_OK;
                }
                self->script_id = id;
                self->installed_generation = requested_generation;
                std::wstring uri;
                if (!to_wide(self->target_uri, uri)) {
                  self->fail(result::failure("E_ARGUMENT", "The navigation URI is not UTF-8"));
                  return S_OK;
                }
                // Navigate to the currently selected document can be interpreted
                // as a fragment-only transition. Reload guarantees a new
                // document and execution of this generation's fresh preload.
                com_string native_source;
                std::string source;
                const bool same_document =
                    SUCCEEDED(self->webview->get_Source(&native_source.value)) &&
                    bounded_utf8(native_source.value, 32768, source) && self->allowed(source);
                self->navigation_ticket = true;
                const HRESULT navigated = same_document ? self->webview->Reload() :
                    self->webview->Navigate(uri.c_str());
                if (FAILED(navigated))
                  self->fail(hresult_failure("WebView2 Navigate/Reload", navigated));
              } catch (...) {
                self->fail(result::failure("E_BACKEND", "WebView2 preload initialization failed"));
              }
              return S_OK;
            }).Get());
    if (FAILED(hr)) {
      installing_script = false;
      fail(hresult_failure("WebView2 AddScriptToExecuteOnDocumentCreated", hr));
    }
  }

  result connect_events() {
    ComPtr<ICoreWebView2Settings> settings;
    HRESULT hr = webview->get_Settings(settings.GetAddressOf());
    if (FAILED(hr)) return hresult_failure("WebView2 get_Settings", hr);
    // Fail startup if an essential security control could not be applied.
    if (FAILED(hr = settings->put_IsScriptEnabled(TRUE)) ||
        FAILED(hr = settings->put_IsWebMessageEnabled(TRUE)) ||
        FAILED(hr = settings->put_AreHostObjectsAllowed(FALSE)) ||
        FAILED(hr = settings->put_AreDevToolsEnabled(FALSE)) ||
        FAILED(hr = settings->put_AreDefaultContextMenusEnabled(FALSE)) ||
        FAILED(hr = settings->put_AreDefaultScriptDialogsEnabled(FALSE)))
      return hresult_failure("WebView2 security settings", hr);
    hr = webview.As(&secured_webview);
    if (FAILED(hr))
      return result::failure("E_UNSUPPORTED",
          "WebView2 Runtime must support ICoreWebView2_18 to refuse external URI launches");

    const std::weak_ptr<state> weak = shared_from_this();
    // Tokens need not outlive setup: Controller.Close unregisters all handlers.
    // Every handler that uses the host captures only a weak state reference.
    EventRegistrationToken token{};
    hr = webview->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [weak](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
              // Deny first, including on conversion failure or an exception.
              HRESULT denied = args->put_Cancel(TRUE);
              const auto self = weak.lock();
              if (!self || !self->active()) return denied;
              try {
                com_string native_uri;
                std::string uri;
                UINT64 id = 0;
                if (SUCCEEDED(args->get_Uri(&native_uri.value)) &&
                    bounded_utf8(native_uri.value, 32768, uri) &&
                    self->installed_generation == self->generation &&
                    self->installed_generation && self->allowed(uri) &&
                    SUCCEEDED(args->get_NavigationId(&id))) {
                  if (self->navigation_ticket) {
                    self->navigation_ticket = false;
                    self->navigation_id = id;
                    return args->put_Cancel(FALSE);
                  }
                  // This is a renderer reload/assignment, not our one native
                  // navigation. Cancel it, revoke the old generation immediately,
                  // and let the common layer issue a fresh token and preload.
                  // Fragment-only navigation never raises NavigationStarting.
                  self->navigation_id = 0;
                  self->installed_generation = 0;
                  const auto reload_generation = ++self->generation;
                  const result queued = self->enqueue([weak, reload_generation] {
                    if (const auto target = weak.lock(); target && target->active() &&
                        target->generation == reload_generation && target->events.reload)
                      target->events.reload();
                  });
                  if (!queued.ok) self->fail(queued);
                }
              } catch (...) {
                self->fail(result::failure("E_BACKEND", "WebView2 navigation validation failed"));
              }
              if (FAILED(denied)) self->fail(hresult_failure("WebView2 cancel navigation", denied));
              return denied;
            }).Get(), &token);
    if (FAILED(hr)) return hresult_failure("WebView2 NavigationStarting", hr);

    hr = webview->add_FrameNavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
              return args->put_Cancel(TRUE);
            }).Get(), &token);
    if (FAILED(hr)) return hresult_failure("WebView2 FrameNavigationStarting", hr);

    hr = webview->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
              return args->put_Handled(TRUE);
            }).Get(), &token);
    if (FAILED(hr)) return hresult_failure("WebView2 NewWindowRequested", hr);

    hr = secured_webview->add_DownloadStarting(
        Callback<ICoreWebView2DownloadStartingEventHandler>(
            [](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT {
              return args->put_Cancel(TRUE);
            }).Get(), &token);
    if (FAILED(hr)) return hresult_failure("WebView2 DownloadStarting", hr);

    hr = secured_webview->add_LaunchingExternalUriScheme(
        Callback<ICoreWebView2LaunchingExternalUriSchemeEventHandler>(
            [](ICoreWebView2*, ICoreWebView2LaunchingExternalUriSchemeEventArgs* args) -> HRESULT {
              return args->put_Cancel(TRUE);
            }).Get(), &token);
    if (FAILED(hr)) return hresult_failure("WebView2 LaunchingExternalUriScheme", hr);

    hr = webview->add_PermissionRequested(
        Callback<ICoreWebView2PermissionRequestedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
              return args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
            }).Get(), &token);
    if (FAILED(hr)) return hresult_failure("WebView2 PermissionRequested", hr);

    hr = webview->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [weak](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
              const auto self = weak.lock();
              if (!self || !self->active()) return S_OK;
              try {
                com_string native_payload, native_source;
                std::string payload, source, current;
                // The base ICoreWebView2 event is top-level-only. Do not register
                // ICoreWebView2Frame::WebMessageReceived or trust a JS frame flag.
                if (FAILED(args->TryGetWebMessageAsString(&native_payload.value)) ||
                    !bounded_utf8(native_payload.value, max_message_characters, payload) ||
                    payload.size() > max_message_characters ||
                    FAILED(args->get_Source(&native_source.value)) ||
                    !bounded_utf8(native_source.value, 32768, source) ||
                    !self->allowed(source) || !self->current_source(current))
                  return S_OK;
                const auto received_generation = self->generation;
                self->enqueue([weak, received_generation, payload = std::move(payload),
                               source = std::move(source)] {
                  const auto target = weak.lock();
                  std::string current_uri;
                  if (target && target->active() &&
                      target->generation == received_generation &&
                      target->allowed(source) && target->current_source(current_uri) &&
                      target->events.message)
                    target->events.message(payload, source, message_frame::main);
                });
              } catch (...) {
                self->fail(result::failure("E_BACKEND", "WebView2 message delivery failed"));
              }
              return S_OK;
            }).Get(), &token);
    if (FAILED(hr)) return hresult_failure("WebView2 WebMessageReceived", hr);

    hr = webview->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [weak](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
              const auto self = weak.lock();
              if (!self || !self->active()) return S_OK;
              try {
                UINT64 id = 0;
                BOOL success = FALSE;
                if (FAILED(args->get_NavigationId(&id)) || !self->navigation_id ||
                    id != self->navigation_id ||
                    self->installed_generation != self->generation)
                  return S_OK;
                if (FAILED(args->get_IsSuccess(&success)) || !success) {
                  COREWEBVIEW2_WEB_ERROR_STATUS status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                  args->get_WebErrorStatus(&status);
                  self->report(result::failure("E_NAVIGATION",
                      "WebView2 navigation failed (status " +
                      std::to_string(static_cast<int>(status)) + ")"));
                  return S_OK;
                }
                std::string source;
                if (!self->current_source(source)) return S_OK;
                const auto loaded_generation = self->generation;
                self->enqueue([weak, loaded_generation, source = std::move(source)] {
                  const auto target = weak.lock();
                  std::string current;
                  if (target && target->active() && target->generation == loaded_generation &&
                      target->allowed(source) && target->current_source(current) &&
                      target->events.loaded)
                    target->events.loaded(source);
                });
              } catch (...) {
                self->fail(result::failure("E_BACKEND", "WebView2 load notification failed"));
              }
              return S_OK;
            }).Get(), &token);
    if (FAILED(hr)) return hresult_failure("WebView2 NavigationCompleted", hr);

    hr = webview->add_ProcessFailed(
        Callback<ICoreWebView2ProcessFailedEventHandler>(
            [weak](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT {
              if (const auto self = weak.lock(); self && self->active())
                self->fail(result::failure("E_WEBVIEW_PROCESS", "A WebView2 process failed"));
              return S_OK;
            }).Get(), &token);
    if (FAILED(hr)) return hresult_failure("WebView2 ProcessFailed", hr);
    return result::success();
  }
};

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* self = reinterpret_cast<state*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lparam);
    self = static_cast<state*>(creation->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (!self) return DefWindowProcW(window, message, wparam, lparam);
  try {
    if (self->taskbar_created_message && message == self->taskbar_created_message) {
      self->restore_tray();
      return 0;
    }
    switch (message) {
      case WM_CLOSE:
        self->shutdown(true);
        return 0;
      case WM_DESTROY:
        // Also handle destruction initiated by the owning native application.
        self->shutdown(true, false);
        return 0;
      case WM_NCDESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
      case WM_SIZE:
        if (self->controller) {
          const HRESULT hr = self->controller->put_IsVisible(
              self->visible && wparam != SIZE_MINIMIZED ? TRUE : FALSE);
          if (FAILED(hr)) self->fail(hresult_failure("WebView2 put_IsVisible", hr));
        }
        self->update_bounds();
        return 0;
      case WM_MOVE:
      case WM_MOVING:
        if (self->controller) self->controller->NotifyParentWindowPositionChanged();
        break;
      case WM_SETFOCUS:
        if (self->controller)
          self->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        return 0;
      case WM_DPICHANGED: {
        const auto* bounds = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window, nullptr, bounds->left, bounds->top,
                     bounds->right - bounds->left, bounds->bottom - bounds->top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        return 0;
      }
      case dispatch_message:
        self->invoke_next();
        return 0;
      case failure_message:
        self->finish_failure();
        return 0;
      case tray_message:
        if (HIWORD(lparam) == tray_icon_id &&
            (LOWORD(lparam) == WM_CONTEXTMENU || LOWORD(lparam) == NIN_SELECT ||
             LOWORD(lparam) == NIN_KEYSELECT))
          self->open_tray_menu();
        return 0;
      case WM_QUERYENDSESSION:
        return TRUE;
      case WM_ENDSESSION:
        if (wparam && self->events.quit_requested) self->events.quit_requested();
        return 0;
      default:
        break;
    }
  } catch (...) {
    // Never unwind through the Win32 callback ABI.
    self->fail(result::failure("E_BACKEND", "The desktop window callback failed"));
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

result default_profile_directory(std::filesystem::path& directory,
                                 const std::filesystem::path& resource_root) {
  com_string app_data;
  HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr,
                                   &app_data.value);
  if (FAILED(hr)) return hresult_failure("SHGetKnownFolderPath", hr);
  // Stable app/root-specific directory, rather than WebView2's potentially
  // unwritable default beside an executable installed in Program Files.
  wchar_t executable[32768]{};
  const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
  if (!length || length >= 32768) return win32_failure("GetModuleFileNameW");
  const std::wstring identity = std::wstring(executable, length) + L"|" + resource_root.native();
  std::uint64_t hash = UINT64_C(14695981039346656037);
  for (const wchar_t c : identity) {
    hash ^= static_cast<std::uint16_t>(c);
    hash *= UINT64_C(1099511628211);
  }
  directory = std::filesystem::path(app_data.value) / L"SOUI" / L"Desktop" /
              std::to_wstring(hash);
  return result::success();
}

class windows_backend final : public backend {
 public:
  windows_backend() : state_(std::make_shared<state>()) {}
  ~windows_backend() override {
    state_->shutdown(false);
    state_.reset();
    if (com_initialized_) CoUninitialize();
  }

  result create(const options& settings, backend_events events) override {
    if (creation_attempted_ || state_->closed)
      return result::failure("E_STATE", "The desktop backend can only be created once");
    creation_attempted_ = true;
    owner_thread_ = GetCurrentThreadId();
    // Root run() can outlive its own HWND. Retain only this group-level callback
    // independently; ordinary window callbacks are revoked by state::shutdown.
    quit_requested_ = events.quit_requested;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr))
      return hresult_failure("CoInitializeEx (WebView2 requires an STA UI thread)", hr);
    com_initialized_ = true;

    // Use the same loader discovery rules as environment creation. Discovery
    // success is not proof that controller creation or the application will run.
    LPWSTR runtime_version = nullptr;
    hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &runtime_version);
    const bool version_found = runtime_version && runtime_version[0] != L'\0';
    CoTaskMemFree(runtime_version);
    if (FAILED(hr) || !version_found)
      return webview_startup_failure("Discover WebView2 Runtime", FAILED(hr) ? hr : E_UNEXPECTED);

    std::wstring title;
    if (settings.title.find('\0') != std::string::npos || !to_wide(settings.title, title))
      return result::failure("E_ARGUMENT", "The window title must be valid UTF-8 without NUL");
    std::filesystem::path profile = settings.user_data_directory;
    if (profile.empty()) {
      const result selected = default_profile_directory(profile, settings.resource_root);
      if (!selected.ok) return selected;
    }
    std::error_code fs_error;
    profile = std::filesystem::absolute(profile, fs_error);
    if (!fs_error) std::filesystem::create_directories(profile, fs_error);
    if (fs_error)
      return result::failure("E_PROFILE", "Cannot create the WebView2 user data directory: " +
                            fs_error.message());

    WNDCLASSEXW type{};
    type.cbSize = sizeof(type);
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpfnWndProc = window_procedure;
    type.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    type.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    type.hIconSm = type.hIcon;
    type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    type.lpszClassName = window_class;
    if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
      return win32_failure("RegisterClassExW");
    RECT bounds{0, 0, settings.width, settings.height};
    if (!AdjustWindowRectEx(&bounds, WS_OVERLAPPEDWINDOW, FALSE, 0))
      return win32_failure("AdjustWindowRectEx");
    state_->events = std::move(events);
    state_->visible = settings.visible;
    state_->enable_resource_server = settings.enable_resource_server;
    state_->window = CreateWindowExW(
        0, window_class, title.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, type.hInstance, state_.get());
    if (!state_->window) {
      const auto failure = win32_failure("CreateWindowExW");
      state_->shutdown(false);
      return failure;
    }
    state_->created = true;
    {
      std::lock_guard<std::mutex> lock(state_->queue_mutex);
      state_->dispatch_window = state_->window;
      state_->accepting_work = true;
    }
    if (settings.visible) ShowWindow(state_->window, SW_SHOW);
    const std::weak_ptr<state> weak = state_;
    hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, profile.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [weak](HRESULT error, ICoreWebView2Environment* environment) -> HRESULT {
              const auto self = weak.lock();
              if (!self || !self->active()) return S_OK;
              try {
                if (FAILED(error) || !environment) {
                  self->fail(webview_startup_failure(
                      "Create WebView2 environment",
                      FAILED(error) ? error : E_UNEXPECTED));
                  return S_OK;
                }
                self->environment = environment;
                const HRESULT creation = environment->CreateCoreWebView2Controller(
                    self->window,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [weak](HRESULT controller_error,
                               ICoreWebView2Controller* controller) -> HRESULT {
                          const auto target = weak.lock();
                          if (!target || !target->active()) {
                            // A controller created after cancellation must be
                            // closed even though its former host no longer exists.
                            if (controller) controller->Close();
                            return S_OK;
                          }
                          try {
                            if (FAILED(controller_error) || !controller) {
                              if (controller) controller->Close();
                              target->fail(hresult_failure("Create WebView2 controller",
                                  FAILED(controller_error) ? controller_error : E_UNEXPECTED));
                              return S_OK;
                            }
                            target->controller = controller;
                            HRESULT configured = controller->get_CoreWebView2(
                                target->webview.GetAddressOf());
                            if (FAILED(configured) || !target->webview) {
                              target->fail(hresult_failure("WebView2 get_CoreWebView2",
                                  FAILED(configured) ? configured : E_UNEXPECTED));
                              return S_OK;
                            }
                            const result connected = target->connect_events();
                            if (!connected.ok) {
                              target->fail(connected);
                              return S_OK;
                            }
                            target->update_bounds();
                            if (!target->active()) return S_OK;
                            configured = controller->put_IsVisible(
                                target->visible && !IsIconic(target->window) ? TRUE : FALSE);
                            if (FAILED(configured)) {
                              target->fail(hresult_failure("WebView2 put_IsVisible", configured));
                              return S_OK;
                            }
                            if (GetFocus() == target->window) {
                              configured = controller->MoveFocus(
                                  COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
                              if (FAILED(configured)) {
                                target->fail(hresult_failure("WebView2 MoveFocus", configured));
                                return S_OK;
                              }
                            }
                            target->install_latest_script();
                          } catch (...) {
                            target->fail(result::failure("E_BACKEND",
                                                       "WebView2 controller initialization failed"));
                          }
                          return S_OK;
                        }).Get());
                if (FAILED(creation))
                  self->fail(hresult_failure("CreateCoreWebView2Controller", creation));
              } catch (...) {
                self->fail(result::failure("E_BACKEND", "WebView2 environment initialization failed"));
              }
              return S_OK;
            }).Get());
    if (FAILED(hr)) {
      const result failure = webview_startup_failure("CreateCoreWebView2EnvironmentWithOptions", hr);
      state_->shutdown(false);
      return failure;
    }
    return result::success();
  }

  result load(std::string uri, std::string preload) override {
    if (!state_->active()) return closed_error();
    std::wstring wide_uri, wide_preload;
    if (uri.find('\0') != std::string::npos || !to_wide(uri, wide_uri) ||
        preload.find('\0') != std::string::npos || !to_wide(preload, wide_preload))
      return result::failure("E_ARGUMENT", "The URI and preload must be valid UTF-8 without NUL");
    if (!state_->allowed(uri))
      return result::failure("E_NAVIGATION", "Navigation is outside the current native selection");
    ++state_->generation;
    state_->target_uri = std::move(uri);
    // WebView2's registration runs in child frames too; no bridge or token
    // executes there, even if an embedding page creates an about:blank/srcdoc frame.
    state_->target_preload = L"(function(){if(window.top!==window)return;\n" +
                             wide_preload + L"\n})();";
    state_->has_load = true;
    state_->navigation_ticket = false;
    state_->navigation_id = 0;
    if (state_->webview) {
      const HRESULT hr = state_->webview->Stop();
      if (FAILED(hr)) {
        state_->fail(hresult_failure("WebView2 Stop", hr));
        return state_->terminal_status;
      }
    }
    state_->install_latest_script();
    return state_->terminal_status;
  }

  result run() override {
    if (!state_->created) return closed_error();
    if (loop_running_) return result::failure("E_STATE", "The desktop loop is already running");
    if (quit_loop_requested_) return state_->terminal_status;
    loop_running_ = true;
    struct running_guard {
      bool& running;
      ~running_guard() { running = false; }
    } reset{loop_running_};
    MSG message{};
    while (!quit_loop_requested_) {
      const BOOL received = GetMessageW(&message, nullptr, 0, 0);
      if (received == -1) {
        const result failure = win32_failure("GetMessageW");
        request_group_quit();
        return failure;
      }
      if (!received) {
        request_group_quit();
        break;
      }
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    return state_->terminal_status;
  }

  result quit_loop() override {
    if (quit_loop_requested_) return result::success();
    quit_loop_requested_ = true;
    // A thread message remains valid after the root window has closed. Do not
    // use WM_QUIT: an unconsumed quit could poison a later application group.
    if (loop_running_ && !PostThreadMessageW(owner_thread_, WM_NULL, 0, 0))
      return win32_failure("PostThreadMessageW");
    return result::success();
  }

  result close() override {
    return state_->shutdown(true);
  }

  result evaluate(std::string script) override {
    if (!state_->active()) return closed_error();
    std::string source;
    if (!state_->current_source(source))
      return result::failure("E_NOT_READY", "The selected WebView2 document is not ready");
    std::wstring wide_script;
    if (!to_wide(script, wide_script) || script.find('\0') != std::string::npos)
      return result::failure("E_ARGUMENT", "The script must be valid UTF-8 without NUL");
    const auto generation = state_->generation;
    const std::weak_ptr<state> weak = state_;
    const HRESULT hr = state_->webview->ExecuteScript(
        wide_script.c_str(),
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [weak, generation](HRESULT error, LPCWSTR) -> HRESULT {
              const auto self = weak.lock();
              if (self && self->active() && self->generation == generation && FAILED(error)) {
                try { self->report(hresult_failure("WebView2 ExecuteScript", error)); }
                catch (...) {}
              }
              return S_OK;
            }).Get());
    return SUCCEEDED(hr) ? result::success() : hresult_failure("WebView2 ExecuteScript", hr);
  }

  result dispatch(std::function<void()> work) override {
    return state_->enqueue(std::move(work));
  }

  result set_title(std::string_view title) override {
    if (!state_->active()) return closed_error();
    std::wstring value;
    if (title.find('\0') != std::string_view::npos || !to_wide(title, value))
      return result::failure("E_ARGUMENT", "The window title must be valid UTF-8 without NUL");
    return SetWindowTextW(state_->window, value.c_str()) ? result::success() :
           win32_failure("SetWindowTextW");
  }

  result resize(int width, int height) override {
    if (!state_->active()) return closed_error();
    if (width <= 0 || height <= 0 || width > 32768 || height > 32768)
      return result::failure("E_ARGUMENT", "Window dimensions must be between 1 and 32768");
    RECT bounds{0, 0, width, height};
    if (!AdjustWindowRectEx(&bounds, WS_OVERLAPPEDWINDOW, FALSE, 0))
      return win32_failure("AdjustWindowRectEx");
    if (!SetWindowPos(state_->window, nullptr, 0, 0, bounds.right - bounds.left,
                      bounds.bottom - bounds.top, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER))
      return win32_failure("SetWindowPos");
    return result::success();
  }

  result show(bool visible) override {
    if (!state_->active()) return closed_error();
    if (state_->controller) {
      const HRESULT hr = state_->controller->put_IsVisible(
          visible && !IsIconic(state_->window) ? TRUE : FALSE);
      if (FAILED(hr)) return hresult_failure("WebView2 put_IsVisible", hr);
    }
    state_->visible = visible;
    ShowWindow(state_->window, visible ? SW_SHOW : SW_HIDE);
    return result::success();
  }

  result focus() override {
    if (!state_->active()) return closed_error();
    if (!state_->visible) return result::failure("E_STATE", "A hidden window cannot be focused");
    if (IsIconic(state_->window)) ShowWindow(state_->window, SW_RESTORE);
    if (!SetForegroundWindow(state_->window))
      return result::failure("E_FOCUS", "Windows refused foreground activation");
    SetFocus(state_->window);
    if (state_->controller) {
      const HRESULT hr = state_->controller->MoveFocus(
          COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
      if (FAILED(hr)) return hresult_failure("WebView2 MoveFocus", hr);
    }
    return result::success();
  }

  value_result clipboard_read() override {
    if (!state_->active()) return {closed_error(), {}};
    clipboard_lock clipboard(state_->window);
    if (!clipboard.opened) return {win32_failure("OpenClipboard"), {}};
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT))
      return {result::failure("E_CLIPBOARD_FORMAT", "The clipboard does not contain text"), {}};
    const HGLOBAL handle = GetClipboardData(CF_UNICODETEXT);
    if (!handle) return {win32_failure("GetClipboardData"), {}};
    const SIZE_T bytes = GlobalSize(handle);
    if (bytes < sizeof(wchar_t) || bytes > (max_clipboard_bytes + 1) * sizeof(wchar_t) ||
        bytes % sizeof(wchar_t))
      return {result::failure("E_CLIPBOARD_SIZE", "Clipboard text has an invalid size"), {}};
    global_lock memory(handle);
    if (!memory.data) return {win32_failure("GlobalLock"), {}};
    const auto* text = static_cast<const wchar_t*>(memory.data);
    const auto capacity = bytes / sizeof(wchar_t);
    std::size_t length = 0;
    while (length < capacity && text[length]) ++length;
    std::string value;
    if (length == capacity || !to_utf8(std::wstring_view(text, length), value))
      return {result::failure("E_CLIPBOARD_TEXT", "Clipboard text is malformed"), {}};
    if (value.size() > max_clipboard_bytes)
      return {result::failure("E_CLIPBOARD_SIZE", "Clipboard text exceeds 1 MiB of UTF-8"), {}};
    return {result::success(), std::move(value)};
  }

  result clipboard_write(std::string_view text) override {
    if (!state_->active()) return closed_error();
    std::wstring value;
    if (text.size() > max_clipboard_bytes || text.find('\0') != std::string_view::npos ||
        !to_wide(text, value))
      return result::failure("E_ARGUMENT", "Clipboard text is too large or is not valid UTF-8");
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, (value.size() + 1) * sizeof(wchar_t));
    if (!handle) return win32_failure("GlobalAlloc");
    {
      global_lock memory(handle);
      if (!memory.data) {
        const auto failure = win32_failure("GlobalLock");
        GlobalFree(handle);
        return failure;
      }
      std::memcpy(memory.data, value.c_str(), (value.size() + 1) * sizeof(wchar_t));
    }
    clipboard_lock clipboard(state_->window);
    if (!clipboard.opened) {
      const auto failure = win32_failure("OpenClipboard");
      GlobalFree(handle);
      return failure;
    }
    if (!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT, handle)) {
      const auto failure = win32_failure("SetClipboardData");
      GlobalFree(handle);
      return failure;
    }
    // Successful SetClipboardData transfers ownership of handle to Windows.
    return result::success();
  }

  value_result choose_directory() override {
    if (!state_->active()) return {closed_error(), {}};
    if (state_->directory_dialog)
      return {result::failure("E_BUSY", "A directory dialog is already open"), {}};
    ComPtr<IFileOpenDialog> dialog;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(dialog.GetAddressOf()));
    if (FAILED(hr)) return {hresult_failure("Create directory dialog", hr), {}};
    FILEOPENDIALOGOPTIONS flags{};
    hr = dialog->GetOptions(&flags);
    if (SUCCEEDED(hr))
      hr = dialog->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                              FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
    if (FAILED(hr)) return {hresult_failure("Configure directory dialog", hr), {}};
    state_->directory_dialog = dialog;
    hr = dialog->Show(state_->window);
    state_->directory_dialog.Reset();
    if (!state_->active()) return {closed_error(), {}};
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
      return {result::failure("E_CANCELLED", "Directory selection was cancelled"), {}};
    if (FAILED(hr)) return {hresult_failure("Show directory dialog", hr), {}};
    ComPtr<IShellItem> selected;
    hr = dialog->GetResult(selected.GetAddressOf());
    if (FAILED(hr)) return {hresult_failure("Read directory selection", hr), {}};
    com_string path;
    hr = selected->GetDisplayName(SIGDN_FILESYSPATH, &path.value);
    if (FAILED(hr)) return {hresult_failure("Read selected directory path", hr), {}};
    std::string value;
    if (!bounded_utf8(path.value, 32768, value))
      return {result::failure("E_PATH", "The selected directory path is invalid"), {}};
    return {result::success(), std::move(value)};
  }

  result popup_menu(const std::vector<menu_item>& items) override {
    if (!state_->active()) return closed_error();
    if (!IsWindowVisible(state_->window))
      return result::failure("E_WINDOW_HIDDEN", "Show the owning window before opening its popup menu");
    POINT position{};
    if (!GetCursorPos(&position)) return win32_failure("GetCursorPos");
    return state_->show_menu(items, position);
  }

  result set_tray(const tray_options& settings) override {
    return state_->update_tray(settings);
  }

  result clear_tray() override {
    if (!state_->active()) return closed_error();
    return state_->remove_tray();
  }

  capabilities get_capabilities() const override {
    return {"webview2", true, true, true, true, true, true, true};
  }

 private:
  void request_group_quit() {
    // WM_QUIT and a broken message pump are process-loop events, not a request
    // to close just the root HWND. The common layer owns closing every member.
    quit_loop_requested_ = true;
    if (quit_requested_) {
      try { quit_requested_(); }
      catch (...) { state_->shutdown(true); }
    } else {
      state_->shutdown(true);
    }
  }

  std::shared_ptr<state> state_;
  bool creation_attempted_{false};
  bool com_initialized_{false};
  bool loop_running_{false};
  bool quit_loop_requested_{false};
  DWORD owner_thread_{0};
  std::function<void()> quit_requested_;
};

}  // namespace

std::unique_ptr<backend> make_backend() {
  return std::make_unique<windows_backend>();
}

}  // namespace soui::desktop::detail
