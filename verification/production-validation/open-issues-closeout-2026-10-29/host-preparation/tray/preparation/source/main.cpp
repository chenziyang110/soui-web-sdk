#include <soui/runtime/services/desktop/tray_service.h>
#include <soui/runtime/services/desktop/menu_service.h>
#include <soui/runtime/services/desktop/global_shortcut.h>
#include <soui/version.h>

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using clock_type = std::chrono::steady_clock;
constexpr wchar_t window_class[] = L"SouiIndependentTrayConsumer";
constexpr UINT callback_message = WM_APP + 19;
constexpr UINT_PTR timer_id = 1;
constexpr UINT menu_update = 1;
constexpr UINT menu_exit = 2;

std::string quote(const std::string& text) {
  std::string result = "\"";
  constexpr char digits[] = "0123456789abcdef";
  for (const auto byte : text) {
    const auto value = static_cast<unsigned char>(byte);
    if (value == '"' || value == '\\') { result += '\\'; result += byte; }
    else if (value < 32) {
      result += "\\u00";
      result += digits[value >> 4];
      result += digits[value & 15];
    } else result += byte;
  }
  return result + '"';
}

const char* boolean(bool value) { return value ? "true" : "false"; }

class log_writer {
 public:
  explicit log_writer(bool interactive) : mode_(interactive ? "interactive" : "smoke-test") {}
  void emit(const char* event, const std::string& fields = {}) noexcept {
    try {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - started_).count();
      std::cout << "{\"sequence\":" << ++sequence_ << ",\"elapsedMs\":" << elapsed
                << ",\"mode\":" << quote(mode_) << ",\"event\":" << quote(event);
      if (!fields.empty()) std::cout << ',' << fields;
      std::cout << "}\n" << std::flush;
      if (!std::cout) failed_ = true;
    } catch (...) { failed_ = true; }
  }
  bool failed() const noexcept { return failed_; }
 private:
  const std::string mode_;
  const clock_type::time_point started_{clock_type::now()};
  std::uint64_t sequence_{0};
  bool failed_{false};
};

class application final {
 public:
  explicit application(bool interactive) : interactive_(interactive), log_(interactive) {}
  ~application() { cleanup(); }

