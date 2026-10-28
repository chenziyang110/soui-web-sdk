#include <soui/host/soui_miniblink_host.h>
#include "../../sdk/src/host/engine_admission.h"
#include "../../sdk/src/host/input_mailbox.h"
#include <windows.h>
#include "../../third_party/include/miniblink/wke.h"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {
using namespace std::chrono_literals;
using soui::sdk::dynamic_miniblink_adapter;
using soui::sdk::engine_thread;
using soui::sdk::detail::engine_admission;
using soui::sdk::detail::engine_lane;

void require(bool value, const char* reason) {
  if (!value) throw std::runtime_error(reason);
}

struct signal final {
  std::promise<void> promise;
  std::shared_future<void> future{promise.get_future().share()};
  std::atomic<bool> opened{false};
  void open() { if (!opened.exchange(true)) promise.set_value(); }
};

// Actual bounded mailbox and Engine registration; only close's completion is
// held so the UI can observe the new retirement phase without a view or DLL.
struct input_retirement_probe final : soui::sdk::detail::engine_input_service {
  engine_thread& engine;
  std::shared_ptr<signal> release, entered{std::make_shared<signal>()};
  soui::sdk::detail::input_mailbox mailbox{[](const soui::sdk::input_packet*) {}};
  std::atomic<unsigned> quiesced{0}, closed{0}, late_calls{0};
  std::atomic<bool> wrong_owner{false}, late_ready{false};
  input_retirement_probe(engine_thread& owner, std::shared_ptr<signal> finish)
      : engine(owner), release(std::move(finish)) {}
  bool pending() const noexcept override { return mailbox.pending() || late_ready.load(); }
  void service() noexcept override {
    if (late_ready.exchange(false)) ++late_calls;
    mailbox.service();
  }
  void quiesce() noexcept override { ++quiesced; mailbox.quiesce(); }
  void close() noexcept override {
    if (!engine.is_owner_thread()) wrong_owner.store(true);
    ++closed; entered->open(); release->future.wait();
    mailbox.close();
  }
};

struct fixture final {
  engine_thread engine;
  std::shared_ptr<signal> entered{std::make_shared<signal>()};
  std::shared_ptr<signal> release{std::make_shared<signal>()};
  dynamic_miniblink_adapter adapter;
  engine_thread* bridge_observer{nullptr};
  std::shared_ptr<input_retirement_probe> input;
  explicit fixture(bool blocked_bridge = false, bool with_input = false, void* reload_view = nullptr) {
    require(engine.start([] { return true; }), "real engine startup failed");
    std::unique_ptr<engine_thread> bridge;
    try {
      if (blocked_bridge) {
        bridge = std::make_unique<engine_thread>();
        require(bridge->start([] { return true; }), "bridge worker startup failed");
        bridge_observer = bridge.get();
      }
      if (with_input) input = std::make_shared<input_retirement_probe>(engine, release);
      if (reload_view) {
        require(!blocked_bridge && !with_input, "reload fixture does not manufacture a Bridge worker");
        engine_admission::bind_reload_fixture(adapter, engine, reload_view);
      } else {
        engine_admission::bind_unmount_fixture(adapter, engine, std::move(bridge), input);
      }
      if (blocked_bridge) {
        require(bridge_observer->post([e=entered,r=release] { e->open(); r->future.wait(); }), "bridge blocker rejected");
        require(entered->future.wait_for(5s) == std::future_status::ready, "bridge blocker did not enter");
      }
    } catch (...) {
      release->open();
      const auto deadline = std::chrono::steady_clock::now() + 5s;
      while (!adapter.unmount().ok) {
        if (std::chrono::steady_clock::now() >= deadline) std::terminate();
        std::this_thread::sleep_for(1ms);
      }
      throw;
    }
  }
  ~fixture() {
    release->open();
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!adapter.unmount().ok) {
      if (std::chrono::steady_clock::now() >= deadline) std::terminate();
      std::this_thread::sleep_for(1ms);
    }
    engine.stop();
  }
  void block_engine() {
    require(engine.post([e=entered,r=release] { e->open(); r->future.wait(); }),
            "FIFO blocker rejected");
    require(entered->future.wait_for(5s) == std::future_status::ready,
            "FIFO blocker did not enter");
  }
  void finish() {
    release->open();
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!adapter.unmount().ok) {
      require(std::chrono::steady_clock::now() < deadline, "original unmount did not finish");
      std::this_thread::sleep_for(1ms);
    }
    require(!engine_admission::unmount_fixture_engine(adapter), "completed adapter retained its Runtime owner");
  }
};

