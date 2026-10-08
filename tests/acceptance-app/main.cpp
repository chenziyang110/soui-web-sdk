#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <soui/host/soui_miniblink_host.h>
#include <soui/json_bridge.h>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace {
using clock_type = std::chrono::steady_clock;
constexpr wchar_t window_class[] = L"SouiPublicSdkAcceptanceV1";

std::string quote(const std::string& value) {
  std::string result = "\"";
  for (const auto byte : value) {
    const auto c = static_cast<unsigned char>(byte);
    if (c == '"' || c == '\\') { result += '\\'; result += byte; }
    else if (c < 32) {
      constexpr char digits[] = "0123456789abcdef";
      result += "\\u00"; result += digits[c >> 4]; result += digits[c & 15];
    } else result += byte;
  }
  return result + '"';
}

class log_writer {
 public:
  explicit log_writer(const std::filesystem::path& file) : output_(file, std::ios::binary) {
    if (!output_) throw std::runtime_error("Cannot create acceptance log");
  }
  void emit(const std::string& event, const std::string& fields = {}) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - started_).count();
    output_ << "{\"event\":" << quote(event) << ",\"elapsedMs\":" << elapsed;
    if (!fields.empty()) output_ << ',' << fields;
    output_ << "}\n";
    output_.flush();
    if (!output_) throw std::runtime_error("Cannot persist acceptance log");
  }
 private:
  std::ofstream output_;
  clock_type::time_point started_{clock_type::now()};
};

class mount_phase_writer {
 public:
  explicit mount_phase_writer(const std::filesystem::path& report) {
    char enabled[2]{};
    if (GetEnvironmentVariableA("SOUI_ACCEPTANCE_MOUNT_TRACE", enabled, sizeof(enabled)) != 1 || enabled[0] != '1')
      return;
    auto file = report;
    file += L".phases.ndjson";
    output_.open(file, std::ios::binary);
    if (!output_) throw std::runtime_error("Cannot create acceptance mount phase log");
  }
  void emit(unsigned cycle, unsigned window, const char* phase) {
    if (!output_.is_open()) return;
    output_ << "{\"event\":\"mount.phase\",\"tickMs\":" << GetTickCount64()
            << ",\"cycle\":" << cycle << ",\"window\":" << window
            << ",\"phase\":" << quote(phase) << "}\n";
    output_.flush();
    if (!output_) throw std::runtime_error("Cannot persist acceptance mount phase log");
  }
 private:
  std::ofstream output_;
};

using window_counts = std::map<std::string, unsigned>;

BOOL CALLBACK count_child_window(HWND window, LPARAM state) {
  char class_name[256] = {};
  if (GetClassNameA(window, class_name, sizeof(class_name)))
    ++(*reinterpret_cast<window_counts*>(state))[class_name];
  return TRUE;
}

BOOL CALLBACK count_process_window(HWND window, LPARAM state) {
  DWORD pid = 0;
  GetWindowThreadProcessId(window, &pid);
  if (pid == GetCurrentProcessId()) {
    count_child_window(window, state);
    EnumChildWindows(window, count_child_window, state);
  }
  return TRUE;
}

std::string process_window_classes() {
  window_counts counts;
  EnumWindows(count_process_window, reinterpret_cast<LPARAM>(&counts));
  HWND message_window = nullptr;
  while ((message_window = FindWindowExA(HWND_MESSAGE, message_window, nullptr, nullptr)) != nullptr) {
    DWORD pid = 0;
    GetWindowThreadProcessId(message_window, &pid);
    if (pid == GetCurrentProcessId()) count_child_window(message_window, reinterpret_cast<LPARAM>(&counts));
  }
  std::string result = "{";
  for (const auto& [name, count] : counts) {
    if (result.size() > 1) result += ',';
    result += quote(name) + ":" + std::to_string(count);
  }
  return result + "}";
}

struct options {
  std::string mode;
  unsigned cycles{0};
  unsigned warmup{0};
  unsigned duration_ms{0};
  unsigned idle_ms{0};
  unsigned settle_ms{0};
  unsigned timeout_ms{0};
  std::filesystem::path report;
};

