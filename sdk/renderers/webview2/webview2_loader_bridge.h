#pragma once
#include <windows.h>
#include <WebView2.h>
#include <filesystem>
namespace soui::sdk::detail::webview2_loader_bridge {
using get_version_fn = HRESULT (STDAPICALLTYPE*)(LPCWSTR, LPWSTR*);
using create_environment_fn = HRESULT (STDAPICALLTYPE*)(LPCWSTR, LPCWSTR, ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
HRESULT get_available_version(LPCWSTR fixed_runtime, LPWSTR* version);
HRESULT create_environment(LPCWSTR fixed_runtime, LPCWSTR user_data, ICoreWebView2EnvironmentOptions* options, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* callback);
}
