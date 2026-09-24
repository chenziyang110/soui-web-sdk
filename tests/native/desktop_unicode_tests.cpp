#include <runtime/services/desktop/unicode_conversion.h>
#include <soui/runtime/services/desktop/desktop_services.h>
#include <soui/runtime/services/desktop/window_commands.h>
#include <soui/runtime/services/permissions/permission_store.h>

#include <iostream>
#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void test_round_trip() {
  const std::wstring folder = L"资料\\目录\\报告";
  const std::string utf8 = u8"资料\\目录\\报告";
  const auto encoded = soui::runtime::detail::wide_to_utf8(folder);
  require(encoded && *encoded == utf8, "Windows folder path must encode to its complete UTF-8 form");
  const auto decoded = soui::runtime::detail::utf8_to_wide(utf8);
  require(decoded && *decoded == folder, "UTF-8 folder path must round-trip without truncation");
}

void test_empty_and_invalid_inputs() {
  const auto empty_wide = soui::runtime::detail::utf8_to_wide("");
  const auto empty_utf8 = soui::runtime::detail::wide_to_utf8(L"");
  require(empty_wide && empty_wide->empty() && empty_utf8 && empty_utf8->empty(),
          "Empty text must remain a valid empty value");
  require(!soui::runtime::detail::utf8_to_wide(std::string("\xC3\x28", 2)),
          "Malformed UTF-8 must be rejected");
  require(!soui::runtime::detail::utf8_to_wide(std::string("a\0b", 3)),
          "Embedded NUL must not silently truncate a title or clipboard value");
  const std::wstring unpaired_surrogate(1, static_cast<wchar_t>(0xD800));
  require(!soui::runtime::detail::wide_to_utf8(unpaired_surrogate),
          "Invalid UTF-16 must not become a successful folder path");
}

void test_public_services_reject_malformed_text_before_native_io() {
  soui::runtime::permission_store permissions;
  const auto token = permissions.issue("session", "app://desktop-test",
                                       {"desktop.window.setTitle", "desktop.clipboard.writeText"});
  const soui::runtime::desktop_request_context context{"session", "app://desktop-test", token, true};
  const soui::runtime::window_service windows(permissions);
  const soui::runtime::clipboard_service clipboard(permissions);
  const std::string malformed("\xC3\x28", 2);
  const auto title = windows.set_title(reinterpret_cast<void*>(1), context, malformed);
  require(!title.ok() && title.code == "E_INVALID_ARGUMENT",
          "A malformed UTF-8 title must fail before touching an HWND");
  const auto invalid_window = windows.set_title(reinterpret_cast<void*>(1), context, "valid title");
  require(!invalid_window.ok() && invalid_window.code == "E_INVALID_ARGUMENT",
          "A valid title must not target an invalid window");
  const auto long_title = windows.set_title(reinterpret_cast<void*>(1), context, std::string(513, 'x'));
  require(!long_title.ok() && long_title.code == "E_INVALID_ARGUMENT",
          "An oversized title must fail before touching an HWND");
  const auto copied = clipboard.write_text(reinterpret_cast<void*>(1), context, malformed);
  require(!copied.ok() && copied.code == "E_INVALID_ARGUMENT",
          "Malformed UTF-8 must be rejected before clipboard work");
  const auto missing_owner = clipboard.write_text(nullptr, context, "valid text");
  require(!missing_owner.ok() && missing_owner.code == "E_INVALID_ARGUMENT",
          "Clipboard write must reject a missing owner before opening the clipboard");
  const auto invalid_owner = clipboard.write_text(reinterpret_cast<void*>(1), context, "valid text");
  require(!invalid_owner.ok() && invalid_owner.code == "E_INVALID_ARGUMENT",
          "Clipboard write must reject an invalid HWND before opening the clipboard");
}

