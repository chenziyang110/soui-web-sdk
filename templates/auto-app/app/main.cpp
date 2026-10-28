#include <soui/host/auto_webview_host.h>
#include <soui/host/soui_miniblink_host.h>
#include <soui/host/webview2_host.h>
#include <soui/json_bridge.h>
#include "generated/actions.hpp"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace {
constexpr UINT_PTR pump_timer = 1;
constexpr ULONGLONG smoke_deadline_ms = 30000;
constexpr ULONGLONG close_deadline_ms = 20000;

struct arguments {
  bool smoke{false};
  bool require_modern{false};
  std::filesystem::path log_file, user_data, fixed_runtime;
};

std::filesystem::path absolute_argument(const std::wstring& value) {
  const std::filesystem::path result{value};
  if (value.empty() || !result.is_absolute() || value.find_first_of(L"\r\n") != std::wstring::npos)
    throw std::runtime_error("Expected an absolute local path.");
  return result.lexically_normal();
}

arguments parse_arguments() {
  int count{};
  auto** raw = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!raw) throw std::runtime_error("Cannot read command line.");
  struct release { LPWSTR* value; ~release() { LocalFree(value); } } owned{raw};
  arguments result;
  for (int index = 1; index < count; ++index) {
    const std::wstring value{raw[index]};
    const auto path_option = [&](const wchar_t* prefix, std::filesystem::path& output) {
      const std::wstring key{prefix};
      if (value.compare(0, key.size(), key) != 0) return false;
      if (!output.empty()) throw std::runtime_error("Duplicate path option.");
      output = absolute_argument(value.substr(key.size()));
      return true;
    };
    if (value == L"--smoke-test" && !result.smoke) result.smoke = true;
    else if (value == L"--require-modern" && !result.require_modern) result.require_modern = true;
    else if (!path_option(L"--smoke-log=", result.log_file) &&
             !path_option(L"--user-data=", result.user_data) &&
             !path_option(L"--webview2-runtime=", result.fixed_runtime))
      throw std::runtime_error("Unknown or duplicate argument.");
  }
  if (!result.fixed_runtime.empty() && !std::filesystem::is_directory(result.fixed_runtime))
    throw std::runtime_error("The fixed runtime path must be an existing directory.");
  return result;
}

class diagnostic_log {
 public:
  explicit diagnostic_log(const std::filesystem::path& file)
      : handle_(CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL, nullptr)) {
    if (handle_ == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create a new diagnostic log.");
  }
  ~diagnostic_log() { CloseHandle(handle_); }
  diagnostic_log(const diagnostic_log&) = delete;
  diagnostic_log& operator=(const diagnostic_log&) = delete;
  void write(std::string value) {
    for (auto& character : value) if (character == '\r' || character == '\n') character = ' ';
    value += '\n';
    const std::lock_guard<std::mutex> guard{mutex_};
    DWORD written{};
    if (!WriteFile(handle_, value.data(), static_cast<DWORD>(value.size()), &written, nullptr) ||
        written != value.size() || !FlushFileBuffers(handle_)) std::terminate();
  }
 private:
  HANDLE handle_{INVALID_HANDLE_VALUE};
  std::mutex mutex_;
};

struct application {
  HWND window{};
  bool smoke{false}, closing{false}, selected_logged{false}, ready_logged{false};
  int exit_code{};
  ULONGLONG started{GetTickCount64()}, close_began{};
  std::atomic<unsigned> greeting_calls{0};
  std::filesystem::path root, profile;
  std::unique_ptr<diagnostic_log> log;
  // Destruction is Host -> Bridge -> log. The callback never outlives this app.
  soui::sdk::json_bridge bridge{"app://auto-starter"};
  std::unique_ptr<soui::sdk::auto_webview_host> host;

