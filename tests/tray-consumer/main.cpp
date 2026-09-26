#include <soui/runtime/services/desktop/tray_service.h>
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
      log_.emit("service.created");
      const auto icon = LoadIconW(nullptr, IDI_INFORMATION);
      if (!icon) throw std::runtime_error("Cannot load the shared application icon");
      const soui::runtime::tray_options options{window_, icon, 19, callback_message,
          "SOUI SDK 独立托盘消费者"};
      const auto installed = tray_->install(options);
      record_response("tray.install", installed);
      if (!installed.ok() || !tray_->installed() || !tray_->desired() || tray_->cleanup_pending())
        throw std::runtime_error("Tray installation was not confirmed");
      started_ = clock_type::now();
      if (!SetTimer(window_, timer_id, 100, nullptr)) throw std::runtime_error("Cannot start bounded run timer");
      if (interactive_) {
        log_.emit("interactive.ready", "\"instructions\":" + quote(
            "Use the tray icon to open the consumer menu; update tooltip or remove and exit. Session limit: 180 seconds.") +
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
  void request_exit(const char* reason, int code) {
    if (code != 0 && exit_code_ == 0) exit_code_ = code;
    if (exit_requested_) return;
    exit_requested_ = true;
    log_.emit("exit.requested", "\"reason\":" + quote(reason) + ",\"exitCode\":" + std::to_string(exit_code_));
    if (menu_active_) EndMenu();
    PostQuitMessage(exit_code_);
  }
  void update_tooltip() {
    const auto response = tray_->update_tooltip("SOUI SDK 托盘提示已更新 · " + std::to_string(++updates_));
    record_response("tray.tooltip", response, "\"update\":" + std::to_string(updates_));
    if (!response.ok()) request_exit("tooltip-failed", 1);
  }
  void show_menu(const soui::runtime::tray_message_result& event) {
    POINT position{};
    if (event.position_valid) { position.x = event.x; position.y = event.y; }
    else if (!GetCursorPos(&position)) throw std::runtime_error("Cannot obtain consumer menu position");
    const HMENU menu = CreatePopupMenu();
    if (!menu) throw std::runtime_error("Cannot create consumer menu");
    const bool populated = AppendMenuW(menu, MF_STRING, menu_update, L"更新托盘提示") != FALSE &&
        AppendMenuW(menu, MF_STRING, menu_exit, L"移除托盘并退出") != FALSE;
    if (!populated) { DestroyMenu(menu); throw std::runtime_error("Cannot populate consumer menu"); }
    log_.emit("menu.opened", "\"scope\":\"consumer-private\"");
    SetForegroundWindow(window_);
    menu_active_ = true;
    const UINT selected = static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
        position.x, position.y, 0, window_, nullptr));
    menu_active_ = false;
    const bool menu_destroyed = DestroyMenu(menu) != FALSE;
    log_.emit("menu.closed", "\"selection\":" + std::to_string(selected) +
        ",\"destroyed\":" + boolean(menu_destroyed));
    PostMessageW(window_, WM_NULL, 0, 0);
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
    if (!menu_destroyed) throw std::runtime_error("Cannot destroy consumer menu");
    if (exit_requested_) return;
    if (selected == menu_update) update_tooltip();
    else if (selected == menu_exit) request_exit("consumer-menu", 0);
  }
  LRESULT dispatch(HWND window, UINT message, WPARAM first, LPARAM second) {
    if (message == WM_CLOSE) {
      request_exit("owner-close", interactive_ ? 0 : 1);
      return 0;
    }
    if (message == WM_TIMER && first == timer_id && !exit_requested_) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - started_).count();
      if (elapsed >= (interactive_ ? 180000 : 10000)) request_exit("deadline-exceeded", 3);
      else if (!interactive_ && updates_ == 0 && elapsed >= 500) update_tooltip();
      else if (!interactive_ && updates_ != 0 && elapsed >= 1000) request_exit("smoke-complete", 0);
      return 0;
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
    if (app->menu_active_) EndMenu();
    PostQuitMessage(1);
    return 0;
  }
  void cleanup() noexcept {
    if (cleaned_) return;
    cleaned_ = true;
    if (window_) KillTimer(window_, timer_id);
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
      cleanup_confirmed_ = removed;
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
  clock_type::time_point started_{};
  unsigned updates_{0};
  unsigned callback_count_{0};
  unsigned taskbar_count_{0};
  int exit_code_{0};
  bool class_registered_{false};
  bool exit_requested_{false};
  bool menu_active_{false};
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
