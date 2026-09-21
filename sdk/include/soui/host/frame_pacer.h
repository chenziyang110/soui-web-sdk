#pragma once

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <thread>
#pragma comment(lib, "winmm.lib")

namespace soui::sdk {
inline constexpr UINT frame_pulse_message = WM_APP + 0x352;

// The timer thread never calls SOUI, Miniblink, or JS. At most one notification
// can be outstanding; late frames are skipped rather than queued for catch-up.
class frame_pacer final {
 public:
  frame_pacer() = default;
  frame_pacer(const frame_pacer&) = delete;
  frame_pacer& operator=(const frame_pacer&) = delete;
  ~frame_pacer() { stop(); }

  bool start(HWND window, unsigned target, bool prefer_high_resolution = true) {
    stop();
    if (!window || (target != 60 && target != 120 && target != 144)) return false;
    LARGE_INTEGER frequency{}, now{};
    if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&now)) return false;
    window_ = window;
    step_ = frequency.QuadPart / target;
    if (step_ <= 0) return false;
    next_ = now.QuadPart + step_;
    frequency_ = frequency.QuadPart;
    // Our precise wake does not improve the engine's own legacy worker timers.
    // Keep this scoped hint balanced for both backends; OS policy may ignore it.
    precision_ = timeBeginPeriod(1) == TIMERR_NOERROR;
    // The API exists on Vista/Win7, but this flag is supported only from
    // Windows 10 1803. A rejected flag falls back without changing the kernel.
    constexpr DWORD high_resolution_flag = 0x00000002;
    waitable_ = prefer_high_resolution ? CreateWaitableTimerExW(nullptr, nullptr, high_resolution_flag,
                                      TIMER_MODIFY_STATE | SYNCHRONIZE) : nullptr;
    if (waitable_) {
      stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
      if (stop_event_) {
        try {
          worker_ = std::thread([this] { wait_loop(); });
          backend_ = 2;
          target_ = target;
          return true;
        } catch (...) { /* Fall back to the legacy queue without a worker. */ }
      }
      if (stop_event_) { CloseHandle(stop_event_); stop_event_ = nullptr; }
      CloseHandle(waitable_); waitable_ = nullptr;
    }
    if (!precision_) { stop(); return false; }
    if (!CreateTimerQueueTimer(&timer_, nullptr, &on_timer, this, 2, 2, WT_EXECUTEINTIMERTHREAD)) {
      stop(); return false;
    }
    target_ = target;
    backend_ = 1;
    return true;
  }
  void stop() noexcept {
    if (stop_event_) SetEvent(stop_event_);
    if (worker_.joinable()) worker_.join();
    if (waitable_) { CancelWaitableTimer(waitable_); CloseHandle(waitable_); waitable_ = nullptr; }
    if (stop_event_) { CloseHandle(stop_event_); stop_event_ = nullptr; }
    if (timer_) { DeleteTimerQueueTimer(nullptr, timer_, INVALID_HANDLE_VALUE); timer_ = nullptr; }
    if (precision_) { timeEndPeriod(1); precision_ = false; }
    target_ = 0;
    backend_ = 0;
    clock_failed_.store(false);
    pending_.store(false, std::memory_order_relaxed);
    // Drain our old coalesced pulse before a new pacing generation starts.
    if (window_) { MSG message{}; while (PeekMessageW(&message, window_, frame_pulse_message, frame_pulse_message, PM_REMOVE)) {} }
    window_ = nullptr;
  }
  [[nodiscard]] unsigned target() const noexcept { return clock_failed_.load() ? 0 : target_; }
  static unsigned query_display_refresh(HWND window) noexcept {
    char override_value[8] = {};
    const DWORD override_length = GetEnvironmentVariableA("SOUI_DISPLAY_REFRESH_OVERRIDE",
                                                           override_value, sizeof(override_value));
    if (override_length && override_length < sizeof(override_value)) {
      const unsigned requested = static_cast<unsigned>(strtoul(override_value, nullptr, 10));
      if (requested == 60 || requested == 120 || requested == 144)
        return requested;
    }
    if (!window) return 0;
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    if (!monitor) return 0;
    MONITORINFOEXW monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    if (!GetMonitorInfoW(monitor, &monitor_info)) return 0;
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsExW(monitor_info.szDevice, ENUM_CURRENT_SETTINGS, &mode, 0)) return 0;
    const DWORD value = mode.dmDisplayFrequency;
    return value >= 30 && value <= 1000 ? static_cast<unsigned>(value) : 0;
  }
  [[nodiscard]] bool failed() const noexcept { return clock_failed_.load(); }
  [[nodiscard]] unsigned backend() const noexcept { return backend_; }
  void acknowledge() noexcept { pending_.store(false, std::memory_order_relaxed); }
#ifdef SOUI_BRIDGE_BENCHMARK
  struct counters { std::uint64_t wakes, due, coalesced, missed; unsigned backend; };
  [[nodiscard]] counters diagnostic_counters() const noexcept {
    return {wakes_.load(), due_.load(), coalesced_.load(), missed_.load(), backend_};
  }
#endif

 private:
  void wait_loop() noexcept {
    HANDLE handles[]{stop_event_, waitable_};
    for (;;) {
      if (WaitForSingleObject(stop_event_, 0) == WAIT_OBJECT_0) return;
      LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
      const auto remaining = next_ - now.QuadPart;
      if (remaining <= 0) { on_timer(this, FALSE); continue; }
      LARGE_INTEGER due{};
      due.QuadPart = -((remaining * 10000000 + frequency_ - 1) / frequency_);
      if (!SetWaitableTimer(waitable_, &due, 0, nullptr, nullptr, FALSE)) { clock_failed_.store(true); return; }
      const auto result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
      if (result == WAIT_OBJECT_0) return;
      if (result != WAIT_OBJECT_0 + 1) { clock_failed_.store(true); return; }
      on_timer(this, FALSE);
    }
  }
  static VOID CALLBACK on_timer(PVOID context, BOOLEAN) {
    auto& self = *static_cast<frame_pacer*>(context);
#ifdef SOUI_BRIDGE_BENCHMARK
    self.wakes_.fetch_add(1, std::memory_order_relaxed);
#endif
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    if (now.QuadPart < self.next_) return;
    const auto elapsed_steps = (now.QuadPart - self.next_) / self.step_ + 1;
#ifdef SOUI_BRIDGE_BENCHMARK
    self.due_.fetch_add(1, std::memory_order_relaxed);
    self.missed_.fetch_add(elapsed_steps - 1, std::memory_order_relaxed);
#endif
    self.next_ += elapsed_steps * self.step_;
    const bool pending = self.pending_.exchange(true, std::memory_order_relaxed);
#ifdef SOUI_BRIDGE_BENCHMARK
    if (pending) self.coalesced_.fetch_add(1, std::memory_order_relaxed);
#endif
    if (!pending &&
        !PostMessageW(self.window_, frame_pulse_message, 0, 0)) {
      self.pending_.store(false, std::memory_order_relaxed);
    }
  }
  HWND window_{nullptr};
  HANDLE timer_{nullptr};
  HANDLE waitable_{nullptr}, stop_event_{nullptr};
  std::thread worker_;
  std::atomic<bool> clock_failed_{false};
  std::atomic<bool> pending_{false};
  std::int64_t step_{1}, next_{0}, frequency_{1};
  unsigned target_{0}, backend_{0};
  bool precision_{false};
#ifdef SOUI_BRIDGE_BENCHMARK
  std::atomic<std::uint64_t> wakes_{0}, due_{0}, coalesced_{0}, missed_{0};
#endif
};
}  // namespace soui::sdk
#endif
