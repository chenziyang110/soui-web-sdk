#pragma once
// Private cold controller UX signal. It neither authorizes a page nor proves
// observation qualification/Host acceptance. The caller must already have
// verified its held observer's natural terminal; this helper cannot prove it.
// No event-name input, event creation/reset, messages or target close operation.
#include "../input-ui-host/manual-observation-gate.h"
#include <cwchar>
#include <new>

namespace soui::benchmark::manual_observation {
struct signal_win32_ops final {
  u64 tick() const noexcept { return GetTickCount64(); }
  handle_result open_process(DWORD pid) const noexcept {
    return win32_ops{}.open_controller(pid);
  }
  bool creation(HANDLE h, u64& value, DWORD& error) const noexcept {
    return win32_ops{}.creation(h, value, error);
  }
  wait_result wait_zero(HANDLE h) const noexcept { return win32_ops{}.wait_zero(h); }
  bool window_matches(HWND hwnd, DWORD expected_pid, DWORD& error) const noexcept {
    DWORD pid{};
    SetLastError(ERROR_SUCCESS);
    if (!GetWindowThreadProcessId(hwnd, &pid)) { error = win32_ops::failure(); return false; }
    if (pid != expected_pid) return false;
    constexpr wchar_t expected[] = L"SouiInputUiHostV1";
    wchar_t actual[64]{};
    SetLastError(ERROR_SUCCESS);
    const auto length = GetClassNameW(hwnd, actual, 64);
    if (!length) { error = win32_ops::failure(); return false; }
    return length == static_cast<int>(sizeof(expected) / sizeof(wchar_t) - 1) &&
        std::wmemcmp(actual, expected, static_cast<std::size_t>(length)) == 0;
  }
  handle_result open_event(const std::wstring& name) const noexcept {
    const auto h = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
    return {h, h ? ERROR_SUCCESS : win32_ops::failure(), 0};
  }
  DWORD set(HANDLE event) const noexcept { return SetEvent(event) ? ERROR_SUCCESS : win32_ops::failure(); }
  DWORD close(HANDLE h) const noexcept { return win32_ops{}.close(h); }
};

namespace signal_detail {
template<class Ops> struct handles final {
  Ops& ops;
  HANDLE host{}, controller{}, event{};
  ~handles() { finish({false, "E_MANUAL_SIGNAL_EXCEPTION", 0, 0}); }
  // Remove ownership before each close, including failed closes. Never retry a
  // consumed handle in cleanup/destruction, and still retire the other handles.
  operation_result finish(operation_result result) noexcept {
    for (HANDLE* slot : {&event, &controller, &host}) {
      const auto h = *slot; *slot = nullptr;
      if (!h) continue;
      const auto error = ops.close(h);
      if (!error) continue;
      if (result.ok) result = {false, "E_MANUAL_SIGNAL_CLOSE", error, 0};
      else if (!result.cleanup_error) result.cleanup_error = error;
    }
    return result;
  }
};
} // namespace signal_detail

// Ops is only a CPU fault-injection seam around these same guarded operations.
// Its close must be noexcept, as with signal_win32_ops; no successful return is
// produced before all three held handles have been explicitly checked closed.
template<class Ops> operation_result signal_with_ops(const binding& identity, Ops& ops) noexcept {
  signal_detail::handles<Ops> held{ops};
  try {
    if (!valid_binding(identity)) return {false, "E_MANUAL_SIGNAL_BINDING", 0, 0};
    const auto started = ops.tick();
    if (identity.expires_tick <= started || identity.expires_tick - started > 150000)
      return {false, "E_MANUAL_SIGNAL_EXPIRY", 0, 0};
    // All production allocations finish before opening any handles.
    const auto name = event_name(identity);
    if (name.empty() || name.size() >= 240) return {false, "E_MANUAL_SIGNAL_NAME", 0, 0};
    const auto host = ops.open_process(identity.host_pid); held.host = host.value;
    if (!held.host || host.error || host.cleanup_error)
      return held.finish({false, "E_MANUAL_SIGNAL_HOST_OPEN", host.error, host.cleanup_error});
    const auto verify_process = [&](HANDLE process, u64 expected, bool is_host) {
      DWORD error{}; u64 actual{};
      if (!ops.creation(process, actual, error) || actual != expected)
        return operation_result{false, is_host ? "E_MANUAL_SIGNAL_HOST_IDENTITY" : "E_MANUAL_SIGNAL_CONTROLLER_IDENTITY", error, 0};
      const auto waited = ops.wait_zero(process);
      if (waited.value != WAIT_TIMEOUT)
        return operation_result{false, is_host ? "E_MANUAL_SIGNAL_HOST_NOT_ALIVE" : "E_MANUAL_SIGNAL_CONTROLLER_NOT_ALIVE", waited.error, 0};
      return operation_result{true, nullptr, 0, 0};
    };
    auto checked = verify_process(held.host, identity.host_creation, true);
    if (!checked.ok) return held.finish(checked);
    const auto controller = ops.open_process(identity.controller_pid); held.controller = controller.value;
    if (!held.controller || controller.error || controller.cleanup_error)
      return held.finish({false, "E_MANUAL_SIGNAL_CONTROLLER_OPEN", controller.error, controller.cleanup_error});
    checked = verify_process(held.controller, identity.controller_creation, false);
    if (!checked.ok) return held.finish(checked);
    const auto window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(identity.hwnd));
    DWORD error{};
    if (!ops.window_matches(window, identity.host_pid, error))
      return held.finish({false, "E_MANUAL_SIGNAL_WINDOW", error, 0});
    const auto event = ops.open_event(name); held.event = event.value;
    if (!held.event || event.error || event.cleanup_error)
      return held.finish({false, "E_MANUAL_SIGNAL_EVENT_OPEN", event.error, event.cleanup_error});
    // Recheck the original instances/window and absolute expiry after opening
    // the event. Holding process handles does not itself mean they remain alive.
    checked = verify_process(held.host, identity.host_creation, true);
    if (!checked.ok) return held.finish(checked);
    checked = verify_process(held.controller, identity.controller_creation, false);
    if (!checked.ok) return held.finish(checked);
    error = 0;
    if (!ops.window_matches(window, identity.host_pid, error))
      return held.finish({false, "E_MANUAL_SIGNAL_WINDOW", error, 0});
    const auto now = ops.tick();
    if (now < started) return held.finish({false, "E_MANUAL_SIGNAL_CLOCK", 0, 0});
    if (now >= identity.expires_tick) return held.finish({false, "E_MANUAL_SIGNAL_EXPIRED", 0, 0});
    const auto set_error = ops.set(held.event); // Exactly one attempt; no retry/reset.
    if (set_error) return held.finish({false, "E_MANUAL_SIGNAL_SET", set_error, 0});
    return held.finish({true, nullptr, 0, 0});
  } catch (const std::bad_alloc&) {
    return held.finish({false, "E_MANUAL_SIGNAL_EXCEPTION", ERROR_NOT_ENOUGH_MEMORY, 0});
  } catch (...) {
    return held.finish({false, "E_MANUAL_SIGNAL_EXCEPTION", ERROR_UNHANDLED_EXCEPTION, 0});
  }
}

inline operation_result signal(const binding& identity) noexcept {
  signal_win32_ops ops;
  return signal_with_ops(identity, ops);
}
} // namespace soui::benchmark::manual_observation
