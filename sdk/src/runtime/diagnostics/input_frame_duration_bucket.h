#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#endif

namespace soui::sdk::diagnostics::input_frame_trace::detail {

// The 64-bucket wire contract is zero for 0/1, floor(log2(ticks)) otherwise.
// Keep this fallback bounded independently of the measured duration.
constexpr std::size_t duration_bucket_portable(std::uint64_t ticks) noexcept {
  std::size_t bucket = 0;
  if (ticks >= (std::uint64_t{1} << 32)) { ticks >>= 32; bucket += 32; }
  if (ticks >= (std::uint64_t{1} << 16)) { ticks >>= 16; bucket += 16; }
  if (ticks >= (std::uint64_t{1} << 8)) { ticks >>= 8; bucket += 8; }
  if (ticks >= (std::uint64_t{1} << 4)) { ticks >>= 4; bucket += 4; }
  if (ticks >= (std::uint64_t{1} << 2)) { ticks >>= 2; bucket += 2; }
  if (ticks >= 2) ++bucket;
  return bucket;
}

inline std::size_t duration_bucket_index(std::uint64_t ticks) noexcept {
#if defined(_MSC_VER) && defined(_M_X64)
  if (!ticks) return 0;
  unsigned long index = 0;
  _BitScanReverse64(&index, ticks);
  return static_cast<std::size_t>(index);
#elif defined(_MSC_VER) && defined(_M_IX86)
  unsigned long index = 0;
  const auto high = static_cast<unsigned long>(ticks >> 32);
  if (high) {
    _BitScanReverse(&index, high);
    return static_cast<std::size_t>(index) + 32;
  }
  const auto low = static_cast<unsigned long>(ticks);
  if (!low) return 0;
  _BitScanReverse(&index, low);
  return static_cast<std::size_t>(index);
#else
  return duration_bucket_portable(ticks);
#endif
}

} // namespace soui::sdk::diagnostics::input_frame_trace::detail
