#pragma once

#include <cstdint>

namespace soui::runtime {
// UI commands carry only values or numeric request IDs, never borrowed pointers.
inline constexpr unsigned window_command_message = 0x84D4U;
enum class window_command : std::uintptr_t { begin_drag = 1, minimize, toggle_maximize, set_title };
}

#ifdef _WIN32
#include <windows.h>
namespace soui::runtime {
[[nodiscard]] bool execute_pending_title_command(HWND window, std::uintptr_t request_id);
inline LRESULT resize_border_hit(HWND window, LPARAM screen_position) {
  if (IsZoomed(window) || !(GetWindowLongPtrW(window, GWL_STYLE) & WS_THICKFRAME)) return HTCLIENT;
  POINT point{static_cast<short>(LOWORD(screen_position)), static_cast<short>(HIWORD(screen_position))};
  ScreenToClient(window, &point);
  RECT bounds{};
  GetClientRect(window, &bounds);
  if (!PtInRect(&bounds, point)) return HTNOWHERE;
  using dpi_function = UINT(WINAPI*)(HWND);
  static const auto get_dpi = reinterpret_cast<dpi_function>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
  const UINT dpi = get_dpi ? get_dpi(window) : 96U;
  const int border = MulDiv(6, static_cast<int>(dpi ? dpi : 96U), 96);
  const bool left = point.x < border, right = point.x >= bounds.right - border;
  const bool top = point.y < border, bottom = point.y >= bounds.bottom - border;
  if (top) return left ? HTTOPLEFT : right ? HTTOPRIGHT : HTTOP;
  if (bottom) return left ? HTBOTTOMLEFT : right ? HTBOTTOMRIGHT : HTBOTTOM;
  if (left) return HTLEFT;
  if (right) return HTRIGHT;
  return HTCLIENT;
}

// Must run in the HWND-owning thread, never under a Bridge/engine lock.
inline bool execute_window_command(HWND window, window_command command, LPARAM payload = 0) {
  if (!IsWindow(window) || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId())
    return false;
  switch (command) {
    case window_command::begin_drag:
      ReleaseCapture();
      SendMessageW(window, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
      return true;
    case window_command::minimize:
      ShowWindow(window, SW_MINIMIZE);
      return true;
    case window_command::toggle_maximize:
      ShowWindow(window, IsZoomed(window) ? SW_RESTORE : SW_MAXIMIZE);
      return true;
    case window_command::set_title:
      return execute_pending_title_command(window, static_cast<std::uintptr_t>(payload));
  }
  return false;
}
}
#endif
