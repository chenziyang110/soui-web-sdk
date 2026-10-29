#include <soui/runtime/services/desktop/tray_service.h>
#include <soui/version.h>

#include <windows.h>

#include <array>
#include <chrono>
#include <cwchar>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using clock_type = std::chrono::steady_clock;
constexpr wchar_t window_class[] = L"SouiIndependentTrayRecoveryConsumer";
constexpr UINT callback_message = WM_APP + 73;
constexpr UINT_PTR timer_id = 1;
constexpr int remove_button_id = 101;
constexpr int finish_button_id = 102;
constexpr auto whole_budget = std::chrono::seconds(90);

std::string quote(const std::string& text) {
  std::string result = "\"";
  constexpr char digits[] = "0123456789abcdef";
  for (const unsigned char byte : text) {
    if (byte == '"' || byte == '\\') { result += '\\'; result += static_cast<char>(byte); }
    else if (byte < 32) {
      result += "\\u00";
      result += digits[byte >> 4];
      result += digits[byte & 15];
    } else result += static_cast<char>(byte);
  }
  return result + '"';
}
const char* boolean(bool value) { return value ? "true" : "false"; }
std::uint64_t file_time(FILETIME value) {
  return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32U) | value.dwLowDateTime;
}
std::uint64_t process_birth(HANDLE process) {
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!GetProcessTimes(process, &created, &exited, &kernel, &user))
    throw std::runtime_error("GetProcessTimes failed: " + std::to_string(GetLastError()));
  if (!file_time(created)) throw std::runtime_error("GetProcessTimes returned an unknown zero birth time");
  return file_time(created);
}
std::string utf8(const std::wstring& value) {
  const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (!count) throw std::runtime_error("Cannot encode Explorer image path: " + std::to_string(GetLastError()));
  std::string result(static_cast<std::size_t>(count), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
      result.data(), count, nullptr, nullptr) != count) throw std::runtime_error("Explorer path encoding changed");
  return result;
}

struct explorer_identity final {
  HWND shell{};
  DWORD pid{};
  std::uint64_t birth{};
  std::wstring image;
  HANDLE process{};
  explorer_identity() = default;
  ~explorer_identity() { if (process) CloseHandle(process); }
  explorer_identity(const explorer_identity&) = delete;
  explorer_identity& operator=(const explorer_identity&) = delete;
  explorer_identity(explorer_identity&& other) noexcept
      : shell(other.shell), pid(other.pid), birth(other.birth), image(std::move(other.image)),
        process(std::exchange(other.process, nullptr)) {}
  explorer_identity& operator=(explorer_identity&&) = delete;
};

