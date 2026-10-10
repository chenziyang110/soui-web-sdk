#include <soui/runtime/agent/windows_local_endpoint.h>
#include <soui/runtime/services/task/task_service.h>

#include <json.hpp>
#include <sddl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

namespace soui::runtime::agent {
namespace {
using json = nlohmann::json;
constexpr std::size_t max_frame = bridge_transport::max_message_bytes;

class native_handle final {
 public:
  explicit native_handle(HANDLE value = nullptr) noexcept : value_(value) {}
  ~native_handle() { reset(); }
  native_handle(const native_handle&) = delete;
  native_handle& operator=(const native_handle&) = delete;
  native_handle(native_handle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
  native_handle& operator=(native_handle&& other) noexcept {
    if (this != &other) { reset(); value_ = std::exchange(other.value_, nullptr); }
    return *this;
  }
  HANDLE get() const noexcept { return value_; }
  explicit operator bool() const noexcept { return value_ && value_ != INVALID_HANDLE_VALUE; }
  void reset() noexcept {
    if (*this) ::CloseHandle(value_);
    value_ = nullptr;
  }
 private:
  HANDLE value_;
};
struct local_free { void operator()(void* value) const noexcept { if (value) ::LocalFree(value); } };
using local_memory = std::unique_ptr<void, local_free>;

[[noreturn]] void lifetime_fail_stop() noexcept { std::terminate(); }
bool same_scope(const local_endpoint_scope& a, const local_endpoint_scope& b) noexcept {
  return a.application == b.application && a.window == b.window &&
      a.document == b.document && a.generation == b.generation;
}
bool valid_scope(const local_endpoint_scope& value) noexcept {
  return value.application && value.window && value.document && value.generation;
}
bool identifier(std::string_view value, bool pipe = false) noexcept {
  if (value.empty() || value.size() > 128) return false;
  return std::all_of(value.begin(), value.end(), [pipe](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '.' || c == '_' || c == '-' || (!pipe && c == ':');
  });
}
bool strict_utf8(const std::string& text) noexcept {
  return !text.empty() && text.size() <= max_frame &&
      ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
          static_cast<int>(text.size()), nullptr, 0) > 0;
}
json bounded_json(const std::string& text, bool wire_keys = false) {
  if (!strict_utf8(text)) throw std::invalid_argument("invalid frame");
  std::vector<std::string> keys;
  return json::parse(text, [&keys, wire_keys](int depth, json::parse_event_t event, json& value) {
    if (depth > static_cast<int>(bridge_transport::max_json_depth))
      throw std::invalid_argument("frame depth");
    if (wire_keys && event == json::parse_event_t::key && depth == 1) {
      const auto key = value.get<std::string>();
      if (keys.size() >= 8 || std::find(keys.begin(), keys.end(), key) != keys.end())
        throw std::invalid_argument("frame keys");
      keys.push_back(key);
    }
    return true;
  });
}
bool hello_frame(const std::string& text) {
  const auto value = bounded_json(text, true);
  return value.is_object() && value.size() == 2 && value.value("kind", "") == "hello" &&
      value.value("schemaVersion", "") == "soui.local/1";
}
bridge_request request_frame(const std::string& text) {
  const auto value = bounded_json(text, true);
  if (!value.is_object()) throw std::invalid_argument("request shape");
  for (const auto& item : value.items()) {
    const auto& key = item.key();
    if (key != "kind" && key != "schemaVersion" && key != "requestId" && key != "method" &&
        key != "params" && key != "operationId" && key != "timeoutMs")
      throw std::invalid_argument("request field");
  }
  if (value.value("kind", "") != "request" || value.value("schemaVersion", "") != "soui.bridge/1" ||
      !value.contains("requestId") || !value["requestId"].is_string() ||
      !value.contains("method") || !value["method"].is_string() ||
      !value.contains("params") || !value["params"].is_object()) throw std::invalid_argument("request fields");
  bridge_request result;
  result.request_id = value["requestId"].get<std::string>();
  result.method = value["method"].get<std::string>();
  if (!identifier(result.request_id) || !identifier(result.method)) throw std::invalid_argument("request ID");
  if (value.contains("operationId")) {
    if (!value["operationId"].is_string()) throw std::invalid_argument("operation ID");
    result.operation_id = value["operationId"].get<std::string>();
    if (!identifier(result.operation_id)) throw std::invalid_argument("operation ID");
  }
  if (value.contains("timeoutMs")) {
    if (!value["timeoutMs"].is_number_unsigned()) throw std::invalid_argument("timeout");
    const auto timeout = value["timeoutMs"].get<std::uint64_t>();
    if (!timeout || timeout > 60000) throw std::invalid_argument("timeout");
    result.timeout_ms = static_cast<std::uint32_t>(timeout);
  }
  result.params_json = value["params"].dump();
  if (result.params_json.size() > bridge_transport::max_params_bytes) throw std::invalid_argument("params bound");
  // Credential members remain empty. handle_local always rewrites them again.
  return result;
}
std::string response_frame(const bridge_request& request, const bridge_response& response) {
  json value{{"kind", "response"}, {"schemaVersion", "soui.bridge/1"},
      {"requestId", request.request_id}, {"correlationId", request.request_id}, {"ok", response.ok}};
  if (!request.operation_id.empty()) value["operationId"] = request.operation_id;
  if (response.ok) value["result"] = response.payload_json.empty() ? json::object() : bounded_json(response.payload_json);
  else value["error"] = {{"code", response.error_code.empty() ? "E_INTERNAL" : response.error_code},
      {"message", response.error_message.empty() ? "Local request rejected." : response.error_message}};
  const auto encoded = value.dump();
  if (encoded.size() > max_frame) throw std::invalid_argument("response bound");
  return encoded;
}
bool same_peer(const windows_peer_identity& a, const windows_peer_identity& b) noexcept {
  const auto x = a.authentication_id(), y = b.authentication_id();
  return a.process_id() == b.process_id() && a.creation_filetime() == b.creation_filetime() &&
      a.user_sid() == b.user_sid() && x.low_part == y.low_part && x.high_part == y.high_part &&
      a.session_id() == b.session_id() && a.process_state() == held_process_state::alive &&
      b.process_state() == held_process_state::alive;
}
local_memory pipe_security() {
  HANDLE raw_token = nullptr;
  if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &raw_token)) return {};
  native_handle token(raw_token);
  DWORD bytes = 0;
  if (::GetTokenInformation(token.get(), TokenGroups, nullptr, 0, &bytes) ||
      ::GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes < sizeof(TOKEN_GROUPS) || bytes > 65536) return {};
  std::vector<unsigned char> buffer(bytes);
  if (!::GetTokenInformation(token.get(), TokenGroups, buffer.data(), bytes, &bytes)) return {};
  const auto* groups = reinterpret_cast<const TOKEN_GROUPS*>(buffer.data());
  if (groups->GroupCount > (buffer.size() - offsetof(TOKEN_GROUPS, Groups)) / sizeof(SID_AND_ATTRIBUTES)) return {};
  PSID logon_sid = nullptr;
  for (DWORD i = 0; i < groups->GroupCount; ++i) {
    if ((groups->Groups[i].Attributes & SE_GROUP_LOGON_ID) != SE_GROUP_LOGON_ID) continue;
    const auto begin = reinterpret_cast<std::uintptr_t>(buffer.data());
    const auto address = reinterpret_cast<std::uintptr_t>(groups->Groups[i].Sid);
    if (logon_sid || address < begin || address > begin + buffer.size() - 8) return {};
    const auto* sid_bytes = reinterpret_cast<const unsigned char*>(groups->Groups[i].Sid);
    const auto sid_length = 8U + 4U * sid_bytes[1];
    if (sid_length > buffer.size() - (address - begin) || !::IsValidSid(groups->Groups[i].Sid)) return {};
    logon_sid = groups->Groups[i].Sid;
  }
  if (!logon_sid) return {};
  LPWSTR sid_text = nullptr;
  if (!::ConvertSidToStringSidW(logon_sid, &sid_text)) return {};
  local_memory text(sid_text);
  // DACL 0x00100083: data read/write, SYNCHRONIZE, FILE_READ_ATTRIBUTES.
  // No GENERIC_WRITE / FILE_APPEND_DATA / FILE_CREATE_PIPE_INSTANCE / READ_CONTROL.
  const std::wstring sddl = L"D:P(A;;0x00100083;;;" + std::wstring(sid_text) + L")";
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
      &descriptor, nullptr)) return {};
  return local_memory(descriptor);
}
}  // namespace

