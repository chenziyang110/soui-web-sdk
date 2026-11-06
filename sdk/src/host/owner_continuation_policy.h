#pragma once
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>

namespace soui::sdk::detail::owner_loop_policy {
constexpr UINT poke_message = WM_APP + 0x351;
struct hooks {
  decltype(&::PostMessageW) post = &::PostMessageW;
  decltype(&::SetTimer) timer = &::SetTimer;
  decltype(&::KillTimer) kill = &::KillTimer;
};
namespace detail {
// One process/module-wide, never-reset namespace for registration, poke and
// timer IDs. A dequeued MSG cannot be purged: IDs must not be recycled.
inline std::mutex ids_mutex;
inline UINT_PTR next_id = 1;
inline UINT_PTR id() {
  std::lock_guard<std::mutex> lock(ids_mutex);
  if (next_id == (std::numeric_limits<UINT_PTR>::max)()) return 0;
  return next_id++;
}
struct entry {
  UINT_PTR incarnation{}, queued{};
  std::uint64_t epoch{};
  // poisoned is permanent fallback: epoch exhaustion, resource or pump failure.
  // Modal/reentry/root-exit legacy is temporary and may resume on SDK reentry.
  bool live{true}, pending{}, legacy{}, serving{}, poisoned{};
  std::weak_ptr<void> lifetime;
  std::function<bool()> pump;
  std::function<void()> notify;
};
struct state {
  std::mutex mutex;
  // Written only while holding mutex. The root may avoid the registry lock
  // entirely when an ordinary application message has no SDK work behind it.
  std::atomic<bool> pending_hint{false};
  std::atomic<bool> resume_hint{false};
  std::array<std::shared_ptr<entry>, 8> entries{};
  DWORD thread{GetCurrentThreadId()};
  HWND window{};
  UINT_PTR timer{};
  UINT_PTR retired_timer{};
  HWND retired_window{};
  unsigned cursor{};
  bool root{}, closing{}, failed{}, timer_cancel_failed{}, retired_cleanup_owed{};
  hooks api;
};
using list = std::array<std::shared_ptr<entry>, 8>;
inline void update_hint_locked(state& s) noexcept {
  bool pending = false, resume = false;
  for (auto& e : s.entries) if (e && e->live) {
    if (e->lifetime.expired()) { e->live = false; e->pending = false; e->queued = 0; }
    else {
      pending = pending || (e->pending && !e->legacy);
      resume = resume || (e->pending && e->legacy && !e->serving && !e->poisoned && !s.timer_cancel_failed);
    }
  }
  s.pending_hint.store(s.root && !s.closing && pending, std::memory_order_release);
  s.resume_hint.store(s.root && !s.closing && resume, std::memory_order_release);
}
inline void owner(const std::shared_ptr<state>& s) {
  if (GetCurrentThreadId() != s->thread) throw std::logic_error("owner thread required");
}
inline void notify(const std::shared_ptr<state>& s, const std::shared_ptr<entry>& e) noexcept {
  auto lease = e->lifetime.lock(); if (!lease) return;
  try { e->notify(); } catch (...) { std::lock_guard<std::mutex> lock(s->mutex); s->failed = true; }
}
inline list fallback_locked(state& s, bool permanent = false) {
  list result{}; unsigned n = 0;
  for (auto& e : s.entries) if (e && e->live && e->pending) {
    e->legacy = true;
    if (permanent) e->poisoned = true;
    result[n++] = e;
  }
  update_hint_locked(s);
  return result;
}
inline void notify_all(const std::shared_ptr<state>& s, const list& a) noexcept {
  for (const auto& e : a) if (e) notify(s, e);
}
inline bool poke_locked(state& s, entry& e) {
  if (e.queued) return true;
  const auto token = id();
  if (!token || !s.api.post(s.window, poke_message, token, static_cast<LPARAM>(e.incarnation))) {
    e.poisoned = e.legacy = true; s.failed = true; return false;
  }
  e.queued = token; return true;
}
inline void retire_timer_locked(state& s) {
  if (!s.timer) return;
  const auto old = s.timer; s.timer = 0; // Old MSGs lose authority before the API call.
  if (!s.api.kill(s.window, old)) {
    s.failed = s.timer_cancel_failed = s.retired_cleanup_owed = true;
    s.retired_timer = old; s.retired_window = s.window; // Sticky failure forbids another arm.
  }
}
inline void refresh_timer(const std::shared_ptr<state>& s) {
  owner(s); list fallback{};
  {
    std::lock_guard<std::mutex> lock(s->mutex);
    update_hint_locked(*s);
    bool pending = false;
    for (auto& e : s->entries) pending = pending || (e && e->live && e->pending && !e->legacy);
    if (!s->root || s->closing || !pending) {
      const bool previously_failed = s->timer_cancel_failed;
      retire_timer_locked(*s);
      if (!previously_failed && s->timer_cancel_failed) fallback = fallback_locked(*s, true);
    } else if (s->timer_cancel_failed) {
      unsigned n = 0;
      for (auto& e : s->entries) if (e && e->live && e->pending && !e->legacy) { e->poisoned = e->legacy = true; fallback[n++] = e; }
    } else if (!s->timer) {
      const auto token = id();
      const auto actual = token ? s->api.timer(s->window, token, 10, nullptr) : 0;
      if (!actual) {
        s->failed = true; fallback = fallback_locked(*s, true);
      } else s->timer = token;
    }
    update_hint_locked(*s);
  }
  notify_all(s, fallback);
}
inline bool service(const std::shared_ptr<state>& s, const std::shared_ptr<entry>& e, bool legacy) {
  owner(s); auto lease = e->lifetime.lock();
  if (!lease) { refresh_timer(s); return false; }
  std::uint64_t epoch{}; bool rejected = false;
  {
    std::lock_guard<std::mutex> lock(s->mutex);
    if (s->closing || !e->live || (!legacy && (!s->root || e->legacy || !e->pending))) return false;
    if (e->serving) { e->legacy = true; rejected = true; }
    else { e->serving = true; epoch = e->epoch; }
    update_hint_locked(*s);
  }
  // The active outer call owns the next wake. Reposting from a rejected
  // nested legacy turn would itself make an infinite posted-message chain.
  if (rejected) return false;
  refresh_timer(s); // Also protects a modal entered inside the first COM/pump callback.
  {
    std::lock_guard<std::mutex> lock(s->mutex);
    if (!e->live || s->closing) { e->serving = false; return false; }
  }
  bool more = true; std::exception_ptr failure;
  try { more = e->pump(); } catch (...) { failure = std::current_exception(); }
  bool wake = false;
  {
    std::lock_guard<std::mutex> lock(s->mutex);
    e->serving = false;
    if (e->live && !s->closing) {
      e->pending = more || e->epoch != epoch;
      if (failure) { e->pending = true; e->poisoned = e->legacy = true; s->failed = true; }
      if (!e->pending && s->root && !e->poisoned && !s->timer_cancel_failed) e->legacy = false;
      wake = (e->legacy || !s->root) && e->pending;
      // Never acknowledge queued here: a worker can still rely on that MSG.
    }
    update_hint_locked(*s);
  }
  if (wake) notify(s, e);
  refresh_timer(s);
  if (failure) std::rethrow_exception(failure);
  return true;
}
inline void shutdown(const std::shared_ptr<state>& s, bool destroy) noexcept {
  list fallback{}; HWND window{}; UINT_PTR timer{}, retired{};
  {
    std::lock_guard<std::mutex> lock(s->mutex);
    if (s->closing) return;
    s->closing = true; s->root = false; window = s->window; timer = s->timer; retired = s->retired_timer; s->timer = 0;
    unsigned n = 0;
    for (auto& e : s->entries) if (e) { e->live = false; e->queued = 0; if (e->pending) fallback[n++] = e; }
    s->window = nullptr;
    update_hint_locked(*s);
  }
  const bool active_stopped = !timer || s->api.kill(window, timer) != FALSE;
  const bool retired_stopped = !retired || retired == timer || s->api.kill(window, retired) != FALSE;
  {
    std::lock_guard<std::mutex> lock(s->mutex);
    if (!active_stopped || !retired_stopped) {
      s->failed = s->timer_cancel_failed = s->retired_cleanup_owed = true;
      if (!active_stopped) s->retired_timer = timer;
      s->retired_window = window;
    } else s->retired_cleanup_owed = false;
  }
  if (destroy && window) {
    const auto destroyed = DestroyWindow(window);
    std::lock_guard<std::mutex> lock(s->mutex);
    if (destroyed) s->retired_cleanup_owed = false; // Actual window destruction cleans its timers.
    else s->failed = true;
  }
  notify_all(s, fallback);
}
inline LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
  auto cookie = reinterpret_cast<std::shared_ptr<state>*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    cookie = new (std::nothrow) std::shared_ptr<state>(*static_cast<std::shared_ptr<state>*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));
    if (!cookie) return FALSE;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cookie));
    std::lock_guard<std::mutex> lock((*cookie)->mutex); (*cookie)->window = hwnd; return TRUE;
  }
  if (!cookie) return DefWindowProcW(hwnd, message, w, l);
  auto s = *cookie; // Keeps the dispatcher state alive across all callbacks.
  if (message == WM_NCDESTROY) {
    { std::lock_guard<std::mutex> lock(s->mutex); if (!s->closing) s->failed = true; }
    shutdown(s, false); SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0); delete cookie; return 0;
  }
  if (message == poke_message || message == WM_TIMER) {
    list fallback{};
    {
      std::lock_guard<std::mutex> lock(s->mutex);
      if (s->closing || hwnd != s->window) return 0;
      if (message == WM_TIMER) {
        if (!s->timer || w != s->timer) return 0;
        fallback = fallback_locked(*s);
      } else for (auto& e : s->entries) if (e && e->live && e->incarnation == static_cast<UINT_PTR>(l) && e->queued != 0 && e->queued == w) {
        e->queued = 0; if (e->pending) { e->legacy = true; fallback[0] = e; } break;
      }
      update_hint_locked(*s);
    }
    notify_all(s, fallback); refresh_timer(s); return 0;
  }
  return DefWindowProcW(hwnd, message, w, l);
}
} // namespace detail