class application final {
 public:
  int run() {
    try {
      own_birth_ = process_birth(GetCurrentProcess());
      emit("run.started", "\"wholeDeadlineMs\":90000,\"timerMs\":50,\"iconId\":73,\"messageSourceVerified\":false");
      taskbar_created_ = RegisterWindowMessageW(L"TaskbarCreated");
      require(taskbar_created_ >= 0xC000 && taskbar_created_ <= 0xFFFF, "TaskbarCreated registration failed");
      WNDCLASSW description{};
      description.lpfnWndProc = &window_proc;
      description.hInstance = GetModuleHandleW(nullptr);
      description.lpszClassName = window_class;
      require(RegisterClassW(&description) != 0, "RegisterClassW failed");
      class_registered_ = true;
      window_ = CreateWindowExW(0, window_class, L"SOUI 托盘恢复验证 · id73", WS_OVERLAPPEDWINDOW,
          CW_USEDEFAULT, CW_USEDEFAULT, 620, 230, nullptr, nullptr, description.hInstance, this);
      require(window_ != nullptr, "CreateWindowExW owner failed");
      status_ = CreateWindowExW(0, L"STATIC", L"等待第一次真实 Explorer 重启。", WS_CHILD | WS_VISIBLE,
          20, 20, 560, 45, window_, nullptr, description.hInstance, nullptr);
      remove_button_ = CreateWindowExW(0, L"BUTTON", L"移除图标", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_DISABLED,
          20, 90, 180, 40, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(remove_button_id)), description.hInstance, nullptr);
      finish_button_ = CreateWindowExW(0, L"BUTTON", L"结束验证", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_DISABLED,
          225, 90, 180, 40, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(finish_button_id)), description.hInstance, nullptr);
      require(status_ && remove_button_ && finish_button_, "CreateWindowExW controls failed");
      ShowWindow(window_, SW_SHOW);
      require(IsWindowVisible(window_) != FALSE, "Owner window is not visible");
      emit("owner.created", "\"visible\":true,\"removeEnabled\":false,\"finishEnabled\":false");
      explorers_[0] = std::make_unique<explorer_identity>(capture_explorer("initial.before-install"));
      tray_ = std::make_unique<soui::runtime::tray_service>();
      const auto icon = LoadIconW(nullptr, IDI_INFORMATION);
      require(icon != nullptr, "LoadIconW failed");
      const auto installed = tray_->install({window_, icon, 73, callback_message, "SOUI 独立恢复验证 id73"});
      response("tray.install", installed);
      require(installed.ok() && resident(), "Initial tray installation not confirmed");
      if (exit_requested_) throw std::runtime_error("Unexpected message during initial installation");
      phase_ = phase::first_restart;
      require(SetTimer(window_, timer_id, 50, nullptr) == timer_id, "SetTimer failed");
      timer_active_ = true;
      emit("stage.await-first-restart", state());
      MSG message{};
      while (!exit_requested_) {
        const auto received = GetMessageW(&message, nullptr, 0, 0);
        require(received != -1, "GetMessageW failed");
        if (expired()) { request_exit("whole-deadline", false); break; }
        if (!received) {
          if (!exit_requested_) request_exit("unrequested-WM_QUIT", false);
          break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
        if (log_failed_) request_exit("log-write-failed", false);
      }
    } catch (const std::exception& error) {
      emit("run.error", "\"message\":" + quote(error.what()) + ",\"win32Error\":" + std::to_string(GetLastError()) + ",\"proofKnown\":false");
      failed_ = true;
    } catch (...) {
      emit("run.error", "\"message\":\"Unknown native exception\",\"proofKnown\":false");
      failed_ = true;
    }
    cleanup();
    // Only the final qualified outcome becomes zero; no later stage clears a failure.
    if (!failed_ && !expired() && !log_failed_ && finished_ && cleanup_confirmed_ &&
        exit_requested_ && taskbar_count_ == 2 && remove_calls_ == 1) exit_code_ = 0;
    emit("run.finished", "\"exitCode\":" + std::to_string(exit_code_) +
        ",\"allStagesComplete\":" + boolean(finished_) + ",\"cleanupConfirmed\":" + boolean(cleanup_confirmed_) +
        ",\"taskbarCreatedCount\":" + std::to_string(taskbar_count_) + ",\"removeCalls\":" + std::to_string(remove_calls_) +
        ",\"messageSourceVerified\":false,\"externalRestartProofRequired\":true,\"focusVerified\":false");
    return log_failed_ ? 1 : exit_code_;
  }

 private:
  enum class phase { setup, first_restart, removal, second_restart, settling, finish };
  bool expired() const { return clock_type::now() >= deadline_; }
  static void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
  static void require_native(bool condition, const char* api) {
    if (!condition) throw std::runtime_error(std::string(api) + " failed: " + std::to_string(GetLastError()));
  }
  void emit(const char* event, const std::string& fields = {}) noexcept {
    // Fixed safety ceiling; reaching it is a terminal logging failure, never a pass.
    if (sequence_ >= 128) { log_failed_ = true; failed_ = true; return; }
    const bool limit_reached = sequence_ == 127;
    if (limit_reached) { log_failed_ = true; failed_ = true; event = "log.limit-reached"; }
    try {
      FILETIME utc{};
      GetSystemTimeAsFileTime(&utc);
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - started_).count();
      std::cout << "{\"sequence\":" << ++sequence_ << ",\"elapsedMs\":" << elapsed
          << ",\"utcFileTime\":" << quote(std::to_string(file_time(utc)))
          << ",\"runId\":" << quote(std::to_string(GetCurrentProcessId()) + "-" + std::to_string(own_birth_))
          << ",\"pid\":" << GetCurrentProcessId() << ",\"birthFileTime\":" << quote(std::to_string(own_birth_))
          << ",\"sdkVersion\":" << quote(soui::sdk::version) << ",\"architecture\":" << quote(sizeof(void*) == 8 ? "x64" : "x86")
          << ",\"event\":" << quote(event);
      if (limit_reached) std::cout << ",\"recordLimit\":128,\"failure\":true";
      else if (!fields.empty()) std::cout << ',' << fields;
      std::cout << "}\n" << std::flush;
      if (!std::cout) log_failed_ = true;
    } catch (...) { log_failed_ = true; }
  }
  bool resident() const { return tray_->installed() && tray_->desired() && !tray_->cleanup_pending(); }
  bool clean() const { return !tray_->installed() && !tray_->desired() && !tray_->cleanup_pending(); }
  std::string state() const {
    return "\"installed\":" + std::string(boolean(tray_->installed())) + ",\"desired\":" + boolean(tray_->desired()) +
        ",\"cleanupPending\":" + boolean(tray_->cleanup_pending());
  }
  void response(const char* event, const soui::runtime::desktop_response& result, const std::string& fields = {}) {
    emit(event, "\"ok\":" + std::string(boolean(result.ok())) + ",\"code\":" + quote(result.code) +
        ",\"message\":" + quote(result.message) + ",\"value\":" + quote(result.value) + ',' + state() + (fields.empty() ? "" : ',' + fields));
  }
  explorer_identity capture_explorer(const char* step) {
    explorer_identity result;
    result.shell = FindWindowW(L"Shell_TrayWnd", nullptr);
    require(result.shell != nullptr, "Shell_TrayWnd not found; Explorer identity unknown");
    require_native(GetWindowThreadProcessId(result.shell, &result.pid) != 0, "GetWindowThreadProcessId");
    require(result.pid != 0, "Shell_TrayWnd owner PID is unknown");
    result.process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, result.pid);
    require_native(result.process != nullptr, "OpenProcess Explorer");
    result.birth = process_birth(result.process);
    std::array<wchar_t, 32768> image{};
    DWORD count = static_cast<DWORD>(image.size());
    require_native(QueryFullProcessImageNameW(result.process, 0, image.data(), &count) != FALSE, "QueryFullProcessImageNameW Explorer");
    require(count != 0, "Explorer image path is empty; identity unknown");
    result.image.assign(image.data(), count);
    const auto separator = result.image.find_last_of(L"\\/");
    require(_wcsicmp(result.image.c_str() + (separator == std::wstring::npos ? 0 : separator + 1), L"explorer.exe") == 0,
        "Shell_TrayWnd owner image is not Explorer");
    DWORD after_pid{};
    require(FindWindowW(L"Shell_TrayWnd", nullptr) == result.shell &&
        GetWindowThreadProcessId(result.shell, &after_pid) != 0 && after_pid == result.pid, "Shell owner changed during identity query");
    const auto wait = WaitForSingleObject(result.process, 0);
    const auto wait_error = wait == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
    emit("explorer.snapshot", "\"step\":" + quote(step) + ",\"shellHwnd\":" + quote(std::to_string(reinterpret_cast<std::uintptr_t>(result.shell))) +
        ",\"explorerPid\":" + std::to_string(result.pid) + ",\"explorerBirthFileTime\":" + quote(std::to_string(result.birth)) +
        ",\"imagePath\":" + quote(utf8(result.image)) + ",\"waitResult\":" + std::to_string(wait) +
        ",\"waitError\":" + std::to_string(wait_error) + ",\"sourceVerified\":false");
    require(wait == WAIT_TIMEOUT, "Explorer is not confirmed alive; query/wait outcome unknown");
    return result;
  }
  static bool same_identity(const explorer_identity& a, const explorer_identity& b) {
    return a.pid == b.pid && a.birth == b.birth && _wcsicmp(a.image.c_str(), b.image.c_str()) == 0;
  }
  void close_snapshot(explorer_identity& identity, const char* step) {
    const auto handle = std::exchange(identity.process, nullptr);
    const auto closed = handle && CloseHandle(handle) != FALSE;
    const auto error = closed ? ERROR_SUCCESS : GetLastError();
    emit("explorer.handle-closed", "\"step\":" + quote(step) + ",\"ok\":" + boolean(closed) + ",\"win32Error\":" + std::to_string(error));
    require(closed, "CloseHandle Explorer failed");
  }
  void confirm_same_explorer(const char* step, std::size_t index) {
    auto current = capture_explorer(step);
    const auto same = same_identity(*explorers_[index], current);
    emit("explorer.continuity", "\"step\":" + quote(step) + ",\"sameIdentity\":" + boolean(same));
    close_snapshot(current, step);
    require(same, "Unexpected Explorer identity outside a restart stage");
  }
  void confirm_restart(const explorer_identity& previous, const explorer_identity& current) {
    const auto old_wait = WaitForSingleObject(previous.process, 0);
    const auto wait_error = old_wait == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
    const auto different = !same_identity(previous, current);
    const auto same_image = _wcsicmp(previous.image.c_str(), current.image.c_str()) == 0;
    emit("explorer.restart-observed", "\"oldPid\":" + std::to_string(previous.pid) +
        ",\"oldBirthFileTime\":" + quote(std::to_string(previous.birth)) + ",\"oldWaitResult\":" + std::to_string(old_wait) +
        ",\"oldWaitError\":" + std::to_string(wait_error) +
        ",\"oldExited\":" + boolean(old_wait == WAIT_OBJECT_0) + ",\"differentIdentity\":" + boolean(different) +
        ",\"sameImagePath\":" + boolean(same_image) + ",\"sourceVerified\":false,\"externalRestartProofRequired\":true");
    require(old_wait == WAIT_OBJECT_0 && different && same_image, "Old Explorer exit and new identity not confirmed");
  }
  void request_exit(const char* reason, bool success) {
    if (!success) failed_ = true;
    if (exit_requested_) return;
    exit_requested_ = true;
    const auto quit_code = success && !failed_ ? 0 : 1;
    emit("exit.requested", "\"reason\":" + quote(reason) + ",\"successRequested\":" + boolean(success) +
        ",\"failureLatched\":" + boolean(failed_) + ",\"requestedQuitCode\":" + std::to_string(quit_code) + ",\"finalExitPendingCleanup\":true");
    PostQuitMessage(quit_code); // Cleanup occurs only after SDK calls and callbacks return.
  }
  void taskbar_message(WPARAM wparam, LPARAM lparam) {
    ++taskbar_count_;
    emit("taskbar.received", "\"count\":" + std::to_string(taskbar_count_) + ",\"sourceVerified\":false");
    require(!in_tray_call_ && !exit_requested_ &&
        ((phase_ == phase::first_restart && taskbar_count_ == 1) || (phase_ == phase::second_restart && taskbar_count_ == 2)),
        "Unexpected or reentrant TaskbarCreated; stopping without retry");
    const auto index = static_cast<std::size_t>(taskbar_count_);
    auto current = std::make_unique<explorer_identity>(capture_explorer(index == 1 ? "first-restart.before-dispatch" : "second-restart.before-dispatch"));
    confirm_restart(*explorers_[index - 1], *current);
    in_tray_call_ = true;
    const auto result = tray_->dispatch_message(taskbar_created_, wparam, lparam);
    in_tray_call_ = false;
    response("tray.taskbar-dispatched", result.response, "\"handled\":" + std::string(boolean(result.handled)) +
        ",\"stage\":" + std::to_string(taskbar_count_) + ",\"sourceVerified\":false");
    require(!exit_requested_ && result.handled && result.response.ok(), "SDK TaskbarCreated dispatch failed");
    explorers_[index] = std::move(current);
    if (index == 1) {
      require(resident(), "First Explorer restart did not recover installed/desired clean state");
      phase_ = phase::removal;
      EnableWindow(remove_button_, TRUE);
      require(IsWindowEnabled(remove_button_) != FALSE, "Remove button did not enable");
      require(SetWindowTextW(status_, L"恢复已确认。请点击“移除图标”，然后第二次重启 Explorer。") != FALSE, "Status update failed");
      emit("stage.recovery-confirmed", state() + ",\"removeEnabled\":true");
    } else {
      require(clean(), "Removed tray icon resurrected after Explorer restart");
      settle_until_ = clock_type::now() + std::chrono::milliseconds(500);
      phase_ = phase::settling;
      emit("stage.no-resurrection", state() + ",\"settleMs\":500,\"finishEnabled\":false");
    }
  }
  void command(WPARAM wparam, LPARAM lparam) {
    if (HIWORD(wparam) != BN_CLICKED) return;
    if (LOWORD(wparam) == remove_button_id || LOWORD(wparam) == finish_button_id)
      require(!exit_requested_, "Button command after exit was requested");
    if (LOWORD(wparam) == remove_button_id) {
      require(reinterpret_cast<HWND>(lparam) == remove_button_ && phase_ == phase::removal &&
          remove_calls_ == 0 && resident() && IsWindowEnabled(remove_button_), "Out-of-phase remove command");
      EnableWindow(remove_button_, FALSE);
      require(IsWindowEnabled(remove_button_) == FALSE, "Remove button did not disable");
      confirm_same_explorer("remove.before-call", 1);
      ++remove_calls_;
      in_tray_call_ = true;
      const auto result = tray_->remove();
      in_tray_call_ = false;
      response("tray.remove", result, "\"call\":1");
      require(!exit_requested_ && result.ok() && clean(), "Explicit tray removal not confirmed");
      phase_ = phase::second_restart;
      require(SetWindowTextW(status_, L"图标已移除。等待第二次真实 Explorer 重启。") != FALSE, "Status update failed");
      emit("stage.await-second-restart", state());
    } else if (LOWORD(wparam) == finish_button_id) {
      require(reinterpret_cast<HWND>(lparam) == finish_button_ && phase_ == phase::finish && clean() &&
          taskbar_count_ == 2 && remove_calls_ == 1 && IsWindowEnabled(finish_button_), "Out-of-phase finish command");
      confirm_same_explorer("finish.before-close", 2);
      finished_ = true;
      request_exit("all-recovery-stages-complete", true);
    }
  }
  void timer() {
    if (expired()) { request_exit("whole-deadline", false); return; }
    if (phase_ == phase::settling && clock_type::now() >= settle_until_) {
      require(clean(), "Tray state changed during 500ms settle");
      confirm_same_explorer("settle.before-finish-enable", 2);
      phase_ = phase::finish;
      EnableWindow(finish_button_, TRUE);
      require(IsWindowEnabled(finish_button_) != FALSE, "Finish button did not enable");
      require(SetWindowTextW(status_, L"第二次重启后仍未复活。请点击“结束验证”。") != FALSE, "Status update failed");
      emit("stage.settled", state() + ",\"finishEnabled\":true");
    }
  }
  static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<application*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
      SetLastError(ERROR_SUCCESS);
      const auto previous = SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
      if (!previous && GetLastError() != ERROR_SUCCESS) {
        self->emit("owner.userdata-failed", "\"win32Error\":" + std::to_string(GetLastError()));
        self->failed_ = true;
        return FALSE;
      }
    }
    if (!self) return DefWindowProcW(window, message, wparam, lparam);
    // The first unexpected event is recorded before fail-stop. Quiesce further
    // synchronous input without destroying the owner inside an active SDK call.
    if (self->exit_requested_ && self->failed_ && message != WM_DESTROY && message != WM_NCDESTROY) return 0;
    try {
      if (self->expired() && !self->cleaning_) { self->request_exit("whole-deadline", false); return 0; }
      if (self->taskbar_created_ && message == self->taskbar_created_) { self->taskbar_message(wparam, lparam); return 0; }
      if (message == WM_COMMAND) { self->command(wparam, lparam); return 0; }
      if (message == WM_TIMER && wparam == timer_id) { self->timer(); return 0; }
      if (message == WM_CLOSE) { self->request_exit("early-owner-close", false); return 0; }
      if (message == WM_DESTROY && !self->cleaning_) self->request_exit("unexpected-owner-destruction", false);
      // Icon activation/menu/focus are outside this fixture; no corresponding SDK call is made.
      if (message == callback_message) return 0;
    } catch (const std::exception& error) {
      self->in_tray_call_ = false;
      self->emit("stage.error", "\"message\":" + quote(error.what()) + ",\"win32Error\":" + std::to_string(GetLastError()) + ",\"proofKnown\":false");
      self->request_exit("stage-failed", false);
      return 0;
    } catch (...) { self->in_tray_call_ = false; self->request_exit("unknown-callback-failure", false); return 0; }
    return DefWindowProcW(window, message, wparam, lparam);
  }
  void cleanup() noexcept {
    cleaning_ = true;
    bool ok = true;
    try {
      if (timer_active_) { const auto stopped = KillTimer(window_, timer_id) != FALSE; emit("timer.stopped", "\"ok\":" + std::string(boolean(stopped))); ok = stopped && ok; timer_active_ = false; }
      if (tray_) {
        if (remove_calls_ == 0) {
          ++remove_calls_;
          const auto removed = tray_->remove();
          response("tray.cleanup-remove", removed, "\"call\":1,\"earlyFailureCleanup\":true");
          ok = removed.ok() && ok;
        }
        const auto confirmed = clean();
        emit("tray.cleanup-state", state() + ",\"confirmed\":" + boolean(confirmed));
        ok = confirmed && ok;
        tray_.reset();
        emit("service.destroyed", "\"beforeOwnerDestruction\":true");
      }
      if (window_) {
        const auto destroyed = DestroyWindow(window_) != FALSE;
        emit("owner.destroyed", "\"ok\":" + std::string(boolean(destroyed)));
        ok = destroyed && ok;
        window_ = nullptr;
      } else ok = false;
      for (auto& explorer : explorers_) if (explorer) close_snapshot(*explorer, "final-cleanup");
      if (class_registered_) {
        const auto unregistered = UnregisterClassW(window_class, GetModuleHandleW(nullptr)) != FALSE;
        emit("owner.class-unregistered", "\"ok\":" + std::string(boolean(unregistered)));
        ok = unregistered && ok;
        class_registered_ = false;
      }
    } catch (const std::exception& error) { emit("cleanup.error", "\"message\":" + quote(error.what())); ok = false; }
    catch (...) { emit("cleanup.error", "\"message\":\"Unknown cleanup exception\""); ok = false; }
    cleanup_confirmed_ = ok;
    if (!ok) failed_ = true;
  }

  const clock_type::time_point started_{clock_type::now()};
  const clock_type::time_point deadline_{started_ + whole_budget};
  clock_type::time_point settle_until_{};
  std::uint64_t own_birth_{};
  std::uint64_t sequence_{};
  std::array<std::unique_ptr<explorer_identity>, 3> explorers_;
  std::unique_ptr<soui::runtime::tray_service> tray_;
  HWND window_{}, status_{}, remove_button_{}, finish_button_{};
  UINT taskbar_created_{};
  phase phase_{phase::setup};
  unsigned taskbar_count_{}, remove_calls_{};
  int exit_code_{1};
  bool class_registered_{}, timer_active_{}, in_tray_call_{}, exit_requested_{}, finished_{}, cleaning_{}, cleanup_confirmed_{}, log_failed_{}, failed_{};
};
} // namespace

int main() { return application{}.run(); }