unsigned number(const std::wstring& value, unsigned maximum) {
  if (value.empty() || value.find_first_not_of(L"0123456789") != std::wstring::npos)
    throw std::runtime_error("Expected an unsigned integer argument");
  const auto parsed = std::stoul(value);
  if (parsed > maximum) throw std::runtime_error("Argument exceeds acceptance limit");
  return static_cast<unsigned>(parsed);
}

options parse(int argc, wchar_t** argv) {
  options result;
  // Exactly these arguments are required; duplicates and unknown arguments fail.
  if (argc != 7) throw std::runtime_error("Expected --mode --cycles --warmup --settle-ms --timeout-ms --report");
  std::array<bool, 8> seen{};
  for (int i = 1; i < argc; ++i) {
    const std::wstring arg{argv[i]};
    const auto separator = arg.find(L'=');
    if (separator == std::wstring::npos) throw std::runtime_error("Arguments require =value");
    const auto key = arg.substr(0, separator);
    const auto value = arg.substr(separator + 1);
    unsigned index = 0;
    if (key == L"--mode") {
      if (value != L"reload" && value != L"recreate" && value != L"multiwindow" &&
          value != L"bridge" && value != L"mixed")
        throw std::runtime_error("Unknown scenario");
      result.mode = value == L"reload" ? "reload" : value == L"recreate" ? "recreate" :
          value == L"bridge" ? "bridge" : value == L"mixed" ? "mixed" : "multiwindow";
    } else if (key == L"--cycles") { index = 1; result.cycles = number(value, 10000); }
    else if (key == L"--warmup") { index = 2; result.warmup = number(value, 100); }
    else if (key == L"--settle-ms") { index = 3; result.settle_ms = number(value, 60000); }
    else if (key == L"--timeout-ms") { index = 4; result.timeout_ms = number(value, 120000); }
    else if (key == L"--report") { index = 5; result.report = value; }
    else if (key == L"--duration-ms") { index = 6; result.duration_ms = number(value, 43200000); }
    else if (key == L"--idle-ms") { index = 7; result.idle_ms = number(value, 60000); }
    else throw std::runtime_error("Unknown argument");
    if (seen[index]) throw std::runtime_error("Duplicate argument");
    seen[index] = true;
  }
  if (!seen[0] || !seen[3] || !seen[4] || !seen[5] ||
      !result.timeout_ms || result.report.empty() ||
      (result.mode == "mixed" ? (!result.duration_ms || !result.idle_ms || seen[1] || seen[2] ||
          result.idle_ms >= result.timeout_ms)
        : (!result.cycles || seen[6] || seen[7]))) throw std::runtime_error("Invalid scenario arguments");
  return result;
}

std::filesystem::path executable_directory() {
  std::wstring text(32768, L'\0');
  const auto size = GetModuleFileNameW(nullptr, text.data(), static_cast<DWORD>(text.size()));
  if (!size || size == text.size()) throw std::runtime_error("Cannot locate executable");
  text.resize(size);
  return std::filesystem::path(text).parent_path();
}

class test_window final {
 public:
  explicit test_window(unsigned id, bool bridge_checks = false) : id_(id), bridge_checks_(bridge_checks), bridge_("app://acceptance-" + std::to_string(id)),
      host_(std::make_unique<soui::sdk::dynamic_miniblink_adapter>()) {
    if (!bridge_.register_method("acceptance.echo", "acceptance.echo", [this](std::string_view params) {
          if (params != "{}") return std::string{"{\"error\":\"E_INVALID_PARAMS\"}"};
          return "{\"window\":" + std::to_string(id_) + ",\"bridgeChecks\":" + (bridge_checks_ ? "true" : "false") + "}";
        }) || !bridge_.register_method("acceptance.ready", "acceptance.ready", [this](std::string_view params) {
          if (params != "{}") return std::string{"{\"error\":\"E_INVALID_PARAMS\"}"};
          ready_count_.fetch_add(1, std::memory_order_release);
          return std::string{"{}"};
        })) throw std::runtime_error("Cannot register acceptance methods");
    if (bridge_checks_ && (!bridge_.register_method("acceptance.reflect", "acceptance.reflect", [](std::string_view params) {
          return params.size() <= 64 ? std::string(params) : std::string{"{\"error\":\"E_INVALID_PARAMS\"}"};
        }) || !bridge_.register_method("acceptance.delay", "acceptance.delay", [](std::string_view params) {
          if (params != "{}") return std::string{"{\"error\":\"E_INVALID_PARAMS\"}"};
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
          return std::string{"{\"delayed\":true}"};
        }) || !bridge_.register_method("acceptance.fail", "acceptance.fail", [](std::string_view) {
          return std::string{"{\"error\":\"E_ACCEPTANCE_FAILURE\",\"message\":\"Expected fixture rejection\"}"};
        }) || !bridge_.register_method("acceptance.large", "acceptance.large", [](std::string_view) {
          return "{\"data\":\"" + std::string(65536, 'x') + "\"}";
        }))) throw std::runtime_error("Cannot register acceptance bridge probes");
  }
  test_window(const test_window&) = delete;
  test_window& operator=(const test_window&) = delete;
  ~test_window() { (void)host_.destroy(); if (handle_) DestroyWindow(handle_); }

