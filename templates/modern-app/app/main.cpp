#include <soui/host/webview2_host.h>
#include <soui/json_bridge.h>
#include "generated/actions.hpp"
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
struct application {
  HWND window{};
  bool smoke{false}, closing{false};
  bool preparation_started{false}, preparation_completed{false}, controller_closed{false};
  int exit_code{0};
  ULONGLONG started{GetTickCount64()};
  ULONGLONG close_began{};
  std::filesystem::path root, profile;
  // Destruction is host -> bridge; borrowed Bridge stays alive through teardown.
  soui::sdk::json_bridge bridge{"app://modern-starter"};
  soui::sdk::webview2_adapter* renderer{}; // Owned by host; used only while host exists.
  std::unique_ptr<soui::sdk::webview_host> host;
  std::unique_ptr<soui::sdk::webview2_runtime_exit_watch> runtime_watch;
  void log(const std::string& value) const { std::ofstream(profile / "smoke.log", std::ios::app) << value << '\n'; }
  void retry_close() {
    if (!SetTimer(window, 2, 20, nullptr)) std::terminate(); // Retained callbacks cannot be abandoned.
  }
  void close() {
    if (!closing) close_began = GetTickCount64();
    closing = true;
    if (GetTickCount64() - close_began >= 10000) {
      exit_code = 3; log("E_RUNTIME_WATCH_DEADLINE"); std::terminate();
    }
    if (preparation_started && !preparation_completed) {
      retry_close(); return; // Retain the actual initializer until its exit identity can be acquired.
    }
    if (!controller_closed) {
      auto result = host ? host->destroy() : soui::sdk::host_result{true, {}, {}};
      // Preparation precedes Host mount; a never-mounted Host has no controller
      // to destroy. Its still-owned adapter must complete unmount explicitly.
      if (result.ok && renderer) result = renderer->unmount();
      if (!result.ok) {
        if (result.code != "E_BUSY") { exit_code = 3; log(result.code + ": " + result.message); }
        retry_close(); return;
      }
      controller_closed = true;
      KillTimer(window, 1);
    }
    if (preparation_started && !runtime_watch) {
      const auto acquired = renderer->acquire_runtime_exit_watch(runtime_watch);
      if (!acquired.ok) {
        exit_code = 3; log(acquired.code + ": " + acquired.message);
        if (acquired.code != "E_RUNTIME_WATCH_UNAVAILABLE" || !renderer->failed_preparation_cleanup_complete())
          std::terminate();
        // A failed preparation never becomes a successful runtime-exit receipt.
      }
    }
    if (runtime_watch) {
      const auto observed = runtime_watch->observe();
      if (observed.phase == soui::sdk::webview2_runtime_exit_phase::failed) {
        exit_code = 3; log("E_RUNTIME_WATCH_FAILED"); std::terminate();
      }
      if (observed.phase != soui::sdk::webview2_runtime_exit_phase::finished) {
        const auto finished = runtime_watch->finish();
        if (!finished.ok) {
          if (finished.code != "E_BUSY") { exit_code = 3; log(finished.code); std::terminate(); }
          retry_close(); return;
        }
      }
    }
    KillTimer(window, 1); KillTimer(window, 2);
    log("host-close-complete");
    DestroyWindow(window);
  }
  void pump() {
    if (!host || closing) return;
    host->pump();
    const auto result = renderer->status();
    if (!result.ok && result.code != "E_PENDING") {
      exit_code = 3; log(result.code + ": " + result.message);
      if (!smoke) MessageBoxA(window, result.message.c_str(), result.code.c_str(), MB_ICONERROR);
      close();
      return;
    }
    if (smoke && host->application_ready()) { log("application-ready"); close(); }
  }
};
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* app = reinterpret_cast<application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    app = static_cast<application*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    app->window = window;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
  }
  if (!app) return DefWindowProcW(window, message, wparam, lparam);
  switch (message) {
    case soui::sdk::webview2_event_message: app->pump(); return 0;
    case WM_DPICHANGED: {
      const auto* bounds = reinterpret_cast<const RECT*>(lparam);
      SetWindowPos(window, nullptr, bounds->left, bounds->top, bounds->right - bounds->left,
                   bounds->bottom - bounds->top, SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_SIZE:
      if (app->host && !app->closing) {
        if (wparam == SIZE_MINIMIZED) (void)app->host->hide();
        else { (void)app->host->show(); (void)app->host->resize(LOWORD(lparam), HIWORD(lparam)); }
      }
      return 0;
    case WM_SETFOCUS: if (app->host && !app->closing) (void)app->host->focus(); return 0;
    case WM_CLOSE: app->close(); return 0;
    case WM_TIMER:
      if (app->closing) app->close();
      else if (app->smoke && GetTickCount64() - app->started > 30000) {
        app->exit_code = 2; app->log("application-timeout"); app->close();
      } else app->pump();
      return 0;
    case WM_DESTROY: PostQuitMessage(app->exit_code); return 0;
    default: return DefWindowProcW(window, message, wparam, lparam);
  }
}
int run(HINSTANCE instance, bool smoke) {
  wchar_t executable[32768]{}, local[MAX_PATH]{};
  const auto executable_length = GetModuleFileNameW(nullptr, executable, 32768);
  if (!executable_length || executable_length >= 32768 ||
      FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, nullptr, 0, local))) return 1;
  application app;
  app.smoke = smoke; app.root = std::filesystem::path(executable).parent_path();
  app.profile = std::filesystem::path(local) / "SouiModernStarter";
  if (smoke) app.profile /= std::wstring(L"smoke-") + std::to_wstring(GetCurrentProcessId());
  std::filesystem::create_directories(app.profile);
  app.log("process-start");
  if (!app_actions::register_Greet(app.bridge, [](const app_actions::GreetInput& input,
                                                const soui::runtime::bridge_request&) {
        return app_actions::GreetOutput{"你好，" + input.name + "。"};
      })) { app.log("E_ACTION_REGISTRATION: Invalid application action contract."); return 1; }
  if (!app.bridge.enable_session_renewal() ||
      !app.bridge.set_session_capabilities({"bridge.describe", "bridge.probe", "app.greet", "bridge.session.renew"})) {
    app.log("E_SESSION_REGISTRATION: Invalid application session capabilities.");
    return 1;
  }
  WNDCLASSW type{};
  type.lpfnWndProc = procedure; type.hInstance = instance; type.lpszClassName = L"SouiModernStarter";
  type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  if (!RegisterClassW(&type)) return 1;
  const auto window = CreateWindowExW(0, type.lpszClassName, L"SouiModernStarter", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
      CW_USEDEFAULT, CW_USEDEFAULT, 1100, 720, nullptr, nullptr, instance, &app);
  if (!window) return 1;
  try {
  auto adapter = std::make_unique<soui::sdk::webview2_adapter>(window, app.profile / "browser");
  auto* renderer = adapter.get();
  app.renderer = renderer;
  app.host = std::make_unique<soui::sdk::webview_host>(std::move(adapter));
  const auto access = renderer->configure_profile_access(soui::sdk::webview2_profile_access::exclusive);
  if (!access.ok) throw std::runtime_error(access.code);
  app.preparation_started = true;
  renderer->prepare_async([&app, window, smoke](auto result) {
    app.preparation_completed = true;
    if (result.ok) result = app.renderer->acquire_runtime_exit_watch(app.runtime_watch);
    if (result.ok && app.runtime_watch->observe().phase == soui::sdk::webview2_runtime_exit_phase::failed)
      result = {false, "E_RUNTIME_WATCH_FAILED", "The exclusive runtime watch failed before mount."};
    if (result.ok && !app.closing) {
      soui::sdk::host_config config;
#ifdef _WIN64
      config.architecture = "x64";
#else
      config.architecture = "x86";
#endif
      config.owner_window = window; config.resource_root = app.root / "resources"; config.bridge_adapter = &app.bridge;
      result = app.host->start(config);
    }
    if (!result.ok) {
      app.exit_code = 3; app.log(result.code + ": " + result.message);
      if (!smoke && !app.closing) MessageBoxA(window, result.message.c_str(), result.code.c_str(), MB_ICONERROR);
      if (!PostMessageW(window, WM_CLOSE, 0, 0)) std::terminate();
    } else if (app.closing) {
      if (!PostMessageW(window, WM_CLOSE, 0, 0)) std::terminate();
    } else app.log("host-started;runtime=" + app.renderer->runtime_version());
  });
  if (IsWindow(window)) {
    ShowWindow(window, smoke ? SW_SHOWNOACTIVATE : SW_SHOW);
    if (smoke && !SetTimer(window, 1, 100, nullptr)) { app.exit_code = 3; app.close(); }
  }
  } catch (...) { app.exit_code = 3; app.log("E_APPLICATION_START"); app.close(); }
  MSG message{};
  while (IsWindow(window)) {
    const auto result = GetMessageW(&message, nullptr, 0, 0);
    if (result > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    else {
      // An early WM_QUIT or message failure cannot drop a live runtime watch.
      app.exit_code = 3; app.close();
      while (IsWindow(window)) {
        for (unsigned count = 0; count != 32 && IsWindow(window) && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++count)
          if (message.message != WM_QUIT) { TranslateMessage(&message); DispatchMessageW(&message); }
        if (IsWindow(window)) { app.close(); MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT); }
      }
    }
  }
  return app.exit_code;
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
  int count = 0;
  auto** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!arguments) return 1;
  const bool smoke = count == 2 && std::wstring(arguments[1]) == L"--smoke-test";
  LocalFree(arguments);
  if (count > 2 || (count == 2 && !smoke)) return 2;
  const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(com)) return 1;
  int result = 1;
  try { result = run(instance, smoke); } catch (...) {}
  CoUninitialize();
  return result;
}