struct return_watchdog final {
  std::shared_ptr<signal> returned{std::make_shared<signal>()};
  std::shared_ptr<std::atomic<bool>> had_to_release{std::make_shared<std::atomic<bool>>(false)};
  std::thread worker;
  explicit return_watchdog(std::shared_ptr<signal> release)
      : worker([done=returned,release=std::move(release),forced=had_to_release] {
          if (done->future.wait_for(2s) != std::future_status::ready) {
            forced->store(true); release->open();
          }
        }) {}
  ~return_watchdog() { finish(); }
  void finish() { returned->open(); if (worker.joinable()) worker.join(); }
};

struct native_reload_observation final {
  unsigned char opaque_view{0};
  engine_thread* engine{nullptr};
  std::atomic<unsigned> reloads{0}, destroys{0};
  std::atomic<bool> wrong_view{false}, wrong_owner{false};
  void observe(wkeWebView view) noexcept {
    if (reinterpret_cast<void*>(view) != &opaque_view) wrong_view.store(true);
    if (!engine || !engine->is_owner_thread()) wrong_owner.store(true);
  }
};

std::atomic<native_reload_observation*> native_reload_observer{nullptr};

void WKE_CALL_TYPE observe_native_reload(wkeWebView view) {
  const auto state = native_reload_observer.load();
  if (!state) std::terminate();
  state->observe(view);
  state->reloads.fetch_add(1);
}

void WKE_CALL_TYPE observe_native_destroy(wkeWebView view) {
  const auto state = native_reload_observer.load();
  if (!state) std::terminate();
  state->observe(view);
  state->destroys.fetch_add(1);
}

class native_reload_spies final {
 public:
  explicit native_reload_spies(native_reload_observation& state)
      : previous_reload_(wkeReload), previous_destroy_(wkeDestroyWebView), previous_run_js_(wkeRunJS) {
    require(!native_reload_observer.load() && !previous_reload_ && !previous_destroy_ && !previous_run_js_,
            "no-DLL reload fixture must not replace an existing native provider");
    native_reload_observer.store(&state);
    wkeReload = &observe_native_reload;
    wkeDestroyWebView = &observe_native_destroy;
    wkeRunJS = nullptr;
  }
  ~native_reload_spies() {
    // reload_fixture destroys its real adapter/engine member first. No queued
    // callback can retain these pointers or the observation at this point.
    wkeReload = previous_reload_;
    wkeDestroyWebView = previous_destroy_;
    wkeRunJS = previous_run_js_;
    native_reload_observer.store(nullptr);
  }
  native_reload_spies(const native_reload_spies&) = delete;
  native_reload_spies& operator=(const native_reload_spies&) = delete;
 private:
  FN_wkeReload previous_reload_;
  FN_wkeDestroyWebView previous_destroy_;
  FN_wkeRunJS previous_run_js_;
};

struct reload_fixture final {
  // Reverse member destruction keeps the spies and opaque view alive through
  // original unmount completion, capture retirement and the real Engine join.
  native_reload_observation native;
  native_reload_spies spies{native};
  fixture host{false, false, &native.opaque_view};
  reload_fixture() { native.engine = &host.engine; }
  void require_owner() const {
    require(!native.wrong_view.load() && !native.wrong_owner.load(),
            "native reload/destroy did not use the original view and Engine owner");
  }
  void finish(unsigned expected_reloads) {
    host.finish();
    require(host.engine.drain_for(5s), "reload fixture callbacks did not retire");
    require(native.reloads.load() == expected_reloads, "reload native call count changed during cleanup");
    require(native.destroys.load() == 1, "original unmount did not destroy the opaque view exactly once");
    require_owner();
  }
};