void test_clipboard_round_trip() {
  const HWND owner = CreateWindowExW(0, L"STATIC", L"SOUI clipboard test", WS_POPUP,
                                     0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  require(owner != nullptr, "Cannot create the clipboard owner window");
  soui::runtime::permission_store permissions;
  const auto token = permissions.issue("session", "app://desktop-test",
                                       {"desktop.clipboard.writeText"});
  const soui::runtime::desktop_request_context context{"session", "app://desktop-test", token, true};
  const soui::runtime::clipboard_service clipboard(permissions);
  const auto result = clipboard.write_text(owner, context, u8"剪贴板验证");
  require(result.ok(), "Public clipboard service must commit Unicode text with a live owner");
  require(OpenClipboard(owner) != FALSE, "Cannot reopen the clipboard for verification");
  const HANDLE data = GetClipboardData(CF_UNICODETEXT);
  require(data != nullptr, "Clipboard contains no Unicode text");
  const auto* value = static_cast<const wchar_t*>(GlobalLock(data));
  require(value != nullptr, "Cannot lock clipboard text");
  const bool matches = std::wstring(value) == L"剪贴板验证";
  GlobalUnlock(data);
  CloseClipboard();
  DestroyWindow(owner);
  require(matches, "Clipboard Unicode text differs from the input");
}

LRESULT CALLBACK test_window_proc(HWND window, UINT message, WPARAM first, LPARAM second) {
  if (message == soui::runtime::window_command_message)
    return soui::runtime::execute_window_command(
        window, static_cast<soui::runtime::window_command>(first), second) ? 1 : 0;
  return DefWindowProcW(window, message, first, second);
}

void test_window_round_trip() {
  const HINSTANCE module = GetModuleHandleW(nullptr);
  constexpr const wchar_t* class_name = L"SouiSdkWindowRoundTrip";
  WNDCLASSW kind{};
  kind.lpfnWndProc = test_window_proc;
  kind.hInstance = module;
  kind.lpszClassName = class_name;
  require(RegisterClassW(&kind) != 0, "Cannot register the test window class");
  const HWND owner = CreateWindowExW(0, class_name, L"Original", WS_OVERLAPPEDWINDOW,
                                      0, 0, 320, 240, nullptr, nullptr, module, nullptr);
  require(owner != nullptr, "Cannot create the test window");
  soui::runtime::permission_store permissions;
  const auto token = permissions.issue("session", "app://desktop-test",
                                       {"desktop.window.setTitle", "desktop.window.minimize",
                                        "desktop.window.toggleMaximize", "desktop.window.close"});
  const soui::runtime::desktop_request_context gesture{"session", "app://desktop-test", token, true};
  const soui::runtime::desktop_request_context no_gesture{"session", "app://desktop-test", token, false};
  const soui::runtime::window_service windows(permissions);
  require(windows.set_title(owner, no_gesture, u8"窗口验证").ok(),
          "Public window title update failed");
  wchar_t title[64]{};
  require(GetWindowTextW(owner, title, 64) > 0 && std::wstring(title) == L"窗口验证",
          "Public window title differs from the input");
  const auto denied = windows.minimize(owner, no_gesture);
  require(!denied.ok() && denied.code == "E_GESTURE_REQUIRED",
          "Window minimize must reject a missing native gesture");
  ShowWindow(owner, SW_SHOWNA);
  require(windows.minimize(owner, gesture).ok() && IsIconic(owner),
          "Public window minimize did not update the owner window");
  require(windows.toggle_maximize(owner, gesture).ok() && IsZoomed(owner),
          "Public window maximize did not update the owner window");
  require(windows.toggle_maximize(owner, gesture).ok() && !IsZoomed(owner),
          "Public window restore did not update the owner window");
  require(windows.close(owner, gesture).ok(), "Public window close was not accepted");
  MSG message{};
  const ULONGLONG deadline = GetTickCount64() + 1000;
  while (IsWindow(owner) && GetTickCount64() < deadline) {
    while (PeekMessageW(&message, owner, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    Sleep(1);
  }
  require(!IsWindow(owner), "Public window close did not destroy its owner");
  require(UnregisterClassW(class_name, module) != FALSE, "Cannot release the test window class");
}

void test_bounded_title_when_window_thread_stalls() {
  const HINSTANCE module = GetModuleHandleW(nullptr);
  constexpr const wchar_t* class_name = L"SouiSdkTitleTimeout";
  std::promise<HWND> ready;
  auto created = ready.get_future();
  std::promise<void> pumping;
  auto pump_started = pumping.get_future();
  std::atomic<bool> stop{false};
  std::thread ui([&] {
    WNDCLASSW kind{};
    kind.lpfnWndProc = test_window_proc;
    kind.hInstance = module;
    kind.lpszClassName = class_name;
    if (!RegisterClassW(&kind)) { ready.set_value(nullptr); return; }
    const HWND window = CreateWindowExW(0, class_name, L"Original", WS_OVERLAPPEDWINDOW,
                                        0, 0, 320, 240, nullptr, nullptr, module, nullptr);
    ready.set_value(window);
    if (window) {
      Sleep(1200);  // Do not process the first title command during its deadline.
      pumping.set_value();
      MSG message{};
      while (!stop.load()) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
          TranslateMessage(&message);
          DispatchMessageW(&message);
        }
        Sleep(1);
      }
      DestroyWindow(window);
    }
    UnregisterClassW(class_name, module);
  });
  const HWND window = created.get();
  if (!window) { stop = true; ui.join(); throw std::runtime_error("Cannot create stalled test window"); }
  soui::runtime::permission_store permissions;
  const auto token = permissions.issue("session", "app://desktop-test", {"desktop.window.setTitle"});
  const soui::runtime::desktop_request_context context{"session", "app://desktop-test", token, false};
  const soui::runtime::window_service windows(permissions);
  const auto start = std::chrono::steady_clock::now();
  const auto stalled = windows.set_title(window, context, u8"不应延迟应用");
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start).count();
  const bool resumed = pump_started.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
  if (resumed) Sleep(30);  // Give the queued, cancelled command a chance to drain.
  wchar_t cancelled_title[64]{};
  const bool cancelled_unchanged = resumed && GetWindowTextW(window, cancelled_title, 64) > 0 &&
                                    std::wstring(cancelled_title) == L"Original";
  const auto recovered = resumed ? windows.set_title(window, context, u8"恢复后标题")
                                 : soui::runtime::desktop_response{};
  wchar_t title[64]{};
  const bool matches = resumed && GetWindowTextW(window, title, 64) > 0 &&
                       std::wstring(title) == L"恢复后标题";
  stop = true;
  ui.join();
  require(!stalled.ok() && stalled.code == "E_WINDOW_TIMEOUT" && elapsed < 600,
          "A blocked owner must not hold the Bridge worker indefinitely");
  require(cancelled_unchanged && recovered.ok() && matches,
          "A cancelled title must not apply, and a later request must recover");
}
}  // namespace

int main(int argc, char** argv) {
  try {
    test_round_trip();
    test_empty_and_invalid_inputs();
    test_public_services_reject_malformed_text_before_native_io();
    if (argc == 2 && std::string(argv[1]) == "--clipboard-roundtrip") test_clipboard_round_trip();
    else if (argc == 2 && std::string(argv[1]) == "--desktop-roundtrip") {
      test_clipboard_round_trip();
      test_window_round_trip();
    }
    else if (argc == 2 && std::string(argv[1]) == "--window-thread-timeout")
      test_bounded_title_when_window_thread_stalls();
    else if (argc != 1) throw std::runtime_error("Unknown desktop Unicode test argument");
    std::cout << "desktop unicode conversion passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
