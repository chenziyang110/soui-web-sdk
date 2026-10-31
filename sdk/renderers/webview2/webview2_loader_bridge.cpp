#include "webview2_loader_bridge.h"
#include <array>
namespace soui::sdk::detail::webview2_loader_bridge {
namespace {
HMODULE loader_module() {
  static HMODULE value = [] {
    std::array<wchar_t, 32768> name{};
    const auto length = GetModuleFileNameW(nullptr, name.data(), static_cast<DWORD>(name.size()));
    if (!length || length >= name.size()) return static_cast<HMODULE>(nullptr);
    std::filesystem::path path(name.data(), name.data() + length);
    path = path.parent_path() / L"WebView2Loader.dll";
    return LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  }();
  return value;
}
}
HRESULT get_available_version(LPCWSTR fixed_runtime, LPWSTR* version) {
  if (!version) return E_INVALIDARG;
  *version = nullptr;
  const auto module = loader_module();
  if (!module) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
  const auto fn = reinterpret_cast<get_version_fn>(GetProcAddress(module, "GetAvailableCoreWebView2BrowserVersionString"));
  return fn ? fn(fixed_runtime, version) : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
}
HRESULT create_environment(LPCWSTR fixed_runtime, LPCWSTR user_data,
    ICoreWebView2EnvironmentOptions* options,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* callback) {
  const auto module = loader_module();
  if (!module) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
  const auto fn = reinterpret_cast<create_environment_fn>(GetProcAddress(module, "CreateCoreWebView2EnvironmentWithOptions"));
  return fn ? fn(fixed_runtime, user_data, options, callback) : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
}
}
