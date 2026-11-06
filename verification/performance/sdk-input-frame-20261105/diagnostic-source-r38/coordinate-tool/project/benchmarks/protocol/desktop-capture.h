#pragma once

#define NOMINMAX
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace soui::benchmark {

enum class desktop_capture_backend { dxgi, gdi_bitblt };

inline const char* desktop_capture_backend_name(desktop_capture_backend backend) noexcept {
  return backend == desktop_capture_backend::gdi_bitblt ? "gdi-bitblt" : "dxgi";
}

inline desktop_capture_backend parse_desktop_capture_backend(const wchar_t* value) {
  if (!value || std::wstring(value) == L"dxgi") return desktop_capture_backend::dxgi;
  if (std::wstring(value) == L"gdi-bitblt") return desktop_capture_backend::gdi_bitblt;
  throw std::runtime_error("Capture backend must be dxgi or gdi-bitblt");
}

inline bool desktop_output_has_identity_rotation(const wchar_t* device_name) {
  if (!device_name || !*device_name) return false;
  DEVMODEW mode{}; mode.dmSize = sizeof(mode);
  return EnumDisplaySettingsW(device_name, ENUM_CURRENT_SETTINGS, &mode) != FALSE &&
      (mode.dmFields & DM_DISPLAYORIENTATION) != 0 && mode.dmDisplayOrientation == DMDO_DEFAULT;
}

inline bool desktop_cursor_overlaps(int x, int y, unsigned width, unsigned height) {
  CURSORINFO cursor{}; cursor.cbSize = sizeof(cursor);
  if (!GetCursorInfo(&cursor)) throw std::runtime_error("cursor-unavailable");
  if (!(cursor.flags & CURSOR_SHOWING)) return false;
  ICONINFO icon{};
  if (!GetIconInfo(cursor.hCursor, &icon)) throw std::runtime_error("cursor-unavailable");
  struct bitmaps {
    HBITMAP color, mask;
    ~bitmaps() { if (color) DeleteObject(color); if (mask) DeleteObject(mask); }
  } cleanup{icon.hbmColor, icon.hbmMask};
  BITMAP bitmap{};
  if (!GetObjectW(icon.hbmColor ? icon.hbmColor : icon.hbmMask, sizeof(bitmap), &bitmap) ||
      bitmap.bmWidth <= 0 || bitmap.bmHeight <= 0)
    throw std::runtime_error("cursor-unavailable");
  const auto cursor_width = static_cast<std::int64_t>(bitmap.bmWidth);
  const auto cursor_height = static_cast<std::int64_t>(icon.hbmColor ? bitmap.bmHeight : bitmap.bmHeight / 2);
  if (!cursor_height) throw std::runtime_error("cursor-unavailable");
  const auto left = static_cast<std::int64_t>(cursor.ptScreenPos.x) - icon.xHotspot;
  const auto top = static_cast<std::int64_t>(cursor.ptScreenPos.y) - icon.yHotspot;
  return left < static_cast<std::int64_t>(x) + width && left + cursor_width > x &&
      top < static_cast<std::int64_t>(y) + height && top + cursor_height > y;
}

// A synchronous desktop-ROI reader. GDI's BI_RGB alpha byte is undefined, so
// canonicalize it to zero before consumers fingerprint the BGRA buffer.
class gdi_screen_roi_capture final {
 public:
  gdi_screen_roi_capture(unsigned width, unsigned height) : width_(width), height_(height) {
    if (!width_ || !height_ || width_ > 4096 || height_ > 8192 ||
        static_cast<std::uint64_t>(width_) * height_ > (64U * 1024U * 1024U) / 4U)
      throw std::runtime_error("capture-failed");
    screen_ = GetDC(nullptr);
    if (!screen_) throw std::runtime_error("capture-failed");
    memory_ = CreateCompatibleDC(screen_);
    if (!memory_) { close(); throw std::runtime_error("capture-failed"); }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(width_);
    info.bmiHeader.biHeight = -static_cast<LONG>(height_);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    bitmap_ = CreateDIBSection(screen_, &info, DIB_RGB_COLORS, &pixels_, nullptr, 0);
    if (!bitmap_ || !pixels_) { close(); throw std::runtime_error("capture-failed"); }
    previous_ = SelectObject(memory_, bitmap_);
    if (!previous_ || previous_ == HGDI_ERROR) { previous_ = nullptr; close(); throw std::runtime_error("capture-failed"); }
  }
  ~gdi_screen_roi_capture() { close(); }
  gdi_screen_roi_capture(const gdi_screen_roi_capture&) = delete;
  gdi_screen_roi_capture& operator=(const gdi_screen_roi_capture&) = delete;

  void capture(int x, int y) {
    if (!BitBlt(memory_, 0, 0, static_cast<int>(width_), static_cast<int>(height_),
                screen_, x, y, SRCCOPY | CAPTUREBLT) || !GdiFlush())
      throw std::runtime_error("capture-failed");
    auto* bytes = static_cast<unsigned char*>(pixels_);
    for (std::size_t offset = 3; offset < byte_count(); offset += 4) bytes[offset] = 0;
  }
  const void* pixels() const noexcept { return pixels_; }
  std::size_t pitch() const noexcept { return static_cast<std::size_t>(width_) * 4; }

 private:
  std::size_t byte_count() const noexcept { return static_cast<std::size_t>(width_) * height_ * 4; }
  void close() noexcept {
    if (memory_ && previous_) SelectObject(memory_, previous_);
    previous_ = nullptr;
    if (bitmap_) DeleteObject(bitmap_);
    bitmap_ = nullptr; pixels_ = nullptr;
    if (memory_) DeleteDC(memory_);
    memory_ = nullptr;
    if (screen_) ReleaseDC(nullptr, screen_);
    screen_ = nullptr;
  }

  unsigned width_{}, height_{};
  HDC screen_{}, memory_{};
  HBITMAP bitmap_{};
  HGDIOBJ previous_{};
  void* pixels_{};
};

} // namespace soui::benchmark
