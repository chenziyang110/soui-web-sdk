#pragma once
// Consumer-only cold UX gate; this is not a public SDK permission or close model.
// Create after the caller verifies its own HWND and before publishing callbacks.
// Keep this object (including controller/event handles) alive until Bridge
// callbacks have been unbound/quiesced. check_now never waits, resets, performs
// file IO or consumes a report. Normal user close must not wait on this gate.
// A signal means controller-observed termination, NOT measurement qualification.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace soui::benchmark::manual_observation {
using u64 = std::uint64_t;
struct binding final {
  std::string run, nonce;
  DWORD host_pid{}, controller_pid{};
  u64 host_creation{}, controller_creation{}, hwnd{}, expires_tick{};
};
enum class state { pending, released, unavailable };
struct check_result final {
  state status{state::unavailable};
  const char* code{"E_MANUAL_GATE_UNINITIALIZED"};
  DWORD win32_error{};
};
struct operation_result final {
  bool ok{};
  const char* code{"E_MANUAL_GATE_UNINITIALIZED"};
  DWORD win32_error{}, cleanup_error{};
};
struct handle_result final { HANDLE value{}; DWORD error{}, cleanup_error{}; };
struct wait_result final { DWORD value{WAIT_FAILED}, error{}; };

inline bool hex32(const std::string& text) noexcept {
  if (text.size() != 32) return false;
  bool nonzero = false;
  for (const char c : text) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    nonzero = nonzero || c != '0';
  }
  return nonzero;
}
inline bool valid_binding(const binding& b) noexcept {
  return hex32(b.run) && hex32(b.nonce) && b.host_pid && b.host_creation &&
      b.controller_pid && b.controller_creation && b.hwnd && b.expires_tick &&
      b.hwnd <= static_cast<u64>(~static_cast<std::uintptr_t>(0));
}
inline std::wstring event_name(const binding& b) {
  if (!valid_binding(b)) return {};
  return L"Local\\SOUI-ManualObservation-Done-v1-" + std::wstring(b.run.begin(), b.run.end()) +
      L"-" + std::to_wstring(b.host_pid) + L"-" + std::to_wstring(b.host_creation) +
      L"-" + std::to_wstring(b.hwnd) + L"-" + std::to_wstring(b.controller_pid) +
      L"-" + std::to_wstring(b.controller_creation) + L"-" + std::wstring(b.nonce.begin(), b.nonce.end());
}

struct win32_ops final {
  static DWORD failure() noexcept { const auto e = GetLastError(); return e ? e : ERROR_GEN_FAILURE; }
  u64 tick() const noexcept { return GetTickCount64(); }
  bool creation(HANDLE process, u64& value, DWORD& error) const noexcept {
    FILETIME born{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &born, &exited, &kernel, &user)) { error = failure(); return false; }
    value = (static_cast<u64>(born.dwHighDateTime) << 32) | born.dwLowDateTime; return value != 0;
  }
  bool self(DWORD& pid, u64& born, DWORD& error) const noexcept {
    pid = GetCurrentProcessId(); return pid && creation(GetCurrentProcess(), born, error);
  }
  handle_result open_controller(DWORD pid) const noexcept {
    const auto h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    return {h, h ? ERROR_SUCCESS : failure(), 0};
  }
  wait_result wait_zero(HANDLE h) const noexcept {
    const auto value = WaitForSingleObject(h, 0);
    return {value, value == WAIT_FAILED ? failure() : ERROR_SUCCESS};
  }
  DWORD close(HANDLE h) const noexcept { return CloseHandle(h) ? ERROR_SUCCESS : failure(); }
  handle_result create_event(const std::wstring& name) const {
    // Only cold creation allocates. Explicit logon-SID DACL, no inherited
    // handles and no Everyone/other-session grants. ACLs cannot bind a PID;
    // same-logon hostile native code remains outside this UX gate's guarantee.
    struct token_guard final {
      const win32_ops& ops; HANDLE value{};
      ~token_guard() { if (value) ops.close(value); }
      DWORD finish() noexcept { const auto h = value; value = nullptr; return h ? ops.close(h) : 0; }
    } token{*this};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) return {nullptr, failure(), 0};
    DWORD needed{};
    SetLastError(ERROR_SUCCESS);
    const auto initial = GetTokenInformation(token.value, TokenGroups, nullptr, 0, &needed);
    const auto initial_error = GetLastError();
    if (initial || initial_error != ERROR_INSUFFICIENT_BUFFER || !needed || needed > 65536) {
      const auto error = initial_error ? initial_error : ERROR_INVALID_DATA;
      return {nullptr, error, token.finish()};
    }
    std::vector<std::max_align_t> storage((needed + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
    if (!GetTokenInformation(token.value, TokenGroups, storage.data(), needed, &needed)) {
      const auto error = failure(); return {nullptr, error, token.finish()};
    }
    const auto* groups = reinterpret_cast<const TOKEN_GROUPS*>(storage.data());
    PSID logon{};
    for (DWORD i = 0; i < groups->GroupCount; ++i) {
      if ((groups->Groups[i].Attributes & SE_GROUP_LOGON_ID) != SE_GROUP_LOGON_ID) continue;
      if (logon || !IsValidSid(groups->Groups[i].Sid)) return {nullptr, ERROR_INVALID_SID, token.finish()};
      logon = groups->Groups[i].Sid;
    }
    if (!logon) return {nullptr, ERROR_NO_SUCH_LOGON_SESSION, token.finish()};
    struct local_memory final { HLOCAL value{}; ~local_memory() { if (value) LocalFree(value); } } sid, descriptor;
    LPWSTR sid_text{};
    if (!ConvertSidToStringSidW(logon, &sid_text)) { const auto error = failure(); return {nullptr, error, token.finish()}; }
    sid.value = sid_text;
    const std::wstring sddl = L"D:P(A;;0x00120002;;;" + std::wstring(sid_text) + L")";
    const auto close_error = token.finish();
    if (close_error) return {nullptr, close_error, 0};
    PSECURITY_DESCRIPTOR security{};
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &security, nullptr))
      return {nullptr, failure(), 0};
    descriptor.value = security;
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), security, FALSE};
    SetLastError(ERROR_SUCCESS);
    // Host only waits/reads security. The separately bound signal helper opens
    // EVENT_MODIFY_STATE, never creates or replaces an event.
    const auto h = CreateEventExW(&attributes, name.c_str(), CREATE_EVENT_MANUAL_RESET, SYNCHRONIZE | READ_CONTROL);
    const auto error = GetLastError();
    return {h, h ? error : (error ? error : ERROR_GEN_FAILURE), 0};
  }
};

