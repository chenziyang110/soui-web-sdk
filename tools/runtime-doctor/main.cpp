#include <soui/host/runtime_profile.h>
#include <soui/version.h>
#include <windows.h>
#include <iostream>

int main() {
  const auto env = soui::sdk::inspect_runtime_environment();
  DEVMODEW display{};
  display.dmSize = sizeof(display);
  const bool display_known = EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &display) != FALSE;
  const auto user32 = GetModuleHandleW(L"user32.dll");
  const bool per_monitor_dpi = user32 && GetProcAddress(user32, "SetThreadDpiAwarenessContext");
  std::cout << std::boolalpha
    << "{\n  \"schema\": \"soui.runtime-environment/1\",\n  \"sdkVersion\": \"" << soui::sdk::version << "\",\n"
    << "  \"osVersionKnown\": " << env.os_version_known << ",\n"
    << "  \"os\": {\"major\": " << env.os_major << ", \"minor\": " << env.os_minor << ", \"build\": " << env.os_build << "},\n"
    << "  \"peKnown\": " << env.pe_known << ",\n  \"windowsPE\": " << env.windows_pe << ",\n"
    << "  \"processArchitecture\": \"" << env.process_architecture << "\",\n"
    << "  \"nativeArchitecture\": \"" << env.native_architecture << "\",\n"
    << "  \"remoteSession\": " << env.remote_session << ",\n"
    << "  \"displayKnown\": " << display_known << ",\n"
    << "  \"display\": {\"width\": " << display.dmPelsWidth << ", \"height\": " << display.dmPelsHeight
    << ", \"refreshHz\": " << display.dmDisplayFrequency << "},\n"
    << "  \"threadDpiContextApi\": " << per_monitor_dpi << ",\n"
    << "  \"rendererStatus\": \"not-probed\",\n  \"compatibilityStatus\": \"unverified\",\n"
    << "  \"releaseReady\": false\n}\n";
  return env.os_version_known && env.pe_known ? 0 : 2;
}
