#include "reference-agent.h"
#include "reference-gui.h"
#include "reference-business.h"
#include <atomic>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace reference {
namespace {
using namespace soui::runtime::agent;
class handle final {
 public:
  explicit handle(HANDLE value = nullptr) noexcept : value_{value} {}
  ~handle() { reset(); }
  handle(const handle&) = delete;
  handle& operator=(const handle&) = delete;
  HANDLE get() const noexcept { return value_; }
  void reset(HANDLE value = nullptr) noexcept {
    if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
    value_ = value;
  }
 private: HANDLE value_;
};
[[noreturn]] void configuration_failure() { throw std::runtime_error("E_REFERENCE_AGENT_CONFIG"); }
bool component_valid(const std::string& value) noexcept {
  if (value.empty() || value.size() > 128) return false;
  for (const unsigned char c : value)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
  return true;
}
std::uint64_t next_identity() noexcept {
  static std::atomic<std::uint64_t> value{1};
  const auto result = value.fetch_add(1, std::memory_order_relaxed);
  if (!result || result == std::numeric_limits<std::uint64_t>::max()) std::terminate();
  return result;
}
std::uint64_t creation(HANDLE process) {
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) configuration_failure();
  const auto result = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32U) | created.dwLowDateTime;
  if (!result) configuration_failure();
  return result;
}
struct token_context final {
  std::vector<std::uint8_t> user;
  LUID authentication{};
  DWORD session{};
};
token_context context(HANDLE process) {
  HANDLE native{};
  if (!OpenProcessToken(process, TOKEN_QUERY, &native)) configuration_failure();
  const handle token{native};
  DWORD length{};
  (void)GetTokenInformation(token.get(), TokenUser, nullptr, 0, &length);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !length || length > 65536) configuration_failure();
  std::vector<std::uint8_t> buffer(length);
  if (!GetTokenInformation(token.get(), TokenUser, buffer.data(), length, &length)) configuration_failure();
  const auto* info = reinterpret_cast<const TOKEN_USER*>(buffer.data());
  if (!IsValidSid(info->User.Sid)) configuration_failure();
  token_context result;
  result.user.resize(GetLengthSid(info->User.Sid));
  if (result.user.empty() || !CopySid(static_cast<DWORD>(result.user.size()), result.user.data(), info->User.Sid)) configuration_failure();
  TOKEN_STATISTICS statistics{};
  if (!GetTokenInformation(token.get(), TokenStatistics, &statistics, sizeof(statistics), &length) ||
      !GetTokenInformation(token.get(), TokenSessionId, &result.session, sizeof(result.session), &length)) configuration_failure();
  result.authentication = statistics.AuthenticationId;
  return result;
}
bool same_context(const token_context& a, const token_context& b) noexcept {
  return a.user == b.user && a.authentication.LowPart == b.authentication.LowPart &&
      a.authentication.HighPart == b.authentication.HighPart && a.session == b.session;
}
bool same_key(const local_endpoint_scope& a, const local_endpoint_scope& b) noexcept {
  return a.application == b.application && a.window == b.window && a.document == b.document && a.generation == b.generation;
}
}
command_line parse_command_line(int count, wchar_t* const* arguments) noexcept {
  try {
    command_line result;
    if (!arguments || count < 1 || count > 8) return result;
    for (int i = 0; i < count; ++i) if (!arguments[i]) return result;
    if (count == 1) { result.valid = true; return result; }
    if (count == 2 && std::wstring{arguments[1]} == L"--smoke-test") { result.valid = result.smoke = true; return result; }
    bool pipe_seen = false, process_seen = false;
    for (int i = 1; i < count; i += 2) {
      if (i + 1 >= count || !arguments[i] || !arguments[i + 1]) return result;
      const std::wstring name{arguments[i]}, value{arguments[i + 1]};
      if (name == L"--agent-pipe" && !pipe_seen) {
        pipe_seen = true;
        if (value.empty() || value.size() > 128) return result;
        for (const auto c : value) { if (c > 127) return result; result.agent.pipe_component.push_back(static_cast<char>(c)); }
        if (!component_valid(result.agent.pipe_component)) return result;
      } else if (name == L"--agent-allow-process" && !process_seen) {
        process_seen = true;
        if (value.empty() || value.size() > 10) return result;
        std::uint64_t number{};
        for (const auto c : value) {
          if (c < L'0' || c > L'9') return result;
          number = number * 10 + static_cast<unsigned>(c - L'0');
          if (number > std::numeric_limits<DWORD>::max()) return result;
        }
        if (!number) return result;
        result.agent.approved_process = static_cast<DWORD>(number);
      } else return result;
    }
    result.valid = pipe_seen && process_seen;
    result.agent.enabled = result.valid;
    return result;
  } catch (...) { return {}; }
}
struct agent::impl final {
  soui::sdk::json_bridge& bridge;
  const agent_options options;
  const DWORD owner{GetCurrentThreadId()};
  handle process;
  handle control;
  std::uint64_t process_creation{};
  token_context process_context;
  windows_local_endpoint* endpoint{}; // Immutable once constructor publishes.
  const local_endpoint_scope key{next_identity(), next_identity(), next_identity(), next_identity()};
  std::function<bool()> current;
  std::atomic<bool> closed{false};
  std::atomic<bool> binding_started{false}; // Monotonic; never reopened after trusted binding begins.
  bool started{false}, stopped{true};
  const bool gui_commands;
  impl(soui::sdk::json_bridge& target, agent_options input, bool gui) : bridge{target}, options{std::move(input)}, gui_commands{gui} {
    if (!options.enabled) {
      if (!options.pipe_component.empty() || options.approved_process) configuration_failure();
      if (gui_commands && (!register_local_business(bridge) || !register_gui_business(bridge))) configuration_failure();
      return;
    }
    if (!component_valid(options.pipe_component) || !options.approved_process || options.approved_process == GetCurrentProcessId()) configuration_failure();
    process.reset(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, options.approved_process));
    if (!process.get() || WaitForSingleObject(process.get(), 0) != WAIT_TIMEOUT) configuration_failure();
    process_creation = creation(process.get());
    process_context = context(process.get());
    if (!same_context(process_context, context(GetCurrentProcess())) || WaitForSingleObject(process.get(), 0) != WAIT_TIMEOUT) configuration_failure();
    control.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!control.get()) configuration_failure();
    if (!register_local_business(bridge)) configuration_failure();
    if (gui_commands && !register_gui_business(bridge)) configuration_failure();
    endpoint = bridge.create_local_endpoint();
    if (!endpoint || endpoint->state() != local_endpoint_state::stopped) configuration_failure();
  }
  bool current_scope(const local_endpoint_scope& scope) {
    return GetCurrentThreadId() == owner && !closed.load(std::memory_order_acquire) && same_key(scope, key) &&
        current && current() && !closed.load(std::memory_order_acquire) &&
        process.get() && WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT;
  }
  std::optional<local_endpoint_policy> authorize(const windows_peer_identity& peer) {
    if (!current_scope(key) || peer.process_state() != held_process_state::alive ||
        peer.process_id() != options.approved_process || peer.creation_filetime() != process_creation ||
        peer.user_sid() != process_context.user || peer.session_id() != process_context.session ||
        peer.authentication_id().low_part != process_context.authentication.LowPart ||
        peer.authentication_id().high_part != process_context.authentication.HighPart) return {};
    local_endpoint_policy policy;
    policy.scope = key;
    policy.session_id = "reference-agent-" + std::to_string(key.application) + "-" + std::to_string(key.document);
    policy.origin = "app://reference-agent";
    policy.methods = {"bridge.describe", "reference.fixture.describe", "reference.form.submit",
      "reference.summary.local.start", "reference.summary.local.query", "reference.summary.local.cancel"};
    if (gui_commands) {
      const auto methods = gui_command_methods();
      policy.methods.insert(policy.methods.end(), methods.begin(), methods.end());
    }
    policy.capabilities = policy.methods; // Exact families plus their declared business effects; no gui.* wildcard.
    if (!current_scope(key)) return {};
    return policy;
  }
};
agent::agent(soui::sdk::json_bridge& bridge, agent_options options) : agent{bridge, std::move(options), false} {}
agent::agent(soui::sdk::json_bridge& bridge, agent_options options, bool gui_commands)
    : data_{std::make_unique<impl>(bridge, std::move(options), gui_commands)} {}
