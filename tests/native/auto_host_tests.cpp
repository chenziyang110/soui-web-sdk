#include <soui/host/auto_webview_host.h>
#include <soui/json_bridge.h>
#include "../../sdk/renderers/auto/auto_host_policy.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>

using namespace soui::sdk;
namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct fixture final {
  std::filesystem::path root;
  HWND window{};
  explicit fixture(bool native_owner = false) {
    root = std::filesystem::temp_directory_path() /
        (L"soui-auto-host-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    require(std::filesystem::create_directory(root), "Create unique empty runtime discovery directory");
    window = CreateWindowExW(0, L"STATIC", L"AutoHost boundary", native_owner ? WS_OVERLAPPEDWINDOW : 0,
        0, 0, native_owner ? 800 : 0, native_owner ? 600 : 0,
        native_owner ? nullptr : HWND_MESSAGE, nullptr, nullptr, nullptr);
    require(window != nullptr, "Create owner message window");
  }
  ~fixture() {
    if (window) DestroyWindow(window);
    std::error_code error;
    std::filesystem::remove(root / "not-a-directory", error);
    std::filesystem::remove(root / "index.html", error);
    std::filesystem::remove(root, error); // Actual Runtime profiles are retained for diagnosis.
  }
};
auto_host_config config_for(fixture& data, bridge_adapter& bridge) {
  auto_host_config config;
#ifdef _WIN64
  config.host.architecture = "x64";
#else
  config.host.architecture = "x86";
#endif
  config.host.owner_window = data.window;
  config.host.bridge_adapter = &bridge;
  wchar_t image[32768]{};
  require(GetModuleFileNameW(nullptr, image, 32768) != 0, "Executable identity");
  // Only a metadata candidate; no case may mount this as a MiniBlink runtime.
  config.host.miniblink_binary = image;
  config.webview2_user_data = data.root / "browser";
  config.webview2_fixed_runtime = data.root;
  config.modern_resource_root = data.root / "missing-modern";
  config.compatibility_resource_root = data.root / "missing-compatibility";
  return config;
}
void rejected_start(auto_host_config config, const char* expected) {
  auto_webview_host host;
  unsigned completions{};
  host_result outcome;
  const auto admitted = host.start_async(config, [&](host_result result) {
    ++completions;
    outcome = std::move(result);
    require(host.shutdown().code == "E_BUSY", "Callback cannot abandon active startup stack");
  });
  require(admitted.ok && completions == 1, "One completion for an admitted validation failure");
  require(!outcome.ok && outcome.code == expected, "Validation error preserved");
  require(!host.application_ready(), "Rejected startup cannot become ready");
  require(host.start_async(config, [](host_result) {}).code == "E_ALREADY_STARTED", "No restart or backend replay");
  require(host.shutdown().ok && host.shutdown_complete(), "Rejected startup has a complete cleanup path");
  require(host.shutdown().ok, "Shutdown is idempotent");
  require(host.status().code == expected, "Successful cleanup does not erase startup failure");
}
void policy_cases() {
  auto absent = detail::classify_auto_discovery(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), {});
  require(absent.result.ok && absent.attempted && !absent.available, "Only real no-runtime result permits fallback");
  for (const auto code : {E_ACCESSDENIED, E_FAIL, E_INVALIDARG, HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)}) {
    const auto value = detail::classify_auto_discovery(code, {});
    require(!value.result.ok && !value.available, "Permission/configuration/unknown failure cannot become absence");
  }
  require(!detail::classify_auto_discovery(S_OK, {}).result.ok, "Success without identity fails closed");
  require(!detail::classify_auto_discovery(S_FALSE, "1.2.3.4").result.ok, "Nonstandard success fails closed");
  const auto found = detail::classify_auto_discovery(S_OK, "130.0.1.2");
  require(found.result.ok && found.available && found.version == "130.0.1.2", "Discovered runtime identity retained");
  const runtime_environment environment{true, 10, 0, 19045, true, false, false, "x64", "x64"};
  require(select_runtime_profile(environment, {true, absent.available}, runtime_profile::automatic).backend == renderer_backend::miniblink,
      "Automatic profile uses compatibility after observed absence");
  require(!select_runtime_profile(environment, {true, absent.available}, runtime_profile::automatic, true).ok,
      "Required modern capability never downgrades");
  require(!select_runtime_profile(environment, {true, absent.available}, runtime_profile::modern).ok,
      "Explicit modern profile never downgrades");
}