class ticket {
  std::weak_ptr<detail::state> state_;
  std::shared_ptr<detail::entry> entry_;
  friend class dispatcher;
public:
  bool request() const {
    auto e = entry_; if (!e) return false;
    auto lease = e->lifetime.lock(); if (!lease) return false;
    auto s = state_.lock();
    if (!s) { try { e->notify(); } catch (...) {} return false; }
    bool fallback = false;
    {
      std::lock_guard<std::mutex> lock(s->mutex);
      if (!e->live || s->closing) fallback = true; // Only original notify; never an old dispatcher token.
      else {
        const bool first = !e->pending;
        if (e->epoch == (std::numeric_limits<std::uint64_t>::max)()) { e->poisoned = e->legacy = true; s->failed = true; }
        else ++e->epoch;
        if (s->timer_cancel_failed) e->poisoned = e->legacy = true;
        e->pending = true; fallback = !s->root || e->legacy;
        if (!fallback && first) fallback = !detail::poke_locked(*s, *e);
      }
      detail::update_hint_locked(*s);
    }
    if (fallback) detail::notify(s, e); return !fallback;
  }
  bool legacy_turn() const { auto s = state_.lock(); auto e = entry_; return s && e ? detail::service(s, e, true) : false; }
  bool pending() const { auto s = state_.lock(); if (!s || !entry_) return false; std::lock_guard<std::mutex> lock(s->mutex); return entry_->pending; }
  bool attached() const { auto s = state_.lock(); if (!s || !entry_) return false; std::lock_guard<std::mutex> lock(s->mutex); return entry_->live && !s->closing; }
};

