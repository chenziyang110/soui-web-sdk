#include <soui/runtime/accessibility/windows_uia_provider.h>
#include <soui/version.h>

#include <iostream>
#include <memory>

static_assert(SOUI_SDK_VERSION_MAJOR == 0 && SOUI_SDK_VERSION_MINOR == 5 && SOUI_SDK_VERSION_PATCH == 0,
              "This consumer verifies the exact installed 0.5.0 SDK API");

int main() {
  std::unique_ptr<soui::runtime::windows_uia_provider> provider;
  const auto result = soui::runtime::windows_uia_provider::create(nullptr, WM_APP + 51U, 1U, {}, {}, provider);
  if (result != E_INVALIDARG || provider) {
    std::cerr << "Installed UIA API failed its actual null-window rejection\n";
    return 1;
  }
  // No HWND or COM is created. This real call rejects before loading system UIA APIs.
  std::cout << "{\"schema\":\"soui.installed-uia-consumer/1\",\"sdkVersion\":\"" << SOUI_SDK_VERSION_STRING
            << "\",\"actualParameterRejection\":true,\"providerAvailabilityQualified\":false,"
               "\"qualificationPassed\":false,\"releaseReady\":false}\n";
  return 0;
}
