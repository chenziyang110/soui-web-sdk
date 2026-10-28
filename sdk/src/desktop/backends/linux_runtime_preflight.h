#pragma once

#include <soui/desktop/app.h>

#include <filesystem>
#include <string_view>
#include <unistd.h>

namespace soui::desktop::detail {

// An empty directory means the deployment has not supplied its helper location.
// This is only a read-only candidate check, not a guarantee of helper startup.
inline result check_webkit_helpers(std::string_view directory) {
  if (directory.empty()) return result::success();
  const auto unavailable = [] {
    return result::failure(
        "E_WEBKIT_RUNTIME",
        "WebKitGTK helpers are unavailable. Install the matching WebKitGTK runtime "
        "and rebuild the SDK with SOUI_DESKTOP_WEBKIT_HELPER_DIR set to its "
        "absolute helper directory containing executable WebKitNetworkProcess "
        "and WebKitWebProcess files.");
  };
  if (directory.find('\0') != std::string_view::npos) return unavailable();
  try {
    const std::filesystem::path root{directory};
    if (!root.is_absolute()) return unavailable();
    for (const auto* name : {"WebKitNetworkProcess", "WebKitWebProcess"}) {
      const auto helper = root / name;
      // status follows symlinks used by ordinary system package installations.
      if (!std::filesystem::is_regular_file(helper) ||
          ::access(helper.c_str(), X_OK) != 0) {
        return unavailable();
      }
    }
  } catch (const std::filesystem::filesystem_error&) {
    return unavailable();
  }
  return result::success();
}

}  // namespace soui::desktop::detail
