#include <soui/host/webview_host.h>

#include <limits>
#include <array>
#include <algorithm>
#include <utility>
#include <windows.h>

namespace soui::sdk {
namespace detail {
struct devtools_host_state final {
  devtools::configuration config;
  devtools::target_identity issued_target;
  std::chrono::steady_clock::time_point expires{};
  std::uint64_t epoch{}, process_birth{}, next_operation{};
  std::uint32_t process{};
  bool issued{}, open_started{}, inspector_retired{}, retiring{}, closing_target{};
  bool finish_attempted{};
  UINT_PTR expiry_timer{};
  soui_miniblink_host* host{}; // Owner-STA only; removed before Host destruction.
  devtools::revoke_reason reason{devtools::revoke_reason::application};
  devtools::operation_result operation;
  std::array<devtools::operation_result, 32> history;
  std::size_t history_cursor{};
  devtools::phase phase{devtools::phase::idle};
};
} // namespace detail
namespace {
host_result fail(const char* code, const char* message) { return {false, code, message}; }
host_result done() { return {true, {}, {}}; }
template<class Call> host_result native_call(Call&& call) {
  try { return call(); }
  catch (...) { return fail("E_NATIVE_EXCEPTION", "The native renderer operation failed; retain its owners for cleanup."); }
}
struct expiry_slot {
  UINT_PTR timer{};
  std::weak_ptr<detail::devtools_host_state> state;
};
thread_local std::array<expiry_slot, 64> expiry_slots;
void disarm_expiry(detail::devtools_host_state& state) noexcept {
  if (!state.expiry_timer) return;
  KillTimer(nullptr, state.expiry_timer);
  for (auto& slot : expiry_slots) if (slot.timer == state.expiry_timer) { slot = {}; break; }
  state.expiry_timer = 0;
  state.host = nullptr;
}
void CALLBACK expiry_tick(HWND, UINT, UINT_PTR timer, DWORD) noexcept {
  for (auto& slot : expiry_slots) {
    if (slot.timer != timer) continue;
    const auto state = slot.state.lock();
    if (!state || !state->host) return;
    try { state->host->pump(); }
    catch (...) {
      state->issued = false;
      state->phase = devtools::phase::failed;
      state->operation.status = devtools::operation_status::failed;
      // Allocation-free failure path; never let C++ unwind through Win32.
      // Preserve the native wake and the owner. An exception cannot abandon an
      // already exposed inspector by removing its only expiry/cleanup wake.
      state->retiring = true;
      state->reason = devtools::revoke_reason::application;
      try { (void)state->host->revoke_devtools(state->reason); } catch (...) {}
    }
    return;
  }
}
host_result arm_expiry(const std::shared_ptr<detail::devtools_host_state>& state,
    soui_miniblink_host* host, std::chrono::milliseconds validity) {
  if (validity == std::chrono::milliseconds::zero()) { disarm_expiry(*state); return done(); }
  for (auto& slot : expiry_slots) {
    if (slot.timer) continue;
    // This native timer is created only for explicitly expiring authority.
    // Once due it continues non-blocking cleanup; it does not reset deadlines.
    const auto timer = SetTimer(nullptr, 0, static_cast<UINT>((std::max)(10LL,
        (std::min)(static_cast<long long>(validity.count()), 1000LL))), expiry_tick);
    if (!timer) return fail("E_DEVTOOLS_TIMER", "The native grant expiry timer could not be created.");
    // Do not remove a previous active lease's timer until the replacement is
    // known to exist. Failure must leave the old grant and deadline intact.
    disarm_expiry(*state);
    state->expiry_timer = timer;
    state->host = host;
    slot.timer = timer;
    slot.state = state;
    return done();
  }
  return fail("E_CAPACITY", "The owner thread has too many expiring native diagnostic grants.");
}
bool pending(const host_result& result) {
  return result.code == "E_BUSY" || result.code == "E_PENDING" ||
      result.code == "E_CURSOR_CLOSE_PENDING" || result.code == "E_DEVTOOLS_PENDING" ||
      result.code == "E_RUNTIME_EXIT_PENDING";
}
bool same(const devtools::target_identity& a, const devtools::target_identity& b) {
  return a.ready && b.ready && a.generation != 0 && a.generation == b.generation &&
      !a.document.empty() && a.document == b.document;
}
bool expired(const detail::devtools_host_state& state) {
  return state.expires != std::chrono::steady_clock::time_point{} &&
      std::chrono::steady_clock::now() >= state.expires;
}
std::uint64_t process_birth() {
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0;
  return (std::uint64_t{created.dwHighDateTime} << 32) | created.dwLowDateTime;
}
devtools::operation_result rejected(const host_result& result) {
  return {0, devtools::operation_status::denied, result.code, result.message};
}
bool new_operation(detail::devtools_host_state& state) {
  if (state.next_operation == (std::numeric_limits<std::uint64_t>::max)()) return false;
  if (state.operation.id) {
    auto previous = state.operation;
    if (state.phase == devtools::phase::opening && previous.status == devtools::operation_status::pending) {
      previous.status = devtools::operation_status::failed;
      previous.code = "E_CANCELLED";
      previous.message = "The open operation was retired before completing.";
    }
    state.history[state.history_cursor] = std::move(previous);
    state.history_cursor = (state.history_cursor + 1) % state.history.size();
  }
  state.operation = {++state.next_operation, devtools::operation_status::pending, {}, {}};
  return true;
}
void publish(detail::devtools_host_state& state, const host_result& result,
             devtools::operation_status success) {
  state.operation.status = result.ok ? success : pending(result)
      ? devtools::operation_status::pending : devtools::operation_status::failed;
  state.operation.code = result.code.substr(0, 128);
  state.operation.message = result.message.substr(0, 1024);
  if (!result.ok && !pending(result)) state.phase = devtools::phase::failed;
}
} // namespace

host_result soui_miniblink_host::require_devtools_owner() const {
  return std::this_thread::get_id() == devtools_owner_ ? done()
      : fail("E_THREAD", "DevTools requires the constructing UI owner thread.");
}

host_result soui_miniblink_host::configure_devtools(const devtools::configuration& config) {
  const auto owner = require_devtools_owner();
  if (!owner.ok) return owner;
  if (lifecycle_operation_ || adapter_cleanup_pending_ || session_)
    return fail("E_BUSY", "Configure DevTools before the first Host start and renderer preparation.");
  if (!adapter_) return fail("E_HOST_ADAPTER_MISSING", "A renderer adapter is required.");
  if (config.access != devtools::policy::disabled && config.access != devtools::policy::application_controlled)
    return fail("E_DEVTOOLS_POLICY", "Unknown native DevTools policy.");
  if (config.ending != devtools::termination::none && config.ending != devtools::termination::detach &&
      config.ending != devtools::termination::destroy_target)
    return fail("E_DEVTOOLS_POLICY", "Unknown native termination policy.");
  if (config.access == devtools::policy::application_controlled && config.ending == devtools::termination::none)
    return fail("E_DEVTOOLS_POLICY", "Native policy must explicitly accept a supported termination strategy.");
  // Allocate before a backend callout; a failed allocation cannot leave policy
  // enabled in the renderer without a corresponding native owner.
  auto state = std::make_shared<detail::devtools_host_state>();
  state->config = config;
  state->process = GetCurrentProcessId();
  state->process_birth = process_birth();
  if (!state->process_birth) return fail("E_PROCESS_IDENTITY", "The native process instance could not be verified.");
  lifecycle_operation_ = true;
  struct reset { bool& value; ~reset() { value = false; } } guard{lifecycle_operation_};
  const auto configured = native_call([&] { return adapter_->configure_devtools_native(config); });
  if (!configured.ok) return configured;
  disarm_devtools_expiry();
  devtools_ = std::move(state);
  return done();
}

devtools::capabilities soui_miniblink_host::devtools_capabilities() const {
  if (!require_devtools_owner().ok || !adapter_ || lifecycle_operation_) return {};
  try { return adapter_->devtools_capabilities_native(); } catch (...) { return {}; }
}

host_result soui_miniblink_host::authorize_devtools(devtools::grant& output,
    std::chrono::milliseconds validity) {
  const auto owner = require_devtools_owner();
  if (!owner.ok) return owner;
  const auto active = require_running();
  if (!active.ok) return active;
  if (!devtools_ || devtools_->config.access != devtools::policy::application_controlled)
    return fail("E_DEVTOOLS_DISABLED", "Native application policy has not enabled controlled DevTools.");
  if (devtools_->retiring || devtools_->phase == devtools::phase::failed)
    return fail("E_CLEANUP_PENDING", "Complete the previous diagnostic operation before issuing another grant.");
  if (validity < std::chrono::milliseconds::zero() || validity > std::chrono::hours(24))
    return fail("E_INVALID_ARGUMENT", "Grant validity must be between zero and 24 hours; zero binds the current document lifetime.");
  if (devtools_->epoch == (std::numeric_limits<std::uint64_t>::max)())
    return fail("E_GRANT_EXHAUSTED", "Native grant identities are exhausted.");
  lifecycle_operation_ = true;
  struct reset { bool& value; ~reset() { value = false; } } guard{lifecycle_operation_};
  devtools::target_identity target;
  const auto prepared = native_call([&] { return adapter_->prepare_devtools_grant(target); });
  if (!prepared.ok) return prepared;
  if (!target.ready || !target.generation || target.document.empty() || target.document.size() > 512)
    return fail("E_NOT_READY", "The renderer has no current trusted document receipt.");
  devtools::grant next;
  next.owner_ = devtools_;
  next.epoch_ = devtools_->epoch + 1;
  next.generation_ = target.generation;
  next.document_ = target.document;
  next.process_ = devtools_->process;
  next.process_birth_ = devtools_->process_birth;
  if (validity != std::chrono::milliseconds::zero()) next.expires_ = std::chrono::steady_clock::now() + validity;
  const auto timer = arm_expiry(devtools_, this, validity);
  if (!timer.ok) return timer;
  devtools_->epoch = next.epoch_;
  devtools_->issued_target = std::move(target);
  devtools_->expires = next.expires_;
  devtools_->issued = true;
  if (devtools_->phase == devtools::phase::drained) {
    // A supported detach can reopen the same live business Host only after
    // the backend has supplied a fresh ready target. Operation IDs never reset.
    devtools_->open_started = devtools_->inspector_retired = devtools_->finish_attempted = false;
    devtools_->phase = devtools::phase::idle;
  }
  output = std::move(next);
  return done();
}

devtools::operation_result soui_miniblink_host::open_devtools(const devtools::grant& grant) {
  const auto owner = require_devtools_owner();
  if (!owner.ok) return rejected(owner);
  const auto active = require_running();
  if (!active.ok) return rejected(active);
  if (!devtools_ || devtools_->config.access != devtools::policy::application_controlled)
    return rejected(fail("E_DEVTOOLS_DISABLED", "Native DevTools is disabled."));
  const auto state = devtools_;
  devtools::target_identity current_target;
  try { current_target = adapter_->devtools_target_native(); }
  catch (...) { return rejected(fail("E_NATIVE_EXCEPTION", "The current native document could not be verified.")); }
  if (!state->issued || state->retiring || state->phase == devtools::phase::failed ||
      grant.owner_.lock() != state || grant.epoch_ != state->epoch ||
      grant.process_ != GetCurrentProcessId() || grant.process_birth_ != process_birth() ||
      grant.generation_ != state->issued_target.generation || grant.document_ != state->issued_target.document ||
      expired(*state) || !same(state->issued_target, current_target))
    return rejected(fail("E_UNAUTHORIZED", "The native grant is expired, revoked or belongs to another target document."));
  // A repeat is the same native operation; it cannot create unbounded windows.
  if (state->open_started) return state->operation;
  if (!new_operation(*state)) return rejected(fail("E_OPERATION_EXHAUSTED", "Native operation identities are exhausted."));
  state->open_started = true; // Even a failed native call may have exposed the target.
  state->phase = devtools::phase::opening;
  lifecycle_operation_ = true;
  struct reset { bool& value; ~reset() { value = false; } } guard{lifecycle_operation_};
  host_result opened;
  try { opened = adapter_->open_devtools_native(); }
  catch (...) { opened = fail("E_NATIVE_EXCEPTION", "The renderer could not finish the native open request; retain it for cleanup."); }
  publish(*state, opened, devtools::operation_status::accepted);
  return state->operation;
}

void soui_miniblink_host::invalidate_devtools_grants() noexcept {
  if (!devtools_) return;
  devtools_->issued = false;
  if (devtools_->epoch != (std::numeric_limits<std::uint64_t>::max)()) ++devtools_->epoch;
}

void soui_miniblink_host::disarm_devtools_expiry() noexcept {
  if (devtools_) disarm_expiry(*devtools_);
}

host_result soui_miniblink_host::reset_devtools_for_mount() {
  if (!devtools_) return done();
  if (devtools_->phase == devtools::phase::failed)
    return fail("E_DEVTOOLS_RETIREMENT_FAILED", "The previous target failed retirement; it cannot admit a new document.");
  invalidate_devtools_grants();
  disarm_devtools_expiry();
  devtools_->issued_target = {};
  devtools_->open_started = devtools_->inspector_retired = devtools_->retiring = devtools_->closing_target = false;
  devtools_->phase = devtools::phase::idle;
  devtools_->finish_attempted = false;
  return done();
}

host_result soui_miniblink_host::retire_devtools_before_unmount() {
  if (!devtools_) return done();
  const auto owner = require_devtools_owner();
  if (!owner.ok) return owner;
  invalidate_devtools_grants();
  if (devtools_->config.access != devtools::policy::application_controlled) return done();
  if (!devtools_->retiring) {
    if (!new_operation(*devtools_)) return fail("E_OPERATION_EXHAUSTED", "Native operation identities are exhausted.");
    devtools_->retiring = true;
    devtools_->reason = devtools::revoke_reason::target_close;
  }
  devtools_->closing_target = true;
  devtools_->phase = devtools::phase::revoking;
  if (devtools_->inspector_retired) return done();
  const auto retired = native_call([&] { return adapter_->revoke_devtools_native(devtools_->reason); });
  if (retired.ok) devtools_->inspector_retired = true;
  publish(*devtools_, retired, devtools::operation_status::pending);
  return retired;
}

host_result soui_miniblink_host::finish_devtools_after_unmount() {
  if (!devtools_ || devtools_->config.access != devtools::policy::application_controlled) return done();
  devtools_->finish_attempted = true;
  const auto retired = native_call([&] { return adapter_->finish_devtools_retirement(); });
  publish(*devtools_, retired, devtools::operation_status::completed);
  if (retired.ok) {
    devtools_->phase = devtools::phase::drained;
    devtools_->retiring = false;
    devtools_->closing_target = false;
    disarm_devtools_expiry();
  } else if (!pending(retired) && devtools_resources_retired()) {
    // Physical owners are gone, but the failure remains sticky and cannot be
    // reported as drained or reused for another sensitive document.
    devtools_->retiring = devtools_->closing_target = false;
    disarm_devtools_expiry();
  }
  return retired;
}

bool soui_miniblink_host::devtools_resources_retired() const {
  if (!devtools_ || !devtools_->finish_attempted || !require_devtools_owner().ok) return false;
  try { return !adapter_->observe_devtools_native().resources_retained; }
  catch (...) { return false; }
}

bool soui_miniblink_host::devtools_preparation_owned() const {
  if (!devtools_ || devtools_->config.access != devtools::policy::application_controlled || !adapter_) return false;
  if (!require_devtools_owner().ok) return true; // Wrong-thread destruction must not silently drop owners.
  try { return adapter_->observe_devtools_native().resources_retained; }
  catch (...) { return true; }
}

devtools::operation_result soui_miniblink_host::revoke_devtools(devtools::revoke_reason reason) {
  const auto owner = require_devtools_owner();
  if (!owner.ok) return rejected(owner);
  if (lifecycle_operation_) return rejected(fail("E_BUSY", "A native lifecycle call is active."));
  if (!devtools_ || devtools_->config.access == devtools::policy::disabled)
    return rejected(fail("E_DEVTOOLS_DISABLED", "Native DevTools is disabled."));
  auto& state = *devtools_;
  if (state.phase == devtools::phase::drained) return state.operation;
  if (!state.retiring) {
    if (!new_operation(state)) return rejected(fail("E_OPERATION_EXHAUSTED", "Native operation identities are exhausted."));
    state.retiring = true;
    state.reason = reason;
  }
  invalidate_devtools_grants();
  state.phase = devtools::phase::revoking;
  // A Host close owns target retirement even if no inspector was opened.
  // Only a grant-only revocation can complete without the close barriers.
  if (!state.open_started && !state.closing_target) {
    state.retiring = false;
    state.phase = devtools::phase::idle;
    publish(state, done(), devtools::operation_status::completed);
    disarm_devtools_expiry();
    return state.operation;
  }
  if (state.closing_target || state.config.ending == devtools::termination::destroy_target) {
    const auto closed = close();
    publish(state, closed, devtools::operation_status::completed);
    return state.operation;
  }
  lifecycle_operation_ = true;
  struct reset { bool& value; ~reset() { value = false; } } guard{lifecycle_operation_};
  const auto retired = native_call([&] { return adapter_->revoke_devtools_native(reason); });
  publish(state, retired, devtools::operation_status::completed);
  if (retired.ok) {
    state.phase = devtools::phase::drained;
    state.inspector_retired = true;
    state.retiring = false;
    disarm_devtools_expiry();
  }
  return state.operation;
}

devtools::observation soui_miniblink_host::observe_devtools() const {
  devtools::observation result;
  if (!require_devtools_owner().ok) { result.code = "E_THREAD"; return result; }
  if (!devtools_ || !adapter_) return result;
  // Avoid backend COM or callback queries from a reentrant lifecycle callout.
  if (!lifecycle_operation_) {
    try { result = adapter_->observe_devtools_native(); }
    catch (...) { result.state = devtools::phase::failed; result.resources_retained = true; result.code = "E_NATIVE_EXCEPTION"; }
  }
  result.operation_id = devtools_->operation.id;
  if (result.state != devtools::phase::failed) result.state = devtools_->phase;
  if (result.state == devtools::phase::opening && result.attached == devtools::truth::yes)
    result.state = devtools::phase::attached;
  result.retirement_required = result.retirement_required ||
      (devtools_->open_started && devtools_->phase != devtools::phase::drained);
  if (!devtools_->finish_attempted || lifecycle_operation_)
    result.resources_retained = result.resources_retained || result.retirement_required;
  if (lifecycle_operation_ && (adapter_cleanup_pending_ || devtools_->retiring || devtools_->closing_target))
    result.resources_retained = true;
  if (!lifecycle_operation_ && devtools_->issued && !devtools_->retiring &&
      devtools_->phase != devtools::phase::failed && !expired(*devtools_))
    try { result.grant_valid = same(devtools_->issued_target, adapter_->devtools_target_native()); }
    catch (...) { result.grant_valid = false; result.code = "E_NATIVE_EXCEPTION"; }
  else result.grant_valid = false;
  if (result.code.empty()) result.code = devtools_->operation.code;
  return result;
}

devtools::operation_result soui_miniblink_host::devtools_operation(std::uint64_t id) const {
  const auto owner = require_devtools_owner();
  if (!owner.ok) return rejected(owner);
  if (devtools_ && id) {
    if (devtools_->operation.id == id) return devtools_->operation;
    for (const auto& operation : devtools_->history) if (operation.id == id) return operation;
  }
  return {id, devtools::operation_status::failed, "E_OPERATION_NOT_FOUND", "The operation is unknown or no longer retained."};
}

bool soui_miniblink_host::devtools_shortcut(unsigned key, bool control, bool shift,
    bool alt, bool repeated) const noexcept {
  if (std::this_thread::get_id() != devtools_owner_ || !devtools_ || lifecycle_operation_ ||
      !running() || devtools_->retiring || alt || repeated ||
      devtools_->config.access != devtools::policy::application_controlled) return false;
  return (devtools_->config.f12 && key == VK_F12 && !control && !shift) ||
      (devtools_->config.ctrl_shift_i && key == 'I' && control && shift);
}

void soui_miniblink_host::pump_devtools() {
  if (!devtools_ || lifecycle_operation_ || !require_devtools_owner().ok) return;
  if (devtools_->retiring) { (void)revoke_devtools(devtools_->reason); return; }
  if (!devtools_->issued) return;
  if (!devtools_->open_started) {
    if (expired(*devtools_)) { invalidate_devtools_grants(); disarm_devtools_expiry(); }
    return;
  }
  const auto observed = observe_devtools();
  if (observed.state == devtools::phase::failed) {
    devtools_->operation.status = devtools::operation_status::failed;
    devtools_->operation.code = observed.code.empty() ? "E_DEVTOOLS_FAILED" : observed.code;
    devtools_->operation.native_error = observed.native_error;
    (void)revoke_devtools(devtools::revoke_reason::application);
    return;
  }
  if (observed.attached == devtools::truth::yes && devtools_->phase == devtools::phase::opening) {
    devtools_->phase = devtools::phase::attached;
    devtools_->operation.status = devtools::operation_status::accepted;
    devtools_->operation.code.clear();
  }
  if (expired(*devtools_)) { (void)revoke_devtools(devtools::revoke_reason::grant_expired); return; }
  devtools::target_identity target;
  try { target = adapter_->devtools_target_native(); } catch (...) {}
  if (!same(devtools_->issued_target, target)) {
    (void)revoke_devtools(devtools::revoke_reason::document_change);
    return;
  }
  if (devtools_->phase == devtools::phase::opening &&
      devtools_->operation.status == devtools::operation_status::pending) {
    lifecycle_operation_ = true;
    struct reset { bool& value; ~reset() { value = false; } } guard{lifecycle_operation_};
    const auto advanced = native_call([&] { return adapter_->open_devtools_native(); });
    publish(*devtools_, advanced, devtools::operation_status::accepted);
  }
}

} // namespace soui::sdk
