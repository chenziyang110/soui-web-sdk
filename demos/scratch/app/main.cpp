#include <soui/host/webview2_host.h>
#include <soui/json_bridge.h>
#include <soui/version.h>
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <atomic>
#include "project_file_save.h"

namespace {
// Replace with the application's trusted native identity/policy decision.
constexpr bool allow_native_devtools = false;
constexpr UINT devtools_open_command = 0x6D01, devtools_end_command = 0x6D02;
struct application {
  HWND window{};
  bool smoke{false};
  std::atomic_bool closing{false};
  bool diagnostic_prompt{false};
  int diagnostic_state{-1};
  bool preparation_started{false}, preparation_completed{false}, controller_closed{false};
  int exit_code{0};
  ULONGLONG started{GetTickCount64()};
  ULONGLONG close_began{};
  std::filesystem::path root, profile;
  std::string prepared_runtime_version; // Published before mount; immutable while the Bridge worker runs.
  // Destruction is host -> bridge; borrowed Bridge stays alive through teardown.
  soui::sdk::json_bridge bridge{"app://scratch-studio"};
  soui::sdk::webview2_adapter* renderer{}; // Owned by host; used only while host exists.
  std::unique_ptr<soui::sdk::webview_host> host;
  std::unique_ptr<soui::sdk::webview2_runtime_exit_watch> runtime_watch;
  scratch_project_export project_export;
  void log(const std::string& value) const { std::ofstream(profile / "smoke.log", std::ios::app) << value << '\n'; }
  void request_devtools() {
    if (!allow_native_devtools || closing || diagnostic_prompt || !host || !host->application_ready()) return;
    diagnostic_prompt=true;
    struct prompt_guard {bool& active;~prompt_guard(){active=false;}} prompt{diagnostic_prompt};
    if (MessageBoxW(window,L"允许调试当前业务窗口？调试者可以查看页面数据并执行代码。结束诊断需要关闭此业务窗口，请先保存工作。",
        L"当前窗口开发者工具",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES) return;
    soui::sdk::devtools::grant grant;
    const auto authorized=host->authorize_devtools(grant);
    if (!authorized.ok) {MessageBoxA(window,authorized.message.c_str(),authorized.code.c_str(),MB_ICONERROR);return;}
    const auto opened=host->open_devtools(grant);
    if (!opened.accepted()) MessageBoxA(window,opened.message.c_str(),opened.code.c_str(),MB_ICONERROR);
    else {log("devtools-open-accepted;attached=unknown;visible=unknown");update_devtools_status();}
  }
  void update_devtools_status() {
    if(!allow_native_devtools || !host || !IsWindow(window))return;
    const auto status=host->observe_devtools();
    if(diagnostic_state==static_cast<int>(status.state))return;
    diagnostic_state=static_cast<int>(status.state);
    using soui::sdk::devtools::phase;
    const wchar_t* title=L"SouiScratch";
    switch(status.state) {
      case phase::opening: title=L"SouiScratch — 诊断请求已接受，连接状态未知";break;
      case phase::attached: title=L"SouiScratch — 开发者工具已连接";break;
      case phase::revoking: title=L"SouiScratch — 正在结束诊断";break;
      case phase::failed: title=L"SouiScratch — 诊断失败，请结束诊断";break;
      case phase::drained: title=L"SouiScratch — 诊断已结束";break;
      default: break;
    }
    SetWindowTextW(window,title);
  }
  bool translate_devtools(const MSG& message) {
    if (!host || closing || message.message!=WM_KEYDOWN ||
        (message.hwnd!=window && !IsChild(window,message.hwnd))) return false;
    if (!host->devtools_shortcut(static_cast<unsigned>(message.wParam),
        (GetKeyState(VK_CONTROL)&0x8000)!=0,(GetKeyState(VK_SHIFT)&0x8000)!=0,
        (GetKeyState(VK_MENU)&0x8000)!=0,(message.lParam&(1LL<<30))!=0)) return false;
    request_devtools(); return true;
  }
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
      if (result.ok && renderer && !allow_native_devtools) result = renderer->unmount();
      if (!result.ok) {
        if (result.code != "E_BUSY") { exit_code = 3; log(result.code + ": " + result.message); }
        const auto diagnostic=host ? host->observe_devtools() : soui::sdk::devtools::observation{};
        if (!allow_native_devtools || diagnostic.resources_retained ||
            diagnostic.state!=soui::sdk::devtools::phase::failed) {retry_close();return;}
      }
      const auto diagnostic=host ? host->observe_devtools() : soui::sdk::devtools::observation{};
      // A previous pump may have physically cleaned a failed retirement. A
      // subsequent successful destroy cannot clear its failure exit status.
      if(diagnostic.state==soui::sdk::devtools::phase::failed && !diagnostic.resources_retained)exit_code=3;
      controller_closed = true;
      KillTimer(window, 1);
    }
    if (preparation_started && !runtime_watch && !allow_native_devtools) {
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
    update_devtools_status();
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
    case soui::sdk::devtools_request_message:
      if (app->host && app->host->devtools_shortcut(static_cast<unsigned>(wparam),
          (lparam&1)!=0,(lparam&2)!=0,(lparam&4)!=0)) app->request_devtools();
      return 0;
    case WM_COMMAND:
      if (LOWORD(wparam)==devtools_open_command) {app->request_devtools();return 0;}
      if (LOWORD(wparam)==devtools_end_command && allow_native_devtools) {app->close();return 0;}
      break;
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
    default: break;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}
int run(HINSTANCE instance, bool smoke) {
  wchar_t executable[32768]{}, local[MAX_PATH]{};
  const auto executable_length = GetModuleFileNameW(nullptr, executable, 32768);
  if (!executable_length || executable_length >= 32768 ||
      FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, nullptr, 0, local))) return 1;
  application app;
  app.smoke = smoke; app.root = std::filesystem::path(executable).parent_path();
  app.profile = std::filesystem::path(local) / "SouiScratch";
  if (smoke) app.profile /= std::wstring(L"smoke-") + std::to_wstring(GetCurrentProcessId());
  std::filesystem::create_directories(app.profile);
  app.log("process-start");
  if (!app.bridge.register_method("app.info", "app.info", [&app](std::string_view) {
        return nlohmann::json{{"sdkVersion", soui::sdk::version}, {"backend", "WebView2"},
            {"application", "SouiScratch"}, {"buildId", __DATE__ " " __TIME__},
            {"runtimeVersion", app.prepared_runtime_version}}.dump();
      })) { app.log("E_ACTION_REGISTRATION: Cannot register application information."); return 1; }
  if (!app.bridge.register_contextual_method("app.saveProject", "app.saveProject", [&app](const auto& request) {
        return app.project_export.accept(request, app.window, app.closing);
      })) { app.log("E_ACTION_REGISTRATION: Cannot register project export."); return 1; }
  if (!app.bridge.register_contextual_method("app.saveMetrics", "app.saveMetrics", [&app](const auto& request) {
        return app.project_export.save_metrics(request, app.window, app.closing);
      })) { app.log("E_ACTION_REGISTRATION: Cannot register metrics export."); return 1; }
  if (!app.bridge.enable_session_renewal() ||
      !app.bridge.set_session_capabilities({"bridge.describe", "bridge.probe", "app.info", "app.saveProject", "app.saveMetrics", "bridge.session.renew"})) {
    app.log("E_SESSION_REGISTRATION: Invalid application session capabilities.");
    return 1;
  }
  WNDCLASSW type{};
  type.lpfnWndProc = procedure; type.hInstance = instance; type.lpszClassName = L"SouiScratch";
  type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  if (!RegisterClassW(&type)) return 1;
  const auto window = CreateWindowExW(0, type.lpszClassName, L"Scratch 学习工作室 · SOUI SDK 0.6", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
      CW_USEDEFAULT, CW_USEDEFAULT, 1440, 900, nullptr, nullptr, instance, &app);
  if (!window) return 1;
  try {
  auto adapter = std::make_unique<soui::sdk::webview2_adapter>(window, app.profile / "browser");
  auto* renderer = adapter.get();
  app.renderer = renderer;
  app.host = std::make_unique<soui::sdk::webview_host>(std::move(adapter));
  if (allow_native_devtools) {
    soui::sdk::devtools::configuration diagnostics;
    diagnostics.access=soui::sdk::devtools::policy::application_controlled;
    diagnostics.ending=soui::sdk::devtools::termination::destroy_target;
    diagnostics.f12=diagnostics.ctrl_shift_i=true;
    const auto configured=app.host->configure_devtools(diagnostics);
    if (!configured.ok) throw std::runtime_error(configured.code);
    HMENU bar=CreateMenu(),menu=CreatePopupMenu();
    if (!bar || !menu) {if(menu)DestroyMenu(menu);if(bar)DestroyMenu(bar);throw std::runtime_error("Cannot create diagnostic menu");}
    if (!AppendMenuW(menu,MF_STRING,devtools_open_command,L"打开当前窗口开发者工具") ||
        !AppendMenuW(menu,MF_STRING,devtools_end_command,L"结束诊断并关闭当前窗口") ||
        !AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(menu),L"诊断") || !SetMenu(window,bar)) {
      DestroyMenu(bar);throw std::runtime_error("Cannot attach diagnostic menu");
    }
    DrawMenuBar(window);
  }
  const auto access = renderer->configure_profile_access(soui::sdk::webview2_profile_access::exclusive);
  if (!access.ok) throw std::runtime_error(access.code);
  app.preparation_started = true;
  renderer->prepare_async([&app, window, smoke](auto result) {
    app.preparation_completed = true;
    if (result.ok && !allow_native_devtools) result = app.renderer->acquire_runtime_exit_watch(app.runtime_watch);
    if (result.ok && app.runtime_watch && app.runtime_watch->observe().phase == soui::sdk::webview2_runtime_exit_phase::failed)
      result = {false, "E_RUNTIME_WATCH_FAILED", "The exclusive runtime watch failed before mount."};
    if (result.ok && !app.closing) {
      app.prepared_runtime_version = app.renderer->runtime_version();
      soui::sdk::host_config config;
#ifdef _WIN64
      config.architecture = "x64";
#else
      config.architecture = "x86";
#endif
      config.owner_window = window; config.resource_root = app.root / "resources"; config.bridge_adapter = &app.bridge;
      config.resource_policy = soui::sdk::local_resource_policy::isolated_local_editor;
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
    if (result > 0) { if(!app.translate_devtools(message)) {TranslateMessage(&message); DispatchMessageW(&message);} }
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
