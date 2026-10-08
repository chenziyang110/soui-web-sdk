#include "supervisor_clock.h"
#include <windows.h>
#include <objbase.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <soui/bridge_adapter.h>
#include <soui/host/soui_miniblink_host.h>
#include <soui/json_bridge.h>
#include <soui/version.h>
#include "session_observation.h"
#include "session_observation_tests.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {
using monotonic_clock = std::chrono::steady_clock;
constexpr std::size_t maximum_record_bytes = 16384;
constexpr wchar_t window_class[] = L"InstalledSouiRenewalProbePreview11";
constexpr const char* schema = "soui.renewal-probe/v2";

const char* architecture() noexcept {
#ifdef _WIN64
  return "x64";
#else
  return "x86";
#endif
}

std::string json_quoted(std::string_view value) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string result{"\""};
  for (const unsigned char byte : value) {
    if (byte == '"' || byte == '\\') {
      result += '\\'; result += static_cast<char>(byte);
    } else if (byte < 0x20U) {
      result += "\\u00"; result += digits[byte >> 4U]; result += digits[byte & 15U];
    } else result += static_cast<char>(byte);
  }
  return result + '"';
}

std::uint64_t elapsed_ms(monotonic_clock::time_point since) {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      monotonic_clock::now() - since).count());
}

std::string unique_run_id() {
  FILETIME now{}; GetSystemTimeAsFileTime(&now);
  const auto ticks = (static_cast<std::uint64_t>(now.dwHighDateTime) << 32U) | now.dwLowDateTime;
  return "renewal-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(ticks);
}

class unique_handle final {
 public:
  explicit unique_handle(HANDLE value = INVALID_HANDLE_VALUE) noexcept : value_(value) {}
  ~unique_handle() { if (value_ != INVALID_HANDLE_VALUE && value_ != nullptr) CloseHandle(value_); }
  unique_handle(const unique_handle&) = delete;
  unique_handle& operator=(const unique_handle&) = delete;
  HANDLE get() const noexcept { return value_; }
 private:
  HANDLE value_;
};

class durable_log final {
 public:
  explicit durable_log(const std::filesystem::path& file)
      : file_(CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr)) {
    if (file_.get() == INVALID_HANDLE_VALUE)
      throw std::runtime_error("Cannot exclusively create the new report file");
  }
  durable_log(const durable_log&) = delete;
  durable_log& operator=(const durable_log&) = delete;
  void emit(std::string_view event, const std::string& fields = {}) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (failed_.load()) throw std::runtime_error("The report writer has already failed");
    const auto elapsed = elapsed_ms(started_);
    const auto next = sequence_ + 1;
    std::string line = "{\"schema\":" + json_quoted(schema) + ",\"seq\":" + std::to_string(next) +
        ",\"elapsedMs\":" + std::to_string(elapsed) + ",\"event\":" + json_quoted(event);
    if (!fields.empty()) line += ',' + fields;
    line += "}\n";
    std::size_t offset = 0;
    while (offset < line.size()) {
      DWORD written = 0;
      const auto remaining = static_cast<DWORD>(std::min<std::size_t>(line.size() - offset, 65536));
      if (!WriteFile(file_.get(), line.data() + offset, remaining, &written, nullptr) || written == 0) {
        failed_.store(true); throw std::runtime_error("Report write failed");
      }
      offset += written;
    }
    if (!FlushFileBuffers(file_.get())) {
      failed_.store(true); throw std::runtime_error("Report durability acknowledgement failed");
    }
    sequence_ = next;
  }
  bool failed() const noexcept { return failed_.load(); }
 private:
  unique_handle file_;
  std::mutex mutex_;
  const monotonic_clock::time_point started_{monotonic_clock::now()};
  std::uint64_t sequence_{0};
  std::atomic<bool> failed_{false};
};

struct options {
  std::filesystem::path runtime;
  std::filesystem::path report;
  std::uint64_t duration_ms{0};
  bool self_check{false};
};

std::uint64_t duration(std::wstring_view text) {
  if (text == L"30000") return 30000;
  if (text == L"2100000") return 2100000;
  if (text == L"28800000") return 28800000;
  throw std::runtime_error("Duration must be exactly 30000, 2100000 or 28800000 milliseconds");
}

