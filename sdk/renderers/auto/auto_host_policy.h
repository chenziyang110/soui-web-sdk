#pragma once

#include <soui/host/auto_webview_host.h>
#include <windows.h>
#include <utility>

namespace soui::sdk::detail {

inline webview2_runtime_discovery classify_auto_discovery(HRESULT result, std::string version) {
  webview2_runtime_discovery value;
  value.attempted = true;
  value.hresult = static_cast<std::int32_t>(result);
  if (result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
    value.result = {true, {}, "No WebView2 runtime was found in the selected search scope."};
  } else if (FAILED(result)) {
    value.result = {false, "E_WEBVIEW2_DISCOVERY", "WebView2 discovery failed; this is not an absent runtime."};
  } else if (result != S_OK || version.empty()) {
    value.result = {false, "E_WEBVIEW2_DISCOVERY", "WebView2 discovery did not return a usable runtime identity."};
  } else {
    value.result = {true, {}, {}};
    value.available = true;
    value.version = std::move(version);
  }
  return value;
}

inline host_result auto_fixed_runtime_path(const std::filesystem::path& path) {
  if (path.empty()) return {true, {}, {}};
  if (!path.is_absolute()) return {false, "E_INVALID_PATH", "Fixed runtime directory must be absolute."};
  std::error_code error;
  const bool directory = std::filesystem::is_directory(path, error);
  if (!directory || error)
    return {false, "E_INVALID_PATH", "Configured fixed runtime directory must exist and be readable."};
  // Metadata success does not establish enumeration permission. In particular,
  // an access failure must not later be reclassified as a missing runtime.
  std::filesystem::directory_iterator readable(path, error);
  if (error) return {false, "E_RUNTIME_DIRECTORY_READ", "Configured runtime directory could not be read."};
  return {true, {}, {}};
}

} // namespace soui::sdk::detail
