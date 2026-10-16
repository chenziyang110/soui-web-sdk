#include <windows.h>
#include <psapi.h>
#include "native-owner-metrics.h"
#include "process-address-census.h"
#include <soui/host/soui_miniblink_host.h>
#include <soui/json_bridge.h>
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
constexpr wchar_t window_class[] = L"SouiPublicSdkBenchmarkV1";
constexpr std::size_t max_result_bytes = 8 * 1024 * 1024;
constexpr std::size_t max_hex_chunk = 16384;

std::string reject(std::string_view code) {
  return "{\"error\":\"" + std::string(code) + "\"}";
}

bool parse_size(std::string_view text, std::size_t maximum, std::size_t& value) {
  if (text.empty() || text.front() == '0' && text.size() > 1) return false;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size() && value <= maximum;
}

class result_sink final {
 public:
  explicit result_sink(std::filesystem::path directory) : directory_(std::move(directory)) {
    std::filesystem::create_directories(directory_);
  }
  std::string begin(std::string_view params) {
    std::cerr << "benchmark.result.begin\n";
    if (params != "{}") return reject("E_INVALID_PARAMS");
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_) return reject("E_BUSY");
    bytes_.clear();
    sequence_ = 0;
    active_ = true;
    return "{\"accepted\":true}";
  }
  std::string chunk(std::string_view params) {
    // The public Bridge normalizes JSON objects and therefore sorts keys.
    constexpr std::string_view prefix = "{\"hex\":\"";
    constexpr std::string_view separator = "\",\"sequence\":";
    if (params.size() < prefix.size() + separator.size() + 2 || params.substr(0, prefix.size()) != prefix ||
        params.back() != '}') return reject("E_INVALID_PARAMS");
    const auto middle = params.find(separator, prefix.size());
    if (middle == std::string_view::npos) return reject("E_INVALID_PARAMS");
    std::size_t incoming_sequence = 0;
    if (!parse_size(params.substr(middle + separator.size(), params.size() - middle - separator.size() - 1),
                    1024, incoming_sequence))
      return reject("E_INVALID_PARAMS");
    const auto hex = params.substr(prefix.size(), middle - prefix.size());
    if (hex.empty() || hex.size() > max_hex_chunk || hex.size() % 2 != 0) return reject("E_INVALID_PARAMS");
    std::string decoded;
    decoded.reserve(hex.size() / 2);
    auto nibble = [](char digit) -> int {
      if (digit >= '0' && digit <= '9') return digit - '0';
      if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
      return -1;
    };
    for (std::size_t i = 0; i < hex.size(); i += 2) {
      const auto high = nibble(hex[i]), low = nibble(hex[i + 1]);
      if (high < 0 || low < 0) return reject("E_INVALID_PARAMS");
      decoded.push_back(static_cast<char>((high << 4) | low));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || incoming_sequence != sequence_) return reject("E_SEQUENCE");
    if (decoded.size() > max_result_bytes - bytes_.size()) return reject("E_LIMIT_MESSAGE_SIZE");
    bytes_ += decoded;
    ++sequence_;
    return "{\"accepted\":true}";
  }
  std::string commit(std::string_view params) {
    std::cerr << "benchmark.result.commit\n";
    constexpr std::string_view prefix = "{\"bytes\":";
    if (params.size() < prefix.size() + 2 || params.substr(0, prefix.size()) != prefix || params.back() != '}')
      return reject("E_INVALID_PARAMS");
    std::size_t expected = 0;
    if (!parse_size(params.substr(prefix.size(), params.size() - prefix.size() - 1), max_result_bytes, expected))
      return reject("E_INVALID_PARAMS");
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || sequence_ == 0 || expected != bytes_.size() ||
        bytes_.find("\"hostKind\":\"soui-miniblink\"") == std::string::npos)
      return reject("E_INCOMPLETE_RESULT");
    const auto name = L"soui-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                      std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(result_count_ + 1) + L".json";
    const auto destination = directory_ / name;
    const auto file = CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return reject("E_IO");
    DWORD written = 0;
    const auto ok = WriteFile(file, bytes_.data(), static_cast<DWORD>(bytes_.size()), &written, nullptr) &&
                    written == bytes_.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok) {
      DeleteFileW(destination.c_str());
      return reject("E_IO");
    }
    bytes_.clear();
    active_ = false;
    ++result_count_;
    return "{\"accepted\":true}";
  }
  std::string phase(std::string_view text) {
    try {
      const auto value = nlohmann::json::parse(text);
      if (!value.is_object() || value.size()!=3 || !value.contains("cycle") ||
          !value.contains("phase") || !value.contains("loaded") ||
          !value["cycle"].is_number_unsigned() || value["cycle"].get<unsigned>()>12 ||
          !value["loaded"].is_number_unsigned() || value["loaded"].get<unsigned>()>24 ||
          !value["phase"].is_string()) return reject("E_INVALID_PARAMS");
      const auto phase=value["phase"].get<std::string>();
      if (phase!="baseline" && phase!="live" && phase!="released" && phase!="finished")
        return reject("E_INVALID_PARAMS");
      LARGE_INTEGER time{};
      if(!QueryPerformanceCounter(&time)) return reject("E_CLOCK");
      std::lock_guard<std::mutex> lock(mutex_); phase_=value; ++phase_sequence_;
      phase_qpc_=static_cast<std::uint64_t>(time.QuadPart);
      return "{\"accepted\":true}";
    } catch (...) { return reject("E_INVALID_PARAMS"); }
  }
  nlohmann::json phase_copy() {
    std::lock_guard<std::mutex> lock(mutex_);
    return {{"sequence",phase_sequence_},{"page",phase_},{"observedQpc",std::to_string(phase_qpc_)}};
  }
  bool has_result() {
    std::lock_guard<std::mutex> lock(mutex_);
    return result_count_ > 0;
  }
 private:
  std::filesystem::path directory_;
  std::mutex mutex_;
  std::string bytes_;
  std::size_t sequence_{0};
  std::size_t result_count_{0};
  bool active_{false};
  nlohmann::json phase_=nullptr;
  unsigned phase_sequence_=0;
  std::uint64_t phase_qpc_=0;
};