options parse(const std::vector<std::wstring>& args) {
  if (args.size() == 1 && args.front() == L"--self-check") return {{}, {}, 0, true};
  if (args.size() != 6) throw std::runtime_error("Expected --runtime ABS_DIR --report ABS_NEW_FILE --duration-ms N");
  options result;
  std::array<bool, 3> seen{};
  for (std::size_t index = 0; index < args.size(); index += 2) {
    std::size_t key = 0;
    if (args[index] == L"--runtime") result.runtime = args[index + 1];
    else if (args[index] == L"--report") { key = 1; result.report = args[index + 1]; }
    else if (args[index] == L"--duration-ms") { key = 2; result.duration_ms = duration(args[index + 1]); }
    else throw std::runtime_error("Unknown probe argument");
    if (seen[key]) throw std::runtime_error("Duplicate probe argument");
    seen[key] = true;
  }
  if (!seen[0] || !seen[1] || !seen[2] || !result.runtime.is_absolute() || !result.report.is_absolute())
    throw std::runtime_error("Runtime and report must both be absolute paths");
  return result;
}

bool same_or_below(const std::filesystem::path& candidate, const std::filesystem::path& directory) {
  const auto a = candidate.native();
  auto b = directory.native();
  while (b.size() > 3 && (b.back() == L'\\' || b.back() == L'/')) b.pop_back();
  if (a.size() < b.size()) return false;
  const auto equal = CompareStringOrdinal(a.data(), static_cast<int>(b.size()), b.data(),
      static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
  return equal && (a.size() == b.size() || b.back() == L'\\' || b.back() == L'/' ||
                   a[b.size()] == L'\\' || a[b.size()] == L'/');
}

void validate_paths(options& settings) {
  settings.runtime = std::filesystem::canonical(settings.runtime);
  if (!std::filesystem::is_directory(settings.runtime)) throw std::runtime_error("Runtime directory is missing");
  const auto report_parent = std::filesystem::canonical(settings.report.parent_path());
  settings.report = report_parent / settings.report.filename();
  if (!std::filesystem::is_directory(report_parent) || settings.report.filename().empty() ||
      same_or_below(settings.report, settings.runtime)) throw std::runtime_error("New report must be outside the runtime directory");
  if (std::filesystem::exists(settings.report)) throw std::runtime_error("Refuse to overwrite an existing report");
  for (const auto& required : {std::filesystem::path{L"node.dll"}, std::filesystem::path{L"resources/index.html"}})
    if (!std::filesystem::is_regular_file(settings.runtime / required)) throw std::runtime_error("A required runtime payload file is missing");
}

bool message_object(std::string_view text) {
  // Transport already validated/canonicalized the JSON. Recognize exactly one
  // string member without importing the SDK's private JSON dependency.
  constexpr std::string_view prefix = "{\"message\":\"";
  if (text.size() < prefix.size() + 2 || text.substr(0, prefix.size()) != prefix) return false;
  for (std::size_t index = prefix.size(); index < text.size(); ++index) {
    if (text[index] == '\\') { ++index; continue; }
    if (text[index] == '"') return text.substr(index) == "\"}";
  }
  return false;
}

enum class probe_method { describe, ready, record, finish, fail };

struct observation {
  unsigned ready_count{0};
  unsigned long_invocations{0};
  unsigned result_invocations{0};
  unsigned shutdown_sessions{0};
  unsigned bootstrap_calls{0};
  unsigned prebootstrap_retirements{0};
  unsigned bound_shutdowns{0};
  std::uint64_t described_at{0};
  std::uint64_t ready_at{0};
  std::uint64_t last_record_at{0};
  std::uint64_t finish_at{0};
  bool finish_requested{false};
  bool long_cancelled{false};
  bool result_stopped{false};
  bool has_scope{false};
  bool scope_active{false};
};

class probe_state final {
 public:
  probe_state(durable_log& log, std::uint64_t duration_ms, std::string run_id)
      : log_(log), duration_ms_(duration_ms), run_id_(std::move(run_id)) {}
  probe_state(const probe_state&) = delete;
  probe_state& operator=(const probe_state&) = delete;

  std::uint64_t now() const { return elapsed_ms(started_); }
  std::uint64_t requested_duration() const noexcept { return duration_ms_; }
  const std::string& run_id() const noexcept { return run_id_; }
  bool failed() const noexcept { return failed_.load() || log_.failed(); }
  bool closing() const noexcept { return closing_.load(); }
  unsigned active_callbacks() const noexcept { return callbacks_.load(); }
  void enter_callback() noexcept { callbacks_.fetch_add(1); }
  void leave_callback() noexcept { callbacks_.fetch_sub(1); }
  void begin_close() noexcept {
    try { std::lock_guard<std::mutex> guard(mutex_); phase_.begin_close(); closing_.store(true); }
    catch (...) { fail("Cannot enter closing phase"); }
  }

  void fail(std::string_view reason) noexcept {
    if (failed_.exchange(true)) return;
    try { log_.emit("run.failed", "\"message\":" + json_quoted(reason)); }
    catch (...) { /* The exit status and absent complete log remain failures. */ }
  }
  observation snapshot() const {
    std::lock_guard<std::mutex> guard(mutex_);
    auto result = observed_;
    result.has_scope = static_cast<bool>(scope_);
    result.scope_active = scope_ && scope_->active();
    result.bootstrap_calls = phase_.bootstrap_calls();
    result.prebootstrap_retirements = phase_.prebootstrap_retirements();
    result.bound_shutdowns = phase_.bound_closes();
    result.shutdown_sessions = phase_.shutdown_callbacks();
    return result;
  }

  void begin_bootstrap() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (failed() || closing() || scope_ || described_ || observed_.ready_count ||
        observed_.long_invocations || observed_.result_invocations)
      throw std::runtime_error("Bootstrap after probe work/closure is forbidden");
    phase_.begin_bootstrap();
    log_.emit("bridge.bootstrap.begin", "\"bootstrapCount\":1,\"phase\":\"bootstrap-in-flight\"");
  }
  void bind_bootstrap(const renewal_probe::metadata& response, const std::string& request_id) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (failed() || closing()) throw std::runtime_error("Cannot bind bootstrap after failure/closure");
    phase_.bind(response, request_id); session_ = phase_.session(); origin_ = phase_.origin();
    log_.emit("bridge.bootstrap.bound", "\"sessionId\":" + json_quoted(session_) +
        ",\"origin\":" + json_quoted(origin_) + ",\"bootstrapCount\":1,\"phase\":\"bound-before-describe\"");
  }

  std::string handle(probe_method method, const soui::runtime::bridge_request& request) noexcept {
    try {
      std::lock_guard<std::mutex> guard(mutex_);
      if (closing() || failed()) throw std::runtime_error("Probe is closing or failed");
      if (!request.authorization || !request.authorization->active() ||
          !request.authorization->matches(request.session_id, request.origin))
        throw std::runtime_error("Probe request has no matching live authorization");
      if (!scope_) {
        if (method != probe_method::describe) throw std::runtime_error("Describe must be the first contextual probe request");
        phase_.describe(request.session_id, request.origin);
        scope_ = request.authorization;
      }
      if (scope_ != request.authorization || session_ != request.session_id || origin_ != request.origin)
        throw std::runtime_error("Document authorization identity changed");
      if (request.params_json.size() > maximum_record_bytes ||
          (!request.token.empty() && request.params_json.find(request.token) != std::string::npos))
        throw std::runtime_error("Probe params exceed the bound or contain a bearer credential");
      if (method != probe_method::record && method != probe_method::fail && request.params_json != "{}")
        throw std::runtime_error("Probe method requires empty params");
      const auto identity = "\"sessionId\":" + json_quoted(session_) + ",\"origin\":" + json_quoted(origin_) +
          ",\"sameAuthorization\":true";
      switch (method) {
        case probe_method::describe:
          if (described_) throw std::runtime_error("Duplicate describe request");
          described_ = true; observed_.described_at = now();
          log_.emit("probe.describe", identity);
          return "{\"runId\":" + json_quoted(run_id_) + ",\"durationMs\":" + std::to_string(duration_ms_) +
              ",\"version\":" + json_quoted(soui::sdk::version) + ",\"architecture\":" + json_quoted(architecture()) + '}';
        case probe_method::ready:
          if (++observed_.ready_count != 1 || observed_.long_invocations != 1 || observed_.result_invocations != 1)
            throw std::runtime_error("Ready must follow exactly one invocation of each task");
          phase_.ready();
          observed_.ready_at = now(); observed_.last_record_at = observed_.ready_at;
          log_.emit("probe.ready", identity + ",\"count\":1");
          return "{}";
        case probe_method::record:
          // JSON object shape was checked by the public transport. Store as an
          // escaped string, never splice page-supplied text into NDJSON fields.
          if (observed_.finish_requested) throw std::runtime_error("Record after finish");
          log_.emit("web.record", identity + ",\"dataJson\":" + json_quoted(request.params_json));
          observed_.last_record_at = now();
          return "{}";
        case probe_method::finish:
          if (observed_.finish_requested || observed_.ready_count != 1 ||
              observed_.long_invocations != 1 || observed_.result_invocations != 1 ||
              !observed_.long_cancelled || !observed_.result_stopped ||
              now() - observed_.ready_at < duration_ms_)
            throw std::runtime_error("Premature, duplicate or incomplete finish");
          log_.emit("probe.finish", identity);
          observed_.finish_requested = true; observed_.finish_at = now();
          return "{}";
        case probe_method::fail:
          if (request.params_json.size() > 4096 || !message_object(request.params_json))
            throw std::runtime_error("Failure diagnostic must be one bounded message string");
          log_.emit("run.failed", identity + ",\"dataJson\":" + json_quoted(request.params_json));
          failed_.store(true);
          return "{}";
      }
      throw std::runtime_error("Invalid probe method");
    } catch (const std::exception& error) {
      fail(error.what());
      return "{\"error\":\"E_PROBE_FAILED\",\"message\":\"Native renewal probe rejected the request.\"}";
    } catch (...) {
      fail("Unexpected probe callback failure");
      return "{\"error\":\"E_PROBE_FAILED\",\"message\":\"Native renewal probe failed.\"}";
    }
  }

  soui::runtime::task_outcome task(bool long_running, const soui::runtime::task_invocation& invocation,
                                  const soui::runtime::task_control& control) noexcept {
    const char* kind = long_running ? "probe.long" : "probe.result";
    try {
      {
        std::lock_guard<std::mutex> guard(mutex_);
        auto& count = long_running ? observed_.long_invocations : observed_.result_invocations;
        ++count;
        if (closing() || failed() || !scope_ || !scope_->active() ||
            session_ != invocation.session_id || origin_ != invocation.origin ||
            invocation.parameters_json != "{}" || count != 1)
          throw std::runtime_error("Invalid or duplicate fixed task invocation");
        log_.emit("task.invoked", "\"kind\":" + json_quoted(kind) + ",\"count\":" + std::to_string(count) +
            ",\"sessionId\":" + json_quoted(invocation.session_id) + ",\"operationId\":" + json_quoted(invocation.operation_id));
      }
      if (!long_running) {
        stopped(false, "completed");
        return {soui::runtime::result_kind::success, "{\"value\":42}", "Fixed result retained."};
      }
      const auto started = monotonic_clock::now();
      std::uint64_t next_progress = 0;
      for (;;) {
        if (control.cancellation_requested()) {
          stopped(true, "cancelled");
          return {soui::runtime::result_kind::task_cancelled, "{}", "Original long task cancelled."};
        }
        if (closing() || failed()) {
          stopped(true, "shutdown");
          return {soui::runtime::result_kind::task_cancelled, "{}", "Probe shutdown."};
        }
        const auto elapsed = elapsed_ms(started);
        if (elapsed >= duration_ms_ + 90000) throw std::runtime_error("Long task exceeded its hard deadline");
        if (elapsed >= next_progress) {
          {
            std::lock_guard<std::mutex> guard(mutex_);
            if (!scope_ || !scope_->active()) throw std::runtime_error("Long task authorization expired or was revoked");
          }
          const auto progress = static_cast<std::uint32_t>(std::min<std::uint64_t>(99, elapsed * 100 / duration_ms_));
          if (!control.report_progress(progress, "elapsedMs=" + std::to_string(elapsed))) {
            if (control.cancellation_requested()) {
              stopped(true, "cancelled");
              return {soui::runtime::result_kind::task_cancelled, "{}", "Original long task cancelled."};
            }
            throw std::runtime_error("Public task progress submission failed");
          }
          next_progress = elapsed + 1000;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    } catch (const std::exception& error) {
      fail(error.what());
    } catch (...) { fail("Unexpected task worker failure"); }
    try { stopped(long_running, "failed"); } catch (...) { fail("Task failure logging failed"); }
    return {soui::runtime::result_kind::failure, "{}", "Native probe worker failed."};
  }

  void native_event(std::string_view data) {
    if (data.size() > maximum_record_bytes) {
      fail("Native event exceeded its bound"); throw std::runtime_error("Oversized native event");
    }
    try { log_.emit("native.event", "\"dataJson\":" + json_quoted(data)); }
    catch (...) { fail("Native event persistence failed"); throw; }
  }

  void session_shutdown(const std::string& session) noexcept {
    try {
      std::lock_guard<std::mutex> guard(mutex_);
      const std::string phase = phase_.phase();
      const char* classification = "rejected";
      bool accepted = false;
      try {
        if (!phase_.bound() && (scope_ || described_ || observed_.ready_count ||
            observed_.long_invocations || observed_.result_invocations))
          throw std::runtime_error("Unbound shutdown after probe work");
        classification = phase_.shutdown(session); accepted = true;
      } catch (...) { /* Persist the actual phase and opaque ID before latching failure. */ }
      log_.emit("session.shutdown", "\"sessionId\":" + json_quoted(session) +
          ",\"phase\":" + json_quoted(phase) + ",\"classification\":" + json_quoted(classification) +
          ",\"bootstrapCount\":" + std::to_string(phase_.bootstrap_calls()) +
          ",\"bound\":" + (phase_.bound() ? "true" : "false") +
          ",\"readyCount\":" + std::to_string(observed_.ready_count));
      if (!accepted) fail("Unexpected document session shutdown or navigation");
    } catch (...) { fail("Session shutdown observation failed"); }
  }

 private:
  void stopped(bool long_running, const char* reason) {
    std::lock_guard<std::mutex> guard(mutex_);
    log_.emit("task.stopped", "\"kind\":" + json_quoted(long_running ? "probe.long" : "probe.result") +
        ",\"reason\":" + json_quoted(reason));
    if (long_running) observed_.long_cancelled = std::string_view(reason) == "cancelled";
    else observed_.result_stopped = std::string_view(reason) == "completed";
  }

  durable_log& log_;
  const std::uint64_t duration_ms_;
  const std::string run_id_;
  const monotonic_clock::time_point started_{monotonic_clock::now()};
  mutable std::mutex mutex_;
  observation observed_;
  renewal_probe::session_phase phase_; // All access is under mutex_.
  std::shared_ptr<const soui::runtime::authorization_scope> scope_;
  std::string session_;
  std::string origin_;
  bool described_{false};
  std::atomic<bool> failed_{false};
  std::atomic<bool> closing_{false};
  std::atomic<unsigned> callbacks_{0};
};

class observed_bridge final : public soui::sdk::bridge_adapter {
 public:
  explicit observed_bridge(probe_state& state) : state_(state), inner_("app://installed-renewal-probe") {
    if (!inner_.enable_session_renewal()) throw std::runtime_error("Cannot enable public session renewal");
    const std::array<std::pair<const char*, probe_method>, 5> methods{{
      {"probe.describe", probe_method::describe}, {"probe.ready", probe_method::ready},
      {"probe.record", probe_method::record}, {"probe.finish", probe_method::finish}, {"probe.fail", probe_method::fail}}};
    for (const auto& method : methods) {
      const auto operation = method.second;
      if (!inner_.register_contextual_method(method.first, method.first,
          [&state, operation](const soui::runtime::bridge_request& request) { return state.handle(operation, request); }))
        throw std::runtime_error("Cannot register public contextual probe method");
    }
    if (!inner_.register_task_handler("probe.long", [&state](const auto& call, const auto& control) {
          return state.task(true, call, control);
        }) || !inner_.register_task_handler("probe.result", [&state](const auto& call, const auto& control) {
          return state.task(false, call, control);
        })) throw std::runtime_error("Cannot register fixed public task handlers");
  }
  std::string handle_json(std::string_view request, bool gesture = false) override {
    struct callback_guard {
      probe_state& state;
      explicit callback_guard(probe_state& value) : state(value) { state.enter_callback(); }
      ~callback_guard() { state.leave_callback(); }
    } guard{state_};
    try {
      const auto metadata = renewal_probe::metadata_reader(request).read();
      if (metadata.method.empty()) throw std::runtime_error("Bridge request method is absent");
      const bool bootstrap = metadata.method == "bridge.bootstrap";
      if (bootstrap) state_.begin_bootstrap();
      auto response = inner_.handle_json(request, gesture);
      if (bootstrap) state_.bind_bootstrap(renewal_probe::metadata_reader(response).read(), metadata.request_id);
      return response; // Same SDK response, with no credential retained in observations.
    }
    catch (...) { state_.fail("Public Bridge request dispatch failed"); throw; }
  }
  void shutdown_session(const std::string& session) override {
    state_.session_shutdown(session);
    inner_.shutdown_session(session);
  }
  void set_owner_window(void* window) noexcept override { inner_.set_owner_window(window); }
  void set_event_sink(event_sink sink) override {
    if (!sink) { inner_.set_event_sink({}); return; }
    auto* state = &state_;
    inner_.set_event_sink([state, send = std::move(sink)](std::string_view event) {
      state->native_event(event);
      try { send(event); }
      catch (...) { state->fail("Native event forwarding failed"); throw; }
    });
  }
 private:
  probe_state& state_; // The caller keeps state and logger alive until inner_ joins workers.
  soui::sdk::json_bridge inner_;
};

void sample_resources(durable_log& log) {
  PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
  DWORD handles = 0;
  if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)) ||
      !GetProcessHandleCount(GetCurrentProcess(), &handles)) throw std::runtime_error("Process resource sampling failed");
  unique_handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)};
  if (snapshot.get() == INVALID_HANDLE_VALUE) throw std::runtime_error("Thread snapshot failed");
  THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
  unsigned threads = 0;
  if (!Thread32First(snapshot.get(), &entry)) throw std::runtime_error("Cannot enumerate thread snapshot");
  do { if (entry.th32OwnerProcessID == GetCurrentProcessId()) ++threads; } while (Thread32Next(snapshot.get(), &entry));
  log.emit("resource.sample", "\"privateBytes\":" + std::to_string(memory.PrivateUsage) +
      ",\"workingSetBytes\":" + std::to_string(memory.WorkingSetSize) + ",\"handles\":" + std::to_string(handles) +
      ",\"threads\":" + std::to_string(threads) + ",\"gdiObjects\":" + std::to_string(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS)) +
      ",\"userObjects\":" + std::to_string(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS)) +
      ",\"resourceGate\":\"not-evaluated\"");
}

