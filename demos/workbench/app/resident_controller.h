#pragma once

#include <exception>
#include "resident_shutdown_policy.h"

// Private Workbench implementation, included after demo_soui_window in main.cpp.
// All state belongs to the application's existing UI thread.
struct resident_run_context {
  std::filesystem::path log;
  std::string run_id;
  std::uint64_t sequence{0};
  ULONGLONG started{0};
  HANDLE file{INVALID_HANDLE_VALUE};
  bool active{false};
  bool log_failed{false};
  resident_run_context() = default;
  resident_run_context(const resident_run_context&) = delete;
  resident_run_context& operator=(const resident_run_context&) = delete;
  ~resident_run_context() { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
};

bool resident_write(resident_run_context& context, const char* event,
                    nlohmann::json fields = nlohmann::json::object()) noexcept {
  if (!context.active) return true;
  try {
    fields["event"] = event;
    fields["runId"] = context.run_id;
    fields["sequence"] = ++context.sequence;
    fields["elapsedMs"] = GetTickCount64() - context.started;
    const auto line = fields.dump() + '\n';
    DWORD written = 0;
    if (line.size() <= MAXDWORD &&
        WriteFile(context.file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) &&
        written == line.size() && FlushFileBuffers(context.file)) return true;
  } catch (...) { }
  context.log_failed = true;
  return false;
}

struct resident_settings {
  soui::sdk::host_config host;
  int width{1100};
  int height{720};
  bool background_pacing{false};
  bool prefer_high_resolution{true};
  bool auto_high_refresh{false};
  bool smoke{false};
};

class resident_controller final {
  using json = nlohmann::json;
  static constexpr wchar_t class_name[] = L"SouiWorkbenchResidentController";
  static constexpr UINT tray_message = WM_APP + 0x424;
  static constexpr UINT_PTR timer_id = 41;
  static constexpr std::uint32_t command_all = 10;
  static constexpr std::uint32_t command_exit = 11;
  static constexpr ULONGLONG ready_timeout = 45000;
  static constexpr ULONGLONG smoke_timeout = 180000;

  struct window_slot {
    std::uint32_t generation{0};
    unsigned ready_revision{0};
    bool ready{false};
    ULONGLONG awaiting_since{0};
    std::unique_ptr<soui::demo::demo_bridge_adapter> bridge;
    std::unique_ptr<soui::sdk::soui_miniblink_host> host;
    std::unique_ptr<demo_soui_window> window;
    HWND hwnd{nullptr};
    ~window_slot() {
      if (window) window->stop_for_resident_close();
      // run() must retain this whole bundle until the actual Host drain has
      // finished. Destruction is not a substitute for that recoverable pump.
      if (host && !host->destroy().ok) std::terminate();
      if (window && hwnd && ::IsWindow(hwnd)) window->DestroyWindow();
      // A live HWND still refers to this SOUI object and its Host/adapter.
      // On an unrecoverable DestroyWindow failure keep that bundle alive until
      // process exit rather than leave the native window with dangling state.
      if (hwnd && ::IsWindow(hwnd)) {
        (void)window.release(); (void)host.release(); (void)bridge.release();
        return;
      }
      window.reset();
      host.reset();
      bridge.reset();
    }
  };

  // Shell/menu/Host calls can synchronously dispatch messages. Defer ownership
  // changes until the outer operation has returned, including modal menus.
  class operation_scope {
   public:
    explicit operation_scope(resident_controller& owner) : owner_(owner) { ++owner_.operation_depth_; }
    ~operation_scope() {
      --owner_.operation_depth_;
      if (owner_.exiting_ && !owner_.operation_depth_ && !owner_.menu_active_) owner_.schedule_shutdown();
    }
   private:
    resident_controller& owner_;
  };

 public:
  resident_controller(SOUI::SApplication& app, resident_settings settings, resident_run_context& context)
      : application_(app), settings_(std::move(settings)), context_(context) {}
  ~resident_controller() {
    shutdown();
    if (!shutdown_complete_) cleanup_fail_stop("Controller destruction preceded actual Host cleanup");
  }

