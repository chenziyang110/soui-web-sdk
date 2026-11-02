#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <soui/host/soui_miniblink_host.h>
#include <soui/json_bridge.h>
#include "shutdown-poll.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <cstddef>
#include <memory>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
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
    const std::lock_guard<std::mutex> lock(mutex_);
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
  std::mutex mutex_;
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

std::string creation_file_time() {
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!::GetProcessTimes(::GetCurrentProcess(), &created, &exited, &kernel, &user))
    throw std::runtime_error("Cannot obtain actual process creation time");
  ULARGE_INTEGER value{}; value.LowPart = created.dwLowDateTime; value.HighPart = created.dwHighDateTime;
  return std::to_string(value.QuadPart);
}

unsigned process_thread_count() {
  const DWORD process_id = GetCurrentProcessId();
  const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    throw std::runtime_error("Cannot sample threads: CreateToolhelp32Snapshot WinError=" + std::to_string(error));
  }
  std::unique_ptr<void, decltype(&CloseHandle)> owner(snapshot, &CloseHandle);
  THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
  constexpr auto owner_pid_extent = offsetof(THREADENTRY32, th32OwnerProcessID) + sizeof(entry.th32OwnerProcessID);
  unsigned threads = 0;
  bool invalid_entry_extent = false;
  DWORD returned_entry_size = 0;
  const char* operation = "Thread32First";
  BOOL available = Thread32First(snapshot, &entry);
  DWORD enumeration_error = available ? ERROR_SUCCESS : GetLastError();
  while (available) {
    if (entry.dwSize < owner_pid_extent || entry.dwSize > sizeof(entry)) {
      invalid_entry_extent = true; returned_entry_size = entry.dwSize;
      break;
    }
    if (entry.th32OwnerProcessID == process_id) ++threads;
    operation = "Thread32Next";
    available = Thread32Next(snapshot, &entry);
    if (!available) enumeration_error = GetLastError();
  }
  // Preserve enumeration failure before closing; format errors only after cleanup.
  const BOOL closed = CloseHandle(owner.release());
  const DWORD close_error = closed ? ERROR_SUCCESS : GetLastError();
  if (invalid_entry_extent || enumeration_error != ERROR_NO_MORE_FILES || !closed) {
    std::string message = "Cannot sample threads";
    if (invalid_entry_extent)
      message += ": " + std::string(operation) + " returned invalid THREADENTRY32 dwSize=" + std::to_string(returned_entry_size);
    else if (enumeration_error != ERROR_NO_MORE_FILES)
      message += ": " + std::string(operation) + " WinError=" + std::to_string(enumeration_error);
    if (!closed) message += "; CloseHandle WinError=" + std::to_string(close_error);
    throw std::runtime_error(message);
  }
  return threads;
}

std::filesystem::path message_port_report_path(std::filesystem::path report) {
  report += L".message-port.ndjson";
  return report;
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
          value != L"bridge" && value != L"mixed" && value != L"session")
        throw std::runtime_error("Unknown scenario");
      result.mode = value == L"reload" ? "reload" : value == L"recreate" ? "recreate" :
          value == L"bridge" ? "bridge" : value == L"mixed" ? "mixed" : value == L"session" ? "session" : "multiwindow";
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
      ((result.mode == "mixed" || result.mode == "session") ? (!result.duration_ms || !result.idle_ms || seen[1] || seen[2] ||
          (result.mode == "mixed" && result.idle_ms >= result.timeout_ms))
        : (!result.cycles || seen[6] || seen[7]))) throw std::runtime_error("Invalid scenario arguments");
  if (result.mode == "session" && result.duration_ms < 3120000)
    throw std::runtime_error("Session continuity requires at least 52 real minutes");
  return result;
}

std::filesystem::path executable_directory() {
  std::wstring text(32768, L'\0');
  const auto size = GetModuleFileNameW(nullptr, text.data(), static_cast<DWORD>(text.size()));
  if (!size || size == text.size()) throw std::runtime_error("Cannot locate executable");
  text.resize(size);
  return std::filesystem::path(text).parent_path();
}

struct session_observer {
  std::mutex mutex;
  std::shared_ptr<const soui::runtime::authorization_scope> scope;
  std::atomic<bool> finish_requested{false}, started{false}, finished{false}, failed{false};
  std::atomic<bool> task_exited{false};
  std::atomic<unsigned> short_exits{0};
};

