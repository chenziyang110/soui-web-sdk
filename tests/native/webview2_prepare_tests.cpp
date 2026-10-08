#include <soui/host/webview2_host.h>
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using soui::sdk::webview2_adapter;
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
void pump_until(const std::function<bool()>& done) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
  while (!done() && std::chrono::steady_clock::now() < deadline) {
    (void)MsgWaitForMultipleObjectsEx(0, nullptr, 20, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
  }
  require(done(), "Preparation completion callback timed out");
}
int main() {
  const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  HWND window = CreateWindowExW(0, L"STATIC", L"Preparation test", WS_OVERLAPPEDWINDOW,
      0, 0, 200, 200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  try {
    require(SUCCEEDED(initialized) && window, "UI STA and owner window");
    wchar_t temporary[MAX_PATH]{};
    require(GetTempPathW(MAX_PATH, temporary) > 0, "Temporary directory");
    const auto profile = std::filesystem::path(temporary) /
        (L"soui-wv2-prepare-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    {
      webview2_adapter adapter(window, "relative-profile");
      unsigned calls = 0;
      adapter.prepare_async([&](auto result) { ++calls; require(!result.ok && result.code == "E_INVALID_PATH", "Relative profile rejected"); });
      require(calls == 1, "Immediate failure completes once");
    }
    {
      // The environment/controller are created asynchronously, then a malformed
      // preload fails in their callback. Previously this never called completed.
      webview2_adapter adapter(window, profile, "/* Missing document identity marker */");
      unsigned calls = 0; bool success = true;
      adapter.prepare_async([&](auto result) { ++calls; success = result.ok; });
      pump_until([&] { return calls != 0; });
      require(calls == 1 && !success && adapter.status().message == "Preload document marker missing",
              "Actual environment/controller must reach the asynchronous preload failure");
      require(adapter.unmount().ok, "Partial preparation cleans up");
      require(calls == 1, "Close does not duplicate a failed callback");
    }
    {
      webview2_adapter adapter(window, profile / "cancelled");
      unsigned calls = 0; bool success = true;
      adapter.prepare_async([&](auto result) { ++calls; success = result.ok; });
      require(adapter.unmount().ok, "Cancel preparation without blocking");
      require(calls == 1 && !success, "Cancellation completes exactly once");
    }
    DestroyWindow(window); CoUninitialize();
    std::cout << "WebView2 preparation failure and cancellation passed; temporary profiles retained at " << profile.string() << '\n';
    return 0;
  } catch (const std::exception& e) {
    if (window) DestroyWindow(window);
    if (SUCCEEDED(initialized)) CoUninitialize();
    std::cerr << e.what() << '\n'; return 1;
  }
}