struct windows_local_endpoint::impl {
  enum class work_kind { none, authorize, request };
  struct connection final {
    explicit connection(std::unique_ptr<const windows_peer_identity> value)
        : peer(std::move(value)), wake(::CreateEventW(nullptr, TRUE, FALSE, nullptr)),
          cleaned(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
      if (!wake || !cleaned) throw std::runtime_error("native event");
    }
    std::mutex mutex;
    const std::unique_ptr<const windows_peer_identity> peer;
    native_handle wake, cleaned;
    std::shared_ptr<native_connection_binding> binding;
    issued_local_grant grant;
    local_endpoint_scope scope;
    work_kind work{work_kind::authorize};
    bridge_request request;
    std::string reply;
    bool closed{false}, cleanup_requested{false}, cleanup_done{false};
    bool owner_busy{false}, reply_ready{false}, unauthorized_reply{false};
  };
  impl(bridge_transport& t, permission_store& p, task_service* service)
      : transport(t), permissions(p), tasks(service) {}
  bridge_transport& transport;
  permission_store& permissions;
  task_service* const tasks;
  std::mutex mutex;
  std::shared_ptr<connection> current;
  native_handle pipe, stop_event, io_event;
  std::thread worker;
  DWORD owner{0};
  bool pumping{false}; // owner only
  local_endpoint_options options;
  authorizer authorize;
  scope_check scope_current;
  std::atomic<bool> stop_requested{false};
  std::atomic<local_endpoint_state> status{local_endpoint_state::stopped};
  std::atomic<local_endpoint_error> error{local_endpoint_error::none};