template<class Ops = win32_ops> class basic_gate final {
 public:
  explicit basic_gate(Ops ops = {}) : ops_(std::move(ops)) {}
  ~basic_gate() { close_checked(); }
  basic_gate(const basic_gate&) = delete;
  basic_gate& operator=(const basic_gate&) = delete;
  operation_result create(const binding& identity) {
    if (attempted_) return {false, "E_MANUAL_GATE_ALREADY_CONFIGURED", 0, 0};
    attempted_ = true;
    if (!valid_binding(identity)) return {false, "E_MANUAL_GATE_BINDING", 0, 0};
    born_tick_ = ops_.tick();
    if (identity.expires_tick <= born_tick_ || identity.expires_tick - born_tick_ > 150000)
      return {false, "E_MANUAL_GATE_EXPIRY", 0, 0};
    // No handles exist before the only potentially allocating identity copy.
    identity_ = identity; name_ = event_name(identity);
    if (name_.empty() || name_.size() >= 240) return {false, "E_MANUAL_GATE_NAME", 0, 0};
    DWORD self_pid{}, error{}; u64 self_creation{};
    if (!ops_.self(self_pid, self_creation, error) || self_pid != identity.host_pid || self_creation != identity.host_creation)
      return {false, "E_MANUAL_GATE_HOST_IDENTITY", error, 0};
    const auto controller = ops_.open_controller(identity.controller_pid);
    controller_ = controller.value;
    if (!controller_ || controller.error || controller.cleanup_error)
      return failed("E_MANUAL_GATE_CONTROLLER_OPEN", controller.error, controller.cleanup_error);
    u64 actual_creation{};
    if (!ops_.creation(controller_, actual_creation, error) || actual_creation != identity.controller_creation)
      return failed("E_MANUAL_GATE_CONTROLLER_IDENTITY", error);
    const auto alive = ops_.wait_zero(controller_);
    if (alive.value != WAIT_TIMEOUT) return failed("E_MANUAL_GATE_CONTROLLER_NOT_ALIVE", alive.error);
    try {
      const auto event = ops_.create_event(name_); event_ = event.value;
      if (!event_ || event.error || event.cleanup_error)
        return failed(event.error == ERROR_ALREADY_EXISTS ? "E_MANUAL_GATE_ALREADY_EXISTS" : "E_MANUAL_GATE_CREATE", event.error, event.cleanup_error);
    } catch (...) {
      close_checked(); throw; // No exception is converted into an accepted gate.
    }
    ready_ = true; return {true, nullptr, 0, 0};
  }
  check_result check_now() const noexcept {
    if (!ready_) return {state::unavailable, "E_MANUAL_GATE_UNINITIALIZED", 0};
    const auto now = ops_.tick();
    if (now < born_tick_) return {state::unavailable, "E_MANUAL_GATE_CLOCK", 0};
    if (now >= identity_.expires_tick) return {state::unavailable, "E_MANUAL_GATE_EXPIRED", 0};
    const auto controller = ops_.wait_zero(controller_);
    if (controller.value != WAIT_TIMEOUT)
      return {state::unavailable, controller.value == WAIT_FAILED ? "E_MANUAL_GATE_CONTROLLER_WAIT" : "E_MANUAL_GATE_CONTROLLER_EXITED", controller.error};
    const auto event = ops_.wait_zero(event_);
    if (event.value == WAIT_TIMEOUT) return {state::pending, nullptr, 0};
    if (event.value == WAIT_OBJECT_0) return {state::released, nullptr, 0};
    return {state::unavailable, "E_MANUAL_GATE_EVENT_WAIT", event.error};
  }
  operation_result close_checked() noexcept {
    ready_ = false; DWORD first{}, secondary{};
    for (HANDLE* slot : {&event_, &controller_}) {
      const auto h = *slot; *slot = nullptr;
      if (h) { const auto error = ops_.close(h); if (!first) first = error; else if (error && !secondary) secondary = error; }
    }
    return {!first, first ? "E_MANUAL_GATE_CLOSE" : nullptr, first, secondary};
  }
  const std::wstring& name() const noexcept { return name_; }
  const binding& identity() const noexcept { return identity_; }
 private:
  operation_result failed(const char* code, DWORD error, DWORD cleanup = 0) noexcept {
    const auto closed = close_checked();
    return {false, code, error, cleanup ? cleanup : (closed.win32_error ? closed.win32_error : closed.cleanup_error)};
  }
  Ops ops_; binding identity_; std::wstring name_;
  HANDLE controller_{}, event_{}; u64 born_tick_{}; bool attempted_{}, ready_{};
};
using gate = basic_gate<>;
} // namespace soui::benchmark::manual_observation
