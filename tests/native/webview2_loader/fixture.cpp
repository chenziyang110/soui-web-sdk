#include <windows.h>
#include <WebView2.h>
#include <cwchar>

extern "C" HRESULT STDAPICALLTYPE loader_version(LPCWSTR fixed, LPWSTR* version) {
  if (!version || !fixed || std::wcscmp(fixed, L"fixture-fixed")) return E_INVALIDARG;
  constexpr wchar_t text[] = L"fixture.1";
  *version = static_cast<LPWSTR>(CoTaskMemAlloc(sizeof(text)));
  if (!*version) return E_OUTOFMEMORY;
  std::wmemcpy(*version, text, sizeof(text) / sizeof(wchar_t));
  return S_OK;
}
extern "C" HRESULT STDAPICALLTYPE loader_environment(LPCWSTR fixed, LPCWSTR profile,
    ICoreWebView2EnvironmentOptions* options,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* callback) {
  return fixed && profile && !options && !callback && !std::wcscmp(fixed, L"fixture-fixed") &&
      !std::wcscmp(profile, L"fixture-profile") ? S_FALSE : E_INVALIDARG;
}
