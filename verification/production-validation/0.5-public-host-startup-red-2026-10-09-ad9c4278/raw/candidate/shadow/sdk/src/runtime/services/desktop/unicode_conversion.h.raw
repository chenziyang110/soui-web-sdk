#pragma once

#ifdef _WIN32
#include <windows.h>

#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace soui::runtime::detail {

inline std::optional<std::wstring> utf8_to_wide(std::string_view value) {
  if (value.find('\0') != std::string_view::npos ||
      value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) return std::nullopt;
  if (value.empty()) return std::wstring{};
  const auto size = static_cast<int>(value.size());
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), size, nullptr, 0);
  if (length <= 0) return std::nullopt;
  std::wstring output(static_cast<std::size_t>(length), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), size,
                          output.data(), length) != length) return std::nullopt;
  return output;
}

inline std::optional<std::string> wide_to_utf8(std::wstring_view value) {
  if (value.find(L'\0') != std::wstring_view::npos ||
      value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) return std::nullopt;
  if (value.empty()) return std::string{};
  const auto size = static_cast<int>(value.size());
  const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), size,
                                        nullptr, 0, nullptr, nullptr);
  if (length <= 0) return std::nullopt;
  std::string output(static_cast<std::size_t>(length), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), size,
                          output.data(), length, nullptr, nullptr) != length) return std::nullopt;
  return output;
}

}  // namespace soui::runtime::detail
#endif