  soui::sdk::host_result mount(const std::filesystem::path& root,
                               mount_phase_writer* phases = nullptr, unsigned cycle = 0) {
    if (phases) phases->emit(cycle, id_, "window.create.begin");
    handle_ = CreateWindowExW(0, window_class, L"SDK independent acceptance", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        80 + static_cast<int>(id_) * 50, 80 + static_cast<int>(id_) * 40, 640, 480,
        nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!handle_) throw std::runtime_error("Cannot create acceptance parent window");
    if (phases) phases->emit(cycle, id_, "window.create.end");
    soui::sdk::host_config config;
#ifdef _WIN64
    config.architecture = "x64";
#else
    config.architecture = "x86";
#endif
    config.miniblink_binary = root / "node.dll";
    config.resource_root = root / "resources" / ("window-" + std::to_string(id_));
    config.owner_window = handle_;
    config.bridge_adapter = &bridge_;
    if (phases) phases->emit(cycle, id_, "host.start.begin");
    const auto result = host_.start(config);
    if (phases) phases->emit(cycle, id_, "host.start.end");
    if (result.ok) {
      if (phases) phases->emit(cycle, id_, "window.show.begin");
      ShowWindow(handle_, SW_SHOWNOACTIVATE);
      if (phases) phases->emit(cycle, id_, "window.show.end");
    }
    return result;
  }
  soui::sdk::host_result reload() { return host_.reload(); }
  soui::sdk::host_result close() { return host_.destroy(); }
  soui::sdk::host_result set_minimized(bool minimized) {
    if (!handle_ || !IsWindow(handle_)) return {false, "E_WINDOW_MISSING", "Acceptance window is missing"};
    ShowWindow(handle_, minimized ? SW_MINIMIZE : SW_RESTORE);
    if (static_cast<bool>(IsIconic(handle_)) != minimized)
      return {false, "E_WINDOW_STATE", "Acceptance window state did not change"};
    return {true, "", ""};
  }
  void pump() { host_.pump(); }
  bool ready(unsigned expected) {
    return ready_count_.load(std::memory_order_acquire) == expected && host_.application_ready();
  }
  unsigned ready_count() const { return ready_count_.load(std::memory_order_acquire); }
  std::string diagnostics() { return host_.application_diagnostics(); }
  static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<test_window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<test_window*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self && message == soui::sdk::bridge_event_message) {
      self->host_.deliver_posted_event(reinterpret_cast<void*>(lparam)); return 0;
    }
    // The runner owns lifecycle transitions; an interactive close is a test abort.
    if (message == WM_CLOSE) { PostQuitMessage(2); return 0; }
    if (message == WM_NCDESTROY) SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    return DefWindowProcW(window, message, wparam, lparam);
  }
 private:
  unsigned id_;
  bool bridge_checks_;
  std::atomic<unsigned> ready_count_{0};
  soui::sdk::json_bridge bridge_; // outlives the host and all its worker callbacks
  soui::sdk::soui_miniblink_host host_;
  HWND handle_{nullptr};
};