[[noreturn]] void cleanup_failure(log_writer* log, std::string_view code) noexcept {
  try { if (log) log->emit("cleanup.failed", "\"code\":" + quote(std::string(code))); }
  catch (...) {
    constexpr char failure[] = "Acceptance cleanup failed; retaining owners until process exit.\n";
    DWORD written{}; ::WriteFile(::GetStdHandle(STD_ERROR_HANDLE), failure, sizeof(failure) - 1, &written, nullptr);
  }
  // Do not unwind HWND/bridge/adapter owners. External Owned supervision retains
  // this failure and disposes remaining members; it is not natural closure.
  ::ExitProcess(3);
}

struct observed_handler_exit {
  std::shared_ptr<session_observer> state;
  log_writer* log;
  unsigned window;
  std::string operation, session;
  bool long_task;
  ~observed_handler_exit() noexcept {
    if (long_task) state->task_exited.store(true, std::memory_order_release);
    else state->short_exits.fetch_add(1, std::memory_order_release);
    try {
      if (log) log->emit("task.handler.exit", "\"window\":" + std::to_string(window) +
          ",\"operationId\":" + quote(operation) + ",\"sessionId\":" + quote(session));
    } catch (...) { state->failed.store(true, std::memory_order_release); }
  }
};

class test_window final {
 public:
  explicit test_window(unsigned id, bool bridge_checks = false, bool session_checks = false, log_writer* log = nullptr, log_writer* message_port_log = nullptr) : id_(id), bridge_checks_(bridge_checks), session_checks_(session_checks), log_(log), message_port_log_(message_port_log), bridge_("app://acceptance-" + std::to_string(id)),
      host_(std::make_unique<soui::sdk::dynamic_miniblink_adapter>()) {
    if (!bridge_.register_method("acceptance.echo", "acceptance.echo", [this](std::string_view params) {
          if (params != "{}") return std::string{"{\"error\":\"E_INVALID_PARAMS\"}"};
          return "{\"window\":" + std::to_string(id_) + ",\"bridgeChecks\":" + (bridge_checks_ ? "true" : "false") + (session_checks_ ? ",\"sessionRole\":" + quote(id_ == 1 ? "anchor" : "background") : "") + "}";
        }) || !bridge_.register_method("acceptance.ready", "acceptance.ready", [this](std::string_view params) {
          if (params != "{}") return std::string{"{\"error\":\"E_INVALID_PARAMS\"}"};
          ready_count_.fetch_add(1, std::memory_order_release);
          return std::string{"{}"};
        })) throw std::runtime_error("Cannot register acceptance methods");
    if (!bridge_.register_action("acceptance.message-port.report", "acceptance.message-port.report", R"contract({
      "protocol":"soui.action/1","title":"Real browser MessageChannel observations","description":"Test-only bounded browser receipts; not a native Worker join proof",
      "inputSchema":{"type":"object","properties":{
        "sequence":{"type":"integer","minimum":0,"maximum":9},
        "caseId":{"type":"string","maxLength":24,"enum":["run","fifo-bidirectional","drained-refill","transfer","close-later-send","callback-reentry","worker-fifo","worker-cooperative-close","worker-terminate"]},
        "status":{"type":"string","maxLength":11,"enum":["begin","pass","failed","unavailable","end"]},
        "sent":{"type":"integer","minimum":0,"maximum":100},"received":{"type":"integer","minimum":0,"maximum":100},
        "quietMs":{"type":"integer","minimum":0,"maximum":1000},"trace":{"type":"string","maxLength":2048}},
        "required":["sequence","caseId","status","sent","received","quietMs","trace"],"additionalProperties":false},
      "outputSchema":{"type":"object","properties":{"accepted":{"type":"boolean"},"documentSession":{"type":"string","minLength":1,"maxLength":256}},
        "required":["accepted","documentSession"],"additionalProperties":false}
    })contract", [this](const soui::runtime::bridge_request& request) {
      if (!message_port_log_ || !request.authorization || !request.authorization->active() || request.session_id.empty())
        return std::string{"{\"error\":\"E_UNAUTHORIZED\"}"};
      message_port_log_->emit("message-port.report", "\"window\":" + std::to_string(id_) +
          ",\"pid\":" + std::to_string(GetCurrentProcessId()) + ",\"creationFileTime\":" + quote(creation_file_time()) +
          ",\"sessionId\":" + quote(request.session_id) + ",\"scopeId\":" + quote(request.authorization->scope_id()) +
          ",\"data\":" + request.params_json);
      return "{\"accepted\":true,\"documentSession\":" + quote(request.session_id) + "}";
    })) throw std::runtime_error("Cannot register real browser observation action");
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
    if (bridge_checks_ && !bridge_.register_task_handler("acceptance.count",
        [state = observation_, log = session_checks_ ? log_ : nullptr, id = id_](const soui::runtime::task_invocation& invocation,
           const soui::runtime::task_control& control) {
          observed_handler_exit exited{state, log, id, invocation.operation_id, invocation.session_id, false};
          const auto steps = invocation.parameters_json == R"({"steps":10})" ? 10 :
                             invocation.parameters_json == R"({"steps":100})" ? 100 : 0;
          if (!steps) return soui::runtime::task_outcome{
              soui::runtime::result_kind::failure, "{}", "Unsupported acceptance input."};
          for (int index = 1; index <= steps; ++index) {
            if (control.cancellation_requested()) return soui::runtime::task_outcome{
                soui::runtime::result_kind::task_cancelled, "{}", "Cancelled."};
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if (!control.report_progress(static_cast<std::uint32_t>(index * 100 / steps), "Counting"))
              return soui::runtime::task_outcome{
                  soui::runtime::result_kind::task_cancelled, "{}", "Cancelled."};
          }
          return soui::runtime::task_outcome{
              soui::runtime::result_kind::success,
              "{\"steps\":" + std::to_string(steps) + "}", "Completed."};
        })) throw std::runtime_error("Cannot register public SDK task probe");
    if (session_checks_) {
      if (!bridge_.enable_session_renewal() || !bridge_.register_contextual_method(
          "acceptance.session.probe", "acceptance.session.probe", [](const soui::runtime::bridge_request& request) {
            if (request.params_json != "{}" || !request.authorization || !request.authorization->active())
              return std::string{"{\"error\":\"E_UNAUTHORIZED\"}"};
            return "{\"scopeId\":" + quote(request.authorization->scope_id()) + "}";
          }) || !bridge_.register_method("acceptance.session.command", "acceptance.session.command",
          [state = observation_](std::string_view params) {
            if (params != "{}") return std::string{"{\"error\":\"E_INVALID_PARAMS\"}"};
            return std::string{"{\"finish\":"} + (state->finish_requested.load() ? "true}" : "false}");
          }) || !bridge_.register_action("acceptance.session.report", "acceptance.session.report", R"contract({
            "protocol":"soui.action/1","title":"Acceptance session observation","description":"Finite public consumer telemetry",
            "inputSchema":{"type":"object","properties":{
              "phase":{"type":"string","maxLength":16,"enum":["started","heartbeat","renewed","finished","background","failed"]},
              "taskId":{"type":"string","minLength":1,"maxLength":128},
              "generation":{"type":"integer","minimum":1,"maximum":2147483647},
              "progressSequence":{"type":"integer","minimum":0,"maximum":2147483647},
              "pendingRequests":{"type":"integer","minimum":0,"maximum":1},
              "subscriptions":{"type":"integer","minimum":0,"maximum":1},
              "state":{"type":"string","maxLength":16,"enum":["running","cancelled","failed"]}},
              "required":["phase","taskId","generation","progressSequence","pendingRequests","subscriptions","state"],"additionalProperties":false},
            "outputSchema":{"type":"object","properties":{"accepted":{"type":"boolean"}},"required":["accepted"],"additionalProperties":false}
          })contract", [this](const soui::runtime::bridge_request& request) {
            if (!request.authorization || !request.authorization->active())
              return std::string{"{\"error\":\"E_UNAUTHORIZED\"}"};
            { const std::lock_guard<std::mutex> lock(observation_->mutex); observation_->scope = request.authorization; }
            log_->emit("session.report", "\"window\":" + std::to_string(id_) +
                ",\"scopeId\":" + quote(request.authorization->scope_id()) +
                ",\"sessionId\":" + quote(request.session_id) + ",\"data\":" + request.params_json);
            // The registered schema admits one scalar phase and no nested fields.
            if (request.params_json.find("\"phase\":\"started\"") != std::string::npos) observation_->started.store(true);
            if (request.params_json.find("\"phase\":\"finished\"") != std::string::npos) observation_->finished.store(true);
            if (request.params_json.find("\"phase\":\"failed\"") != std::string::npos) observation_->failed.store(true);
            return std::string{"{\"accepted\":true}"};
          })) throw std::runtime_error("Cannot register public session acceptance probes");
      if (id_ == 1 && !bridge_.register_task_handler("acceptance.session",
          [state = observation_, log = log_](const soui::runtime::task_invocation& invocation,
                                             const soui::runtime::task_control& control) {
            observed_handler_exit exited{state, log, 1, invocation.operation_id, invocation.session_id, true};
            if (invocation.parameters_json != "{}") return soui::runtime::task_outcome{
                soui::runtime::result_kind::failure, "{}", "Unsupported session input."};
            auto next = clock_type::now();
            std::uint32_t sequence = 0;
            while (!control.cancellation_requested()) {
              if (clock_type::now() >= next) {
                ++sequence;
                if (!control.report_progress(99, "session-tick:" + std::to_string(sequence))) break;
                next = clock_type::now() + std::chrono::seconds(1);
              }
              std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            return soui::runtime::task_outcome{soui::runtime::result_kind::task_cancelled, "{}", "Cancelled."};
          })) throw std::runtime_error("Cannot register actual session task");
    }
  }
  test_window(const test_window&) = delete;
  test_window& operator=(const test_window&) = delete;
  ~test_window() noexcept {
    try {
      const auto closed = host_.destroy();
      if (!closed.ok) cleanup_failure(log_, closed.code);
      if (handle_ && !::DestroyWindow(handle_)) cleanup_failure(log_, "E_PARENT_WINDOW_CLOSE");
    } catch (...) { cleanup_failure(log_, "E_ACCEPTANCE_CLEANUP_EXCEPTION"); }
  }

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
  soui::sdk::host_result close() {
    const auto result = host_.destroy();
    if (result.ok && session_checks_) {
      std::shared_ptr<const soui::runtime::authorization_scope> scope;
      { const std::lock_guard<std::mutex> lock(observation_->mutex); scope = observation_->scope; }
      const bool exited = id_ == 1 ? observation_->task_exited.load() : observation_->short_exits.load() == 2;
      if (!scope || scope->active() || !exited) throw std::runtime_error("Real session cleanup was not observed");
      log_->emit("session.revoked", "\"window\":" + std::to_string(id_) + ",\"scopeId\":" + quote(scope->scope_id()) +
          ",\"active\":false,\"handlerExited\":true");
    }
    return result;
  }
  std::shared_ptr<session_observer> observation() const { return observation_; }
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
  bool session_checks_;
  log_writer* log_;
  log_writer* message_port_log_;
  std::shared_ptr<session_observer> observation_{std::make_shared<session_observer>()};
  std::atomic<unsigned> ready_count_{0};
  soui::sdk::json_bridge bridge_; // outlives the host and all its worker callbacks
  soui::sdk::soui_miniblink_host host_;
  HWND handle_{nullptr};
};