struct options {
  std::filesystem::path resources;
  std::filesystem::path miniblink;
  std::filesystem::path results;
  bool smoke{false};
  bool exit_on_result{false};
};

options parse(int argc, wchar_t** argv) {
  if (argc < 4 || argc > 5) throw std::runtime_error(
      "Expected --resources=PATH --miniblink=PATH --result-dir=PATH [--smoke|--exit-on-result]");
  options result;
  for (int i = 1; i < argc; ++i) {
    const std::wstring arg(argv[i]);
    if (arg == L"--smoke") {
      if (result.smoke) throw std::runtime_error("Duplicate --smoke option");
      result.smoke = true;
      continue;
    }
    if (arg == L"--exit-on-result") {
      if (result.exit_on_result) throw std::runtime_error("Duplicate --exit-on-result option");
      result.exit_on_result = true;
      continue;
    }
    auto assign = [&](std::wstring_view name, std::filesystem::path& destination) {
      if (arg.rfind(name, 0) != 0) return false;
      if (!destination.empty() || arg.size() == name.size()) throw std::runtime_error("Missing or duplicate benchmark path");
      destination = arg.substr(name.size());
      return true;
    };
    if (!assign(L"--resources=", result.resources) && !assign(L"--miniblink=", result.miniblink) &&
        !assign(L"--result-dir=", result.results)) throw std::runtime_error("Unknown benchmark option");
  }
  if (result.resources.empty() || result.miniblink.empty() || result.results.empty() ||
      !std::filesystem::is_regular_file(result.resources / "index.html") ||
      !std::filesystem::is_regular_file(result.miniblink)) throw std::runtime_error("Benchmark inputs are missing");
  if (result.smoke && result.exit_on_result)
    throw std::runtime_error("Smoke and full-run result exit are mutually exclusive");
  return result;
}

