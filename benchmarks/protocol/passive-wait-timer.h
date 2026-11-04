#pragma once
// Private InputTrace wait primitive, shared with the no-window CPU fixture.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "passive-observation.h"

namespace soui::benchmark {
// One reusable timer belongs only to this observer. Unlike frame_pacer::start,
// it never requests a process/system timer-resolution change.
struct passive_wait_timer {
  HANDLE timer{};
  bool high_resolution{}, armed{};
  passive_wait_timer() = default;
  passive_wait_timer(const passive_wait_timer&) = delete;
  passive_wait_timer& operator=(const passive_wait_timer&) = delete;
  void create(bool prefer_high_resolution = true) {
    passive_require(timer == nullptr, "wait-timer-failed");
    using create_fn = HANDLE (WINAPI*)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
    const auto create_timer = reinterpret_cast<create_fn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateWaitableTimerExW"));
    if (prefer_high_resolution && create_timer) timer = create_timer(nullptr, nullptr, 0x00000002, TIMER_MODIFY_STATE | SYNCHRONIZE);
    high_resolution = timer != nullptr;
    if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    passive_require(timer != nullptr, "wait-timer-failed");
  }
  void arm(std::uint64_t ticks, std::uint64_t frequency) {
    std::int64_t relative{};
    passive_require(passive_relative_due_100ns(ticks, frequency, relative), "wait-timer-failed");
    LARGE_INTEGER due{}; due.QuadPart = relative;
    passive_require(SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) != FALSE, "wait-timer-failed");
    armed = true;
  }
  void cancel() {
    if (!armed) return;
    const bool cancelled = CancelWaitableTimer(timer) != FALSE;
    armed = false;
    passive_require(cancelled, "wait-timer-failed");
  }
  void close() {
    if (!timer) return;
    const HANDLE closing = timer; timer = nullptr;
    const bool cancelled = !armed || CancelWaitableTimer(closing) != FALSE;
    armed = false;
    const bool closed = CloseHandle(closing) != FALSE;
    passive_require(cancelled && closed, "wait-timer-close-failed");
  }
  ~passive_wait_timer() {
    // Exceptional unwinding cannot throw. Callers explicitly check close()
    // before every success; close() detaches before either Win32 close result.
    if (timer) { if (armed) CancelWaitableTimer(timer); CloseHandle(timer); }
  }
  const char* backend() const { return high_resolution ? "high-resolution-waitable" : "standard-waitable"; }
};

template<class Clock, class Pump>
void passive_wait_until(HANDLE process, Clock& clock, passive_wait_timer& timer,
                        std::uint64_t until, std::uint64_t frequency, Pump&& pump) {
  for (;;) {
    pump(); passive_require(WaitForSingleObject(process, 0) == WAIT_TIMEOUT, "target-closed");
    const auto now = clock.read();
    if (now >= until) { timer.cancel(); return; }
    timer.arm(until - now, frequency);
    const HANDLE handles[]{process, timer.timer};
    const DWORD result = MsgWaitForMultipleObjectsEx(2, handles, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    passive_require(result != WAIT_OBJECT_0, "target-closed");
    passive_require(result == WAIT_OBJECT_0 + 1 || result == WAIT_OBJECT_0 + 2, "wait-timer-failed");
    if (result == WAIT_OBJECT_0 + 1) timer.armed = false; // Auto-reset one-shot signal was consumed.
    // Message wakes never move the original QPC deadline or skip its checks.
  }
}
} // namespace soui::benchmark
