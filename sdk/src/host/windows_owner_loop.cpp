#include <soui/host/windows_owner_loop.h>

#include "owner_continuation.h"
#include "owner_continuation_policy.h"

#include <atomic>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace soui::sdk::detail {
namespace {
struct root_scope;
struct loop_context {
  const DWORD owner{GetCurrentThreadId()};
  owner_loop_policy::dispatcher dispatcher;
  root_scope* top{}; // Owner-only stack; the policy still has exactly one root.
  bool registration_failed{};
};
thread_local std::weak_ptr<loop_context> current_context;

struct root_scope final {
  std::shared_ptr<loop_context> context;
  root_scope* previous{};
  explicit root_scope(std::shared_ptr<loop_context> value) : context(std::move(value)) {
    if (!context) return;
    // A destroyed dispatcher is an optimizer failure, not a reason to make
    // the application's ordinary GetMessage/dispatch loop unusable.
    if (!context->dispatcher.available()) { context.reset(); return; }
    previous = context->top;
    context->top = this;
    if (!previous) {
      try { context->dispatcher.begin_root(); }
      catch (...) {
        context->top = previous; // Policy rolls back only its own published root.
        throw;
      }
    }
  }
  ~root_scope() {
    if (!context) return;
    if (context->top != this) std::terminate();
    context->top = previous;
    if (!previous) {
      try { context->dispatcher.end_root(); }
      catch (...) { context->registration_failed = true; }
    }
  }
  root_scope(const root_scope&) = delete;
  root_scope& operator=(const root_scope&) = delete;
};
} // namespace

struct owner_continuation_registration::state {
  const DWORD owner{GetCurrentThreadId()};
  std::weak_ptr<loop_context> context;
  std::weak_ptr<void> lifetime;
  std::function<bool()> pump;
  std::function<void()> notify;
  owner_loop_policy::ticket ticket;
  std::atomic<bool> revoked{false};

  void notify_legacy() noexcept {
    auto lease = lifetime.lock();
    if (!lease) return;
    try { notify(); } catch (...) {}
  }
};

owner_continuation_registration::owner_continuation_registration(std::shared_ptr<state> value)
    : state_(std::move(value)) {}

owner_continuation_registration::~owner_continuation_registration() { reset(); }

void owner_continuation_registration::request() noexcept {
  const auto value = state_; // The adapter must retain this object during request.
  if (value->revoked.load(std::memory_order_acquire)) { value->notify_legacy(); return; }
  try { value->ticket.request(); }
  catch (...) { value->notify_legacy(); }
}

void owner_continuation_registration::reset() noexcept {
  const auto value = state_;
  if (GetCurrentThreadId() != value->owner) std::terminate();
  if (value->revoked.exchange(true, std::memory_order_acq_rel)) return;
  if (auto context = value->context.lock()) {
    try { context->dispatcher.detach(value->ticket); }
    catch (...) { context->registration_failed = true; value->notify_legacy(); }
  }
}

bool owner_continuation_registration::service_legacy() {
  const auto value = state_;
  if (GetCurrentThreadId() != value->owner) throw std::logic_error("owner thread required");
  if (value->revoked.load(std::memory_order_acquire)) return false;
  const auto context = value->context.lock();
  if (!context || !value->ticket.attached()) return false;
  value->ticket.legacy_turn();
  // Reentry rejection is handled by the outer service's epoch/wake obligation.
  // Returning false there would let the adapter bypass that ownership guard.
  return true;
}

std::unique_ptr<owner_continuation_registration> attach_current_owner_continuation(
    std::weak_ptr<void> lifetime, std::function<bool()> pump,
    std::function<void()> notify) noexcept {
  auto context = current_context.lock();
  if (!context || context->owner != GetCurrentThreadId()) return nullptr;
  try {
    if (lifetime.expired() || !pump || !notify) {
      context->registration_failed = true;
      return nullptr;
    }
    auto value = std::make_shared<owner_continuation_registration::state>();
    value->context = context;
    value->lifetime = std::move(lifetime);
    value->pump = std::move(pump);
    value->notify = std::move(notify);
    std::weak_ptr<owner_continuation_registration::state> weak = value;
    value->ticket = context->dispatcher.attach(value->lifetime, [weak] {
      const auto live = weak.lock();
      if (!live || live->revoked.load(std::memory_order_acquire)) return false;
      auto lease = live->lifetime.lock();
      return lease ? live->pump() : false;
    }, [weak] { if (const auto live = weak.lock()) live->notify_legacy(); });
    try {
      return std::unique_ptr<owner_continuation_registration>(new owner_continuation_registration(value));
    } catch (...) {
      value->revoked.store(true, std::memory_order_release);
      context->dispatcher.detach(value->ticket);
      throw;
    }
  } catch (...) {
    context->registration_failed = true;
    return nullptr; // The adapter retains its original notification path.
  }
}
} // namespace soui::sdk::detail