void actual_fixed_runtime_path() {
  wchar_t configured[32768]{};
  const auto length = GetEnvironmentVariableW(L"SOUI_TEST_WEBVIEW2_FIXED_RUNTIME", configured, 32768);
  require(length && length < 32768,
      "Fixed runtime path fixture requires SOUI_TEST_WEBVIEW2_FIXED_RUNTIME (no skip)");
  std::filesystem::path preferred{configured};
  preferred.make_preferred();
  const std::filesystem::path generic{preferred.generic_wstring()};
  const auto original = generic.native();
  require(original.find(L'/') != std::wstring::npos, "Fixture must exercise forward-slash runtime input");
  const auto native = inspect_webview2_runtime(preferred);
  require(native.result.ok && native.attempted && native.available && native.hresult == S_OK && !native.version.empty(),
      "Fixed runtime fixture requires a valid runtime for the current architecture (no skip)");
  const auto forward = inspect_webview2_runtime(generic);
  require(forward.result.ok && forward.attempted && forward.available &&
      forward.hresult == native.hresult && forward.version == native.version,
      "Both separator forms discover the same actual fixed runtime");
  require(generic.native() == original, "Discovery preserves the caller's fixed runtime path");
  std::cout << "actual-fixed-runtime-path;native-hresult=" << native.hresult
            << ";forward-hresult=" << forward.hresult << ";runtime=" << forward.version
            << ";caller-path=preserved\n";
}

