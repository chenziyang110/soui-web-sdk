#include <soui/host/webview2_host.h>
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

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
void close_adapter(webview2_adapter& adapter) {
  pump_until([&] {
    const auto closed = adapter.unmount();
    require(closed.ok || closed.code == "E_BUSY", "Preparation cleanup failed");
    return closed.ok;
  });
}
int main(int argc, char** argv) {
  const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  HWND window = CreateWindowExW(0, L"STATIC", L"Preparation test", WS_OVERLAPPEDWINDOW,
      0, 0, 200, 200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  try {
    require(SUCCEEDED(initialized) && window, "UI STA and owner window");
    const std::string selected = argc == 1 ? "all" : argc == 2 ? argv[1] : "invalid-arguments";
    require(selected == "all" || selected == "invalid" || selected == "failure" ||
        selected == "cancel" || selected == "reentrant" || selected == "callback-exception", "Unknown preparation case");
    wchar_t temporary[MAX_PATH]{};
    require(GetTempPathW(MAX_PATH, temporary) > 0, "Temporary directory");
    const auto profile = std::filesystem::path(temporary) /
        (L"soui-wv2-prepare-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::cout << "Native architecture: " << sizeof(void*) * 8 << "; selected case: " << selected << '\n';
    if (selected == "all" || selected == "invalid") {
      webview2_adapter adapter(window, "relative-profile");
      unsigned calls = 0;
      adapter.prepare_async([&](auto result) { ++calls; require(!result.ok && result.code == "E_INVALID_PATH", "Relative profile rejected"); });
      require(calls == 1, "Immediate failure completes once");
      close_adapter(adapter);
    }
    if (selected == "all" || selected == "failure") {
      // The environment/controller are created asynchronously, then a malformed
      // preload fails in their callback. Previously this never called completed.
      webview2_adapter adapter(window, profile / "failure", "/* Missing document identity marker */");
      unsigned calls = 0; bool success = true;
      adapter.prepare_async([&](auto result) { ++calls; success = result.ok; });
      pump_until([&] { return calls != 0; });
      require(calls == 1 && !success && adapter.status().message == "Preload document marker missing",
              "Actual environment/controller must reach the asynchronous preload failure");
      close_adapter(adapter);
      require(calls == 1, "Close does not duplicate a failed callback");
      std::cout << "Failure runtime: " << adapter.runtime_version() << '\n';
    }
    if (selected == "all" || selected == "cancel") {
      webview2_adapter adapter(window, profile / "cancelled");
      unsigned calls = 0; bool success = true;
      adapter.prepare_async([&](auto result) { ++calls; success = result.ok; });
      const auto initial_close = adapter.unmount();
      require(!initial_close.ok && initial_close.code == "E_BUSY", "Pending environment cancellation must retain native callback ownership");
      require(calls == 1 && !success, "Cancellation completes exactly once");
      close_adapter(adapter);
      require(calls == 1 && !success, "Late native completion cannot replace cancellation or duplicate the callback");
      std::cout << "Cancelled environment: first close E_BUSY, then settled close\n";
    }
    if (selected == "all" || selected == "reentrant") {
      webview2_adapter adapter(window, profile / "reentrant");
      unsigned calls = 0; bool success = false;
      soui::sdk::host_result inside;
      adapter.prepare_async([&](auto result) { ++calls; success = result.ok; inside = adapter.unmount(); });
      pump_until([&] { return calls != 0; });
      require(calls == 1 && success && !inside.ok && inside.code == "E_BUSY", "Preparation callback cannot destroy its own active native callback");
      close_adapter(adapter);
      require(calls == 1, "Reentrant close preserves single completion");
      std::cout << "Reentrant runtime: " << adapter.runtime_version() << '\n';
    }
    if (selected == "all" || selected == "callback-exception") {
      webview2_adapter adapter(window, profile / "callback-exception");
      unsigned calls = 0; bool success = false;
      adapter.prepare_async([&](auto result) { ++calls; success = result.ok; throw std::runtime_error("controlled completion exception"); });
      pump_until([&] { return calls != 0; });
      require(calls == 1 && success && !adapter.status().ok && adapter.status().code == "E_CALLBACK", "Successful preparation followed by completion exception remains a failed result");
      close_adapter(adapter);
      require(calls == 1 && !adapter.status().ok && adapter.status().code == "E_CALLBACK", "Cleanup must not erase callback failure");
      std::cout << "Exception runtime: " << adapter.runtime_version() << '\n';
    }
    DestroyWindow(window); window = nullptr; CoUninitialize();
    std::cout << "WebView2 preparation boundaries passed; temporary profiles retained at " << profile.string() << '\n';
    return 0;
  } catch (const std::exception& e) {
    if (window) DestroyWindow(window);
    if (SUCCEEDED(initialized)) CoUninitialize();
    std::cerr << e.what() << '\n'; return 1;
  }
}