void require_reload_pending(const soui::sdk::host_result& result) {
  require(!result.ok && result.code == "E_RELOAD_PENDING", "actual reload lost its pending operation");
}

void complete_reload(fixture& host) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  for (;;) {
    const auto result = host.adapter.reload();
    if (result.ok) return;
    require_reload_pending(result);
    require(std::chrono::steady_clock::now() < deadline, "original pending reload did not complete");
    std::this_thread::sleep_for(1ms);
  }
}

void blocked_fifo_reload_keeps_one_owned_operation() {
  reload_fixture f;
  auto& host = f.host;
  host.block_engine();
  return_watchdog watchdog(host.release);
  const auto result = host.adapter.reload(); // Actual public adapter path on its owner.
  require(!watchdog.had_to_release->load() && !host.release->opened.load(),
          "actual reload waited for the blocked FIFO instead of returning pending");
  require_reload_pending(result);
  for (unsigned n=0; n<4; ++n) require_reload_pending(host.adapter.reload());
  watchdog.finish();
  require(!watchdog.had_to_release->load() && !host.release->opened.load(), "reload polling waited for the blocked FIFO");
  require(f.native.reloads.load() == 0, "blocked reload reached the native provider");
  require(engine_admission::unmount_fixture_engine(host.adapter) == &host.engine,
          "pending reload released its original Engine owner");
  require(engine_admission::observe(host.engine).queued[static_cast<unsigned>(engine_lane::ordinary)] == 1,
          "reload polling duplicated work in the actual ordinary FIFO");
  host.release->open();
  require(host.engine.drain_for(5s), "accepted reload did not leave the actual FIFO");
  complete_reload(host);
  require(f.native.reloads.load() == 1, "accepted reload did not execute exactly once");
  f.require_owner();
  f.finish(1);
}

void closing_cancels_a_queued_reload_before_native_execution() {
  reload_fixture f;
  auto& host = f.host;
  host.block_engine();
  return_watchdog watchdog(host.release);
  const auto reload = host.adapter.reload();
  const auto close = host.adapter.unmount();
  require(!watchdog.had_to_release->load() && !host.release->opened.load(),
          "reload/close waited for the blocked FIFO");
  require_reload_pending(reload);
  require(!close.ok && close.code == "E_CURSOR_CLOSE_PENDING", "close lost its original pending continuation");
  require(host.adapter.reload().code == "E_CURSOR_CLOSE_PENDING", "closing adapter accepted another reload");
  const auto queued = engine_admission::observe(host.engine);
  require(queued.queued[static_cast<unsigned>(engine_lane::ordinary)] == 1 &&
          queued.queued[static_cast<unsigned>(engine_lane::cleanup)] == 1,
          "close discarded or duplicated the accepted reload/barrier tasks");
  require(f.native.reloads.load() == 0 && f.native.destroys.load() == 0 &&
          engine_admission::unmount_fixture_engine(host.adapter) == &host.engine,
          "close released the provider/Engine before queued work drained");
  watchdog.finish();
  require(!watchdog.had_to_release->load() && !host.release->opened.load(), "closing reload poll waited for the FIFO");
  f.finish(0); // Original release, cancellation task, unmount and native destroy.
}

