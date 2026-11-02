#include <soui/host/auto_webview_host.h>
#include <soui/version.h>
#include <filesystem>
#include <iostream>

int wmain(int argc, wchar_t** argv) {
  if (argc != 2) {
    std::cerr << "Pass an existing empty absolute Fixed Runtime directory.\n";
    return 2;
  }
  const std::filesystem::path empty_runtime{argv[1]};
  std::error_code error;
  if (!empty_runtime.is_absolute() || !std::filesystem::is_empty(empty_runtime, error) || error) {
    std::cerr << "The fixture requires an empty absolute directory.\n";
    return 2;
  }
  const auto discovery = soui::sdk::inspect_webview2_runtime(empty_runtime);
  std::cout << "sdk=" << soui::sdk::version
            << ";attempted=" << discovery.attempted
            << ";available=" << discovery.available
            << ";hresult=" << discovery.hresult
            << ";code=" << discovery.result.code << '\n';
  return discovery.result.ok && discovery.attempted && !discovery.available ? 0 : 1;
}
