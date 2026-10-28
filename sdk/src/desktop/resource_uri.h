#pragma once
#include <string>
#include <string_view>

namespace soui::desktop::detail {
// Syntactic guard only. Authority still requires the exact per-window selected
// document in backend_events::allow_navigation; this is not a general URL grant.
inline bool is_resource_uri(std::string_view uri) noexcept {
  uri = uri.substr(0, uri.find('#'));
  if (uri.size() > 65536 || uri.substr(0, 17) != "http://127.0.0.1:") return false;
  const auto port_end = uri.find('/', 17);
  if (port_end == std::string_view::npos || port_end == 17 || port_end - 17 > 5) return false;
  unsigned port = 0;
  for (std::size_t i = 17; i < port_end; ++i) {
    if (uri[i] < '0' || uri[i] > '9') return false;
    port = port * 10 + static_cast<unsigned>(uri[i] - '0');
  }
  if (!port || port > 65535 || uri[17] == '0') return false;
  const auto prefix = port_end + 1;
  if (uri.size() < prefix + 65 || uri[prefix + 64] != '/') return false;
  for (std::size_t i = prefix; i < prefix + 64; ++i)
    if (!((uri[i] >= '0' && uri[i] <= '9') || (uri[i] >= 'a' && uri[i] <= 'f'))) return false;
  for (const unsigned char byte : uri)
    if (byte <= 32 || byte == 127 || byte == '\\') return false;
  return uri.find('?') == std::string_view::npos;
}

inline std::string resource_path_uri(std::string_view base, std::string_view relative) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::string uri(base);
  for (const unsigned char byte : relative) {
    if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
        (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' ||
        byte == '.' || byte == '~' || byte == '/') uri += static_cast<char>(byte);
    else { uri += '%'; uri += hex[byte >> 4U]; uri += hex[byte & 15U]; }
  }
  return uri;
}
}  // namespace soui::desktop::detail