class application final {
 public:
  application(const options& settings, probe_state& state, durable_log& log)
      : settings_(settings), state_(state), log_(log) {}
  application(const application&) = delete;
  application& operator=(const application&) = delete;
  ~application() { shutdown(); }

  void execute() {
    WNDCLASSEXW description{}; description.cbSize = sizeof(description);
    description.hInstance = GetModuleHandleW(nullptr); description.lpfnWndProc = procedure;
    description.lpszClassName = window_class; description.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&description)) throw std::runtime_error("Cannot register probe window class");
    class_registered_ = true;
    bridge_ = std::make_unique<observed_bridge>(state_);
    host_ = std::make_unique<soui::sdk::soui_miniblink_host>(std::make_unique<soui::sdk::dynamic_miniblink_adapter>());
    handle_ = CreateWindowExW(0, window_class, L"SOUI SDK same-document renewal validation",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 100, 80, 760, 540, nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!handle_) throw std::runtime_error("Cannot create probe owner window");
    soui::sdk::host_config config;
    config.architecture = architecture(); config.miniblink_binary = settings_.runtime / "node.dll";
    config.resource_root = settings_.runtime / "resources";
    config.owner_window = handle_; config.bridge_adapter = bridge_.get();
    runtime_attempted_ = true;
    const auto mounted = host_->start(config);
    if (!mounted.ok) throw std::runtime_error("Public Host mount failed: " + mounted.code);
    mounted_ = true;
    log_.emit("host.mounted", "\"mountCount\":1,\"reloadCount\":0");
    ShowWindow(handle_, SW_SHOWNORMAL);
    UpdateWindow(handle_);
    std::uint64_t next_sample = 0;
    bool host_ready_seen = false;
    while (!state_.failed()) {
      service();
      const auto sampled = renewal_probe::sample_clock(state_);
      const auto now = sampled.now;
      const auto& observed = sampled.observed;
      if (observed.ready_count == 1 && host_->application_ready()) host_ready_seen = true;
      if (!host_ready_seen && now > 60000) throw std::runtime_error("Initial page/Host readiness exceeded 60 seconds");
      if (observed.ready_count > 1) throw std::runtime_error("Ready was reported more than once");
      if (observed.ready_count == 1 && now - observed.last_record_at > 30000)
        throw std::runtime_error("Page record gap exceeded 30 seconds");
      if (now > settings_.duration_ms + 120000) throw std::runtime_error("Overall probe deadline exceeded");
      if (now >= next_sample) { sample_resources(log_); next_sample = now + 5000; }
      if (observed.finish_requested && host_ready_seen && now - observed.finish_at >= 500 && state_.active_callbacks() == 0) {
        completed_ = true; break;
      }
      MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
  }

  void shutdown() noexcept {
    if (shutdown_done_) return;
    shutdown_done_ = true;
    state_.begin_close();
    try {
      if (host_) {
        mounted_ = false;
        const auto destroyed = host_->destroy();
        const auto observed = state_.snapshot();
        log_.emit("host.destroyed", "\"ok\":" + std::string(destroyed.ok ? "true" : "false") +
            ",\"code\":" + json_quoted(destroyed.code) + ",\"activeScope\":" + (observed.scope_active ? "true" : "false"));
        if (!destroyed.ok || observed.scope_active) state_.fail("Host close failed or left authorization active");
      }
    } catch (...) { state_.fail("Exception while closing Host"); }
    // Logging can fail after destroy; never let it reverse Host/Bridge lifetime.
    host_.reset();
    // All SDK workers join here while callback state and the durable log remain alive.
    try { bridge_.reset(); } catch (...) { state_.fail("Exception while joining Bridge workers"); }
    if (runtime_attempted_) {
      try {
        const auto result = soui::sdk::shutdown_miniblink_runtime();
        log_.emit("runtime.shutdown", "\"ok\":" + std::string(result.ok ? "true" : "false") + ",\"code\":" + json_quoted(result.code));
        if (!result.ok) state_.fail("Runtime shutdown failed");
      } catch (...) { state_.fail("Exception while shutting down Runtime"); }
    }
    if (handle_ && !DestroyWindow(handle_)) state_.fail("Owner window destruction failed");
    handle_ = nullptr;
    if (class_registered_ && !UnregisterClassW(window_class, GetModuleHandleW(nullptr))) state_.fail("Window class removal failed");
    class_registered_ = false;
    if (!completed_) state_.fail("Probe ended without its explicit completion request");
  }

 private:
  void service() {
    MSG message{};
    for (unsigned count = 0; count < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++count) {
      if (message.message == WM_QUIT) throw std::runtime_error("Unexpected native quit before explicit finish");
      TranslateMessage(&message); DispatchMessageW(&message);
    }
    if (host_) host_->pump();
  }
  static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    auto* self = reinterpret_cast<application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<application*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self) {
      try {
        if (message == soui::sdk::bridge_event_message && self->host_) {
          self->host_->deliver_posted_event(reinterpret_cast<void*>(lparam)); return 0;
        }
        if (message == WM_CLOSE) { self->state_.fail("User closed the probe before explicit finish"); return 0; }
        if (message == WM_SIZE && self->mounted_ && wparam != SIZE_MINIMIZED) {
          const auto resized = self->host_->resize(std::max(1, static_cast<int>(LOWORD(lparam))), std::max(1, static_cast<int>(HIWORD(lparam))));
          if (!resized.ok) self->state_.fail("Public Host resize failed");
        }
        if (message == WM_SETFOCUS && self->mounted_) {
          const auto focused = self->host_->focus();
          if (!focused.ok) self->state_.fail("Public Host focus failed");
        }
        if (message == WM_NCDESTROY) {
          SetWindowLongPtrW(window, GWLP_USERDATA, 0);
          self->handle_ = nullptr;
          if (!self->state_.closing()) self->state_.fail("Owner window disappeared before explicit close");
        }
      } catch (...) { self->state_.fail("Native window callback failed"); }
    }
    return DefWindowProcW(window, message, wparam, lparam);
  }
  const options& settings_;
  probe_state& state_;
  durable_log& log_;
  // Declaration and explicit destruction order both keep bridge alive past Host.
  std::unique_ptr<observed_bridge> bridge_;
  std::unique_ptr<soui::sdk::soui_miniblink_host> host_;
  HWND handle_{nullptr};
  bool mounted_{false};
  bool runtime_attempted_{false};
  bool class_registered_{false};
  bool completed_{false};
  bool shutdown_done_{false};
};