agent::~agent() { if (!data_->stopped) std::terminate(); } // No dereference of endpoint after Bridge join.
bool agent::start_bound_document(std::function<bool()> current) {
  auto& d = *data_;
  if (GetCurrentThreadId() != d.owner || !d.endpoint || d.started || d.closed.load()) return false;
  d.current = std::move(current);
  if (!d.current_scope(d.key)) return false;
  d.binding_started.store(true, std::memory_order_seq_cst);
  if (!d.current_scope(d.key)) { request_close(); return false; }
  d.stopped = false;
  const auto result = d.endpoint->start(local_endpoint_options{d.options.pipe_component},
      [&d](const windows_peer_identity& peer) { return d.authorize(peer); },
      [&d](const local_endpoint_scope& key) { return d.current_scope(key); });
  if (result != local_endpoint_error::none) { request_close(); (void)stop_owner(); return false; }
  d.started = true;
  if (!d.current_scope(d.key)) { request_close(); return false; }
  return true;
}
void agent::pump_owner() noexcept {
  auto& d = *data_;
  if (GetCurrentThreadId() != d.owner || !d.endpoint || !d.started) return;
  // One native control event, observed by the existing bounded owner pump.
  // A racing reset cannot reopen the independent atomic admission-close bit.
  if (d.control.get() && WaitForSingleObject(d.control.get(), 0) == WAIT_OBJECT_0)
    if (!ResetEvent(d.control.get())) std::terminate();
  try { if (!d.current_scope(d.key)) request_close(); }
  catch (...) { request_close(); }
  if (d.closed.load(std::memory_order_acquire)) (void)stop_owner();
  else (void)d.endpoint->pump_owner(8, 4);
}
void agent::request_close() noexcept {
  auto& d = *data_;
  d.closed.store(true, std::memory_order_release);
  if (d.endpoint) d.endpoint->request_stop();
  if (d.control.get() && !SetEvent(d.control.get())) std::terminate();
}
local_endpoint_stop agent::stop_owner() noexcept {
  auto& d = *data_;
  if (GetCurrentThreadId() != d.owner) return local_endpoint_stop::wrong_owner_thread;
  request_close();
  if (!d.endpoint) return local_endpoint_stop::stopped;
  if (d.started) (void)d.endpoint->revoke_scope(d.key); // No current connection is a normal false result.
  const auto result = d.endpoint->poll_stop();
  if (result == local_endpoint_stop::stopped) { d.stopped = true; d.current = {}; }
  return result;
}
bool agent::enabled() const noexcept { return data_->options.enabled; }
bool agent::started() const noexcept { return data_->started; }
bool agent::closed() const noexcept { return data_->closed.load(std::memory_order_acquire); }
DWORD agent::selected_process_id() const noexcept { return data_->options.approved_process; }
std::uint64_t agent::selected_creation_filetime() const noexcept { return data_->process_creation; }
std::string agent::handle_json(std::string_view request, bool gesture) { return data_->bridge.handle_json(request, gesture); }
void agent::shutdown_session(const std::string& session) {
  // Initial Host mounting may clean a prior session before any trusted document
  // binding. Keep that cleanup real without disabling the first listener. Both
  // sides of the Bridge call observe a concurrent binding publication; owner
  // probes also recheck the actual receipt/digest around publication and start.
  const auto close_if_bound = [this]() noexcept {
    if (data_->binding_started.load(std::memory_order_seq_cst)) request_close();
  };
  close_if_bound();
  try { data_->bridge.shutdown_session(session); }
  catch (...) { close_if_bound(); throw; }
  close_if_bound();
}
void agent::set_owner_window(void* window) noexcept { data_->bridge.set_owner_window(window); }
void agent::set_event_sink(event_sink sink) { data_->bridge.set_event_sink(std::move(sink)); }
} // namespace reference