class runner final {
 public:
  runner(const options& settings, log_writer& log)
      : settings_(settings), log_(log), phases_(settings.report), message_port_log_(message_port_report_path(settings.report)), root_(executable_directory()) {}
  void execute() {
    const auto scenario = (settings_.mode == "mixed" || settings_.mode == "session") ?
      ",\"durationMs\":" + std::to_string(settings_.duration_ms) +
      ",\"idleMs\":" + std::to_string(settings_.idle_ms) :
      ",\"cycles\":" + std::to_string(settings_.cycles) +
      ",\"warmup\":" + std::to_string(settings_.warmup);
    log_.emit("run.begin", "\"mode\":" + quote(settings_.mode) + scenario +
      ",\"settleMs\":" + std::to_string(settings_.settle_ms) +
      ",\"timeoutMs\":" + std::to_string(settings_.timeout_ms) +
      ",\"pid\":" + std::to_string(GetCurrentProcessId()) + ",\"creationFileTime\":" + quote(creation_file_time()));
    sample(0, "process");
    auto total = settings_.warmup + settings_.cycles;
    if (settings_.mode == "mixed") {
      total = run_mixed();
    } else if (settings_.mode == "session") {
      total = run_session();
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
    step("runtime.shutdown", 0, total + 1, [this] { return poll_shutdown(); });
    sample(total + 1, "runtime-closed");
    step("runtime.reject-restart", 0, total + 1, [this] {
      test_window attempted(1);
      const auto result = attempted.mount(root_);
      return soui::sdk::host_result{!result.ok && result.code == "E_RUNTIME_SHUTDOWN", result.code, result.message};
    });
    step("runtime.shutdown-idempotent", 0, total + 1, [this] { return poll_shutdown(); });
    log_.emit("run.complete");
  }
 private:
  unsigned run_session() {
    mount(1, 0); reject_busy(0);
    const auto state = windows_[0]->observation();
    if (!state->started.load()) throw std::runtime_error("Anchor task/subscription was not established");
    sample(0, "anchor");
    const auto deadline = clock_type::now() + std::chrono::milliseconds(settings_.duration_ms);
    auto next_background = clock_type::now();
    unsigned cycle = 0;
    while (clock_type::now() < deadline) {
      if (state->failed.load() || state->finished.load()) throw std::runtime_error("Anchor ended before its real duration");
      if (clock_type::now() >= next_background) {
        ++cycle; log_.emit("cycle.begin", "\"cycle\":" + std::to_string(cycle));
        step("minimize", 1, cycle, [this] { return windows_[0]->set_minimized(true); });
        settle();
        step("restore", 1, cycle, [this] { return windows_[0]->set_minimized(false); });
        mount(2, cycle); mount(3, cycle); close(3, cycle); close(2, cycle);
        sample(cycle, "anchor"); log_.emit("cycle.complete", "\"cycle\":" + std::to_string(cycle));
        next_background = clock_type::now() + std::chrono::milliseconds(settings_.idle_ms);
      }
      service(); MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    state->finish_requested.store(true);
    until([&] {
      if (state->failed.load()) throw std::runtime_error("Anchor continuity failed");
      return state->finished.load();
    });
    if (!state->task_exited.load()) throw std::runtime_error("Actual anchor handler has not exited");
    log_.emit("session.end"); sample(cycle + 1, "anchor-finished"); close(1, cycle + 1);
    return cycle;
  }
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
  soui::sdk::host_result poll_shutdown() {
    return soui::acceptance::poll_runtime_shutdown(
        [this](const std::function<bool()>& ready) { until(ready); },
        [] { return soui::sdk::shutdown_miniblink_runtime(); });
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
      window = std::make_unique<test_window>(id, settings_.mode == "bridge" || settings_.mode == "mixed" || settings_.mode == "session", settings_.mode == "session", &log_, &message_port_log_);
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
      soui::sdk::host_result result{false, "", ""};
      bool accepted = false;
      // Poll the same pending reload, then wait for its document within one
      // original deadline. Never request a successor after reload succeeds.
      until([&] {
        if (!accepted) {
          result = window->reload();
          if (!result.ok) return result.code != "E_RELOAD_PENDING";
          accepted = true;
        }
        return window->ready(expected);
      });
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
      const auto deadline = clock_type::now() + std::chrono::milliseconds(settings_.timeout_ms);
      for (;;) {
        const auto result = window->close();
        if (result.ok) {
          window.reset();
          return result;
        }
        if ((result.code != "E_BUSY" && result.code != "E_CURSOR_CLOSE_PENDING") ||
            clock_type::now() >= deadline) return result;
        // Preserve the real owners while the public close contract is pending.
        service();
        if (clock_type::now() >= deadline) return result;
        MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (clock_type::now() >= deadline) return result;
      }
    });
  }
  void sample(unsigned cycle, const char* phase) {
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
    DWORD handles = 0;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))
        || !GetProcessHandleCount(GetCurrentProcess(), &handles)) throw std::runtime_error("Cannot sample process resources");
    const auto threads = process_thread_count();
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
  log_writer message_port_log_;
  std::filesystem::path root_;
  std::array<std::unique_ptr<test_window>, 3> windows_;
};
} // namespace

static int run_acceptance(int argc, wchar_t** argv) {
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

// Use the default Unicode GUI CRT startup, including its original wide argv
// configuration. Keep Owned's inherited diagnostic handles untouched.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  return run_acceptance(__argc, __wargv);
}