void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }

int self_check() {
  const auto phase_cases = renewal_probe::session_observation_self_check();
  require(parse({L"--self-check"}).self_check, "Self-check CLI failed");
  require(parse({L"--runtime", L"C:\\runtime", L"--report", L"C:\\records\\probe.ndjson", L"--duration-ms", L"2100000"}).duration_ms == 2100000,
      "Long duration CLI failed");
  for (const auto& args : std::vector<std::vector<std::wstring>>{
      {L"--self-check", L"extra"},
      {L"--runtime", L"relative", L"--report", L"C:\\r.ndjson", L"--duration-ms", L"30000"},
      {L"--runtime", L"C:\\runtime", L"--report", L"C:\\r.ndjson", L"--duration-ms", L"29999"},
      {L"--runtime", L"C:\\runtime", L"--runtime", L"C:\\other", L"--duration-ms", L"30000"}}) {
    bool rejected = false;
    try { (void)parse(args); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Invalid CLI unexpectedly accepted");
  }
  require(same_or_below(L"C:\\Runtime\\resources\\x", L"c:\\runtime"), "Runtime child boundary check failed");
  require(!same_or_below(L"C:\\runtime-other\\x", L"C:\\runtime"), "Sibling path boundary check failed");
  require(json_quoted("quote\"\\\n\t") == "\"quote\\\"\\\\\\u000a\\u0009\"", "JSON quoting failed");
  require(message_object("{\"message\":\"valid\\\"text\"}") && !message_object("{\"message\":\"x\",\"extra\":1}"), "Diagnostic shape check failed");
  std::wstring temp(32768, L'\0');
  const DWORD length = GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
  require(length != 0 && length < temp.size(), "Cannot locate temporary self-check directory");
  temp.resize(length);
  const auto report = std::filesystem::path(temp) / std::filesystem::path(unique_run_id() + "-self-check.ndjson");
  {
    durable_log log(report);
    bool exclusive = false;
    try { durable_log duplicate(report); } catch (const std::exception&) { exclusive = true; }
    require(exclusive, "Report overwrite was not rejected");
    std::atomic<bool> thread_failure{false};
    auto write = [&] {
      try { for (unsigned count = 0; count < 4; ++count) log.emit("self.check", "\"dataJson\":" + json_quoted("{\"text\":\"line\\nvalue\"}")); }
      catch (...) { thread_failure.store(true); }
    };
    std::thread first(write);
    try { std::thread second(write); second.join(); }
    catch (...) { first.join(); throw; }
    first.join();
    require(!thread_failure.load() && !log.failed(), "Concurrent durable logging failed");
  }
  std::ifstream input(report, std::ios::binary);
  std::string line;
  unsigned count = 0;
  while (std::getline(input, line)) {
    ++count;
    require(line.find("\"seq\":" + std::to_string(count) + ',') != std::string::npos, "Concurrent log sequence is not ordered");
    require(line.find("\"schema\":\"soui.renewal-probe/v2\"") != std::string::npos && line.back() == '}', "Malformed self-check envelope");
  }
  require(count == 8, "Concurrent report record count differs");
  std::cout << "{\"status\":\"self-check-passed\",\"version\":" << json_quoted(soui::sdk::version)
      << ",\"architecture\":" << json_quoted(architecture()) << ",\"records\":8,\"fixtureRevision\":2,\"phaseCases\":" << phase_cases
      << ",\"guiExecuted\":false,\"realRenewalVerified\":false}\n";
  return 0;
}

int run(const options& settings) {
  durable_log log(settings.report);
  probe_state state(log, settings.duration_ms, unique_run_id());
  log.emit("run.started", "\"durationMs\":" + std::to_string(settings.duration_ms) +
      ",\"version\":" + json_quoted(soui::sdk::version) + ",\"architecture\":" + json_quoted(architecture()) +
      ",\"runId\":" + json_quoted(state.run_id()) + ",\"pid\":" + std::to_string(GetCurrentProcessId()) +
      ",\"realRenewalRequired\":" + (settings.duration_ms >= 2100000 ? "true" : "false"));
  const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(com)) throw std::runtime_error("Cannot initialize the UI COM apartment");
  {
    application app(settings, state, log);
    try { app.execute(); }
    catch (const std::exception& error) { state.fail(error.what()); }
    catch (...) { state.fail("Unexpected application failure"); }
    app.shutdown();
  }
  CoUninitialize();
  const auto observed = state.snapshot();
  if (observed.ready_count != 1 || observed.long_invocations != 1 || observed.result_invocations != 1 ||
      observed.bootstrap_calls != 1 || observed.bound_shutdowns != 1 || observed.prebootstrap_retirements > 1 ||
      observed.shutdown_sessions != observed.bound_shutdowns + observed.prebootstrap_retirements ||
      !observed.long_cancelled || !observed.result_stopped ||
      !observed.finish_requested || observed.scope_active)
    state.fail("Final invocation, completion or authorization state differs");
  const int exit_code = state.failed() ? 1 : 0;
  log.emit("run.finished", "\"exitCode\":" + std::to_string(exit_code) +
      ",\"readyCount\":" + std::to_string(observed.ready_count) +
      ",\"longInvocations\":" + std::to_string(observed.long_invocations) +
      ",\"resultInvocations\":" + std::to_string(observed.result_invocations) +
      ",\"shutdownSessions\":" + std::to_string(observed.shutdown_sessions) +
      ",\"bootstrapCount\":" + std::to_string(observed.bootstrap_calls) +
      ",\"prebootstrapRetirements\":" + std::to_string(observed.prebootstrap_retirements) +
      ",\"boundShutdowns\":" + std::to_string(observed.bound_shutdowns) +
      ",\"realRenewalVerified\":false,\"releaseReady\":false");
  // The host verifier, not this process-exit marker, evaluates elapsed renewal evidence.
  return exit_code;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
  try {
    std::vector<std::wstring> args;
    for (int index = 1; index < argc; ++index) args.emplace_back(argv[index]);
    auto settings = parse(args);
    if (std::string_view(soui::sdk::version) != "0.3.0-preview.11") throw std::runtime_error("This fixture requires frozen preview.11");
    // This branch does not initialize COM/Runtime, construct a bridge/Host, or create HWNDs.
    if (settings.self_check) return self_check();
    validate_paths(settings);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetProcessDPIAware();
    return run(settings);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  } catch (...) { std::cerr << "Unexpected probe entry failure\n"; return 1; }
}
