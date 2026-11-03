#pragma once

#include <string_view>

namespace soui::runtime::detail {

// Validate Unicode scalar encodings without normalization or replacement.
// Embedded NUL is valid UTF-8; each caller applies its own text-field policy.
inline bool valid_utf8(std::string_view value) noexcept {
  std::size_t offset = 0;
  while (offset < value.size()) {
    const auto first = static_cast<unsigned char>(value[offset++]);
    if (first <= 0x7fU) continue;
    unsigned trailing = 0;
    unsigned char lower = 0x80U, upper = 0xbfU;
    if (first >= 0xc2U && first <= 0xdfU) {
      trailing = 1;
    } else if (first >= 0xe0U && first <= 0xefU) {
      trailing = 2;
      if (first == 0xe0U) lower = 0xa0U;  // No overlong encodings.
      if (first == 0xedU) upper = 0x9fU;  // No UTF-16 surrogates.
    } else if (first >= 0xf0U && first <= 0xf4U) {
      trailing = 3;
      if (first == 0xf0U) lower = 0x90U;  // No overlong encodings.
      if (first == 0xf4U) upper = 0x8fU;  // At most U+10FFFF.
    } else {
      return false;
    }
    if (value.size() - offset < trailing) return false;
    const auto second = static_cast<unsigned char>(value[offset++]);
    if (second < lower || second > upper) return false;
    for (unsigned index = 1; index < trailing; ++index) {
      const auto next = static_cast<unsigned char>(value[offset++]);
      if (next < 0x80U || next > 0xbfU) return false;
    }
  }
  return true;
}

}  // namespace soui::runtime::detail
