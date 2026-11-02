#include "webview2_loader_bridge.h"
#include <array>
#include <cwchar>

namespace soui::sdk::detail::webview2_loader_bridge {
namespace {
using version_fn = HRESULT(STDAPICALLTYPE*)(LPCWSTR, LPWSTR*);
using environment_fn = HRESULT(STDAPICALLTYPE*)(LPCWSTR, LPCWSTR,
    ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
struct loader final {
  HMODULE module{};
  version_fn version{};
  environment_fn environment{};
  HRESULT result{E_FAIL};
  bool absent{};

  loader() noexcept {
    std::array<wchar_t, 32768> image{};
    const auto length = GetModuleFileNameW(nullptr, image.data(), static_cast<DWORD>(image.size()));
    if (!length || length >= image.size()) {
      result = HRESULT_FROM_WIN32(length ? ERROR_INSUFFICIENT_BUFFER : GetLastError());
      return;
    }
    std::size_t start = length;
    while (start && image[start - 1] != L'\\' && image[start - 1] != L'/') --start;
    constexpr wchar_t filename[] = L"WebView2Loader.dll";
    if (!start || start + std::size(filename) > image.size()) {
      result = HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME);
      return;
    }
    std::wmemcpy(image.data() + start, filename, std::size(filename));
    const auto attributes = GetFileAttributesW(image.data());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
      const auto error = GetLastError();
      absent = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
      result = HRESULT_FROM_WIN32(error);
      return;
    }
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
      result = HRESULT_FROM_WIN32(ERROR_DIRECTORY);
      return;
    }
    // Absolute application-local path; never search PATH or load by basename.
    // Suppress only this thread's loader error UI, restoring its previous mode.
    DWORD previous_mode{};
    if (!SetThreadErrorMode(SEM_FAILCRITICALERRORS, &previous_mode)) {
      result = HRESULT_FROM_WIN32(GetLastError());
      return;
    }
    module = LoadLibraryExW(image.data(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    const auto load_error = module ? ERROR_SUCCESS : GetLastError();
    if (!SetThreadErrorMode(previous_mode, nullptr)) { result = HRESULT_FROM_WIN32(GetLastError()); return; }
    if (!module) { result = HRESULT_FROM_WIN32(load_error); return; }
    version = reinterpret_cast<version_fn>(GetProcAddress(module, "GetAvailableCoreWebView2BrowserVersionString"));
    environment = reinterpret_cast<environment_fn>(GetProcAddress(module, "CreateCoreWebView2EnvironmentWithOptions"));
    if (!version || !environment) {
      version = nullptr;
      environment = nullptr;
      absent = true;
      result = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
      return;
    }
    result = S_OK;
  }
  // No FreeLibrary: callbacks and a successfully acquired Runtime may outlive
  // an individual Host. First success/failure remains fixed for this process.
};
const loader& process_loader() noexcept { static const loader value; return value; }
}
bool unavailable() noexcept { return process_loader().absent; }
HRESULT get_available_version(LPCWSTR fixed_runtime, LPWSTR* version) noexcept {
  if (!version) return E_INVALIDARG;
  *version = nullptr;
  const auto& value = process_loader();
  return FAILED(value.result) ? value.result : value.version(fixed_runtime, version);
}
HRESULT create_environment(LPCWSTR fixed_runtime, LPCWSTR user_data,
    ICoreWebView2EnvironmentOptions* options,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* callback) noexcept {
  const auto& value = process_loader();
  return FAILED(value.result) ? value.result : value.environment(fixed_runtime, user_data, options, callback);
}
}
