#include <runtime/services/desktop/unicode_conversion.h>
#include <soui/runtime/services/desktop/desktop_services.h>
#include <soui/runtime/services/permissions/permission_store.h>

#include <iostream>
#include <stdexcept>
#include <string>

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
  const auto copied = clipboard.write_text(context, malformed);
  require(!copied.ok() && copied.code == "E_INVALID_ARGUMENT",
          "Malformed UTF-8 must not clear or replace the clipboard");
}
}  // namespace

int main() {
  try {
    test_round_trip();
    test_empty_and_invalid_inputs();
    test_public_services_reject_malformed_text_before_native_io();
    std::cout << "desktop unicode conversion passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