  int run() {
    try {
      log_.emit("run.started", "\"sdkVersion\":" + quote(soui::sdk::version) +
          ",\"architecture\":" + quote(sizeof(void*) == 8 ? "x64" : "x86") +
          ",\"pid\":" + std::to_string(GetCurrentProcessId()) +
          ",\"timeoutMs\":" + std::to_string(interactive_ ? 180000 : 10000));
      taskbar_created_ = RegisterWindowMessageW(L"TaskbarCreated");
      if (!taskbar_created_) throw std::runtime_error("Cannot register TaskbarCreated message");
      WNDCLASSW description{};
      description.lpfnWndProc = &window_proc;
      description.hInstance = GetModuleHandleW(nullptr);
      description.lpszClassName = window_class;
      if (!RegisterClassW(&description)) throw std::runtime_error("Cannot register owner window class");
      class_registered_ = true;
      window_ = CreateWindowExW(0, window_class, L"SOUI tray consumer", WS_OVERLAPPEDWINDOW,
          CW_USEDEFAULT, CW_USEDEFAULT, 360, 200, nullptr, nullptr, description.hInstance, this);
      if (!window_) throw std::runtime_error("Cannot create hidden top-level owner window");
      log_.emit("owner.created", "\"visible\":" + std::string(boolean(IsWindowVisible(window_) != FALSE)));
      tray_ = std::make_unique<soui::runtime::tray_service>();
      menu_ = std::make_unique<soui::runtime::menu_service>();
      shortcut_ = std::make_unique<soui::runtime::global_shortcut>();
      conflict_probe_ = std::make_unique<soui::runtime::global_shortcut>();
      log_.emit("service.created");
      const auto icon = LoadIconW(nullptr, IDI_INFORMATION);
      if (!icon) throw std::runtime_error("Cannot load the shared application icon");
      const soui::runtime::tray_options options{window_, icon, 19, callback_message,
          "SOUI SDK 独立托盘消费者"};
      const auto installed = tray_->install(options);
      record_response("tray.install", installed);
      if (!installed.ok() || !tray_->installed() || !tray_->desired() || tray_->cleanup_pending())
        throw std::runtime_error("Tray installation was not confirmed");
      soui::runtime::global_shortcut_options shortcut_options{window_, 100,
          soui::runtime::shortcut_modifiers::control | soui::runtime::shortcut_modifiers::alt, VK_F9, true};
      const auto registered = shortcut_->register_shortcut(shortcut_options);
      record_shortcut("shortcut.register", registered, *shortcut_);
      if (!registered.ok() || !shortcut_->active())
        throw std::runtime_error("Ctrl+Alt+F9 registration was not confirmed");
      shortcut_options.id = 101;
      const auto conflict = conflict_probe_->register_shortcut(shortcut_options);
      record_shortcut("shortcut.conflict-probe", conflict, *conflict_probe_);
      if (conflict.ok() || conflict.native_error != ERROR_HOTKEY_ALREADY_REGISTERED || conflict_probe_->active())
        throw std::runtime_error("Duplicate shortcut did not report the expected native conflict");
      started_ = clock_type::now();
      if (!SetTimer(window_, timer_id, 100, nullptr)) throw std::runtime_error("Cannot start bounded run timer");
      if (interactive_) {
        log_.emit("interactive.ready", "\"instructions\":" + quote(
            "Use the tray icon or Ctrl+Alt+F9 to open the public SDK menu; update tooltip or exit. Session limit: 180 seconds.") +
            ",\"trustedGesture\":false");
      }
      MSG message{};
      int received = 0;
      while ((received = static_cast<int>(GetMessageW(&message, nullptr, 0, 0))) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
      if (received < 0) throw std::runtime_error("Owner message loop failed");
      if (!exit_requested_) throw std::runtime_error("Message loop ended without a consumer exit request");
      log_.emit("loop.ended", "\"requested\":true");
    } catch (const std::exception& error) {
      exit_code_ = 1;
      log_.emit("run.error", "\"message\":" + quote(error.what()) +
          ",\"win32Error\":" + std::to_string(GetLastError()));
    } catch (...) {
      exit_code_ = 1;
      log_.emit("run.error", "\"message\":\"Unknown native exception\"");
    }
    cleanup();
    if (log_.failed()) exit_code_ = 1;
    log_.emit("run.finished", "\"exitCode\":" + std::to_string(exit_code_) +
        ",\"cleanupConfirmed\":" + boolean(cleanup_confirmed_) +
        ",\"callbackCount\":" + std::to_string(callback_count_) +
        ",\"shortcutCount\":" + std::to_string(shortcut_count_) +
        ",\"menuCount\":" + std::to_string(menu_count_) +
        ",\"taskbarCreatedCount\":" + std::to_string(taskbar_count_));
    return log_.failed() ? 1 : exit_code_;
  }

 private:
  std::string state() const {
    return "\"installed\":" + std::string(boolean(tray_->installed())) +
        ",\"desired\":" + boolean(tray_->desired()) +
        ",\"cleanupPending\":" + boolean(tray_->cleanup_pending());
  }
  void record_response(const char* event, const soui::runtime::desktop_response& response,
                       const std::string& fields = {}) {
    log_.emit(event, "\"ok\":" + std::string(boolean(response.ok())) +
        ",\"code\":" + quote(response.code) + ",\"message\":" + quote(response.message) +
        ',' + state() + (fields.empty() ? "" : ',' + fields));
  }
  void record_shortcut(const char* event, const soui::runtime::global_shortcut_response& response,
                       const soui::runtime::global_shortcut& shortcut) {
    log_.emit(event, "\"ok\":" + std::string(boolean(response.ok())) +
        ",\"code\":" + quote(response.code) + ",\"message\":" + quote(response.message) +
        ",\"nativeError\":" + std::to_string(response.native_error) +
        ",\"active\":" + boolean(shortcut.active()) +
        ",\"cleanupPending\":" + boolean(shortcut.cleanup_pending()) +
        ",\"retired\":" + boolean(shortcut.retired()));
  }
  void cancel_menu() {
    if (!menu_active_ || !menu_) return;
    const auto cancelled = menu_->cancel();
    record_response("menu.cancel-request", cancelled);
    if (!cancelled.ok()) exit_code_ = 1;
  }
  void request_exit(const char* reason, int code) {
    if (code != 0 && exit_code_ == 0) exit_code_ = code;
    if (exit_requested_) return;
    exit_requested_ = true;
    log_.emit("exit.requested", "\"reason\":" + quote(reason) + ",\"exitCode\":" + std::to_string(exit_code_));
    cancel_menu();
    PostQuitMessage(exit_code_);
  }
  void update_tooltip() {
    const auto response = tray_->update_tooltip("SOUI SDK 托盘提示已更新 · " + std::to_string(++updates_));
    record_response("tray.tooltip", response, "\"update\":" + std::to_string(updates_));
    if (!response.ok()) request_exit("tooltip-failed", 1);
  }
  void show_menu(const soui::runtime::tray_message_result& event = {}) {
    POINT position{};
    if (event.position_valid) { position.x = event.x; position.y = event.y; }
    else if (!GetCursorPos(&position)) throw std::runtime_error("Cannot obtain consumer menu position");
    using kind = soui::runtime::menu_item_kind;
    const std::vector<soui::runtime::menu_item> items{
        {kind::command, menu_update, "更新托盘提示", true, false, {}},
        {kind::submenu, 0, "公共 SDK 菜单", true, false,
            {{kind::command, 3, "禁用项", false, false, {}},
             {kind::command, 4, "已勾选项", true, true, {}}}},
        {kind::separator, 0, {}, true, false, {}},
        {kind::command, menu_exit, "移除托盘并退出", true, false, {}}};
    log_.emit("menu.requested", "\"scope\":\"public-sdk\",\"observation\":" + std::to_string(++menu_count_));
    menu_active_ = true;
    soui::runtime::menu_result result;
    try { result = menu_->show(window_, items, position.x, position.y); }
    catch (...) { menu_active_ = false; throw; }
    menu_active_ = false;
    record_response("menu.returned", result.response, "\"selection\":" + std::to_string(result.command_id) +
        ",\"hasSelection\":" + boolean(result.has_selection) +
        ",\"nativeError\":" + std::to_string(result.native_error) +
        ",\"menuCleanupPending\":" + boolean(menu_->cleanup_pending()));
    if (!result.response.ok() || menu_->cleanup_pending()) request_exit("menu-failed", 1);
    if (!interactive_ && (!smoke_cancel_sent_ || result.has_selection))
      request_exit("smoke-menu-incomplete", 1);
    // Return focus even if a timeout/close request interrupted the modal menu.
    // Cleanup runs only after this message handler returns to the main loop.
    if (tray_ && tray_->installed()) {
      const auto focus = tray_->return_focus();
      record_response("tray.focus", focus, "\"exitRequested\":" + std::string(boolean(exit_requested_)));
      if (!focus.ok()) request_exit("focus-return-failed", 1);
    } else {
      log_.emit("tray.focus.skipped", "\"reason\":\"Tray installation is not confirmed\"");
      request_exit("focus-return-unavailable", 1);
    }
    if (exit_requested_) return;
    if (result.has_selection && result.command_id == menu_update) update_tooltip();
    else if (result.has_selection && result.command_id == menu_exit) request_exit("consumer-menu", 0);
  }
  LRESULT dispatch(HWND window, UINT message, WPARAM first, LPARAM second) {
    if (message == WM_CLOSE) {
      request_exit("owner-close", interactive_ ? 0 : 1);
      return 0;
    }
    if (message == WM_TIMER && first == timer_id && !exit_requested_) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - started_).count();
      if (elapsed >= (interactive_ ? 180000 : 10000)) request_exit("deadline-exceeded", 3);
      else if (!interactive_ && menu_active_ && !smoke_cancel_sent_) {
        const auto result = menu_->cancel();
        // Foreground activation may dispatch a timer before native tracking
        // begins. That tick must not consume the one successful cancellation.
        if (result.code != "E_MENU_NOT_TRACKING") {
          record_response("menu.cancel-request", result);
          smoke_cancel_sent_ = result.ok();
          if (!result.ok()) request_exit("menu-cancel-failed", 1);
        }
      } else if (!interactive_ && smoke_stage_ == 0 && elapsed >= 500) {
        smoke_stage_ = 1;
        show_menu();
        smoke_stage_ = 2;
      } else if (!interactive_ && smoke_stage_ == 2 && updates_ == 0) update_tooltip();
      else if (!interactive_ && smoke_stage_ == 2 && updates_ != 0 && elapsed >= 1500)
        request_exit("smoke-complete", exit_code_);
      return 0;
    }
    if (shortcut_ && message == WM_HOTKEY) {
      const auto event = shortcut_->dispatch_message(message, static_cast<std::uintptr_t>(first),
          static_cast<std::intptr_t>(second));
      record_shortcut("shortcut.message", event.response, *shortcut_);
      log_.emit("shortcut.notification", "\"handled\":" + std::string(boolean(event.handled)) +
          ",\"activated\":" + boolean(event.activated) + ",\"sourceVerified\":false");
      if (!event.response.ok()) request_exit("shortcut-dispatch-failed", 1);
      if (event.activated) {
        ++shortcut_count_;
        if (interactive_ && !exit_requested_ && !menu_active_) show_menu();
      }
      if (event.handled) return 0;
    }
    if (tray_) {
      const auto event = tray_->dispatch_message(message, static_cast<std::uintptr_t>(first),
          static_cast<std::intptr_t>(second));
      if (message == taskbar_created_) {
        record_response("taskbar.created", event.response,
            "\"observation\":" + std::to_string(++taskbar_count_) +
            ",\"handled\":" + boolean(event.handled) + ",\"sourceVerified\":false");
        // Keep listening after recovery failure so a later notification can retry.
        if (!event.response.ok()) exit_code_ = 1;
      }
      if (message == callback_message) {
        const char* name = event.event == soui::runtime::tray_event::activated ? "activated" :
            event.event == soui::runtime::tray_event::context_menu ? "context-menu" : "none";
        record_response("tray.callback", event.response, "\"notification\":" + quote(name) +
            ",\"observation\":" + std::to_string(++callback_count_) +
            ",\"handled\":" + boolean(event.handled) + ",\"trustedGesture\":false" +
            ",\"positionValid\":" + boolean(event.position_valid) +
            ",\"x\":" + std::to_string(event.x) + ",\"y\":" + std::to_string(event.y));
        if (!event.response.ok()) exit_code_ = 1;
      }
      if (interactive_ && !exit_requested_ && !menu_active_ && event.event != soui::runtime::tray_event::none)
        show_menu(event);
      if (event.handled) return 0;
    }
    return DefWindowProcW(window, message, first, second);
  }
  static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM first, LPARAM second) noexcept {
    auto* app = reinterpret_cast<application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      app = static_cast<application*>(reinterpret_cast<CREATESTRUCTW*>(second)->lpCreateParams);
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window, message, first, second);
    try { return app->dispatch(window, message, first, second); }
    catch (const std::exception& error) {
      app->log_.emit("dispatch.error", "\"message\":" + quote(error.what()));
    } catch (...) { app->log_.emit("dispatch.error", "\"message\":\"Unknown native exception\""); }
    app->exit_code_ = 1;
    app->exit_requested_ = true;
    try { app->cancel_menu(); } catch (...) {}
    PostQuitMessage(1);
    return 0;
  }
  void cleanup() noexcept {
    if (cleaned_) return;
    cleaned_ = true;
    cleanup_confirmed_ = true;
    if (window_) KillTimer(window_, timer_id);
    if (menu_) {
      bool cleared = false;
      for (unsigned attempt = 1; attempt <= 3 && !cleared; ++attempt) {
        try {
          const auto result = menu_->cleanup();
          record_response("menu.cleanup", result, "\"attempt\":" + std::to_string(attempt));
          if (!result.ok()) exit_code_ = 1;
          cleared = result.ok() && !menu_->cleanup_pending();
        } catch (...) { exit_code_ = 1; log_.emit("menu.cleanup.error"); }
      }
      if (!cleared) { cleanup_confirmed_ = false; exit_code_ = 1; }
      menu_.reset();
    }
    auto remove_shortcut = [this](std::unique_ptr<soui::runtime::global_shortcut>& value, const char* event) {
      if (!value) return;
      bool cleared = false;
      for (unsigned attempt = 1; attempt <= 3 && !cleared; ++attempt) {
        try {
          const auto result = value->unregister_shortcut();
          record_shortcut(event, result, *value);
          if (!result.ok()) exit_code_ = 1;
          cleared = result.ok() && !value->active() && !value->cleanup_pending();
        } catch (...) { exit_code_ = 1; log_.emit("shortcut.cleanup.error"); }
      }
      if (!cleared) { cleanup_confirmed_ = false; exit_code_ = 1; }
      value.reset();
    };
    remove_shortcut(conflict_probe_, "shortcut.conflict-probe.unregister");
    remove_shortcut(shortcut_, "shortcut.unregister");
    if (tray_) {
      bool removed = false;
      for (unsigned attempt = 1; attempt <= 3 && !removed; ++attempt) {
        try {
          const auto response = tray_->remove();
          record_response("tray.remove", response, "\"attempt\":" + std::to_string(attempt));
          if (!response.ok()) exit_code_ = 1;
          removed = response.ok() && !tray_->installed() && !tray_->desired() && !tray_->cleanup_pending();
        } catch (...) {
          exit_code_ = 1;
          log_.emit("tray.remove.error", "\"message\":\"Removal threw an exception\"");
        }
      }
      cleanup_confirmed_ = cleanup_confirmed_ && removed;
      if (!removed) exit_code_ = 1;
      tray_.reset();
      log_.emit("service.destroyed", "\"removalConfirmed\":" + std::string(boolean(removed)));
    }
    if (window_) {
      const bool destroyed = DestroyWindow(window_) != FALSE;
      window_ = nullptr;
      if (!destroyed) { cleanup_confirmed_ = false; exit_code_ = 1; }
      log_.emit("owner.destroyed", "\"ok\":" + std::string(boolean(destroyed)));
    }
    if (class_registered_) {
      const bool removed = UnregisterClassW(window_class, GetModuleHandleW(nullptr)) != FALSE;
      class_registered_ = false;
      if (!removed) { cleanup_confirmed_ = false; exit_code_ = 1; }
      log_.emit("owner-class.removed", "\"ok\":" + std::string(boolean(removed)));
    }
  }

  const bool interactive_;
  log_writer log_;
  HWND window_{nullptr};
  UINT taskbar_created_{0};
  std::unique_ptr<soui::runtime::tray_service> tray_;
  std::unique_ptr<soui::runtime::menu_service> menu_;
  std::unique_ptr<soui::runtime::global_shortcut> shortcut_;
  std::unique_ptr<soui::runtime::global_shortcut> conflict_probe_;
  clock_type::time_point started_{};
  unsigned updates_{0};
  unsigned callback_count_{0};
  unsigned shortcut_count_{0};
  unsigned menu_count_{0};
  unsigned smoke_stage_{0};
  unsigned taskbar_count_{0};
  int exit_code_{0};
  bool class_registered_{false};
  bool exit_requested_{false};
  bool menu_active_{false};
  bool smoke_cancel_sent_{false};
  bool cleaned_{false};
  bool cleanup_confirmed_{false};
};
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2 || (std::string(argv[1]) != "--smoke-test" && std::string(argv[1]) != "--interactive")) {
    std::cerr << "Expected exactly --smoke-test or --interactive\n";
    return 2;
  }
  application consumer(std::string(argv[1]) == "--interactive");
  return consumer.run();
}
