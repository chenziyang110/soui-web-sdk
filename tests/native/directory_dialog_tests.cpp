#include <soui/json_bridge.h>
#include <json.hpp>

#include <windows.h>
#include <objbase.h>

#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using json = nlohmann::json;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

LRESULT CALLBACK owner_proc(HWND window, UINT message, WPARAM first, LPARAM second) {
  return DefWindowProcW(window, message, first, second);
}

BOOL CALLBACK find_dialog(HWND window, LPARAM output) {
  wchar_t kind[32]{};
  GetClassNameW(window, kind, 32);
  if (IsWindowVisible(window) && std::wstring(kind) == L"#32770") {
    *reinterpret_cast<HWND*>(output) = window;
    return FALSE;
  }
  return TRUE;
}

void test_public_dialog_cancel() {
  const HINSTANCE module = GetModuleHandleW(nullptr);
  constexpr const wchar_t* class_name = L"SouiSdkDirectoryDialogProbe";
  WNDCLASSW kind{};
  kind.lpfnWndProc = owner_proc;
  kind.hInstance = module;
  kind.lpszClassName = class_name;
  require(RegisterClassW(&kind) != 0, "Cannot register directory dialog owner class");
  const HWND owner = CreateWindowExW(0, class_name, L"SOUI directory probe",
                                     WS_OVERLAPPEDWINDOW, 100, 100, 400, 280,
                                     nullptr, nullptr, module, nullptr);
  require(owner != nullptr, "Cannot create directory dialog owner");
  ShowWindow(owner, SW_SHOWNORMAL);
  UpdateWindow(owner);

  soui::sdk::json_bridge bridge("app://directory-probe");
  require(bridge.enable_directory_service(), "Cannot enable public directory module");
  bridge.set_owner_window(owner);
  const json bootstrap{{"requestId", "directory-boot"}, {"sessionId", ""},
                       {"origin", ""}, {"token", ""}, {"method", "bridge.bootstrap"},
                       {"params", {{"protocol", "soui.bridge/1"}}}};
  const auto boot = json::parse(bridge.handle_json(bootstrap.dump()));
  require(boot.at("ok").get<bool>(), "Directory bridge bootstrap failed");
  const auto& auth = boot.at("result");
  const json request{{"requestId", "directory-cancel"}, {"sessionId", auth.at("sessionId")},
                     {"origin", auth.at("origin")}, {"token", auth.at("token")},
                     {"method", "desktop.dialog.pickDirectory"}, {"params", json::object()}};

  std::promise<DWORD> worker_ready;
  auto worker_id = worker_ready.get_future();
  std::promise<std::string> finished;
  auto result = finished.get_future();
  std::thread worker([&] {
    worker_ready.set_value(GetCurrentThreadId());
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(apartment)) {
      finished.set_value("STA initialization failed");
      return;
    }
    try { finished.set_value(bridge.handle_json(request.dump(), true)); }
    catch (const std::exception& error) { finished.set_value(error.what()); }
    CoUninitialize();
  });
  const DWORD dialog_thread = worker_id.get();
  bool observed = false;
  bool cancelled = false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
         std::chrono::steady_clock::now() < deadline) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    HWND dialog = nullptr;
    EnumThreadWindows(dialog_thread, find_dialog, reinterpret_cast<LPARAM>(&dialog));
    if (dialog != nullptr) {
      observed = true;
      if (!cancelled) cancelled = PostMessageW(dialog, WM_CLOSE, 0, 0) != FALSE;
    }
    Sleep(20);
  }
  if (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
    TerminateProcess(GetCurrentProcess(), 4);  // Keep the guest command script from hanging.
  const auto response = result.get();
  worker.join();
  bridge.set_owner_window(nullptr);
  DestroyWindow(owner);
  UnregisterClassW(class_name, module);
  require(observed && cancelled, "The real directory dialog was not observed and cancelled");
  const auto parsed = json::parse(response);
  require(parsed.at("ok").get<bool>() && parsed.at("result").at("cancelled").get<bool>() &&
          parsed.at("result").at("path") == "", "Public directory cancellation result is invalid");
}
}  // namespace

int main(int argc, char** argv) {
  try {
    require(argc == 2 && std::string(argv[1]) == "--cancel", "Expected --cancel");
    test_public_dialog_cancel();
    std::cout << "public directory dialog cancellation passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