void full_ordinary_fifo_retries_one_pending_reload() {
  reload_fixture f;
  auto& host = f.host;
  host.block_engine();
  const auto fillers = std::make_shared<std::atomic<unsigned>>(0);
  for (std::size_t n=0; n<engine_admission::ordinary_limit; ++n)
    require(host.engine.post([fillers] { fillers->fetch_add(1); }), "ordinary capacity fixture was not admitted");
  return_watchdog watchdog(host.release);
  const auto result = host.adapter.reload();
  require(!watchdog.had_to_release->load() && !host.release->opened.load(), "full FIFO reload waited for capacity");
  require_reload_pending(result);
  const auto receipt = host.adapter.execution_context_receipt();
  for (unsigned n=0; n<4; ++n) {
    require_reload_pending(host.adapter.reload());
    require(host.adapter.execution_context_receipt() == receipt, "capacity retry replaced the pending context receipt");
  }
  watchdog.finish();
  require(!watchdog.had_to_release->load() && !host.release->opened.load(), "capacity polling waited for the FIFO");
  require(engine_admission::observe(host.engine).queued[static_cast<unsigned>(engine_lane::ordinary)] ==
          engine_admission::ordinary_limit && f.native.reloads.load() == 0,
          "full FIFO retries displaced existing work or called the native provider");
  host.release->open();
  require(host.engine.drain_for(5s), "ordinary capacity fixture did not drain");
  require(fillers->load() == engine_admission::ordinary_limit, "reload retry lost accepted ordinary work");
  complete_reload(host);
  require(f.native.reloads.load() == 1, "capacity retry did not execute the same reload exactly once");
  f.finish(1);
}

void blocked_fifo_returns_pending_without_duplicate_work() {
  fixture f;
  f.block_engine();
  // Test-only recovery watchdog: release the old blocking implementation so
  // RED returns a real assertion, not a killed or timed-out test process.
  return_watchdog watchdog(f.release);
  const auto result = f.adapter.unmount();
  watchdog.finish();
  require(!watchdog.had_to_release->load() && !f.release->opened.load(),
          "actual unmount waited for the blocked FIFO instead of returning pending");
  require(!result.ok && result.code == "E_CURSOR_CLOSE_PENDING", "blocked unmount lost pending");
  for (unsigned n=0; n<4; ++n)
    require(f.adapter.unmount().code == "E_CURSOR_CLOSE_PENDING", "repeat lost original pending step");
  require(engine_admission::unmount_fixture_engine(f.adapter) == &f.engine, "pending released its Runtime owner");
  require(engine_admission::observe(f.engine).queued[static_cast<unsigned>(engine_lane::cleanup)] == 1,
          "polling duplicated cleanup in the actual FIFO");
  require(f.adapter.reload().code == "E_CURSOR_CLOSE_PENDING", "closing adapter accepted reload");
  require(f.adapter.show(true).code == "E_CURSOR_CLOSE_PENDING", "closing adapter accepted show");
  require(f.adapter.focus().code == "E_CURSOR_CLOSE_PENDING", "closing adapter accepted focus");
  require(f.adapter.resize(1,1).code == "E_CURSOR_CLOSE_PENDING", "closing adapter accepted resize");
  require(!f.adapter.accepts_execution_context_receipt(0), "closing adapter accepted a legacy context");
  require(!f.adapter.application_ready(), "closing adapter retained readiness");
  require(f.adapter.application_diagnostics() == "document=waiting,bridge=waiting", "closing diagnostic posted work");
  f.adapter.pump();
  require(engine_admission::observe(f.engine).queued[0] == 0, "closing pump queued ordinary work");
  f.finish();
}

void blocked_bridge_worker_remains_owned() {
  fixture f(true);
  return_watchdog watchdog(f.release);
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (f.bridge_observer->state() != soui::sdk::engine_thread_state::stopping) {
    const auto pending = f.adapter.unmount();
    require(!watchdog.had_to_release->load() && !f.release->opened.load(), "Bridge close waited for its blocked worker");
    require(!pending.ok && pending.code == "E_CURSOR_CLOSE_PENDING", "bridge close did not retain pending");
    require(std::chrono::steady_clock::now() < deadline, "bridge stop was not requested");
    std::this_thread::sleep_for(1ms);
  }
  watchdog.finish();
  require(!watchdog.had_to_release->load() && !f.release->opened.load() && engine_admission::unmount_fixture_engine(f.adapter) == &f.engine,
          "pending Bridge discarded its original worker/Runtime owner");
  f.finish();
}