namespace soui::sdk {
struct windows_owner_loop::state {
  const DWORD owner{GetCurrentThreadId()};
  std::shared_ptr<detail::loop_context> context;
  host_result initialization{true, {}, {}};
  bool running{}, destroyed{};
};

windows_owner_loop::windows_owner_loop() : state_(std::make_shared<state>()) {
  try {
    state_->context = detail::current_context.lock();
    if (!state_->context) {
      state_->context = std::make_shared<detail::loop_context>();
      detail::current_context = state_->context;
    }
  } catch (const std::exception& error) {
    state_->initialization = {false, "E_OWNER_CONTINUATION_UNAVAILABLE", error.what()};
  } catch (...) {
    state_->initialization = {false, "E_OWNER_CONTINUATION_UNAVAILABLE", "Owner dispatcher creation failed"};
  }
}

windows_owner_loop::~windows_owner_loop() {
  if (GetCurrentThreadId() != state_->owner) std::terminate();
  // run() holds its own state/context until its stack scope unwinds, even if
  // application code deletes the loop in a dispatched callback.
  state_->destroyed = true;
}

host_result windows_owner_loop::status() const {
  const auto value = state_;
  if (GetCurrentThreadId() != value->owner)
    return {false, "E_WRONG_THREAD", "Owner loop requires its creating thread"};
  if (!value->initialization.ok) return value->initialization;
  if (!value->context || value->context->registration_failed || value->context->dispatcher.failed())
    return {false, "E_OWNER_CONTINUATION_UNAVAILABLE", "Owner continuation registration or notification failed; legacy wakeups remain available"};
  return {true, {}, {}};
}

namespace {
bool ordinary_idle_message(const MSG& message) noexcept {
  // SOUI's existing IsIdleMessage classification, without a SOUI dependency.
  return message.message != WM_MOUSEMOVE && message.message != WM_NCMOUSEMOVE &&
      message.message != WM_PAINT && message.message != 0x0118; // WM_SYSTIMER
}
} // namespace

int windows_owner_loop::run(const windows_owner_loop_callbacks& callbacks) {
  const auto value = state_;
  if (GetCurrentThreadId() != value->owner) throw std::logic_error("owner thread required");
  if (value->running) throw std::logic_error("one windows_owner_loop instance cannot reenter run");
  struct running_scope {
    std::shared_ptr<state> value;
    explicit running_scope(std::shared_ptr<state> s) : value(std::move(s)) { value->running = true; }
    ~running_scope() { value->running = false; }
  } running(value);
  // A hook may clear or destroy the caller's callbacks holder while running.
  // Retain independent targets for this complete run, including nested calls.
  const auto hooks = callbacks;
  detail::root_scope root(value->context);
  auto* dispatcher = root.context ? &root.context->dispatcher : nullptr;
  const auto quitting = [&] { return value->destroyed || (hooks.quit_requested && hooks.quit_requested()); };
  const auto prepare_external = [&] {
    if (dispatcher && dispatcher->has_pending()) dispatcher->before_external_callback();
  };
  const auto service_once = [&] {
    return dispatcher && dispatcher->has_pending() && dispatcher->service_turn();
  };
  bool do_idle = true, first_idle = true, idle_yielded = false;
  std::uint32_t idle_count = 0;
  for (;;) {
    if (quitting()) return 0;
    if (dispatcher) dispatcher->resume_root();
    MSG message{};
    // No-work MiniBlink/legacy paths go straight to the ordinary blocking
    // retrieval; Peek is needed only to offer idle or pending SDK work.
    if ((do_idle && hooks.idle) || (dispatcher && dispatcher->has_pending())) {
      const bool waiting = PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE) != FALSE;
      if (quitting()) return 0; // PeekMessage can dispatch sent messages.
      if (!waiting) {
        if (idle_yielded && dispatcher && dispatcher->has_pending()) {
          idle_yielded = false;
          service_once();
          // This fresh empty result grants one SDK attempt, independently
          // of a business idle hook which continually reports more work.
          continue;
        }
        if (do_idle && hooks.idle) {
          if (first_idle) { prepare_external(); first_idle = false; }
          do_idle = hooks.idle(idle_count);
          idle_yielded = true;
          if (idle_count != (std::numeric_limits<std::uint32_t>::max)()) ++idle_count;
          if (quitting()) return 0;
          // A business idle callback may post/paint or run a nested loop.
          // Its preceding empty Peek cannot authorize an SDK service turn.
          // Recheck the unfiltered queue even when business idle returned false.
          continue;
        } else {
          do_idle = false;
          if (service_once()) continue;
        }
      }
    }
    const auto result = GetMessageW(&message, nullptr, 0, 0);
    if (result >= 0 && hooks.message_retrieved) hooks.message_retrieved(message);
    if (result == -1) {
      const auto error = GetLastError();
      if (hooks.get_message_error) {
        prepare_external();
        if (hooks.get_message_error(error)) continue;
      }
      throw std::system_error(static_cast<int>(error), std::system_category(), "GetMessageW");
    }
    if (!result) return static_cast<int>(message.wParam);
    if (quitting()) return 0;
    if (dispatcher && dispatcher->is_private_message(message) && dispatcher->consume_root(message, false)) continue;
    prepare_external();
    if (hooks.dispatch) hooks.dispatch(message);
    else { TranslateMessage(&message); DispatchMessageW(&message); }
    if (quitting()) return 0;
    const bool idle_message = hooks.is_idle_message ? hooks.is_idle_message(message) : ordinary_idle_message(message);
    if (idle_message) { do_idle = true; first_idle = true; idle_count = 0; }
  }
}
} // namespace soui::sdk
