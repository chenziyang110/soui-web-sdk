#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <cstddef>
#include <cstdint>

// External native-STA diagnostic only. Same wire layout as burst-page.js.
// It is not an SDK renderer or a physical-presentation completion marker.
namespace soui::benchmark::native_burst_marker {
constexpr int x = 8, y = 8, width = 64, height = 36;
using bytes = std::array<std::uint8_t, 16>;
using pixels = std::array<std::uint32_t, width * height>;

inline bool encode(std::uint32_t nonce, std::uint32_t ack, std::uint32_t frame,
    std::uint32_t delivered, bytes& result) noexcept {
  result.fill(0);
  if (ack > 100000 || delivered > ack || frame == 0 || frame > 65535) return false;
  result[0] = 167; result[1] = 83;
  // nonce=0 intentionally produces the same fault marker as the reference page;
  // the existing independent decoder rejects it, never treating it as success.
  for (unsigned i = 0; i < 4; ++i) result[2 + i] = static_cast<std::uint8_t>(nonce >> (8 * i));
  for (unsigned i = 0; i < 3; ++i) result[6 + i] = static_cast<std::uint8_t>(ack >> (8 * i));
  result[9] = static_cast<std::uint8_t>(frame);
  result[10] = static_cast<std::uint8_t>(frame >> 8);
  result[11] = static_cast<std::uint8_t>(1U | ((delivered >> 16) << 4));
  result[14] = static_cast<std::uint8_t>(delivered);
  result[15] = static_cast<std::uint8_t>(delivered >> 8);
  std::uint16_t crc = 65535;
  for (unsigned i = 0; i < 16; ++i) {
    if (i == 12 || i == 13) continue;
    crc = static_cast<std::uint16_t>(crc ^ (static_cast<unsigned>(result[i]) << 8));
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = static_cast<std::uint16_t>((crc << 1) ^ ((crc & 32768) ? 4129 : 0));
  }
  result[12] = static_cast<std::uint8_t>(crc);
  result[13] = static_cast<std::uint8_t>(crc >> 8);
  return true;
}

inline void raster(const bytes& marker, std::uint32_t ack, pixels& result) noexcept {
  // BI_RGB DWORDs are B,G,R,unused on the supported Windows x86/x64 targets.
  // The reference content probe encodes low/middle/high ACK bytes as R/G/B.
  const auto colour = ((ack & 255U) << 16) | (ack & 65280U) | ((ack >> 16) & 255U);
  result.fill(colour);
  for (unsigned bit = 0; bit < 128; ++bit) {
    const auto value = (marker[bit / 8] & (1U << (bit % 8))) ? 0x00ffffffU : 0U;
    for (unsigned dy = 0; dy < 4; ++dy)
      for (unsigned dx = 0; dx < 4; ++dx)
        result[((bit / 16) * 4 + dy) * width + (bit % 16) * 4 + dx] = value;
  }
  // Bottom four rows retain the same ACK, including client content probe (10,42).
}

inline bool draw(HDC target, std::uint32_t nonce, std::uint32_t ack,
    std::uint32_t frame, std::uint32_t delivered) noexcept {
  bytes marker{};
  if (!target || !encode(nonce, ack, frame, delivered, marker)) return false;
  pixels image{}; // Fixed 9216 bytes, no heap or retained GDI object.
  raster(marker, ack, image);
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  info.bmiHeader.biSizeImage = static_cast<DWORD>(sizeof(image));
  // Normal WM_PAINT/MM_TEXT client DC only. One call covers both marker and
  // content pixel; a successful return is not atomic/visible/physical proof.
  // https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-setdibitstodevice
  return SetDIBitsToDevice(target, x, y, width, height, 0, 0, 0, height,
      image.data(), &info, DIB_RGB_COLORS) == height;
}
} // namespace soui::benchmark::native_burst_marker
