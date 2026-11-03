#pragma once

#include "secure_random.h"

#include <array>
#include <stdexcept>
#include <string>

namespace soui::runtime::detail {

// RFC 4122 / RFC 9562 version 4, in canonical network-byte-order text.
// Callers supply random bytes; setting version/variant leaves 122 random bits.
inline std::string format_uuid_v4(std::array<unsigned char, 16> bytes) {
  bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0fU) | 0x40U);
  bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3fU) | 0x80U);
  constexpr char hex[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(36);
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    if (index == 4 || index == 6 || index == 8 || index == 10) result += '-';
    result += hex[bytes[index] >> 4U];
    result += hex[bytes[index] & 0x0fU];
  }
  return result;
}

inline std::string random_uuid_v4() {
  std::array<unsigned char, 16> bytes{};
  if (!secure_random(bytes.data(), bytes.size())) throw std::runtime_error("UUID entropy unavailable");
  return format_uuid_v4(bytes);
}

}  // namespace soui::runtime::detail