class dispatcher {
  std::shared_ptr<detail::state> state_ = std::make_shared<detail::state>();
public:
  explicit dispatcher(hooks api = {}) {
    auto s = state_; s->api = api;
    WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = detail::window_proc;
    wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"SouiSdkOwnerContinuation";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("dispatcher class");
    WNDCLASSEXW actual{sizeof(actual)};
    if (!GetClassInfoExW(wc.hInstance, wc.lpszClassName, &actual) || actual.lpfnWndProc != wc.lpfnWndProc) throw std::runtime_error("dispatcher class collision");
    if (!CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, &s)) throw std::runtime_error("dispatcher window");
  }
  dispatcher(const dispatcher&) = delete;
  dispatcher& operator=(const dispatcher&) = delete;
  ~dispatcher() { if (GetCurrentThreadId() != state_->thread) std::terminate(); detail::shutdown(state_, true); }
  ticket attach(std::weak_ptr<void> lifetime, std::function<bool()> pump, std::function<void()> legacy) {
    auto s = state_; detail::owner(s); auto e = std::make_shared<detail::entry>();
    e->incarnation = detail::id(); e->lifetime = std::move(lifetime); e->pump = std::move(pump); e->notify = std::move(legacy);
    std::shared_ptr<detail::entry> retired; ticket result;
    {
      std::lock_guard<std::mutex> lock(s->mutex);
      if (!e->incarnation || s->closing) throw std::runtime_error("registration unavailable: retain original legacy path");
      for (auto& slot : s->entries) if (!slot || !slot->live) { retired = std::move(slot); slot = e; result.state_ = s; result.entry_ = e; break; }
    } // Destroy retired callbacks outside the mutex, including captured owners.
    if (!result.entry_) throw std::runtime_error("registration capacity: retain original legacy path");
    return result;
  }
  void detach(ticket& t) {
    auto s = state_; detail::owner(s); auto e = t.entry_; if (!e || t.state_.lock() != s) return;
    bool wake{}; { std::lock_guard<std::mutex> lock(s->mutex); wake = e->pending; e->live = false; e->queued = 0; detail::update_hint_locked(*s); }
    if (wake) detail::notify(s, e); detail::refresh_timer(s);
  }
  void begin_root() {
    auto s = state_; detail::owner(s);
    {
      std::lock_guard<std::mutex> lock(s->mutex);
      if (s->root || s->closing || !s->window) throw std::logic_error("one available root only");
      s->root = true;
      detail::update_hint_locked(*s);
    }
    try { detail::refresh_timer(s); }
    catch (...) {
      // Only this invocation's successfully published root is rolled back.
      // A rejected duplicate begin never reaches this catch or ends a root.
      detail::list fallback{};
      {
        std::lock_guard<std::mutex> lock(s->mutex);
        s->root = false; s->failed = true;
        fallback = detail::fallback_locked(*s, true);
      }
      detail::notify_all(s, fallback);
      try { detail::refresh_timer(s); } catch (...) {}
      throw;
    }
  }
  void end_root() {
    auto s = state_; detail::owner(s); detail::list a;
    { std::lock_guard<std::mutex> lock(s->mutex); if (!s->root) throw std::logic_error("no matching root"); s->root = false; a = detail::fallback_locked(*s); }
    detail::notify_all(s, a); detail::refresh_timer(s);
  }
  // Called only at an actual SDK loop iteration boundary, never by WndProc
  // or while a TLS flag spans foreign dispatch. Permanent failures stay legacy.
  void resume_root() {
    if (!state_->resume_hint.load(std::memory_order_acquire)) return;
    auto s = state_; detail::owner(s); bool restored = false;
    {
      std::lock_guard<std::mutex> lock(s->mutex);
      if (s->root && !s->closing && !s->timer_cancel_failed) {
        for (auto& e : s->entries) if (e && e->live && e->pending && e->legacy && !e->serving && !e->poisoned && !e->lifetime.expired()) {
          e->legacy = false; restored = true;
        }
      }
      detail::update_hint_locked(*s);
    }
    if (restored) detail::refresh_timer(s);
  }
  bool service_turn() {
    auto s = state_; detail::owner(s); std::shared_ptr<detail::entry> selected;
    { std::lock_guard<std::mutex> lock(s->mutex); detail::update_hint_locked(*s); if (!s->root || s->closing) return false;
      for (unsigned n = 0; n < s->entries.size(); ++n) { const auto i = s->cursor++ % s->entries.size(); auto e = s->entries[i];
        if (e && e->live && e->pending && !e->legacy && !e->lifetime.expired()) { selected = e; break; } } }
    return selected ? detail::service(s, selected, false) : false;
  }
  void before_external_callback() {
    auto s = state_; detail::owner(s); detail::list fallback{};
    { std::lock_guard<std::mutex> lock(s->mutex); unsigned n = 0;
      for (auto& e : s->entries) if (e && e->live && e->pending && !e->legacy && !detail::poke_locked(*s, *e)) fallback[n++] = e; }
    detail::notify_all(s, fallback); detail::refresh_timer(s);
  }
  bool consume_root(const MSG& message, bool service_now = true) {
    auto s = state_; detail::owner(s);
    { std::lock_guard<std::mutex> lock(s->mutex);
      if (!s->window || message.hwnd != s->window || (message.message != poke_message && message.message != WM_TIMER)) return false;
      if (message.message == WM_TIMER) { if (!s->timer || message.wParam != s->timer) return true; }
      else { bool found = false; for (auto& e : s->entries) if (e && e->live && e->incarnation == static_cast<UINT_PTR>(message.lParam) && e->queued != 0 && e->queued == message.wParam) { e->queued = 0; found = true; break; } if (!found) return true; }
    }
    // The SDK idle-first loop acknowledges ownership only. The default keeps
    // the original single-root policy entry point for its direct callers.
    if (service_now) service_turn();
    detail::refresh_timer(s); return true;
  }
  bool has_pending() const noexcept { return state_->pending_hint.load(std::memory_order_acquire); }
  bool available() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return !state_->closing && state_->window != nullptr;
  }
  // Owner-only, and intentionally lock-free: only that thread writes HWND.
  bool is_private_message(const MSG& message) const noexcept {
    return state_->window && message.hwnd == state_->window &&
        (message.message == poke_message || message.message == WM_TIMER);
  }
  HWND window() const { std::lock_guard<std::mutex> lock(state_->mutex); return state_->window; }
  UINT_PTR timer_id() const { std::lock_guard<std::mutex> lock(state_->mutex); return state_->timer; }
  UINT_PTR retired_timer_id() const { std::lock_guard<std::mutex> lock(state_->mutex); return state_->retired_timer; }
  bool timer_cleanup_owed() const { std::lock_guard<std::mutex> lock(state_->mutex); return state_->retired_cleanup_owed; }
  bool timer_cancel_failed() const { std::lock_guard<std::mutex> lock(state_->mutex); return state_->timer_cancel_failed; }
  void close() { auto s = state_; detail::owner(s); detail::shutdown(s, true); }
  bool failed() const { std::lock_guard<std::mutex> lock(state_->mutex); return state_->failed; }
};
} // namespace soui::sdk::detail::owner_loop_policy
