#pragma once
#include <windows.h>
#include <WebView2.h>

namespace soui::sdk::detail::webview2_loader_bridge {
// Private process-lifetime loader. Both exports are admitted before either is called.
bool unavailable() noexcept;
HRESULT get_available_version(LPCWSTR fixed_runtime, LPWSTR* version) noexcept;
HRESULT create_environment(LPCWSTR fixed_runtime, LPCWSTR user_data,
    ICoreWebView2EnvironmentOptions* options,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* callback) noexcept;
}
