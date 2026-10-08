#pragma once
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace soui::runtime {
// This host serves the legacy Vue entry and the React migration entry. Hash
// navigation stays local; only these two explicit local documents are allowed
// to inherit the Bridge session.
inline bool is_trusted_entry_url(std::string_view url, const std::filesystem::path& root) noexcept {
  try {
    if (url.size() > 131072) return false;
    url = url.substr(0, url.find('#'));
    if (url.size() < 8 || url.find('?') != std::string_view::npos) return false;
    std::string scheme(url.substr(0, 8));
    for (auto& value : scheme) value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    if (scheme != "file:///") return false;
    const auto hex = [](char value) -> int {
      if (value >= '0' && value <= '9') return value - '0';
      if (value >= 'a' && value <= 'f') return value - 'a' + 10;
      if (value >= 'A' && value <= 'F') return value - 'A' + 10;
      return -1;
    };
    std::string decoded;
    for (std::size_t i = 7; i < url.size(); ++i) {
      unsigned char value = static_cast<unsigned char>(url[i]);
      if (value == '%') {
        if (i + 2 >= url.size() || hex(url[i + 1]) < 0 || hex(url[i + 2]) < 0) return false;
        value = static_cast<unsigned char>(hex(url[i + 1]) * 16 + hex(url[i + 2]));
        i += 2;
      }
      if (value < 32 || value == 127) return false;
      decoded.push_back(static_cast<char>(value));
    }
#ifdef _WIN32
    if (decoded.size() < 4 || decoded[0] != '/' || !std::isalpha(static_cast<unsigned char>(decoded[1])) ||
        decoded[2] != ':' || decoded[3] != '/') return false;
    decoded.erase(0, 1);
#endif
    std::error_code error;
    const auto candidate = std::filesystem::u8path(decoded);
    auto candidate_name = candidate.lexically_normal().generic_u8string();
    const auto file_name = candidate.filename().generic_u8string();
    if (file_name != "index.html" && file_name != "react.html") return false;
    auto expected_name = (root / file_name).lexically_normal().generic_u8string();
#ifdef _WIN32
    for (auto& value : candidate_name) if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
    for (auto& value : expected_name) if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
#endif
    // Reject other paths before touching the filesystem (including mapped
    // network drives); equivalence then protects case-sensitive directories.
    if (candidate_name != expected_name) return false;
    const bool same_file = std::filesystem::equivalent(candidate, root / file_name, error);
    return !error && same_file;
  } catch (...) { return false; }
}
}
