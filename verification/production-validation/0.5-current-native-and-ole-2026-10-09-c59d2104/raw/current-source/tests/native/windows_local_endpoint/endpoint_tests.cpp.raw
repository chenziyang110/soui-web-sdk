#include <soui/runtime/agent/windows_local_endpoint.h>
#include <json.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace soui::runtime {
// TEST_ONLY: observe the actual map, never mint/alter a grant or credential.
struct permission_store_test_access {
  static std::size_t grants(permission_store& store) {
    std::lock_guard<std::mutex> lock(store.mutex_); return store.grants_.size();
  }
};
}
namespace {
using namespace soui::runtime;
using namespace soui::runtime::agent;
using json = nlohmann::json;
std::atomic<std::int64_t> clock_ms{0};
std::atomic<windows_local_endpoint*> close_on_clock{nullptr};
permission_store::clock_type::time_point now() {
  if (auto* endpoint = close_on_clock.exchange(nullptr)) endpoint->request_stop();
  return permission_store::clock_type::time_point{} + std::chrono::milliseconds(clock_ms.load());
}
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
[[noreturn]] void native_pending_failure() noexcept {
  // A test failure cannot free a buffer/endpoint still borrowed by native I/O.
  ::TerminateProcess(::GetCurrentProcess(), 91); ::ExitProcess(91);
}
class handle final {
 public:
  explicit handle(HANDLE value = nullptr) noexcept : value_(value) {}
  ~handle() { if (*this) ::CloseHandle(value_); }
  handle(const handle&) = delete;
  handle& operator=(const handle&) = delete;
  HANDLE get() const noexcept { return value_; }
  explicit operator bool() const noexcept { return value_ && value_ != INVALID_HANDLE_VALUE; }
 private: HANDLE value_;
};
std::wstring pipe_name(const std::string& component) {
  return L"\\\\.\\pipe\\" + std::wstring(component.begin(), component.end());
}
std::string component() {
  static std::atomic<unsigned> serial{0};
  return "soui-endpoint-test-" + std::to_string(::GetCurrentProcessId()) + "-" +
      std::to_string(::GetTickCount64()) + "-" + std::to_string(serial.fetch_add(1));
}
class client final {
 public:
  explicit client(const std::string& component)
      : pipe_(open(pipe_name(component))), event_(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
    if (!event_) {
      const DWORD error = ::GetLastError();
      std::cerr << "Client CreateEventW failed: error=" << error << '\n';
    }
    require(pipe_ && event_, "Client pipe/event creation failed");
  }
  bool write_raw(std::string body) {
    const auto length = static_cast<std::uint32_t>(body.size());
    std::array<unsigned char, 4> header{{static_cast<unsigned char>(length),
        static_cast<unsigned char>(length >> 8U), static_cast<unsigned char>(length >> 16U),
        static_cast<unsigned char>(length >> 24U)}};
    return bytes(header.data(), header.size(), true) && bytes(body.data(), body.size(), true);
  }
  bool write_length(std::uint32_t length) {
    std::array<unsigned char, 4> header{{static_cast<unsigned char>(length),
        static_cast<unsigned char>(length >> 8U), static_cast<unsigned char>(length >> 16U),
        static_cast<unsigned char>(length >> 24U)}};
    return bytes(header.data(), header.size(), true);
  }
  bool write_fragmented(std::string body) {
    const auto length = static_cast<std::uint32_t>(body.size());
    std::array<unsigned char, 4> header{{static_cast<unsigned char>(length),
        static_cast<unsigned char>(length >> 8U), static_cast<unsigned char>(length >> 16U),
        static_cast<unsigned char>(length >> 24U)}};
    for (auto& byte : header) if (!bytes(&byte, 1, true)) return false;
    for (auto& byte : body) if (!bytes(&byte, 1, true)) return false;
    return true;
  }
  std::string read_raw() {
    std::array<unsigned char, 4> header{};
    if (!bytes(header.data(), header.size(), false)) return {};
    const auto length = static_cast<std::uint32_t>(header[0]) |
        (static_cast<std::uint32_t>(header[1]) << 8U) | (static_cast<std::uint32_t>(header[2]) << 16U) |
        (static_cast<std::uint32_t>(header[3]) << 24U);
    require(length && length <= bridge_transport::max_message_bytes, "Unbounded server frame");
    std::string body(length, '\0');
    return bytes(body.data(), body.size(), false) ? body : std::string{};
  }
  void hello() {
    require(write_raw(R"({"kind":"hello","schemaVersion":"soui.local/1"})"), "Handshake write failed");
    const auto raw = read_raw();
    require(!raw.empty() && json::parse(raw) == json{{"kind", "hello"},
        {"schemaVersion", "soui.local/1"}, {"ok", true}}, "Explicit native authorization failed");
  }
  json call(const char* method, json params = {{"n", 2}}) {
    require(write_raw(json{{"kind", "request"}, {"schemaVersion", "soui.bridge/1"},
        {"requestId", "request:one"}, {"method", method}, {"params", std::move(params)}}.dump()), "Request write failed");
    const auto raw = read_raw(); require(!raw.empty(), "Expected server response missing");
    const auto response = json::parse(raw);
    require(response.at("requestId") == "request:one" && !response.contains("token") &&
        !response.contains("sessionId") && !response.contains("origin") && !response.contains("subject") &&
        !response.contains("bearer"), "Response disclosed native authority/context");
    return response;
  }
 private:
  static HANDLE open(const std::wstring& name) {
    const auto deadline = ::GetTickCount64() + 4000;
    DWORD last_error = ERROR_SUCCESS;
    do {
      HANDLE value = ::CreateFileW(name.c_str(), FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE,
          0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IMPERSONATION, nullptr);
      if (value != INVALID_HANDLE_VALUE) return value;
      const DWORD error = ::GetLastError();
      last_error = error;
      if (error != ERROR_PIPE_BUSY && error != ERROR_FILE_NOT_FOUND) break;
      (void)::WaitNamedPipeW(name.c_str(), 10);
      ::Sleep(1);
    } while (::GetTickCount64() < deadline);
    std::cerr << "Client CreateFileW failed: error=" << last_error
        << " desiredAccess=0x00100003" << '\n';
    return INVALID_HANDLE_VALUE;
  }
  bool transfer(void* buffer, DWORD length, DWORD& transferred, bool write) {
    require(::ResetEvent(event_.get()) != FALSE, "Client event reset failed");
    OVERLAPPED operation{}; operation.hEvent = event_.get();
    const BOOL immediate = write ? ::WriteFile(pipe_.get(), buffer, length, &transferred, &operation) :
        ::ReadFile(pipe_.get(), buffer, length, &transferred, &operation);
    if (immediate) {
      if (::GetOverlappedResult(pipe_.get(), &operation, &transferred, FALSE)) return true;
      const DWORD completion_error = ::GetLastError();
      if (completion_error != ERROR_IO_INCOMPLETE && completion_error != ERROR_IO_PENDING) return false;
    } else if (::GetLastError() != ERROR_IO_PENDING) return false;
    const DWORD waited = ::WaitForSingleObject(event_.get(), 4000);
    if (waited != WAIT_OBJECT_0) {
      (void)::CancelIoEx(pipe_.get(), &operation);
      // Wait for real completion before operation/buffer storage can unwind.
      if (::WaitForSingleObject(event_.get(), 2000) != WAIT_OBJECT_0) native_pending_failure();
      DWORD ignored = 0;
      if (!::GetOverlappedResult(pipe_.get(), &operation, &ignored, FALSE)) {
        const DWORD error = ::GetLastError();
        if (error == ERROR_IO_INCOMPLETE || error == ERROR_IO_PENDING) native_pending_failure();
      }
      return false;
    }
    return ::GetOverlappedResult(pipe_.get(), &operation, &transferred, FALSE) != FALSE;
  }
  bool bytes(void* storage, std::size_t length, bool write) {
    auto* data = static_cast<unsigned char*>(storage);
    for (std::size_t offset = 0; offset < length;) {
      DWORD count = 0;
      if (!transfer(data + offset, static_cast<DWORD>(length - offset), count, write) ||
          !count || count > length - offset) return false;
      offset += count;
    }
    return true;
  }
  handle pipe_, event_;
};
json object_schema(json properties = json::object(), json required = json::array()) {
  return {{"type", "object"}, {"properties", std::move(properties)}, {"required", std::move(required)},
      {"additionalProperties", false}};
}
std::string contract() {
  return json{{"protocol", "soui.action/1"}, {"title", "Bounded native calculation"},
      {"description", "An application-approved test projection."},
      {"inputSchema", object_schema({{"n", {{"type", "integer"}, {"minimum", 1}, {"maximum", 10}}}}, {"n"})},
      {"outputSchema", object_schema({{"value", {{"type", "integer"}, {"minimum", 0}, {"maximum", 100}}}}, {"value"})}}.dump();
}
struct environment final {
  const DWORD owner = ::GetCurrentThreadId();
  DWORD expected_peer_pid = ::GetCurrentProcessId();
  std::uint64_t expected_peer_creation = 0;
  permission_store store{&now}; capability_registry registry;
  bridge_transport transport{registry, store}; windows_local_endpoint endpoint{transport, store};
  std::string name = component(); local_endpoint_scope key{11, 22, 33, 44};
  unsigned authorizations{0}, actions{0}, discovery_callbacks{0};
  bool current{true}, throw_action{false}, throw_authorizer{false};
  std::function<void()> inside_action;
  std::shared_ptr<const authorization_scope> last_scope;
  std::vector<std::string> private_tokens;
  environment() {
    clock_ms.store(0); close_on_clock.store(nullptr);
    require(registry.register_contextual_method("bridge.describe", "bridge.describe", [&](const bridge_request&) {
      ++discovery_callbacks; return R"({"private":"must not run"})";
    }), "Discovery registration failed");
    const auto callback = [this](const bridge_request& request) {
      require(::GetCurrentThreadId() == owner, "Business callback ran on I/O thread"); ++actions;
      require(request.session_id == "native-endpoint" && request.origin == "app://endpoint" &&
          !request.token.empty() && !request.native_user_gesture && request.authorization &&
          request.authorization->active(), "Actual local native context not bound");
      last_scope = request.authorization; private_tokens.push_back(request.token);
      if (inside_action) inside_action();
      if (throw_action) throw std::runtime_error("private callback detail must not cross wire");
      return R"({"value":2})";
    };
    require(registry.register_action("work.allowed", "work.run", contract(), callback) &&
        registry.allow_local_action("work.allowed"), "Local action registration failed");
    require(registry.register_action("work.denied", "work.run", contract(), callback) &&
        registry.allow_local_action("work.denied"), "Denied action fixture registration failed");
    require(registry.register_action("work.hidden", "work.run", contract(), callback), "Hidden action registration failed");
  }
  ~environment() {
    endpoint.request_stop();
    const auto deadline = ::GetTickCount64() + 5000;
    while (endpoint.poll_stop() != local_endpoint_stop::stopped) {
      if (::GetTickCount64() >= deadline) native_pending_failure(); ::Sleep(1);
    }
    auto* expected = &endpoint; (void)close_on_clock.compare_exchange_strong(expected, nullptr);
  }
  void start(bool authorize = true, std::uint32_t owner_timeout = 2000, std::uint32_t io_timeout = 2000) {
    local_endpoint_options options{name, io_timeout, owner_timeout};
    windows_local_endpoint::authorizer callback;
    if (authorize) callback = [this](const windows_peer_identity& peer) -> std::optional<local_endpoint_policy> {
      require(::GetCurrentThreadId() == owner && peer.process_id() == expected_peer_pid &&
          (!expected_peer_creation || peer.creation_filetime() == expected_peer_creation) &&
          peer.creation_filetime() && peer.process_state() == held_process_state::alive && !peer.user_sid().empty(),
          "Authorizer lacks actual expected client identity or ran on I/O thread"); ++authorizations;
      if (throw_authorizer) throw std::runtime_error("private authorization detail");
      return local_endpoint_policy{key, "native-endpoint", "app://endpoint",
          {"work.run", "bridge.describe"}, {"work.allowed", "bridge.describe"}};
    };
    require(endpoint.start(options, std::move(callback), [this](const local_endpoint_scope& value) {
      require(::GetCurrentThreadId() == owner, "Scope callback ran on I/O thread");
      return current && value.application == key.application && value.window == key.window &&
          value.document == key.document && value.generation == key.generation;
    }) == local_endpoint_error::none, "Explicit endpoint start failed");
  }
  template<class Predicate> void pump_until(Predicate condition) {
    const auto deadline = ::GetTickCount64() + 6000;
    while (!condition()) {
      (void)endpoint.pump_owner(4, 2);
      require(::GetTickCount64() < deadline, "Owner progress exceeded test budget"); ::Sleep(1);
    }
  }
  void stop() {
    endpoint.request_stop();
    pump_until([&] { return endpoint.poll_stop() == local_endpoint_stop::stopped; });
    require(permission_store_test_access::grants(store) == 0, "Exact grant remains in actual store after cleanup");
    if (last_scope) require(!last_scope->active(), "Stopped connection revived its captured actual scope");
  }
};
class client_thread final {
 public:
  template<class Function> client_thread(environment& environment, Function function)
      : environment_(environment), worker_([this, function = std::move(function)] {
        try { function(); } catch (...) { error_ = std::current_exception(); }
        done_.store(true);
      }) {}
  ~client_thread() {
    if (worker_.joinable()) {
      environment_.endpoint.request_stop();
      if (::WaitForSingleObject(static_cast<HANDLE>(worker_.native_handle()), 6000) != WAIT_OBJECT_0)
        native_pending_failure();
      worker_.join();
    }
  }
  bool done() const noexcept { return done_.load(); }
  void finish() {
    environment_.pump_until([&] { return done(); });
    if (::WaitForSingleObject(static_cast<HANDLE>(worker_.native_handle()), 1000) != WAIT_OBJECT_0)
      native_pending_failure();
    worker_.join(); if (error_) std::rethrow_exception(error_);
  }
 private:
  environment& environment_; std::atomic<bool> done_{false}; std::exception_ptr error_; std::thread worker_;
};
class client_process final {
 public:
  explicit client_process(const std::string& component) {
    std::array<wchar_t, 32768> module{};
    const DWORD length = ::GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    require(length && length < module.size(), "Actual executable path unavailable");
    const std::wstring executable(module.data(), length);
    std::wstring command = L"\"" + executable + L"\" --client-child " +
        std::wstring(component.begin(), component.end());
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
    require(::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &native_) != FALSE,
        "Fixed self-executable child creation failed");
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!::GetProcessTimes(native_.hProcess, &created, &exited, &kernel, &user)) native_pending_failure();
    creation_ = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32U) | created.dwLowDateTime;
    if (!creation_ || !native_.dwProcessId) native_pending_failure();
  }
  ~client_process() {
    // Do not relinquish a pending child on test timeout/unwind. The outer
    // Root-owned job must account for and close every forced failure member.
    if (!completed()) native_pending_failure();
    ::CloseHandle(native_.hThread); ::CloseHandle(native_.hProcess);
  }
  client_process(const client_process&) = delete;
  client_process& operator=(const client_process&) = delete;
  DWORD id() const noexcept { return native_.dwProcessId; }
  std::uint64_t creation() const noexcept { return creation_; }
  bool completed() const noexcept { return ::WaitForSingleObject(native_.hProcess, 0) == WAIT_OBJECT_0; }
  DWORD exit_code() const {
    require(completed(), "Child exit requested before actual process completion");
    DWORD value = STILL_ACTIVE;
    require(::GetExitCodeProcess(native_.hProcess, &value) != FALSE, "Actual child exit code unavailable");
    return value;
  }
 private:
  PROCESS_INFORMATION native_{}; std::uint64_t creation_{0};
};
int run_client_child(const char* component) {
  try {
    const std::string name(component);
    require(!name.empty() && name.size() <= 128, "Child local component invalid");
    for (const unsigned char c : name)
      require((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
          c == '.' || c == '_' || c == '-', "Child local component invalid");
    client channel(name); channel.hello();
    require(channel.call("work.allowed").at("result") == json{{"value", 2}}, "Child native action failed");
    return 0;
  } catch (...) { std::cerr << "Fixed local client child failed\n"; return 1; }
}
void external_process() {
  environment f; f.start();
  client_process child(f.name);
  // Populate native expected identity before the first owner pump. The child
  // provides no PID, FILETIME, subject or bearer over the pipe.
  f.expected_peer_pid = child.id(); f.expected_peer_creation = child.creation();
  f.pump_until([&] { return child.completed(); });
  require(child.exit_code() == 0 && child.id() != ::GetCurrentProcessId() &&
      f.authorizations == 1 && f.actions == 1, "Actual independent child handshake/action/natural exit failed");
  f.stop();
}
void invalid_options() {
  environment f;
  for (const auto& name : {"", "\\\\other\\pipe\\remote", "nested/name", "with:colon", "with space"})
    require(f.endpoint.start(local_endpoint_options{name}) == local_endpoint_error::invalid_options,
        "Unsafe local pipe component accepted");
  require(f.endpoint.state() == local_endpoint_state::stopped && f.authorizations == 0,
      "Invalid/default endpoint started a service");
}
void default_deny() {
  environment f; f.start(false);
  client_thread subject(f, [&] {
    client c(f.name); require(c.write_raw(R"({"kind":"hello","schemaVersion":"soui.local/1"})"), "Hello write failed");
    require(c.read_raw().empty(), "Empty authorizer granted authority");
  }); subject.finish(); f.stop(); require(f.authorizations == 0 && f.actions == 0, "Default deny ran callbacks");
}
void handshake_required() {
  environment f; f.start();
  client_thread subject(f, [&] {
    client c(f.name); require(c.write_raw(R"({"kind":"request","schemaVersion":"soui.bridge/1","requestId":"one","method":"work.allowed","params":{"n":2}})"), "Early request failed");
    require(c.read_raw().empty(), "Request without actual hello accepted");
  }); subject.finish(); f.stop(); require(f.authorizations == 0 && f.actions == 0, "Invalid hello entered native authorization");
}
void owner_dispatch() {
  environment f; f.start();
  client_thread subject(f, [&] {
    client c(f.name); c.hello();
    const auto discovery = c.call("bridge.describe", json::object());
    require(discovery.at("ok") == true && discovery.at("result").at("methods").size() == 1 &&
        discovery.at("result").at("methods")[0].at("name") == "work.allowed", "Discovery exceeded exact native method policy");
    require(c.call("work.allowed").at("result") == json{{"value", 2}}, "Native action result incorrect");
    const auto denied = c.call("work.denied");
    require(denied.at("ok") == false && denied.at("error").at("code") == "E_UNAUTHORIZED", "Whitelist bypass accepted");
  }); subject.finish(); f.stop();
  require(f.authorizations == 1 && f.actions == 1 && f.discovery_callbacks == 0, "Owner local dispatch count/route incorrect");
}
void wire_credentials() {
  environment f; f.start();
  client_thread subject(f, [&] {
    for (const auto* field : {"token", "subject", "nativeUserGesture", "grant", "sessionId", "origin"}) {
      client c(f.name); c.hello();
      json frame{{"kind", "request"}, {"schemaVersion", "soui.bridge/1"}, {"requestId", "one"},
          {"method", "work.allowed"}, {"params", {{"n", 2}}}};
      frame[field] = "wire cannot create authority";
      require(c.write_raw(frame.dump()) && c.read_raw().empty(), "Wire authority field accepted");
    }
  }); subject.finish(); f.stop(); require(f.authorizations == 6 && !f.actions, "Credential rejection entered business");
}
void consecutive_requests() {
  environment f; f.start();
  client_thread subject(f, [&] {
    client c(f.name); c.hello();
    for (unsigned i = 0; i < 64; ++i)
      require(c.call("work.allowed").at("result") == json{{"value", 2}}, "Consecutive same-connection request dropped/replayed");
  }); subject.finish(); f.stop();
  require(f.authorizations == 1 && f.actions == 64 && f.private_tokens.size() == 64,
      "Consecutive requests reauthorized/replayed or lost owner work");
}
void fragmented_frame() {
  environment f; f.start();
  client_thread subject(f, [&] {
    client c(f.name);
    require(c.write_fragmented(R"({"kind":"hello","schemaVersion":"soui.local/1"})"), "Fragmented hello write failed");
    const auto hello = c.read_raw();
    require(!hello.empty() && json::parse(hello).at("ok") == true, "Fragmented hello not authorized");
    require(c.write_fragmented(R"({"kind":"request","schemaVersion":"soui.bridge/1","requestId":"one","method":"work.allowed","params":{"n":2}})"),
        "Fragmented request write failed");
    const auto raw = c.read_raw();
    require(!raw.empty() && json::parse(raw).at("result") == json{{"value", 2}}, "Legal fragmented request rejected");
  }); subject.finish(); f.stop(); require(f.authorizations == 1 && f.actions == 1, "Fragmented frame replayed or lost dispatch");
}
void frame_bounds() {
  environment f; f.start();
  client_thread subject(f, [&] {
    { client c(f.name); require(c.write_length(65537) && c.read_raw().empty(), "Oversized frame accepted"); }
    { client c(f.name); require(c.write_raw(std::string("\xC0\xAF", 2)) && c.read_raw().empty(), "Invalid UTF-8 accepted"); }
    const std::string prefix = R"({"kind":"request","schemaVersion":"soui.bridge/1","requestId":"one","method":"work.allowed","params":)";
    for (const auto& params : {std::string("[]"), std::string(34, '[') + "0" + std::string(34, ']'),
        json{{"n", std::string(33000, 'x')}}.dump()}) {
      client c(f.name); c.hello();
      require(c.write_raw(prefix + params + "}") && c.read_raw().empty(), "Invalid/bounded params accepted");
    }
  }); subject.finish(); f.stop(); require(f.authorizations == 3 && f.actions == 0, "Frame bounds entered callbacks");
}
void duplicate_fields() {
  environment f; f.start();
  client_thread subject(f, [&] {
    client c(f.name); c.hello();
    require(c.write_raw(R"({"kind":"request","schemaVersion":"soui.bridge/1","requestId":"one","method":"work.denied","method":"work.allowed","params":{"n":2}})") &&
        c.read_raw().empty(), "Duplicate top-level method accepted");
  }); subject.finish(); f.stop(); require(f.actions == 0, "Duplicate dispatch reached business");
}
void navigation_revoke() {
  environment f; f.start(); std::atomic<unsigned> phase{0};
  client_thread subject(f, [&] {
    client c(f.name); c.hello(); phase.store(1);
    while (phase.load() != 2) ::Sleep(1);
    require(c.write_raw(R"({"kind":"request","schemaVersion":"soui.bridge/1","requestId":"one","method":"work.allowed","params":{"n":2}})"), "Queued request write failed");
    phase.store(3); require(c.read_raw().empty(), "Revoked generation delivered result");
  });
  f.pump_until([&] { return phase.load() == 1; }); phase.store(2);
  const auto deadline = ::GetTickCount64() + 3000;
  while (phase.load() != 3 && !subject.done()) { require(::GetTickCount64() < deadline, "Client queue progress failed"); ::Sleep(1); }
  f.current = false;
  require(f.endpoint.revoke_scope(f.key), "Native navigation scope could not revoke admission");
  subject.finish(); f.stop(); require(f.actions == 0, "Action sent before revoke ran without owner admission");
}
void reconnect() {
  environment f; f.start();
  client_thread subject(f, [&] {
    for (unsigned i = 0; i < 2; ++i) { client c(f.name); c.hello(); require(c.call("work.allowed").at("ok") == true, "Reauthorized action failed"); }
  }); subject.finish(); f.stop();
  require(f.authorizations == 2 && f.actions == 2 && f.private_tokens.size() == 2 &&
      f.private_tokens[0] != f.private_tokens[1], "Reconnect reused native authorization or replayed actions");
}
void callback_throw() {
  environment f; f.throw_action = true; f.start();
  client_thread subject(f, [&] {
    client c(f.name); c.hello(); const auto result = c.call("work.allowed");
    require(result.at("ok") == false && result.at("error").at("code") == "E_NATIVE_FAILURE" &&
        result.dump().find("private callback detail") == std::string::npos, "Callback throw disclosed details or accepted output");
  }); subject.finish(); f.stop(); require(f.actions == 1, "Throwing callback repeated or disappeared");
}
void authorizer_throw() {
  environment f; f.throw_authorizer = true; f.start();
  client_thread subject(f, [&] {
    client c(f.name); require(c.write_raw(R"({"kind":"hello","schemaVersion":"soui.local/1"})") &&
        c.read_raw().empty(), "Authorizer throw delivered authorization");
  }); subject.finish(); f.stop(); require(f.authorizations == 1 && !f.actions &&
      f.endpoint.last_error() == local_endpoint_error::callback_failure, "Authorizer failure cleanup/category incorrect");
}
void issue_close_race() {
  environment f; close_on_clock.store(&f.endpoint); f.start();
  client_thread subject(f, [&] {
    client c(f.name); require(c.write_raw(R"({"kind":"hello","schemaVersion":"soui.local/1"})") &&
        c.read_raw().empty(), "Grant issued after binding-close was sent");
  }); subject.finish(); f.stop();
  require(f.authorizations == 1 && !close_on_clock.load() && !f.actions, "Actual issue_local close interleave did not execute");
}
void lease_expiry() {
  environment f; f.start(); std::atomic<unsigned> phase{0};
  client_thread subject(f, [&] {
    client c(f.name); c.hello(); phase.store(1);
    while (phase.load() != 2) ::Sleep(1);
    const auto result = c.call("work.allowed");
    require(result.at("ok") == false && result.at("error").at("code") == "E_UNAUTHORIZED" &&
        !result.contains("result"), "Default expiry delivered business output/renewed lease");
    require(c.read_raw().empty(), "Expired authorization connection remained reusable");
  });
  f.pump_until([&] { return phase.load() == 1; }); clock_ms.store(30 * 60 * 1000); phase.store(2);
  subject.finish(); f.stop(); require(!f.actions && f.authorizations == 1, "Expiry entered business or implicitly reauthorized");
}
void partial_read_stop() {
  environment f; f.start(); std::atomic<bool> written{false};
  client_thread subject(f, [&] {
    client c(f.name); require(c.write_length(500), "Partial frame header failed"); written.store(true);
    require(c.read_raw().empty(), "Stop produced a partial-frame result");
  });
  f.pump_until([&] { return written.load(); }); f.endpoint.request_stop(); subject.finish(); f.stop();
  require(!f.authorizations && !f.actions, "Partial handshake reached application callbacks");
}
void owner_timeout() {
  environment f; f.start(true, 30, 2000); std::atomic<bool> written{false};
  client_thread subject(f, [&] {
    client c(f.name); require(c.write_raw(R"({"kind":"hello","schemaVersion":"soui.local/1"})"), "Timeout hello write failed");
    written.store(true); require(c.read_raw().empty(), "Unpumped owner timeout granted authority");
  });
  const auto deadline = ::GetTickCount64() + 5000;
  while (!subject.done()) { require(::GetTickCount64() < deadline, "Owner timeout did not close I/O"); ::Sleep(1); }
  subject.finish(); f.stop(); require(written.load() && !f.authorizations && !f.actions, "Timed-out queued authorization later ran");
}
void callback_stop() {
  environment f; std::atomic<bool> entered{false}; handle release(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
  require(static_cast<bool>(release), "Callback gate creation failed");
  f.inside_action = [&] { entered.store(true); require(::WaitForSingleObject(release.get(), 3000) == WAIT_OBJECT_0, "Callback release missing"); };
  f.start();
  client_thread subject(f, [&] {
    client c(f.name); c.hello();
    require(c.write_raw(R"({"kind":"request","schemaVersion":"soui.bridge/1","requestId":"one","method":"work.allowed","params":{"n":2}})"), "Callback stop request write failed");
    const auto deadline = ::GetTickCount64() + 3000;
    while (!entered.load()) { require(::GetTickCount64() < deadline, "Business callback never entered"); ::Sleep(1); }
    f.endpoint.request_stop(); require(::SetEvent(release.get()) != FALSE, "Callback gate release failed");
    require(c.read_raw().empty(), "Stopped callback delivered a result");
  }); subject.finish(); f.stop(); require(f.actions == 1 && f.last_scope && !f.last_scope->active(), "Stop rolled back/replayed action or leaked scope");
}
void security_first_instance() {
  environment f; const auto name = pipe_name(f.name);
  { handle squatter(::CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_WAIT,
        1, 256, 256, 0, nullptr));
    require(static_cast<bool>(squatter), "First-instance fixture unavailable");
    require(f.endpoint.start(local_endpoint_options{f.name}) == local_endpoint_error::pipe_unavailable,
        "Endpoint accepted a preexisting server name"); }
  f.start();
  handle forbidden(::CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
      FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IMPERSONATION, nullptr));
  require(!forbidden && ::GetLastError() == ERROR_ACCESS_DENIED, "DACL granted generic write/create-instance alias");
  handle competing(::CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_BYTE | PIPE_WAIT, 1, 256, 256, 0, nullptr));
  require(!competing, "DACL/maxInstances admitted a second server instance");
  f.stop();
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 3 && std::string(argv[1]) == "--client-child") return run_client_child(argv[2]);
  struct test { const char* name; void (*run)(); };
  const test cases[]{{"invalid_options", invalid_options}, {"default_deny", default_deny},
      {"handshake_required", handshake_required}, {"owner_dispatch", owner_dispatch},
      {"wire_credentials", wire_credentials}, {"frame_bounds", frame_bounds},
      {"consecutive_requests", consecutive_requests},
      {"fragmented_frame", fragmented_frame},
      {"external_process", external_process},
      {"duplicate_fields", duplicate_fields}, {"navigation_revoke", navigation_revoke},
      {"reconnect", reconnect}, {"callback_throw", callback_throw}, {"authorizer_throw", authorizer_throw},
      {"issue_close_race", issue_close_race}, {"lease_expiry", lease_expiry},
      {"partial_read_stop", partial_read_stop}, {"owner_timeout", owner_timeout},
      {"callback_stop", callback_stop}, {"security_first_instance", security_first_instance}};
  try {
    require(argc == 2, "One finite case name required");
    for (const auto& item : cases) if (std::string(argv[1]) == item.name) {
      item.run(); std::cout << "windows_local_endpoint " << item.name << " passed\n"; return 0;
    }
    throw std::runtime_error("Unknown finite case");
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
  catch (...) { std::cerr << "Test failure\n"; return 1; }
}