  std::shared_ptr<connection> get_current() { std::lock_guard<std::mutex> lock(mutex); return current; }
  void close_connection(const std::shared_ptr<connection>& c) {
    if (!c) return;
    std::lock_guard<std::mutex> lock(c->mutex);
    c->closed = true; c->cleanup_requested = true; c->work = work_kind::none;
    if (c->binding) c->binding->close();
    if (!::SetEvent(c->wake.get())) lifetime_fail_stop();
  }
  // All scopes stay in the reserved connection control slot until this succeeds.
  bool cleanup(const std::shared_ptr<connection>& c) {
    std::shared_ptr<const authorization_scope> scope;
    { std::lock_guard<std::mutex> lock(c->mutex);
      if (!c->cleanup_requested || c->cleanup_done || c->owner_busy) return false;
      scope = c->grant.scope;
    }
    if (scope) {
      (void)permissions.revoke_scope(scope);
      if (tasks) (void)tasks->shutdown_scope(scope); // owner only; cancellation request, not join
    }
    { std::lock_guard<std::mutex> lock(c->mutex);
      if (c->binding) c->binding->close();
      c->grant = {}; c->request = {}; c->reply.clear();
      c->cleanup_done = true;
      if (!::SetEvent(c->cleaned.get())) lifetime_fail_stop();
    }
    return true;
  }
  bool await_operation(OVERLAPPED& operation, BOOL immediate, DWORD native_error,
      DWORD& bytes, ULONGLONG deadline) {
    if (immediate) {
      if (::GetOverlappedResult(pipe.get(), &operation, &bytes, FALSE)) return !stop_requested.load();
      const DWORD completion_error = ::GetLastError();
      if (completion_error != ERROR_IO_INCOMPLETE && completion_error != ERROR_IO_PENDING) return false;
      native_error = ERROR_IO_PENDING; // retain storage if an actual operation is still pending
    }
    if (native_error != ERROR_IO_PENDING) return false;
    bool cancelled = false;
    for (;;) {
      if (::GetOverlappedResult(pipe.get(), &operation, &bytes, FALSE))
        return !cancelled && !stop_requested.load();
      const auto query_error = ::GetLastError();
      if (query_error != ERROR_IO_INCOMPLETE && query_error != ERROR_IO_PENDING) return false;
      if (!cancelled && (stop_requested.load() || (deadline && ::GetTickCount64() >= deadline))) {
        (void)::CancelIoEx(pipe.get(), &operation);
        cancelled = true;
      }
      // Even after timeout/cancel, this stack OVERLAPPED and its caller's buffer
      // remain owned until actual completion. UI poll_stop remains pending.
      if (cancelled) {
        if (::WaitForSingleObject(io_event.get(), 50) == WAIT_FAILED) lifetime_fail_stop();
      } else {
        const HANDLE handles[]{stop_event.get(), io_event.get()};
        DWORD wait = INFINITE;
        if (deadline) {
          const auto now = ::GetTickCount64();
          wait = now >= deadline ? 0 : static_cast<DWORD>(std::min<ULONGLONG>(deadline - now, 60000));
        }
        if (::WaitForMultipleObjects(2, handles, FALSE, wait) == WAIT_FAILED) lifetime_fail_stop();
      }
    }
  }
  bool connect() {
    if (!::ResetEvent(io_event.get())) return false;
    OVERLAPPED operation{}; operation.hEvent = io_event.get();
    const BOOL immediate = ::ConnectNamedPipe(pipe.get(), &operation);
    const DWORD native_error = immediate ? ERROR_SUCCESS : ::GetLastError();
    if (!immediate && native_error == ERROR_PIPE_CONNECTED) return !stop_requested.load();
    DWORD bytes = 0;
    return await_operation(operation, immediate, native_error, bytes, 0);
  }
  bool send_allowed_locked(const connection& c) {
    if (c.closed || stop_requested.load() || !c.binding || !c.binding->active() ||
        !c.grant.scope || (!c.unauthorized_reply && !c.grant.scope->active())) return false;
    const auto checked = verify_windows_named_pipe_peer(pipe.get());
    return checked.verified() && same_peer(*c.peer, *checked.identity);
  }
  bool transfer(void* buffer, DWORD length, DWORD& bytes, ULONGLONG deadline,
      const std::shared_ptr<connection>& c = {}, bool sending = false) {
    if (stop_requested.load() || (deadline && ::GetTickCount64() >= deadline)) return false;
    if (!::ResetEvent(io_event.get())) return false;
    OVERLAPPED operation{}; operation.hEvent = io_event.get();
    BOOL immediate = FALSE; DWORD native_error = ERROR_SUCCESS;
    if (c) {
      // Revoke and each send submission share this lock. No completion wait or
      // application callback is inside it; already admitted bytes are not undone.
      std::lock_guard<std::mutex> lock(c->mutex);
      if (c->closed || stop_requested.load() || (sending && !send_allowed_locked(*c))) return false;
      immediate = sending ? ::WriteFile(pipe.get(), buffer, length, &bytes, &operation) :
          ::ReadFile(pipe.get(), buffer, length, &bytes, &operation);
      native_error = immediate ? ERROR_SUCCESS : ::GetLastError();
    } else {
      immediate = ::ReadFile(pipe.get(), buffer, length, &bytes, &operation);
      native_error = immediate ? ERROR_SUCCESS : ::GetLastError();
    }
    return await_operation(operation, immediate, native_error, bytes, deadline);
  }
  bool exact(void* storage, std::size_t length, ULONGLONG deadline,
      const std::shared_ptr<connection>& c = {}, bool sending = false) {
    auto* bytes = static_cast<unsigned char*>(storage);
    for (std::size_t offset = 0; offset < length;) {
      DWORD count = 0;
      if (!transfer(bytes + offset, static_cast<DWORD>(length - offset), count, deadline, c, sending) ||
          !count || count > length - offset) return false;
      offset += count;
    }
    return true;
  }
  std::optional<std::string> receive(const std::shared_ptr<connection>& c = {}) {
    const auto deadline = ::GetTickCount64() + options.io_timeout_ms;
    std::array<unsigned char, 4> header{};
    if (!exact(header.data(), header.size(), deadline, c)) return {};
    const std::uint32_t length = static_cast<std::uint32_t>(header[0]) |
        (static_cast<std::uint32_t>(header[1]) << 8U) | (static_cast<std::uint32_t>(header[2]) << 16U) |
        (static_cast<std::uint32_t>(header[3]) << 24U);
    if (!length || length > max_frame) return {};
    std::string body(length, '\0');
    if (!exact(body.data(), body.size(), deadline, c) || !strict_utf8(body)) return {};
    return body;
  }
  bool send(const std::shared_ptr<connection>& c, std::string& body) {
    if (body.empty() || body.size() > max_frame) return false;
    const auto length = static_cast<std::uint32_t>(body.size());
    std::array<unsigned char, 4> header{{static_cast<unsigned char>(length),
        static_cast<unsigned char>(length >> 8U), static_cast<unsigned char>(length >> 16U),
        static_cast<unsigned char>(length >> 24U)}};
    const auto deadline = ::GetTickCount64() + options.io_timeout_ms;
    return exact(header.data(), header.size(), deadline, c, true) && exact(body.data(), body.size(), deadline, c, true);
  }
  bool owner_reply(const std::shared_ptr<connection>& c, DWORD timeout) {
    const HANDLE handles[]{stop_event.get(), c->wake.get()};
    if (::WaitForMultipleObjects(2, handles, FALSE, timeout) != WAIT_OBJECT_0 + 1) return false;
    std::string body;
    { std::lock_guard<std::mutex> lock(c->mutex);
      if (c->closed || !c->reply_ready) return false;
      body = std::move(c->reply); c->reply_ready = false;
      if (!::ResetEvent(c->wake.get())) return false;
    }
    return send(c, body);
  }
  void run_connection(const std::shared_ptr<connection>& c) {
    if (!owner_reply(c, options.owner_timeout_ms)) return;
    while (!stop_requested.load()) {
      const auto body = receive(c); if (!body) return;
      const auto checked = verify_windows_named_pipe_peer(pipe.get());
      if (!checked.verified() || !same_peer(*c->peer, *checked.identity)) return;
      auto request = request_frame(*body);
      const auto timeout = std::min(request.timeout_ms, options.owner_timeout_ms);
      { std::lock_guard<std::mutex> lock(c->mutex);
        // Reply publication can precede the owner's final bookkeeping. The old
        // request is already owner-local, so the empty single slot may queue
        // the next frame; owner_busy still blocks dispatch/precise cleanup.
        if (c->closed || c->work != work_kind::none) return;
        c->request = std::move(request); c->work = work_kind::request;
      }
      if (!owner_reply(c, timeout)) return;
      bool terminal = false;
      { std::lock_guard<std::mutex> lock(c->mutex); terminal = c->unauthorized_reply; }
      if (terminal) {
        // Write completion is not client consumption. Give a sequential client
        // a bounded chance to read the fixed denial before disconnect discards
        // buffered data. No further frame is parsed or dispatched.
        unsigned char ignored = 0;
        (void)exact(&ignored, 1, ::GetTickCount64() + options.io_timeout_ms, c);
        return;
      }
    }
  }
  void run() noexcept {
    try {
      while (!stop_requested.load()) {
        if (!connect()) { if (!stop_requested.load()) error.store(local_endpoint_error::io_failure); break; }
        status.store(local_endpoint_state::connected);
        std::shared_ptr<connection> c;
        try {
          const auto hello = receive();
          if (hello && hello_frame(*hello)) {
            auto verified = verify_windows_named_pipe_peer(pipe.get());
            if (verified.verified()) {
              c = std::make_shared<connection>(std::move(verified.identity));
              { std::lock_guard<std::mutex> lock(mutex); current = c; }
              run_connection(c);
            }
          }
        } catch (const std::bad_alloc&) { error.store(local_endpoint_error::out_of_memory); }
          catch (...) { error.store(local_endpoint_error::io_failure); }
        close_connection(c);
        // No FlushFileBuffers: a client must not make server shutdown wait for
        // consumption. All actual overlapped operations have completed here.
        (void)::DisconnectNamedPipe(pipe.get());
        if (c && !stop_requested.load()) {
          const HANDLE handles[]{stop_event.get(), c->cleaned.get()};
          if (::WaitForMultipleObjects(2, handles, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) break;
          std::lock_guard<std::mutex> lock(mutex); if (current == c) current.reset();
        }
        if (!stop_requested.load()) status.store(local_endpoint_state::listening);
      }
    } catch (const std::bad_alloc&) { error.store(local_endpoint_error::out_of_memory); }
      catch (...) { error.store(local_endpoint_error::internal_failure); }
    try { close_connection(get_current()); } catch (...) { lifetime_fail_stop(); }
    stop_requested.store(true); status.store(local_endpoint_state::stopping);
    if (!::SetEvent(stop_event.get())) lifetime_fail_stop();
  }
  bool owner_valid() noexcept {
    if (owner && owner == ::GetCurrentThreadId()) return true;
    error.store(local_endpoint_error::wrong_owner_thread); return false;
  }
  void publish_reply(const std::shared_ptr<connection>& c, std::string reply, bool unauthorized = false) {
    std::lock_guard<std::mutex> lock(c->mutex);
    if (!c->closed && !stop_requested.load()) {
      c->reply = std::move(reply); c->reply_ready = true; c->unauthorized_reply = unauthorized;
      if (!::SetEvent(c->wake.get())) lifetime_fail_stop();
    }
  }
  void authorize_connection(const std::shared_ptr<connection>& c) {
    const auto policy = authorize ? authorize(*c->peer) : std::optional<local_endpoint_policy>{};
    if (!policy || !valid_scope(policy->scope) || !scope_current || !scope_current(policy->scope) ||
        c->peer->process_state() != held_process_state::alive) { close_connection(c); return; }
    auto binding = permissions.create_local_binding(policy->session_id, policy->origin);
    { std::lock_guard<std::mutex> lock(c->mutex);
      c->binding = binding; c->scope = policy->scope;
      if (c->closed || stop_requested.load()) binding->close();
    }
    auto grant = permissions.issue_local(binding, policy->capabilities, policy->methods);
    { std::lock_guard<std::mutex> lock(c->mutex);
      // Save precise cleanup ownership even when issue_local lost to close.
      c->grant = std::move(grant);
    }
    if (!scope_current(policy->scope) || !binding->active()) { close_connection(c); return; }
    publish_reply(c, R"({"kind":"hello","schemaVersion":"soui.local/1","ok":true})");
  }
  void execute(const std::shared_ptr<connection>& c, const bridge_request& request) {
    std::shared_ptr<const native_connection_binding> binding;
    std::shared_ptr<const authorization_scope> scope;
    std::string bearer;
    local_endpoint_scope key;
    { std::lock_guard<std::mutex> lock(c->mutex);
      if (c->closed || stop_requested.load()) return;
      binding = c->binding; scope = c->grant.scope; bearer = c->grant.token; key = c->scope;
    }
    if (!binding || !binding->active() || !scope || !scope_current || !scope_current(key) ||
        c->peer->process_state() != held_process_state::alive) { close_connection(c); return; }
    if (!scope->active()) {
      // Only a fixed denial may cross an expired grant; no business dispatch or
      // discovery runs. Native scope/peer/binding admission still gates the send.
      bridge_response denied; denied.error_code = "E_UNAUTHORIZED";
      denied.error_message = "Local authorization unavailable; reconnect for approval.";
      publish_reply(c, response_frame(request, denied), true); return;
    }
    bridge_response response;
    if (request.method == "bridge.describe") {
      response.payload_json = transport.describe_local(request, binding, bearer);
      const auto payload = bounded_json(response.payload_json);
      if (payload.contains("error") && payload["error"].is_string()) {
        response.error_code = payload["error"].get<std::string>();
        response.error_message = "Local discovery rejected."; response.payload_json.clear();
      } else response.ok = true;
    } else response = transport.handle_local(request, binding, bearer);
    if (!scope_current(key) || !binding->active() || !scope->active() ||
        c->peer->process_state() != held_process_state::alive) { close_connection(c); return; }
    publish_reply(c, response_frame(request, response));
  }
};

windows_local_endpoint::windows_local_endpoint(bridge_transport& transport,
    permission_store& permissions, task_service* tasks)
    : data_(std::make_unique<impl>(transport, permissions, tasks)) {}
windows_local_endpoint::~windows_local_endpoint() {
  if (data_->worker.joinable()) lifetime_fail_stop();
}
local_endpoint_error windows_local_endpoint::start(const local_endpoint_options& options,
    authorizer authorize, scope_check current) noexcept {
  auto& d = *data_;
  if (d.owner && d.owner != ::GetCurrentThreadId()) return local_endpoint_error::wrong_owner_thread;
  if (d.worker.joinable()) return local_endpoint_error::already_started;
  if (!identifier(options.pipe_component, true) || !options.io_timeout_ms || options.io_timeout_ms > 60000 ||
      !options.owner_timeout_ms || options.owner_timeout_ms > 60000) return local_endpoint_error::invalid_options;
  try {
    auto security = pipe_security(); if (!security) return local_endpoint_error::security_unavailable;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), security.get(), FALSE};
    const std::wstring name = L"\\\\.\\pipe\\" + std::wstring(options.pipe_component.begin(), options.pipe_component.end());
    native_handle pipe(::CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
        FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
        PIPE_REJECT_REMOTE_CLIENTS, 1, static_cast<DWORD>(max_frame), static_cast<DWORD>(max_frame), 0, &attributes));
    if (!pipe) return local_endpoint_error::pipe_unavailable;
    native_handle stop(::CreateEventW(nullptr, TRUE, FALSE, nullptr)), io(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!stop || !io) return local_endpoint_error::native_start_failed;
    std::lock_guard<std::mutex> lock(d.mutex);
    d.options = options; d.authorize = std::move(authorize); d.scope_current = std::move(current);
    d.pipe = std::move(pipe); d.stop_event = std::move(stop); d.io_event = std::move(io);
    d.owner = ::GetCurrentThreadId(); d.stop_requested.store(false); d.error.store(local_endpoint_error::none);
    d.status.store(local_endpoint_state::listening);
    try { d.worker = std::thread([&d] { d.run(); }); }
    catch (...) { d.pipe.reset(); d.stop_event.reset(); d.io_event.reset(); d.authorize = {}; d.scope_current = {};
      d.status.store(local_endpoint_state::stopped); return local_endpoint_error::native_start_failed; }
    return local_endpoint_error::none;
  } catch (const std::bad_alloc&) { d.error.store(local_endpoint_error::out_of_memory); return local_endpoint_error::out_of_memory; }
    catch (...) { d.error.store(local_endpoint_error::internal_failure); return local_endpoint_error::internal_failure; }
}
std::size_t windows_local_endpoint::pump_owner(std::size_t max_count, std::uint32_t time_budget_ms) noexcept {
  auto& d = *data_;
  if (!d.owner_valid() || d.pumping || !max_count || !time_budget_ms) return 0;
  d.pumping = true;
  struct pump_exit { bool& busy; ~pump_exit() { busy = false; } } finish{d.pumping};
  const auto deadline = ::GetTickCount64() + std::min<std::uint32_t>(time_budget_ms, 50);
  std::size_t count = 0;
  try {
    while (count < std::min<std::size_t>(max_count, 64) && ::GetTickCount64() < deadline) {
      const auto c = d.get_current(); if (!c) break;
      if (d.cleanup(c)) { ++count; continue; }
      impl::work_kind work = impl::work_kind::none; bridge_request request;
      { std::lock_guard<std::mutex> lock(c->mutex);
        if (c->closed || d.stop_requested.load() || c->owner_busy || c->work == impl::work_kind::none) break;
        c->owner_busy = true; work = c->work; c->work = impl::work_kind::none; request = std::move(c->request);
      }
      try { if (work == impl::work_kind::authorize) d.authorize_connection(c); else d.execute(c, request); }
      catch (const std::bad_alloc&) { d.error.store(local_endpoint_error::out_of_memory); d.close_connection(c); }
      catch (...) { d.error.store(local_endpoint_error::callback_failure); d.close_connection(c); }
      { std::lock_guard<std::mutex> lock(c->mutex); c->owner_busy = false; }
      ++count;
    }
  } catch (...) { d.error.store(local_endpoint_error::internal_failure); request_stop(); }
  return count;
}
bool windows_local_endpoint::revoke_scope(const local_endpoint_scope& scope) noexcept {
  auto& d = *data_; if (!d.owner_valid() || !valid_scope(scope)) return false;
  try {
    const auto c = d.get_current(); if (!c) return false;
    { std::lock_guard<std::mutex> lock(c->mutex); if (!same_scope(c->scope, scope)) return false; }
    d.close_connection(c);
    std::lock_guard<std::mutex> lock(d.mutex);
    // A cleaned old scope must not cancel the next connection's listen/read.
    if (d.current == c && d.pipe) (void)::CancelIoEx(d.pipe.get(), nullptr);
    return true;
  } catch (...) { d.error.store(local_endpoint_error::internal_failure); request_stop(); return false; }
}
void windows_local_endpoint::request_stop() noexcept {
  auto& d = *data_;
  try {
    std::shared_ptr<impl::connection> c;
    { std::lock_guard<std::mutex> lock(d.mutex);
      d.stop_requested.store(true);
      if (d.status.load() == local_endpoint_state::stopped) return;
      d.status.store(local_endpoint_state::stopping); c = d.current;
      if (d.stop_event && !::SetEvent(d.stop_event.get())) lifetime_fail_stop();
      if (d.pipe) (void)::CancelIoEx(d.pipe.get(), nullptr);
    }
    d.close_connection(c);
  } catch (...) { lifetime_fail_stop(); }
}
local_endpoint_stop windows_local_endpoint::poll_stop() noexcept {
  auto& d = *data_;
  if (!d.worker.joinable()) return local_endpoint_stop::stopped;
  if (!d.owner_valid()) return local_endpoint_stop::wrong_owner_thread;
  if (!d.stop_requested.load() || d.pumping) return local_endpoint_stop::pending;
  (void)pump_owner(8, 4);
  if (::WaitForSingleObject(static_cast<HANDLE>(d.worker.native_handle()), 0) != WAIT_OBJECT_0)
    return local_endpoint_stop::pending;
  try {
    const auto c = d.get_current();
    if (c) { d.close_connection(c); (void)pump_owner(8, 4);
      std::lock_guard<std::mutex> lock(c->mutex); if (!c->cleanup_done) return local_endpoint_stop::pending; }
    d.worker.join();
    std::lock_guard<std::mutex> lock(d.mutex);
    d.current.reset(); d.pipe.reset(); d.stop_event.reset(); d.io_event.reset();
    d.authorize = {}; d.scope_current = {}; d.status.store(local_endpoint_state::stopped);
    return local_endpoint_stop::stopped;
  } catch (...) { d.error.store(local_endpoint_error::internal_failure); return local_endpoint_stop::pending; }
}
local_endpoint_state windows_local_endpoint::state() const noexcept { return data_->status.load(); }
local_endpoint_error windows_local_endpoint::last_error() const noexcept { return data_->error.load(); }
}  // namespace soui::runtime::agent
