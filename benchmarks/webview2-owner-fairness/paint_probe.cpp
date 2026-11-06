#include "paint_probe.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace owner_fairness {
namespace {
constexpr wchar_t probe_class[] = L"SouiOwnerFairnessPaintProbe";
constexpr UINT_PTR timer_id = 71;
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
}
std::uint64_t paint_probe::now() {
  LARGE_INTEGER tick{};
  if (!QueryPerformanceCounter(&tick) || tick.QuadPart <= 0) {
    value_.fault = true; return 0;
  }
  return static_cast<std::uint64_t>(tick.QuadPart);
}
void paint_probe::increment(std::uint64_t& count) {
  if (count == (std::numeric_limits<std::uint64_t>::max)()) value_.fault = true;
  else ++count;
}
void paint_probe::create(HWND parent, HINSTANCE instance) {
  LARGE_INTEGER frequency{};
  require(QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0, "probe QPC frequency");
  value_.frequency = static_cast<std::uint64_t>(frequency.QuadPart);
  instance_ = instance;
  WNDCLASSW type{}; type.hInstance = instance; type.lpfnWndProc = procedure; type.lpszClassName = probe_class;
  require(RegisterClassW(&type) != 0, "probe RegisterClass");
  window_ = CreateWindowExW(0, probe_class, L"Native paint progress", WS_CHILD | WS_VISIBLE,
      0, 288, 480, 32, parent, nullptr, instance, this);
  require(window_ != nullptr, "probe CreateWindow");
  require(SetTimer(window_, timer_id, 20, nullptr) != 0, "probe SetTimer");
}
void paint_probe::invalidate() noexcept {
  try {
    const bool ok = InvalidateRect(window_, nullptr, FALSE) != FALSE;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ok) value_.fault = true;
    if (ok && value_.stage >= 1 && value_.stage <= 4)
      increment(value_.phases[value_.stage - 1].invalidations);
  } catch (...) { callback_fault_.store(true); }
}
std::uint64_t paint_probe::advance(unsigned next) {
  std::lock_guard<std::mutex> lock(mutex_);
  require(next == value_.stage + 1 && next <= 5, "native phase order differs");
  const auto tick = now();
  if (value_.stage) {
    auto& previous = value_.phases[value_.stage - 1];
    if (tick < previous.begin || tick < previous.last_paint || tick < previous.last_timer) value_.fault = true;
    else {
      previous.end = tick;
      previous.max_paint_gap = (std::max)(previous.max_paint_gap, tick - previous.last_paint);
      previous.max_timer_gap = (std::max)(previous.max_timer_gap, tick - previous.last_timer);
    }
  }
  value_.stage = next;
  if (next <= 4) {
    auto& phase = value_.phases[next - 1];
    phase.begin = phase.last_paint = phase.last_timer = tick;
  }
  stage_.store(next);
  return tick;
}
void paint_probe::note(bool paint) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (value_.stage < 1 || value_.stage > 4) return;
  auto& phase = value_.phases[value_.stage - 1];
  auto& last = paint ? phase.last_paint : phase.last_timer;
  auto& gap = paint ? phase.max_paint_gap : phase.max_timer_gap;
  const auto tick = now();
  if (tick < last) value_.fault = true;
  else { gap = (std::max)(gap, tick - last); last = tick; }
  increment(paint ? phase.paints : phase.timers);
}
void paint_probe::owner_event() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (value_.stage >= 1 && value_.stage <= 4) increment(value_.phases[value_.stage - 1].owner_events);
}
probe_snapshot paint_probe::snapshot() {
  std::lock_guard<std::mutex> lock(mutex_); auto result=value_;
  result.fault=result.fault || callback_fault_.load(); return result;
}
LRESULT CALLBACK paint_probe::procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* self = reinterpret_cast<paint_probe*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    self = static_cast<paint_probe*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  try {
    if (self && message == WM_PAINT) {
      self->note(true); // Dispatch observation, not a unique frame or completed present.
      PAINTSTRUCT paint{}; const auto dc = BeginPaint(window, &paint);
      const bool good = dc && FillRect(dc, &paint.rcPaint, reinterpret_cast<HBRUSH>(COLOR_HIGHLIGHT + 1));
      const bool ended = EndPaint(window, &paint) != FALSE;
      if (!good || !ended) self->callback_fault_.store(true);
      return 0;
    }
    if (self && message == WM_TIMER && wparam == timer_id) { self->note(false); return 0; }
  } catch (...) {
    if (self) self->callback_fault_.store(true);
    // The normal default procedure validates paint after an observer failure.
    // No C++ exception crosses the Win32 callback and no failed sample qualifies.
  }
  return DefWindowProcW(window, message, wparam, lparam);
}
void paint_probe::destroy() {
  require(KillTimer(window_, timer_id) != FALSE, "probe KillTimer");
  require(DestroyWindow(window_) != FALSE, "probe DestroyWindow"); window_ = nullptr;
  require(UnregisterClassW(probe_class, instance_) != FALSE, "probe UnregisterClass");
}
} // namespace owner_fairness