  int run() {
    try {
      emit("resident.started", {{"sdkVersion", soui::sdk::version},
          {"architecture", settings_.host.architecture}, {"smoke", settings_.smoke},
          {"readyTimeoutMs", ready_timeout}, {"smokeTimeoutMs", smoke_timeout},
          {"scope", "window lifecycle; no concurrent-task or genuine-input assertion"}});
      if (context_.log_failed) throw std::runtime_error("Cannot persist resident evidence");
      WNDCLASSW description{};
      description.lpfnWndProc = &window_proc;
      description.hInstance = GetModuleHandleW(nullptr);
      description.lpszClassName = class_name;
      if (!RegisterClassW(&description)) throw std::runtime_error("Cannot register resident controller class");
      class_registered_ = true;
      controller_ = CreateWindowExW(0, class_name, L"SOUI Workbench resident controller", WS_OVERLAPPED,
          0, 0, 0, 0, nullptr, nullptr, description.hInstance, this);
      if (!controller_) throw std::runtime_error("Cannot create hidden resident controller");
      emit("controller.created", {{"visible", IsWindowVisible(controller_) != FALSE}});
      taskbar_message_ = RegisterWindowMessageW(L"TaskbarCreated");
      if (!taskbar_message_) throw std::runtime_error("Cannot register TaskbarCreated notification");
      tray_ = std::make_unique<soui::runtime::tray_service>();
      menu_ = std::make_unique<soui::runtime::menu_service>();
      shortcut_ = std::make_unique<soui::runtime::global_shortcut>();
      {
        operation_scope operation(*this);
        const auto icon = LoadIconW(nullptr, IDI_APPLICATION);
        const auto result = tray_->install({controller_, icon, 41, tray_message,
            "SOUI Workbench · 三窗口驻留 · Ctrl+Alt+W"});
        desktop_result("tray.install", result);
        if (!result.ok() || !tray_->installed() || !tray_->desired() || tray_->cleanup_pending())
          throw std::runtime_error("Resident tray installation was not confirmed");
        const auto registered = shortcut_->register_shortcut({controller_, 41,
            soui::runtime::shortcut_modifiers::control | soui::runtime::shortcut_modifiers::alt,
            static_cast<std::uint32_t>('W'), true});
        shortcut_result("shortcut.register", registered);
        if (!registered.ok() || !shortcut_->active())
          throw std::runtime_error("Ctrl+Alt+W could not be registered");
      }
      restore_all();
      if (exiting_) throw std::runtime_error("Resident initialization was interrupted");
      if (!SetTimer(controller_, timer_id, 100, nullptr)) throw std::runtime_error("Cannot start resident supervision timer");
      emit("resident.running", services());
      loop_started_ = true;
      const int loop_result = application_.GetMsgLoop()->Run();
      loop_started_ = false;
      emit("loop.returned", {{"exitCode", loop_result}, {"orderedShutdown", shutdown_complete_}});
      if (loop_result != 0) fail(loop_result);
      if (!shutdown_complete_) {
        fail(1);
        emit("loop.unexpected-exit");
        begin_shutdown();
        shutdown();
      }
    } catch (const std::exception& error) {
      fail(1);
      emit("resident.error", {{"message", error.what()}, {"win32Error", GetLastError()}});
      begin_shutdown();
      shutdown();
    } catch (...) {
      fail(1);
      emit("resident.error", {{"message", "Unknown resident exception"}});
      begin_shutdown();
      shutdown();
    }
    finish_shutdown_before_return();
    if (context_.log_failed) fail(1);
    emit("resident.finished", {{"exitCode", exit_code_}, {"orderedShutdown", shutdown_complete_},
        {"smokeCompleted", smoke_phase_ == 6}});
    return context_.log_failed ? 1 : exit_code_;
  }