class runner final {
 public:
  runner(const options& settings, log_writer& log)
      : settings_(settings), log_(log), phases_(settings.report), root_(executable_directory()) {}
  void execute() {
    const auto scenario = settings_.mode == "mixed" ?
      ",\"durationMs\":" + std::to_string(settings_.duration_ms) +
      ",\"idleMs\":" + std::to_string(settings_.idle_ms) :
      ",\"cycles\":" + std::to_string(settings_.cycles) +
      ",\"warmup\":" + std::to_string(settings_.warmup);
    log_.emit("run.begin", "\"mode\":" + quote(settings_.mode) + scenario +
      ",\"settleMs\":" + std::to_string(settings_.settle_ms) +
      ",\"timeoutMs\":" + std::to_string(settings_.timeout_ms) +
      ",\"pid\":" + std::to_string(GetCurrentProcessId()));
    sample(0, "process");
    auto total = settings_.warmup + settings_.cycles;
    if (settings_.mode == "mixed") {
      total = run_mixed();
    } else if (settings_.mode == "reload") {
      mount(1, 0);
      reject_busy(0);
      settle(); sample(0, "mounted");
      for (unsigned cycle = 1; cycle <= total; ++cycle) {
        reload(1, cycle); settle(); sample(cycle, "cycle");
      }
      close(1, total + 1);
    } else {
      for (unsigned cycle = 1; cycle <= total; ++cycle) {
        mount(1, cycle);
        if (cycle == 1) reject_busy(cycle);
        if (settings_.mode == "multiwindow") {
          mount(2, cycle); mount(3, cycle);
          close(2, cycle);
          reload(1, cycle); reload(3, cycle);
          mount(2, cycle);
          close(3, cycle); close(2, cycle);
        }
        close(1, cycle); settle(); sample(cycle, "cycle");
      }
    }
    settle(); sample(total + 1, "closed");
    step("runtime.shutdown", 0, total + 1, [] { return soui::sdk::shutdown_miniblink_runtime(); });
    sample(total + 1, "runtime-closed");
    step("runtime.reject-restart", 0, total + 1, [this] {
      test_window attempted(1);
      const auto result = attempted.mount(root_);
      return soui::sdk::host_result{!result.ok && result.code == "E_RUNTIME_SHUTDOWN", result.code, result.message};
    });
    step("runtime.shutdown-idempotent", 0, total + 1, [] { return soui::sdk::shutdown_miniblink_runtime(); });
    log_.emit("run.complete");
  }
 private:
  unsigned run_mixed() {
    const auto deadline = clock_type::now() + std::chrono::milliseconds(settings_.duration_ms);
    unsigned cycle = 0;
    do {
      ++cycle;
      log_.emit("cycle.begin", "\"cycle\":" + std::to_string(cycle));
      mount(1, cycle);
      if (cycle == 1) reject_busy(cycle);
      reload(1, cycle);
      step("minimize", 1, cycle, [this] { return windows_[0]->set_minimized(true); });
      settle();
      step("restore", 1, cycle, [this] { return windows_[0]->set_minimized(false); });
      mount(2, cycle); mount(3, cycle);
      close(2, cycle);
      reload(1, cycle); reload(3, cycle);
      mount(2, cycle);
      close(3, cycle); close(2, cycle); close(1, cycle);
      step("idle", 0, cycle, [this] {
        const auto idle_deadline = clock_type::now() + std::chrono::milliseconds(settings_.idle_ms);
        while (clock_type::now() < idle_deadline) {
          service(); MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        return soui::sdk::host_result{true, "", ""};
      });
      sample(cycle, "cycle");
      log_.emit("cycle.complete", "\"cycle\":" + std::to_string(cycle));
    } while (clock_type::now() < deadline);
    return cycle;
  }
  void service() {
    MSG message{};
    for (unsigned n = 0; n < 32 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++n) {
      if (message.message == WM_QUIT) throw std::runtime_error("Window or runtime requested early quit");
      TranslateMessage(&message); DispatchMessageW(&message);
    }
    for (auto& window : windows_) if (window) window->pump();
  }
  void until(const std::function<bool()>& ready) {
    const auto deadline = clock_type::now() + std::chrono::milliseconds(settings_.timeout_ms);
    while (!ready()) {
      if (clock_type::now() >= deadline) {
        std::string detail = "Page/bridge readiness timed out";
        for (auto& window : windows_) if (window) detail += "; " + window->diagnostics();
        throw std::runtime_error(detail);
      }
      service(); MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
  }
  void settle() {
    const auto deadline = clock_type::now() + std::chrono::milliseconds(settings_.settle_ms);
    while (clock_type::now() < deadline) {
      service(); MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
  }
  void step(const char* action, unsigned id, unsigned cycle, const std::function<soui::sdk::host_result()>& work) {
    const auto fields = "\"action\":" + quote(action) + ",\"window\":" + std::to_string(id) + ",\"cycle\":" + std::to_string(cycle);
    log_.emit("step.begin", fields);
    const auto result = work();
    if (!result.ok) {
      log_.emit("step.failed", fields + ",\"code\":" + quote(result.code) + ",\"message\":" + quote(result.message));
      throw std::runtime_error("Public host operation failed: " + result.code);
    }
    log_.emit("step.complete", fields);
  }
  void mount(unsigned id, unsigned cycle) {
    step("mount", id, cycle, [&] {
      auto& window = windows_.at(id - 1);
      if (window) throw std::runtime_error("Window slot is already occupied");
      phases_.emit(cycle, id, "construct.begin");
      window = std::make_unique<test_window>(id, settings_.mode == "bridge" || settings_.mode == "mixed");
      phases_.emit(cycle, id, "construct.end");
      const auto result = window->mount(root_, &phases_, cycle);
      if (result.ok) {
        phases_.emit(cycle, id, "ready.wait.begin");
        until([&] { return window->ready(1); });
        phases_.emit(cycle, id, "ready.wait.end");
      }
      return result;
    });
  }
  void reload(unsigned id, unsigned cycle) {
    step("reload", id, cycle, [&] {
      auto& window = windows_.at(id - 1);
      const auto expected = window->ready_count() + 1;
      const auto result = window->reload();
      if (result.ok) until([&] { return window->ready(expected); });
      return result;
    });
  }
  void reject_busy(unsigned cycle) {
    step("runtime.reject-busy", 0, cycle, [] {
      const auto result = soui::sdk::shutdown_miniblink_runtime();
      return soui::sdk::host_result{!result.ok && result.code == "E_RUNTIME_IN_USE", result.code, result.message};
    });
  }
  void close(unsigned id, unsigned cycle) {
    step("close", id, cycle, [&] {
      auto& window = windows_.at(id - 1);
      const auto result = window->close();
      if (result.ok) window.reset();
      return result;
    });
  }
  void sample(unsigned cycle, const char* phase) {
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
    DWORD handles = 0;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))
        || !GetProcessHandleCount(GetCurrentProcess(), &handles)) throw std::runtime_error("Cannot sample process resources");
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot sample threads");
    THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
    unsigned threads = 0;
    if (Thread32First(snapshot, &entry)) do {
      if (entry.th32OwnerProcessID == GetCurrentProcessId()) ++threads;
    } while (Thread32Next(snapshot, &entry));
    CloseHandle(snapshot);
    log_.emit("sample", "\"phase\":" + quote(phase) + ",\"cycle\":" + std::to_string(cycle)
      + ",\"privateBytes\":" + std::to_string(memory.PrivateUsage) + ",\"workingSetBytes\":" + std::to_string(memory.WorkingSetSize)
      + ",\"handles\":" + std::to_string(handles) + ",\"threads\":" + std::to_string(threads)
      + ",\"gdiObjects\":" + std::to_string(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS))
      + ",\"userObjects\":" + std::to_string(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS))
      + ",\"windowClasses\":" + process_window_classes());
  }
  const options& settings_;
  log_writer& log_;
  mount_phase_writer phases_;
  std::filesystem::path root_;
  std::array<std::unique_ptr<test_window>, 3> windows_;
};
} // namespace

int wmain(int argc, wchar_t** argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  SetProcessDPIAware();
  const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  std::unique_ptr<log_writer> log;
  int exit_code = 1;
  try {
    if (FAILED(com)) throw std::runtime_error("Cannot initialize UI COM apartment");
    const auto settings = parse(argc, argv);
    log = std::make_unique<log_writer>(settings.report);
    WNDCLASSEXW description{}; description.cbSize = sizeof(description);
    description.hInstance = GetModuleHandleW(nullptr); description.lpfnWndProc = test_window::procedure;
    description.lpszClassName = window_class; description.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&description)) throw std::runtime_error("Cannot register acceptance window class");
    {
      runner application(settings, *log);
      application.execute();
    }
    exit_code = 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    if (log) try { log->emit("run.failed", "\"message\":" + quote(error.what())); } catch (...) {}
  }
  if (SUCCEEDED(com)) CoUninitialize();
  return exit_code;
}
