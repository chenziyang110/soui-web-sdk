#include "webview2_loader_bridge.h"
#include "../../../sdk/renderers/auto/auto_host_policy.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <atomic>

namespace bridge = soui::sdk::detail::webview2_loader_bridge;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int wmain(int argc, wchar_t** argv) {
  try {
    require(argc == 3, "Expected fixture mode and complete DLL");
    const std::wstring mode(argv[1]);
    wchar_t image[32768]{};
    require(GetModuleFileNameW(nullptr, image, 32768) != 0, "Actual executable path");
    const auto dll = std::filesystem::path(image).parent_path() / L"WebView2Loader.dll";
    // Each case owns a separate executable directory; no path injection exists
    // in the production loader and the current working directory is irrelevant.
    HANDLE locked = INVALID_HANDLE_VALUE;
    if (mode == L"missing" || mode == L"invalid-image" || mode == L"locked") {
      std::error_code error; std::filesystem::remove(dll, error);
    }
    if (mode == L"invalid-image") { std::ofstream out(dll, std::ios::binary); out << "not a PE image"; }
    if (mode == L"locked") {
      require(CopyFileW(argv[2], dll.c_str(), FALSE), "Stage exact locked DLL fixture");
      locked = CreateFileW(dll.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
      require(locked != INVALID_HANDLE_VALUE, "Hold exclusive file fixture");
    }
    require(bridge::get_available_version(nullptr, nullptr) == E_INVALIDARG, "Null output rejected");
    LPWSTR version = reinterpret_cast<LPWSTR>(1);
    const auto result = bridge::get_available_version(L"fixture-fixed", &version);
    const auto classified = soui::sdk::detail::classify_auto_discovery(result,
        version ? "fixture.1" : "", bridge::unavailable());
    if (mode == L"complete") {
      require(result == S_OK && version && std::wstring(version) == L"fixture.1", "Actual version export invoked");
      require(classified.result.ok && classified.available, "Valid loader permits modern discovery");
      CoTaskMemFree(version);
      require(bridge::create_environment(L"fixture-fixed", L"fixture-profile", nullptr, nullptr) == S_FALSE,
          "Actual creation export invoked with original ABI arguments");
      std::atomic<unsigned> successes{};
      std::vector<std::thread> callers;
      for (unsigned i = 0; i != 8; ++i) callers.emplace_back([&] {
        LPWSTR text{};
        if (bridge::get_available_version(L"fixture-fixed", &text) == S_OK && text) ++successes;
        CoTaskMemFree(text);
      });
      for (auto& caller : callers) caller.join();
      require(successes == 8 && GetModuleHandleW(L"WebView2Loader.dll"), "Concurrent calls retain process module");
    } else {
      require(FAILED(result) && version == nullptr, "Failure clears output and calls no partial export");
      require(bridge::create_environment(nullptr, nullptr, nullptr, nullptr) == result, "Both calls share atomic admission");
      const bool absent = mode == L"missing" || mode == L"version-only" || mode == L"create-only";
      require(bridge::unavailable() == absent, "Only file/export absence is optional unavailable");
      require(classified.result.ok == absent && !classified.available, "AutoHost preserves real load errors");
      if (locked != INVALID_HANDLE_VALUE) { CloseHandle(locked); locked = INVALID_HANDLE_VALUE; }
      if (mode == L"missing") {
        require(CopyFileW(argv[2], dll.c_str(), FALSE), "Stage DLL after initial absence");
        require(bridge::get_available_version(L"fixture-fixed", &version) == result && !version,
            "Failure remains fixed; no running process backend switch");
        require(DeleteFileW(dll.c_str()), "Remove process-owned late file fixture");
      }
    }
    for (const auto code : {E_ACCESSDENIED, E_FAIL, E_INVALIDARG, HRESULT_FROM_WIN32(ERROR_BAD_EXE_FORMAT)})
      require(!soui::sdk::detail::classify_auto_discovery(code, {}, true).result.ok, "Permission/configuration errors never permit fallback");
    require(!soui::sdk::detail::classify_auto_discovery(HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND), {}).result.ok,
        "Ordinary Runtime discovery error is not optional loader absence");
    std::wcout << mode << L": passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