void input_retirement_remains_pending_until_actual_owner_completion() {
  fixture f(false, true);
  return_watchdog watchdog(f.release);
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (f.input->entered->future.wait_for(0ms) != std::future_status::ready) {
    const auto pending = f.adapter.unmount();
    require(!pending.ok && pending.code == "E_CURSOR_CLOSE_PENDING", "input retirement did not retain pending");
    require(std::chrono::steady_clock::now() < deadline, "input retirement never reached the Engine owner");
    std::this_thread::sleep_for(1ms);
  }
  watchdog.finish();
  require(!watchdog.had_to_release->load() && !f.release->opened.load(), "input retirement waited for its callback");
  require(f.input->quiesced.load() == 1 && f.input->closed.load() == 1 && !f.input->wrong_owner.load(),
          "input retirement was not quiesced once and closed on its real Engine owner");
  soui::sdk::input_packet late;
  late.kind = soui::sdk::input_kind::key_down;
  require(f.input->mailbox.post(late) == soui::sdk::input_admission_result::stopped,
          "quiescing input admitted a late packet");
  for (unsigned n=0; n<4; ++n) {
    require(f.adapter.unmount().code == "E_CURSOR_CLOSE_PENDING", "input polling lost its original pending result");
    f.adapter.pump();
  }
  require(engine_admission::unmount_fixture_engine(f.adapter) == &f.engine && f.input->closed.load() == 1,
          "pending input released the Runtime owner or duplicated unregister");
  require(engine_admission::observe(f.engine).queued[0] == 0, "input closing pump queued ordinary work");
  f.finish();
  require(f.input->closed.load() == 1, "completed input was closed twice");
  // A delayed producer wake cannot re-enter an already detached service.
  f.input->late_ready.store(true);
  engine_admission::notify_input(f.engine);
  const auto fence = std::make_shared<signal>();
  require(f.engine.post([fence] { fence->open(); }), "late input fence rejected");
  require(fence->future.wait_for(5s) == std::future_status::ready, "late input fence did not finish");
  require(f.input->late_calls.load() == 0 && f.input->mailbox.post(late) == soui::sdk::input_admission_result::stopped,
          "retired input accepted a late callback or packet");
}

void unused_adapter_can_cross_construction_thread() {
  dynamic_miniblink_adapter adapter;
  const auto result = std::async(std::launch::async, [&adapter] { return adapter.unmount(); }).get();
  require(result.ok, "unused adapter teardown incorrectly required its construction thread");
}

void early_devtools_revocation_rejects_focus_before_unmount_continuation() {
  fixture f;
  engine_admission::revoke_devtools_fixture(f.adapter);
  const auto focused = f.adapter.focus();
  require(!focused.ok && focused.code == "E_CURSOR_CLOSE_PENDING",
          "revoked target reached native focus before its teardown continuation existed");
  std::intptr_t result{};
  require(!f.adapter.handle_native_window_message(WM_SETFOCUS, 0, 0, result),
          "late focus entered the revoked target before unmount continuation creation");
  f.finish();
}
} // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "unused-transfer") {
      unused_adapter_can_cross_construction_thread();
    } else if (argc == 2 && std::string_view(argv[1]) == "devtools-early-retirement") {
      early_devtools_revocation_rejects_focus_before_unmount_continuation();
    } else if (argc == 2 && std::string_view(argv[1]) == "reload") {
      blocked_fifo_reload_keeps_one_owned_operation();
      closing_cancels_a_queued_reload_before_native_execution();
      full_ordinary_fifo_retries_one_pending_reload();
      std::cout << "Actual public adapter reload FIFO continuation passed; no DLL or debugger pause qualified\n";
      return 0;
    } else {
      require(argc == 1, "unexpected test argument");
      blocked_fifo_returns_pending_without_duplicate_work();
      input_retirement_remains_pending_until_actual_owner_completion();
      blocked_bridge_worker_remains_owned();
    }
    std::cout << "Actual no-view Miniblink unmount continuation passed; no DLL or debugger pause qualified\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