 private:
  unsigned live_count() const {
    return static_cast<unsigned>(std::count_if(slots_.begin(), slots_.end(),
        [](const auto& item) { return item && item->hwnd && IsWindow(item->hwnd); }));
  }
  unsigned owned_slot_count() const noexcept {
    return static_cast<unsigned>(std::count_if(slots_.begin(), slots_.end(),
        [](const auto& item) { return item != nullptr; }));
  }
  [[noreturn]] void cleanup_fail_stop(const char* reason) noexcept {
    fail(1);
    try { emit("exit.cleanup-fail-stop", {{"reason", reason}, {"ownedSlots", owned_slot_count()}}); }
    catch (...) { context_.log_failed = true; }
    std::terminate();
  }
  void begin_shutdown() noexcept {
    exiting_ = true;
    if (!shutdown_policy_.started()) {
      shutdown_policy_.begin(GetTickCount64());
      emit("exit.cleanup-deadline", {{"budgetMs", resident_shutdown_policy::budget_ms}});
    }
  }
  void require_shutdown_budget() noexcept {
    if (!exiting_ || shutdown_complete_) return;
    if (!shutdown_policy_.started())
      cleanup_fail_stop("Exit has no total shutdown deadline");
    const auto now = GetTickCount64();
    if (shutdown_policy_.expired(now)) {
      emit("exit.cleanup-timeout", {{"budgetMs", resident_shutdown_policy::budget_ms},
          {"elapsedMs", shutdown_policy_.elapsed(now)}});
      cleanup_fail_stop("The total resident shutdown deadline expired with owners retained");
    }
  }
  void finish_shutdown_before_return() noexcept {
    // A startup exception or an external WM_QUIT can end the normal SOUI
    // loop while an endpoint still owns callbacks. Keep every member alive
    // and continue the same thread's SOUI filtering/native message dispatch.
    loop_started_ = false;
    begin_shutdown();
    shutdown_posted_ = false;
    shutdown();
    loop_started_ = !shutdown_complete_;
    while (!shutdown_complete_) {
      require_shutdown_budget();
      const auto waited = MsgWaitForMultipleObjectsEx(0, nullptr,
          static_cast<DWORD>(shutdown_policy_.wait_ms(GetTickCount64())),
          QS_ALLINPUT, MWMO_INPUTAVAILABLE);
      require_shutdown_budget();
      if (waited == WAIT_FAILED) cleanup_fail_stop("The cleanup message pump failed");
      if (waited == WAIT_TIMEOUT) { shutdown(); continue; }
      if (waited != WAIT_OBJECT_0) cleanup_fail_stop("The cleanup wait returned an unexpected status");
      MSG message{};
      const bool received = PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE;
      // PeekMessage can dispatch sent messages before returning.
      require_shutdown_budget();
      if (shutdown_complete_) break;
      if (!received) { shutdown(); continue; }
      if (message.message == WM_QUIT) {
        fail(1);
        emit("exit.cleanup-quit-deferred");
        // WM_QUIT is an exit request, never a successful drain receipt.
        loop_started_ = false;
        shutdown();
        loop_started_ = !shutdown_complete_;
        continue;
      }
      try { application_.GetMsgLoop()->OnMsg(&message); }
      catch (const std::exception& error) {
        fail(1);
        try { emit("exit.cleanup-dispatch-error", {{"message", error.what()}}); }
        catch (...) { context_.log_failed = true; }
      } catch (...) {
        fail(1);
        emit("exit.cleanup-dispatch-error");
      }
      // The dispatched member/native stack has returned before any slot can
      // be released. Retry even if an application message filter consumed the
      // posted shutdown message; only actual close results permit completion.
      shutdown();
    }
    loop_started_ = false;
  }
  bool business_dispatch_active() const {
    return std::any_of(slots_.begin(), slots_.end(), [](const auto& slot) {
      return slot && slot->window && slot->window->resident_dispatch_active();
    });
  }
  void emit(const char* event, json fields = json::object()) noexcept {
    try {
      fields["liveWindows"] = live_count();
      (void)resident_write(context_, event, std::move(fields));
    } catch (...) { context_.log_failed = true; }
  }
  void fail(int code) { if (!exit_code_ && code) exit_code_ = code; }
  json services() const {
    return {{"trayInstalled", tray_ && tray_->installed()}, {"trayDesired", tray_ && tray_->desired()},
        {"trayCleanupPending", tray_ && tray_->cleanup_pending()},
        {"shortcutActive", shortcut_ && shortcut_->active()},
        {"shortcutCleanupPending", shortcut_ && shortcut_->cleanup_pending()},
        {"menuCleanupPending", menu_ && menu_->cleanup_pending()}};
  }
  void desktop_result(const char* event, const soui::runtime::desktop_response& result,
                       json fields = json::object()) {
    fields["ok"] = result.ok(); fields["code"] = result.code; fields["message"] = result.message;
    fields["services"] = services();
    emit(event, std::move(fields));
    if (!result.ok()) fail(1);
  }
  void shortcut_result(const char* event, const soui::runtime::global_shortcut_response& result,
                        json fields = json::object()) {
    fields["ok"] = result.ok(); fields["code"] = result.code; fields["message"] = result.message;
    fields["nativeError"] = result.native_error; fields["services"] = services();
    emit(event, std::move(fields));
    if (!result.ok()) fail(1);
  }
  void host_result(const char* event, unsigned index, const soui::sdk::host_result& result) {
    emit(event, {{"slot", index + 1}, {"generation", generations_[index]},
        {"ok", result.ok}, {"code", result.code}, {"message", result.message}});
    if (!result.ok) fail(1);
  }
  void open_slot(unsigned index) {
    if (exiting_ || index >= slots_.size()) return;
    operation_scope operation(*this);
    if (slots_[index]) {
      auto& slot = *slots_[index];
      ShowWindow(slot.hwnd, SW_RESTORE);
      const bool activated = SetForegroundWindow(slot.hwnd) != FALSE;
      const auto focused = slot.host->focus();
      host_result("window.focus", index, focused);
      emit("window.restored", {{"slot", index + 1}, {"generation", slot.generation}, {"activated", activated}});
      if (!focused.ok) request_exit("window-focus-failed", 1);
      return;
    }
    auto owned = std::make_unique<window_slot>();
    owned->generation = ++generations_[index];
    if (!owned->generation) throw std::runtime_error("Resident slot generation exhausted");
    owned->bridge = std::make_unique<soui::demo::demo_bridge_adapter>();
    owned->host = std::make_unique<soui::sdk::soui_miniblink_host>(std::make_unique<soui::sdk::dynamic_miniblink_adapter>());
    owned->window = std::make_unique<demo_soui_window>(*owned->host, *owned->bridge, false,
        std::filesystem::path{}, settings_.background_pacing, settings_.prefer_high_resolution, settings_.auto_high_refresh);
    owned->window->configure_resident(controller_, index + 1, owned->generation);
    slots_[index] = std::move(owned);
    auto& slot = *slots_[index];
    slot.hwnd = slot.window->Create(nullptr, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0,
        60 + static_cast<int>(index) * 45, 60 + static_cast<int>(index) * 35, settings_.width, settings_.height);
    if (!slot.hwnd) throw std::runtime_error("Cannot create resident business window");
    // XML creation may reset SOUI host attributes; enforce the policy again.
    slot.window->configure_resident(controller_, index + 1, slot.generation);
    emit("window.created", {{"slot", index + 1}, {"generation", slot.generation}, {"wndType", "normal"}});
    auto config = settings_.host;
    config.owner_window = slot.hwnd;
    config.bridge_adapter = slot.bridge.get();
    const auto started = slot.host->start(config);
    host_result("host.started", index, started);
    if (!started.ok) throw std::runtime_error("A resident business Host failed to start");
    slot.awaiting_since = GetTickCount64();
    const std::wstring title = L"SOUI Workbench - " + std::to_wstring(index + 1);
    SetWindowTextW(slot.hwnd, title.c_str());
    slot.window->ShowWindow(SW_SHOWNOACTIVATE);
    slot.window->UpdateWindow();
    if (!slot.window->SetTimer(1, 16)) throw std::runtime_error("Cannot start resident business-window timer");
  }
  void restore_all() { for (unsigned index = 0; index < slots_.size() && !exiting_; ++index) open_slot(index); }
  void close_slot(unsigned index, std::uint32_t generation) {
    require_shutdown_budget();
    if (index >= slots_.size() || !slots_[index] || slots_[index]->generation != generation) {
      emit("window.close-stale", {{"slot", index + 1}, {"generation", generation}});
      return;
    }
    // A nested message pump can deliver the posted close before an older
    // business-window handler returns. SOUI's current-message pointer spans
    // its complete native dispatch, including chained/default processing.
    if (slots_[index]->window->resident_dispatch_active()) {
      deferred_closes_[index] = generation;
      return;
    }
    operation_scope operation(*this);
    auto& slot = *slots_[index];
    emit("window.close-start", {{"slot", index + 1}, {"generation", generation}});
    slot.window->stop_for_resident_close();
    const auto closed = slot.host->destroy();
    host_result("host.destroyed", index, closed);
    require_shutdown_budget();
    if (!closed.ok) {
      deferred_closes_[index] = generation;
      emit("window.close-pending", {{"slot", index + 1}, {"generation", generation}, {"code", closed.code}});
      if (resident_classify_host_close(closed.ok, closed.code) == resident_host_close::fail_stop)
        cleanup_fail_stop("Host destruction returned a non-retryable error with its owner retained");
      return;
    }
    const bool destroyed = !slot.hwnd || !IsWindow(slot.hwnd) || slot.window->DestroyWindow() != FALSE;
    emit("window.destroyed", {{"slot", index + 1}, {"generation", generation}, {"ok", destroyed}});
    if (!destroyed) {
      fail(1);
      emit("window.close-incomplete", {{"slot", index + 1}, {"generation", generation}});
      if (!in_shutdown_) request_exit("window-destroy-failed", 1);
      return;
    }
    require_shutdown_budget();
    slot.hwnd = nullptr;
    slot.window.reset();
    slot.host.reset();
    emit("host.released", {{"slot", index + 1}, {"generation", generation}});
    slot.bridge.reset();
    emit("adapter.released", {{"slot", index + 1}, {"generation", generation}});
    slots_[index].reset();
    emit("window.closed", {{"slot", index + 1}, {"generation", generation}, {"ok", closed.ok && destroyed}});
    if ((!closed.ok || !destroyed) && !in_shutdown_) request_exit("window-close-failed", 1);
  }
  void reload_slot(unsigned index) {
    operation_scope operation(*this);
    auto& slot = *slots_.at(index);
    const auto reloaded = slot.host->reload();
    host_result("host.reloaded", index, reloaded);
    if (!reloaded.ok) throw std::runtime_error("Resident smoke reload failed");
    slot.ready = false;
    slot.awaiting_since = GetTickCount64();
  }
  bool all_ready() const {
    return std::all_of(slots_.begin(), slots_.end(), [](const auto& slot) { return slot && slot->ready; });
  }
  void send_window_close(unsigned index) {
    if (!slots_[index] || !PostMessageW(slots_[index]->hwnd, WM_CLOSE, 0, 0))
      throw std::runtime_error("Cannot request the resident window close");
  }
  void on_timer() {
    require_shutdown_budget();
    if (context_.log_failed) request_exit("evidence-write-failed", 1);
    if (settings_.smoke && GetTickCount64() - context_.started > smoke_timeout)
      request_exit("resident-smoke-timeout", 3);
    if (exiting_) { schedule_shutdown(); return; }
    if (operation_depth_ || menu_active_ || business_dispatch_active()) return;
    for (unsigned index = 0; index < slots_.size(); ++index) {
      if (deferred_closes_[index]) {
        const auto generation = deferred_closes_[index]; deferred_closes_[index] = 0;
        close_slot(index, generation);
      }
    }
    if (deferred_action_) { const auto action = deferred_action_; deferred_action_ = 0; act(action); }
    if (exiting_) return;
    operation_scope polling(*this);
    for (unsigned index = 0; index < slots_.size(); ++index) {
      if (!slots_[index] || slots_[index]->ready) continue;
      auto& slot = *slots_[index];
      if (slot.host->application_ready()) {
        slot.ready = true;
        ++slot.ready_revision;
        emit("window.ready", {{"slot", index + 1}, {"generation", slot.generation},
            {"revision", slot.ready_revision}, {"hostLifecycleSession", slot.host->session()->session_id()}});
      } else if (GetTickCount64() - slot.awaiting_since > ready_timeout) {
        emit("window.ready-timeout", {{"slot", index + 1}, {"generation", slot.generation},
            {"diagnostics", slot.host->application_diagnostics()}});
        request_exit("window-ready-timeout", 3); return;
      }
    }
    if (!settings_.smoke) return;
    if (smoke_phase_ == 0 && all_ready()) {
      emit("smoke.initial-three-ready"); send_window_close(1); smoke_phase_ = 1;
    } else if (smoke_phase_ == 1 && !slots_[1]) {
      emit("smoke.middle-window-closed"); reload_slot(0); reload_slot(2); open_slot(1); smoke_phase_ = 2;
    } else if (smoke_phase_ == 2 && all_ready()) {
      emit("smoke.survivors-reloaded-middle-recreated");
      for (unsigned index = 0; index < slots_.size(); ++index) send_window_close(index);
      smoke_phase_ = 3;
    } else if (smoke_phase_ == 3 && live_count() == 0) {
      empty_since_ = GetTickCount64(); emit("smoke.empty-resident.begin", services()); smoke_phase_ = 4;
    } else if (smoke_phase_ == 4 && GetTickCount64() - empty_since_ >= 500) {
      const bool retained = IsWindow(controller_) && tray_->installed() && tray_->desired() && shortcut_->active();
      auto evidence = services(); evidence["ok"] = retained;
      emit("smoke.empty-resident.confirmed", std::move(evidence));
      if (!retained) throw std::runtime_error("Empty resident controller lost its native services");
      restore_all(); smoke_phase_ = 5;
    } else if (smoke_phase_ == 5 && all_ready()) {
      emit("smoke.final-three-recreated"); smoke_phase_ = 6; request_exit("resident-smoke-complete", 0);
    }
  }
  void queue_action(std::uint32_t command) {
    if (exiting_) return;
    if (!PostMessageW(controller_, resident_action_message, command, 0)) request_exit("action-post-failed", 1);
  }
  void act(std::uint32_t command) {
    if (exiting_) return;
    if (command == command_exit) { request_exit("explicit-menu-exit", 0); return; }
    if (operation_depth_ || menu_active_ || business_dispatch_active()) { deferred_action_ = command; return; }
    if (command >= 1 && command <= 3) open_slot(command - 1);
    else if (command == command_all) restore_all();
  }
  void show_menu(const soui::runtime::tray_message_result& trigger) {
    if (exiting_ || menu_active_ || operation_depth_) return;
    POINT point{trigger.x, trigger.y};
    if (!trigger.position_valid && !GetCursorPos(&point))
      throw std::runtime_error("Cannot position the resident tray menu");
    const std::vector<soui::runtime::menu_item> items{
        {soui::runtime::menu_item_kind::command, 1, "打开主窗口", true, slots_[0] != nullptr, {}},
        {soui::runtime::menu_item_kind::command, 2, "打开工作窗口 A", true, slots_[1] != nullptr, {}},
        {soui::runtime::menu_item_kind::command, 3, "打开工作窗口 B", true, slots_[2] != nullptr, {}},
        {soui::runtime::menu_item_kind::command, command_all, "恢复全部窗口 · Ctrl+Alt+W", true, false, {}},
        {soui::runtime::menu_item_kind::separator, 0, "", true, false, {}},
        {soui::runtime::menu_item_kind::command, command_exit, "退出 SOUI Workbench", true, false, {}}};
    operation_scope operation(*this);
    menu_active_ = true;
    emit("menu.opening", {{"trustedGesture", false}});
    soui::runtime::menu_result selected;
    try { selected = menu_->show(controller_, items, point.x, point.y); }
    catch (...) { menu_active_ = false; throw; }
    menu_active_ = false;
    desktop_result("menu.finished", selected.response, {{"hasSelection", selected.has_selection},
        {"command", selected.command_id}, {"nativeError", selected.native_error}});
    if (tray_->installed()) {
      const auto focused = tray_->return_focus(); desktop_result("tray.focus", focused);
      if (!focused.ok()) request_exit("tray-focus-failed", 1);
    } else { emit("tray.focus-skipped", services()); request_exit("tray-not-installed-after-menu", 1); }
    if (!selected.response.ok()) request_exit("menu-operation-failed", 1);
    if (!exiting_ && selected.has_selection) queue_action(selected.command_id);
  }
  void request_exit(const char* reason, int code) {
    if (settings_.smoke && smoke_phase_ != 6 && code == 0) code = 3;
    fail(code);
    if (!exiting_) { begin_shutdown(); emit("exit.requested", {{"reason", reason}, {"exitCode", exit_code_},
        {"smokeIncomplete", settings_.smoke && smoke_phase_ != 6}}); }
    require_shutdown_budget();
    if (menu_active_ && menu_) {
      const auto cancelled = menu_->cancel(); desktop_result("menu.cancel", cancelled);
    }
    schedule_shutdown();
  }
  void schedule_shutdown() noexcept {
    require_shutdown_budget();
    if (!controller_ || in_shutdown_ || shutdown_complete_ || shutdown_posted_ || operation_depth_ ||
        menu_active_ || business_dispatch_active()) return;
    shutdown_posted_ = PostMessageW(controller_, resident_exit_message, 0, 1) != FALSE;
    if (!shutdown_posted_) { fail(1); PostQuitMessage(exit_code_); }
  }
  void shutdown() noexcept {
    if (shutdown_complete_) return;
    begin_shutdown();
    require_shutdown_budget();
    if (in_shutdown_ || operation_depth_ || menu_active_ || business_dispatch_active()) return;
    in_shutdown_ = true;
    if (controller_) KillTimer(controller_, timer_id);
    emit("exit.cleanup-start");
    try {
      if (shortcut_) {
        for (unsigned attempt = 1; attempt <= 3; ++attempt) {
          require_shutdown_budget();
          operation_scope operation(*this);
          const auto result = shortcut_->unregister_shortcut();
          shortcut_result("shortcut.unregister", result, {{"attempt", attempt}});
          if (result.ok() && !shortcut_->active() && !shortcut_->cleanup_pending()) break;
        }
      }
      if (menu_) {
        for (unsigned attempt = 1; attempt <= 3; ++attempt) {
          require_shutdown_budget();
          operation_scope operation(*this);
          const auto result = menu_->cleanup(); desktop_result("menu.cleanup", result, {{"attempt", attempt}});
          if (result.ok() && !menu_->cleanup_pending()) break;
        }
      }
      if (tray_) {
        for (unsigned attempt = 1; attempt <= 3; ++attempt) {
          require_shutdown_budget();
          operation_scope operation(*this);
          const auto result = tray_->remove(); desktop_result("tray.remove", result, {{"attempt", attempt}});
          if (result.ok() && !tray_->installed() && !tray_->desired() && !tray_->cleanup_pending()) break;
        }
      }
      for (unsigned index = 0; index < slots_.size(); ++index)
        for (unsigned attempt = 0; attempt < 3 && slots_[index]; ++attempt)
          close_slot(index, slots_[index]->generation);
    } catch (const std::exception& error) {
      fail(1); emit("exit.cleanup-error", {{"message", error.what()}});
    } catch (...) { fail(1); emit("exit.cleanup-error", {{"message", "Unknown cleanup exception"}}); }
    require_shutdown_budget();
    if (owned_slot_count() != 0) {
      // Preserve each actual HWND and its Host until native borrowers and
      // callbacks leave. This controller remains available for bounded retries.
      in_shutdown_ = false;
      if (!controller_ || !IsWindow(controller_) || !SetTimer(controller_, timer_id,
          static_cast<UINT>(shutdown_policy_.wait_ms(GetTickCount64())), nullptr))
        cleanup_fail_stop("Pending Host cleanup has no live controller retry timer");
      emit("exit.cleanup-pending", {{"liveWindows", live_count()}, {"ownedSlots", owned_slot_count()}});
      return;
    }
    shortcut_.reset(); menu_.reset(); tray_.reset();
    emit("services.destroyed");
    const auto runtime = soui::sdk::shutdown_miniblink_runtime();
    emit("runtime.shutdown", {{"ok", runtime.ok}, {"code", runtime.code}, {"message", runtime.message}});
    if (!runtime.ok) fail(7);
    require_shutdown_budget();
    if (controller_) {
      const bool destroyed = DestroyWindow(controller_) != FALSE;
      if (!destroyed) SetWindowLongPtrW(controller_, GWLP_USERDATA, 0);
      controller_ = nullptr;
      emit("controller.destroyed", {{"ok", destroyed}});
      if (!destroyed) fail(1);
    }
    if (class_registered_) {
      const bool removed = UnregisterClassW(class_name, GetModuleHandleW(nullptr)) != FALSE;
      class_registered_ = false;
      emit("controller-class.removed", {{"ok", removed}});
      if (!removed) fail(1);
    }
    require_shutdown_budget();
    shutdown_complete_ = true;
    emit("exit.cleanup-finished", {{"exitCode", exit_code_}});
    if (loop_started_) PostQuitMessage(exit_code_);
  }
  LRESULT dispatch(HWND hwnd, UINT message, WPARAM first, LPARAM second) {
    require_shutdown_budget();
    if (message == WM_CLOSE) { request_exit("controller-close", 0); return 0; }
    if (message == resident_exit_message) {
      shutdown_posted_ = false;
      if (second == 1) shutdown(); else request_exit("window-command-post-failed", first ? 1 : 0);
      return 0;
    }
    if (message == resident_close_message) {
      const auto id = static_cast<unsigned>(first);
      const auto generation = static_cast<std::uint32_t>(second);
      if (id < 1 || id > slots_.size()) return 0;
      if (!slots_[id - 1] || slots_[id - 1]->generation != generation) {
        emit("window.close-stale", {{"slot", id}, {"generation", generation}});
        return 0;
      }
      if (operation_depth_ || menu_active_) deferred_closes_[id - 1] = generation;
      else if (!exiting_) close_slot(id - 1, generation);
      return 0;
    }
    if (message == resident_action_message) { act(static_cast<std::uint32_t>(first)); return 0; }
    if (message == WM_TIMER && first == timer_id) { on_timer(); return 0; }
    if (!exiting_ && tray_ && (message == tray_message || message == taskbar_message_)) {
      soui::runtime::tray_message_result event;
      {
        operation_scope operation(*this);
        event = tray_->dispatch_message(message, first, second);
        desktop_result(message == taskbar_message_ ? "tray.taskbar-created" : "tray.notification", event.response,
            {{"handled", event.handled}, {"notification", static_cast<unsigned>(event.event)}, {"trustedGesture", false}});
      }
      if (event.event == soui::runtime::tray_event::context_menu) show_menu(event);
      else if (event.event == soui::runtime::tray_event::activated) queue_action(command_all);
      if (event.handled) return 0;
    }
    if (!exiting_ && shortcut_ && message == WM_HOTKEY) {
      soui::runtime::global_shortcut_message_result event;
      {
        operation_scope operation(*this);
        event = shortcut_->dispatch_message(message, first, second);
        shortcut_result("shortcut.notification", event.response,
            {{"handled", event.handled}, {"activated", event.activated}, {"trustedGesture", false}});
      }
      if (event.activated) queue_action(command_all);
      if (event.handled) return 0;
    }
    return DefWindowProcW(hwnd, message, first, second);
  }
  static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM first, LPARAM second) noexcept {
    auto* self = reinterpret_cast<resident_controller*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<resident_controller*>(reinterpret_cast<CREATESTRUCTW*>(second)->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, message, first, second);
    try { return self->dispatch(hwnd, message, first, second); }
    catch (const std::exception& error) {
      self->emit("controller.dispatch-error", {{"message", error.what()}});
    } catch (...) { self->emit("controller.dispatch-error", {{"message", "Unknown message exception"}}); }
    self->request_exit("controller-dispatch-failed", 1);
    return 0;
  }

  SOUI::SApplication& application_;
  const resident_settings settings_;
  resident_run_context& context_;
  HWND controller_{nullptr};
  UINT taskbar_message_{0};
  std::unique_ptr<soui::runtime::tray_service> tray_;
  std::unique_ptr<soui::runtime::menu_service> menu_;
  std::unique_ptr<soui::runtime::global_shortcut> shortcut_;
  std::array<std::unique_ptr<window_slot>, 3> slots_;
  std::array<std::uint32_t, 3> generations_{};
  std::array<std::uint32_t, 3> deferred_closes_{};
  unsigned operation_depth_{0};
  std::uint32_t deferred_action_{0};
  unsigned smoke_phase_{0};
  ULONGLONG empty_since_{0};
  resident_shutdown_policy shutdown_policy_;
  int exit_code_{0};
  bool class_registered_{false};
  bool loop_started_{false};
  bool menu_active_{false};
  bool exiting_{false};
  bool shutdown_posted_{false};
  bool in_shutdown_{false};
  bool shutdown_complete_{false};
};