class benchmark_window final {
 public:
  benchmark_window(soui::sdk::soui_miniblink_host& host, result_sink& sink,
                   bool smoke, bool exit_on_result)
      : host_(host), sink_(sink), smoke_(smoke), exit_on_result_(exit_on_result) {}
  void sample(const char* stage) {
    soui::sdk::diagnostics::cache_observation cache;
    (void)soui::sdk::read_miniblink_cache_metrics(cache);
    const auto address = soui::acceptance::address_census::capture();
    FILETIME created{}, exited{}, kernel{}, user{};
    if(!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
      throw std::runtime_error("Native process identity unavailable");
    const auto creation = (static_cast<std::uint64_t>(created.dwHighDateTime)<<32U)|created.dwLowDateTime;
    LARGE_INTEGER frequency{},time{};
    if(!QueryPerformanceFrequency(&frequency)||!QueryPerformanceCounter(&time))
      throw std::runtime_error("Native sampling clock unavailable");
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb=sizeof(memory);
    if(!GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)))
      throw std::runtime_error("Native memory sample unavailable");
    nlohmann::json row{{"event","cache.sample"},{"stage",stage},{"phase",sink_.phase_copy()},
      {"elapsedMs",GetTickCount64()-started_},{"namedCaches",soui::acceptance::metrics::serialize(cache)},
      {"captureClock",{{"valid",true},{"frequency",std::to_string(frequency.QuadPart)},{"qpc",std::to_string(time.QuadPart)},{"captureThreadId",std::to_string(GetCurrentThreadId())}}},
      {"addressSpace",soui::acceptance::address_census::serialize(address)},
      {"nativeIdentity",{{"pid",std::to_string(GetCurrentProcessId())},{"creationFileTime",std::to_string(creation)},{"captureThreadId",std::to_string(GetCurrentThreadId())}}},
      {"privateBytes",std::to_string(memory.PrivateUsage)}};
    std::cout<<row.dump()<<'\n'; std::cout.flush();
    if(std::string_view(stage)=="running") (void)soui::sdk::request_miniblink_cache_sample();
  }
  HWND handle() const { return handle_; }
  void request_close() {
    if (!closing_) { closing_=true; close_started_=GetTickCount64(); }
  }
  void close_step(HWND window) {
    const auto closed=host_.destroy();
    if (!closed.ok) {
      if ((closed.code=="E_BUSY" || closed.code=="E_CURSOR_CLOSE_PENDING") &&
          GetTickCount64()-close_started_<20000) return;
      std::cerr << "Host close failed: " << closed.code << ": " << closed.message << '\n';
      exit_code_=2; KillTimer(window,1); PostQuitMessage(exit_code_); return;
    }
    KillTimer(window,1);
    try { sample("host-destroyed"); }
    catch(const std::exception& error){exit_code_=4;std::cerr<<error.what()<<'\n';}
    DestroyWindow(window);
  }
  void set_handle(HWND handle) { handle_ = handle; }
  int exit_code() const { return exit_code_; }
  static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<benchmark_window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<benchmark_window*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wparam, lparam);
    if (message == soui::sdk::bridge_event_message) {
      self->host_.deliver_posted_event(reinterpret_cast<void*>(lparam));
      return 0;
    }
    switch (message) {
      case WM_TIMER:
        if (self->closing_) { self->close_step(window); return 0; }
        self->host_.pump();
        if (GetTickCount64()-self->last_sample_>=1000) {
          self->last_sample_=GetTickCount64();
          try { self->sample("running"); }
          catch(const std::exception& error){self->exit_code_=4;std::cerr<<error.what()<<'\n';PostMessageW(window,WM_CLOSE,0,0);}
        }
        if(GetTickCount64()-self->started_>180000){self->exit_code_=5;PostMessageW(window,WM_CLOSE,0,0);}
        if ((self->smoke_ || self->exit_on_result_) && self->sink_.has_result())
          PostMessageW(window, WM_CLOSE, 0, 0);
        else if (self->smoke_ && GetTickCount64() - self->started_ > 30000) {
          self->exit_code_ = 3;
          std::cerr << "benchmark smoke timeout: documentReady=" << self->host_.document_ready()
                    << " applicationReady=" << self->host_.application_ready()
                    << " diagnostics=" << self->host_.application_diagnostics() << '\n';
          PostMessageW(window, WM_CLOSE, 0, 0);
        }
        return 0;
      case WM_SIZE:
        if (wparam != SIZE_MINIMIZED) (void)self->host_.resize(std::max(1, static_cast<int>(LOWORD(lparam))),
                                                              std::max(1, static_cast<int>(HIWORD(lparam))));
        return 0;
      case WM_SETFOCUS: (void)self->host_.focus(); return 0;
      case WM_CLOSE: {
        self->request_close();
        return 0;
      }
      case WM_DESTROY: PostQuitMessage(self->exit_code_); return 0;
      case WM_NCDESTROY: SetWindowLongPtrW(window, GWLP_USERDATA, 0); break;
      default: break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
  }
 private:
  soui::sdk::soui_miniblink_host& host_;
  result_sink& sink_;
  bool smoke_;
  bool exit_on_result_;
  ULONGLONG started_{GetTickCount64()};
  ULONGLONG last_sample_{0};
  bool closing_{false};
  ULONGLONG close_started_{0};
  HWND handle_{nullptr};
  int exit_code_{0};
};