  void selection_log() {
    if (!host || selected_logged) return;
    selected_logged = true;
    const auto& selected = host->selection();
    const auto& discovery = host->discovery();
    log->write(std::string{"backend-selected;backend="} + soui::sdk::backend_name(selected.backend) +
               ";reason=" + selected.reason + ";code=" + selected.code);
    log->write(std::string{"runtime-discovery;attempted="} + (discovery.attempted ? "true" : "false") +
               ";available=" + (discovery.available ? "true" : "false") +
               ";hresult=" + std::to_string(discovery.hresult) + ";version=" + discovery.version);
  }
  void fail(const soui::sdk::host_result& result) {
    exit_code = 3;
    log->write(result.code + ": " + result.message);
    if (!smoke && !closing) MessageBoxA(window, result.message.c_str(), result.code.c_str(), MB_ICONERROR);
    if (!PostMessageW(window, WM_CLOSE, 0, 0)) std::terminate();
  }
  void close() {
    if (!closing) { closing = true; close_began = GetTickCount64(); }
    if (GetTickCount64() - close_began >= close_deadline_ms) {
      log->write("E_HOST_CLOSE_TIMEOUT"); std::terminate();
    }
    if (host) {
      host->pump();
      const auto result = host->shutdown();
      if (!result.ok) {
        if (result.code != "E_BUSY") { exit_code = 3; log->write(result.code + ": " + result.message); }
        if (!SetTimer(window, pump_timer, 16, nullptr)) std::terminate();
        return; // Keep the HWND, bridge, Host and UI pump alive until drained.
      }
      if (!host->shutdown_complete()) std::terminate();
    }
    KillTimer(window, pump_timer);
    log->write("host-shutdown-complete");
    DestroyWindow(window);
  }
  void pump() {
    if (closing) { close(); return; }
    if (!host) return;
    host->pump();
    const auto status = host->status();
    if (!status.ok && status.code != "E_PENDING") { fail(status); return; }
    if (!ready_logged && host->application_ready()) {
      if (greeting_calls.load() == 0) {
        fail({false, "E_ACTION_ROUNDTRIP", "The page reported ready before the native action ran."}); return;
      }
      ready_logged = true;
      log->write("action-roundtrip;method=app.greet;calls=" + std::to_string(greeting_calls.load()));
      log->write("application-ready");
      if (smoke && !PostMessageW(window, WM_CLOSE, 0, 0)) std::terminate();
    }
    if (smoke && !ready_logged && GetTickCount64() - started >= smoke_deadline_ms) {
      log->write("application-diagnostics;" + host->application_diagnostics());
      fail({false, "E_APPLICATION_TIMEOUT", "The application did not become ready before the deadline."});
    }
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
    case soui::sdk::bridge_event_message:
      if (app->host) app->host->deliver_posted_event(reinterpret_cast<void*>(lparam));
      return 0;
    case WM_TIMER: app->pump(); return 0;
    case WM_SIZE:
      if (app->host && !app->closing) {
        if (wparam == SIZE_MINIMIZED) (void)app->host->hide();
        else { (void)app->host->show(); (void)app->host->resize(LOWORD(lparam), HIWORD(lparam)); }
      }
      return 0;
    case WM_SETFOCUS: if (app->host && !app->closing) (void)app->host->focus(); return 0;
    case WM_DPICHANGED: {
      const auto* bounds = reinterpret_cast<const RECT*>(lparam);
      SetWindowPos(window, nullptr, bounds->left, bounds->top, bounds->right - bounds->left,
                   bounds->bottom - bounds->top, SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_CLOSE: app->close(); return 0;
    case WM_DESTROY: PostQuitMessage(app->exit_code); return 0;
    default: return DefWindowProcW(window, message, wparam, lparam);
  }
}

int run(HINSTANCE instance, const arguments& options) {
  wchar_t executable[32768]{}, local[MAX_PATH]{};
  const auto length = GetModuleFileNameW(nullptr, executable, 32768);
  if (!length || length >= 32768 ||
      FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, nullptr, 0, local)))
    throw std::runtime_error("Cannot locate application directories.");
  application app;
  app.smoke = options.smoke;
  app.root = std::filesystem::path(executable).parent_path();
  app.profile = options.user_data;
  if (app.profile.empty()) {
    app.profile = std::filesystem::path(local) / "SouiAutoStarter";
    std::filesystem::create_directories(app.profile);
    if (options.smoke) app.profile /= std::wstring{L"smoke-"} + std::to_wstring(GetCurrentProcessId()) +
                                      L"-" + std::to_wstring(GetTickCount64());
  }
  if ((options.smoke || !options.user_data.empty()) && !std::filesystem::create_directory(app.profile))
    throw std::runtime_error("User data directory must be new.");
  const auto default_log = options.smoke ? app.profile / "smoke.log" : app.profile /
    (std::wstring{L"run-"} + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L".log");
  app.log = std::make_unique<diagnostic_log>(options.log_file.empty() ? default_log : options.log_file);
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
    throw std::runtime_error("Cannot read process creation identity.");
  const auto identity = (static_cast<ULONGLONG>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
  app.log->write("process-start;pid=" + std::to_string(GetCurrentProcessId()) + ";creationFileTime=" + std::to_string(identity));
  if (!app_actions::register_Greet(app.bridge, [&app](const app_actions::GreetInput& input,
                                                    const soui::runtime::bridge_request&) {
        ++app.greeting_calls;
        return app_actions::GreetOutput{"你好，" + input.name + "。"};
      }) || !app.bridge.enable_session_renewal() ||
      !app.bridge.set_session_capabilities({"bridge.describe", "bridge.probe", "app.greet", "bridge.session.renew"})) {
    app.log->write("E_ACTION_REGISTRATION"); return 1;
  }
  WNDCLASSW type{};
  type.lpfnWndProc = procedure; type.hInstance = instance; type.lpszClassName = L"SouiAutoStarter";
  type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  if (!RegisterClassW(&type)) { app.log->write("E_WINDOW_CLASS"); return 1; }
  const auto window = CreateWindowExW(0, type.lpszClassName, L"SouiAutoStarter", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
    CW_USEDEFAULT, CW_USEDEFAULT, 1000, 720, nullptr, nullptr, instance, &app);
  if (!window) { app.log->write("E_WINDOW_CREATE"); return 1; }
  try {
    app.host = std::make_unique<soui::sdk::auto_webview_host>();
    soui::sdk::auto_host_config config;
#ifdef _WIN64
    config.host.architecture = "x64";
#else
    config.host.architecture = "x86";
#endif
    config.host.owner_window = window;
    config.host.bridge_adapter = &app.bridge;
    config.host.miniblink_binary = app.root / "node.dll";
    config.modern_resource_root = app.root / "resources" / "web-modern";
    config.compatibility_resource_root = app.root / "resources" / "web-compat";
    config.webview2_user_data = app.profile / "browser";
    config.webview2_fixed_runtime = options.fixed_runtime;
    config.requires_modern_web = options.require_modern;
    if (!SetTimer(window, pump_timer, 16, nullptr)) throw std::runtime_error("Cannot start UI timer.");
    const auto accepted = app.host->start_async(config, [&app](const soui::sdk::host_result result) {
      app.selection_log();
      if (!result.ok) { app.fail(result); return; }
      app.log->write("host-started;runtime=" + app.host->runtime_version());
      RECT client{};
      if (GetClientRect(app.window, &client)) (void)app.host->resize(client.right, client.bottom);
    });
    if (!accepted.ok) { app.selection_log(); app.fail(accepted); }
    if (!options.smoke) ShowWindow(window, SW_SHOW);
  } catch (const std::exception& error) {
    app.fail({false, "E_APPLICATION_START", error.what()});
  }
  MSG message{};
  while (IsWindow(window)) {
    const auto result = GetMessageW(&message, nullptr, 0, 0);
    if (result > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    else {
      app.exit_code = 3; app.log->write("E_EARLY_MESSAGE_LOOP_EXIT"); app.close();
      while (IsWindow(window)) {
        for (unsigned count = 0; count != 32 && IsWindow(window) && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++count)
          if (message.message != WM_QUIT) { TranslateMessage(&message); DispatchMessageW(&message); }
        if (IsWindow(window)) { app.close(); MsgWaitForMultipleObjects(0, nullptr, FALSE, 16, QS_ALLINPUT); }
      }
    }
  }
  app.host.reset();
  const auto stopped = soui::sdk::wait_for_miniblink_runtime_shutdown(std::chrono::seconds{20});
  if (!stopped.ok) { app.log->write(stopped.code + ": " + stopped.message); return 3; }
  app.log->write("host-close-complete");
  app.log->write("process-complete;exit=" + std::to_string(app.exit_code));
  return app.exit_code;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
  const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(com)) return 1;
  int result = 1;
  try { result = run(instance, parse_arguments()); }
  catch (const std::exception& error) { OutputDebugStringA(error.what()); }
  CoUninitialize();
  return result;
}