void actual_profile_path() {
  fixture data{true};
  json_bridge bridge{"app://auto-host-profile-path"};
  auto config = config_for(data, bridge);
  config.webview2_fixed_runtime.clear();
  config.modern_resource_root = data.root;
  config.compatibility_resource_root = data.root;
  std::ofstream(data.root / "index.html") << "<!doctype html><title>Profile path preflight fixture</title>";
  const auto invalid_parent = data.root / "not-a-directory";
  const std::string marker = "An existing file must remain unchanged by profile preflight.";
  std::ofstream(invalid_parent) << marker;
  const auto discovered = inspect_webview2_runtime();
  require(discovered.result.ok && discovered.available,
      "Profile preflight fixture requires an installed WebView2 runtime (no skip)");
  std::cout << "actual-installed-discovery-hresult=" << discovered.hresult << ";runtime=" << discovered.version << '\n';
  // This tests synchronous configuration rejection after real backend selection,
  // not the Runtime's asynchronous initialization failure or callback behavior.
  const std::filesystem::path profiles[]{invalid_parent, invalid_parent / "profile",
      invalid_parent / "missing-parent" / "profile", data.root / "invalid*profile"};
  for (const auto& profile : profiles) {
    config.webview2_user_data = profile;
    auto_webview_host host;
    unsigned completions{};
    host_result outcome;
    const auto admitted = host.start_async(config, [&](host_result result) {
      ++completions;
      outcome = std::move(result);
    });
    require(admitted.ok && completions == 1 && !outcome.ok && outcome.code == "E_INVALID_PATH",
        "Invalid profile receives exactly one synchronous configuration rejection");
    require(host.selection().backend == renderer_backend::webview2 && host.discovery().available &&
        host.discovery().attempted, "Profile rejection retains actual WebView2 selection without fallback");
    require(host.runtime_version().empty() && !host.document_ready() && !host.application_ready(),
        "Profile preflight must not initialize a Runtime or publish readiness");
    require(host.show().code == outcome.code && host.reload().code == outcome.code,
        "Profile rejection prevents running operations");
    require(host.start_async(config, [](host_result) {}).code == "E_ALREADY_STARTED",
        "Profile rejection cannot restart or replay another backend");
    require(host.shutdown().ok && host.shutdown_complete() && host.shutdown().ok,
        "Profile preflight cleanup completes immediately and remains idempotent");
    require(completions == 1 && host.status().code == outcome.code,
        "Cleanup retains the original single configuration failure");
    std::cout << "actual-profile-path;backend=webview2;fallback=false;runtime-created=false;cleanup=complete;"
                 "completion-count=" << completions << ";code=" << outcome.code << ";profile=" << profile.string() << '\n';
  }
  std::ifstream preserved(invalid_parent);
  const std::string contents{std::istreambuf_iterator<char>(preserved), std::istreambuf_iterator<char>()};
  require(contents == marker, "Profile validation preserves the existing file");
  unsigned entries{};
  for (const auto& entry : std::filesystem::directory_iterator(data.root)) {
    require(entry.path().filename() == "index.html" || entry.path().filename() == "not-a-directory",
        "Profile validation does not create filesystem entries");
    ++entries;
  }
  require(entries == 2, "Profile preflight neither creates nor removes fixture entries");
}
void actual_callback_exception() {
  fixture data{true};
  json_bridge bridge{"app://auto-host-callback-exception"};
  auto config = config_for(data, bridge);
  config.webview2_fixed_runtime.clear();
  config.webview2_user_data = data.root / "new-parent" / "browser";
  require(!std::filesystem::exists(config.webview2_user_data.parent_path()),
      "Actual successful startup must allow new profile directories under an existing ancestor");
  config.modern_resource_root = data.root / "web";
  config.compatibility_resource_root = config.modern_resource_root;
  require(std::filesystem::create_directory(config.modern_resource_root), "Create isolated web resources");
  std::ofstream(config.modern_resource_root / "index.html") <<
      "<!doctype html><title>Actual callback exception fixture</title><p>Mounted WebView2</p>";
  const auto discovered = inspect_webview2_runtime();
  require(discovered.result.ok && discovered.available,
      "Actual callback-exception fixture requires an installed WebView2 runtime (no skip)");
  std::cout << "actual-installed-discovery-hresult=" << discovered.hresult << ";runtime=" << discovered.version << '\n';
  auto_webview_host host;
  unsigned completions{};
  host_result mounted;
  const auto admitted = host.start_async(config, [&](host_result result) {
    ++completions;
    mounted = std::move(result);
    std::cout << "actual-callback-completion;ok=" << (mounted.ok ? "true" : "false")
              << ";code=" << mounted.code << ";message=" << mounted.message << '\n';
    throw std::runtime_error("Controlled completion exception after actual mounting");
  });
  require(admitted.ok, "Admit actual callback-exception startup");
  const auto began = GetTickCount64();
  bool observed{};
  bool failure_preserved{}, not_ready{}, show_rejected{}, reload_rejected{};
  while (!host.shutdown_complete()) {
    MSG message{};
    for (unsigned count = 0; count != 32 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++count) {
      if (message.message != WM_QUIT) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    host.pump();
    if (completions) {
      if (!observed) {
        // Record before shutdown marks the host closing. This catches the
        // regression where an E_CALLBACK host still admitted show/reload.
        failure_preserved = host.status().code == "E_CALLBACK";
        not_ready = !host.document_ready() && !host.application_ready();
        show_rejected = host.show().code == "E_CALLBACK";
        reload_rejected = host.reload().code == "E_CALLBACK";
        observed = true;
      }
      const auto closed = host.shutdown();
      if (!closed.ok && closed.code != "E_BUSY")
        std::cerr << "actual-callback-cleanup-error=" << closed.code << ':' << closed.message << '\n';
    }
    if (GetTickCount64() - began >= 30000) {
      std::cerr << "actual-callback-exception deadline; owning runtime must not be abandoned\n";
      std::terminate();
    }
    if (!host.shutdown_complete()) MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
  }
  require(completions == 1 && mounted.ok, "Actual successful mount must precede the single throwing completion");
  require(std::filesystem::is_directory(config.webview2_user_data),
      "The actual Runtime may create the valid new profile after preflight");
  require(host.selection().backend == renderer_backend::webview2 && !host.runtime_version().empty(),
      "Callback failure retains the actual selected Runtime without fallback");
  require(observed && failure_preserved && not_ready && show_rejected && reload_rejected,
      "Callback failure must close readiness and operation admission before shutdown");
  require(host.status().code == "E_CALLBACK" && !host.document_ready() && !host.application_ready(),
      "Natural cleanup must preserve the callback failure without publishing readiness");
  require(host.shutdown().ok, "Callback-failure cleanup is idempotent");
  std::cout << "actual-callback-exception;backend=webview2;runtime=" << host.runtime_version()
            << ";fallback=false;ready=false;show=rejected;reload=rejected;cleanup=complete;completion-count="
            << completions << ";profile=" << config.webview2_user_data.string() << '\n';
}
}
int main(int argc, char** argv) {
  try {
    const bool profile_path = argc == 2 && std::string(argv[1]) == "--profile-path";
    const bool callback_exception = argc == 2 && std::string(argv[1]) == "--callback-exception";
    const bool fixed_runtime_path = argc == 2 && std::string(argv[1]) == "--fixed-runtime-path";
    require(argc == 1 || profile_path || callback_exception || fixed_runtime_path, "Unknown auto host fixture option");
    policy_cases();
    auto_webview_host no_com;
    require(no_com.start_async({}, [](host_result) {}).code == "E_COM_APARTMENT", "COM STA is required");
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    require(SUCCEEDED(initialized), "Initialize owner STA");
    struct apartment final { ~apartment() { CoUninitialize(); } } com;
    if (fixed_runtime_path) { actual_fixed_runtime_path(); return 0; }
    if (profile_path) { actual_profile_path(); return 0; }
    if (callback_exception) { actual_callback_exception(); return 0; }
    fixture data;
    json_bridge bridge{"app://auto-host-contract"};
    auto config = config_for(data, bridge);
    require(!inspect_webview2_runtime(L"relative").result.ok, "Relative fixed runtime rejected before discovery");
    const auto missing = inspect_webview2_runtime(data.root / "missing");
    require(!missing.result.ok && !missing.attempted, "Nonexistent configured directory is a configuration error");
    std::ofstream(data.root / "not-a-directory") << "not a runtime";
    require(!inspect_webview2_runtime(data.root / "not-a-directory").result.ok, "File is not a runtime directory");
    const auto absent = inspect_webview2_runtime(data.root);
    require(absent.result.ok && absent.attempted && !absent.available &&
        absent.hresult == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)),
        "Actual loader reports absent runtime in an existing empty directory");
    std::cout << "actual-empty-directory-discovery-hresult=" << absent.hresult << '\n';
    config.webview2_fixed_runtime = data.root / "missing";
    rejected_start(config, "E_INVALID_PATH");
    config.webview2_fixed_runtime = data.root;
    config.requires_modern_web = true;
    rejected_start(config, "E_WEBVIEW2_UNAVAILABLE");
    config.requires_modern_web = false;
    config.requested = runtime_profile::modern;
    rejected_start(config, "E_WEBVIEW2_UNAVAILABLE");
    config.requested = runtime_profile::automatic;
    rejected_start(config, "E_RESOURCE_MISSING");
    config.host.architecture = "unknown";
    rejected_start(config, "E_INVALID_ARCHITECTURE");
    auto_webview_host wrong_owner;
    host_result result;
    unsigned callbacks{};
    std::thread worker([&] { result = wrong_owner.start_async({}, [&](host_result) { ++callbacks; }); });
    worker.join();
    require(result.code == "E_THREAD" && callbacks == 0, "Wrong thread cannot admit startup");
    require(wrong_owner.shutdown().ok, "Unused wrapper cleanup");
    std::cout << "Auto host boundary contracts passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