int run(int argc, wchar_t** argv) {
  const auto settings = parse(argc, argv);
  SetEnvironmentVariableW(L"SOUI_GPU_COMPOSITOR", nullptr);
  SetEnvironmentVariableW(L"SOUI_GPU_DIRECT_PRESENT", nullptr);
  // The SDK navigation guard accepts the local index.html entry. A smoke
  // fixture supplies its auto-finish script under that same basename.
  SetEnvironmentVariableW(L"SOUI_ENTRY_FILE", nullptr);
  result_sink sink(settings.results);
  soui::sdk::json_bridge bridge("app://benchmark",soui::sdk::diagnostics::mode::scalar);
  if(!bridge.register_method("cache.calibration.phase","cache.calibration.phase",[&](std::string_view value){return sink.phase(value);}))
    throw std::runtime_error("Cannot register calibration observation");
  if (!bridge.register_method("benchmark.result.begin", "benchmark.result.begin", [&](std::string_view value) { return sink.begin(value); }) ||
      !bridge.register_method("benchmark.result.chunk", "benchmark.result.chunk", [&](std::string_view value) { return sink.chunk(value); }) ||
      !bridge.register_method("benchmark.result.commit", "benchmark.result.commit", [&](std::string_view value) { return sink.commit(value); }) ||
      !bridge.register_method("benchmark.result.status", "benchmark.result.status", [&](std::string_view value) {
        std::cerr << "benchmark.result.status " << std::string(value.substr(0, 512)) << '\n';
        return std::string{"{}"};
      }))
    throw std::runtime_error("Cannot register benchmark result methods");
  soui::sdk::soui_miniblink_host host(std::make_unique<soui::sdk::dynamic_miniblink_adapter>(soui::sdk::diagnostics::mode::scalar));
  benchmark_window window(host, sink, settings.smoke, settings.exit_on_result);
  WNDCLASSEXW description{};
  description.cbSize = sizeof(description);
  description.hInstance = GetModuleHandleW(nullptr);
  description.lpfnWndProc = benchmark_window::procedure;
  description.lpszClassName = window_class;
  description.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  if (!RegisterClassExW(&description)) throw std::runtime_error("Cannot register benchmark window class");
  // Electron requests a 1100 x 720 content area. Win32 CreateWindowExW takes
  // outer dimensions, so include the non-client frame before creating ours.
  RECT outer{0, 0, 1100, 720};
  if (!AdjustWindowRectEx(&outer, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, FALSE, 0))
    throw std::runtime_error("Cannot size benchmark content area");
  const auto handle = CreateWindowExW(0, window_class, L"SOUI SDK benchmark", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                      CW_USEDEFAULT, CW_USEDEFAULT, outer.right - outer.left,
                                      outer.bottom - outer.top, nullptr, nullptr,
                                      GetModuleHandleW(nullptr), &window);
  if (!handle) throw std::runtime_error("Cannot create benchmark window");
  RECT content{};
  if (!GetClientRect(handle, &content) || content.right != 1100 || content.bottom != 720) {
    DestroyWindow(handle);
    throw std::runtime_error("Benchmark content area differs from the paired Electron host");
  }
  window.set_handle(handle);
  soui::sdk::host_config config;
#ifdef _WIN64
  config.architecture = "x64";
#else
  config.architecture = "x86";
#endif
  config.miniblink_binary = settings.miniblink;
  config.resource_root = settings.resources;
  config.owner_window = handle;
  config.bridge_adapter = &bridge;
  const auto started = host.start(config);
  if (!started.ok) {
    DestroyWindow(handle);
    throw std::runtime_error(started.code + ": " + started.message);
  }
  ShowWindow(handle, settings.smoke ? SW_SHOWNOACTIVATE : SW_SHOW);
  UpdateWindow(handle);
  SetTimer(handle, 1, 16, nullptr);
  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  const auto shutdown = soui::sdk::wait_for_miniblink_runtime_shutdown(std::chrono::seconds{20});
  if (!shutdown.ok) throw std::runtime_error(shutdown.code + ": " + shutdown.message);
  return window.exit_code();
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  SetProcessDPIAware();
  const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(com)) return 1;
  int code = 1;
  try { code = run(argc, argv); }
  catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
  CoUninitialize();
  return code;
}
