#include <soui/host/webview2_host.h>
#include <soui/json_bridge.h>
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <json.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {
using json = nlohmann::json;
struct application {
  HWND window{};
  bool smoke{false}, closing{false};
  int exit_code{0};
  ULONGLONG started{GetTickCount64()};
  std::filesystem::path root, profile;
  // Destruction is host -> bridge; borrowed Bridge stays alive through teardown.
  soui::sdk::json_bridge bridge{"app://modern-starter"};
  soui::sdk::webview2_adapter* renderer{}; // Owned by host; used only while host exists.
  std::unique_ptr<soui::sdk::webview_host> host;
  void log(const std::string& value) const { std::ofstream(profile / "smoke.log", std::ios::app) << value << '\n'; }
  void close() {
    closing = true;
    const auto result = host ? host->destroy() : soui::sdk::host_result{true, {}, {}};
    if (!result.ok) {
      if (result.code != "E_BUSY") { exit_code = 3; log(result.code + ": " + result.message); }
      SetTimer(window, 2, 20, nullptr);
      return;
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
  const auto contract = json{{"protocol", "soui.action/1"}, {"title", "问候"}, {"description", "由 C++ 返回问候消息。"},
    {"inputSchema", {{"type", "object"}, {"properties", {{"name", {{"type", "string"}, {"minLength", 1}, {"maxLength", 32}}}}},
      {"required", json::array({"name"})}, {"additionalProperties", false}}},
    {"outputSchema", {{"type", "object"}, {"properties", {{"message", {{"type", "string"}, {"maxLength", 200}}}}},
      {"required", json::array({"message"})}, {"additionalProperties", false}}}}.dump();
  if (!app.bridge.register_action("app.greet", "app.greet", contract, [](const auto& request) {
        return json{{"message", "你好，" + json::parse(request.params_json).at("name").template get<std::string>() + "。"}}.dump();
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
  auto adapter = std::make_unique<soui::sdk::webview2_adapter>(window, app.profile / "browser");
  auto* renderer = adapter.get();
  app.renderer = renderer;
  app.host = std::make_unique<soui::sdk::webview_host>(std::move(adapter));
  renderer->prepare_async([&](auto result) {
    if (app.closing) return;
    if (result.ok) {
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
      if (!smoke) MessageBoxA(window, result.message.c_str(), result.code.c_str(), MB_ICONERROR);
      PostMessageW(window, WM_CLOSE, 0, 0);
    } else app.log("host-started;runtime=" + renderer->runtime_version());
  });
  ShowWindow(window, smoke ? SW_SHOWNOACTIVATE : SW_SHOW);
  if (smoke) SetTimer(window, 1, 100, nullptr);
  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
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
