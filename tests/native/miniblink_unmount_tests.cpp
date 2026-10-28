#include <soui/host/soui_miniblink_host.h>
#include "../../sdk/src/host/engine_admission.h"
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

struct fixture final {
  engine_thread engine;
  std::shared_ptr<signal> entered{std::make_shared<signal>()};
  std::shared_ptr<signal> release{std::make_shared<signal>()};
  dynamic_miniblink_adapter adapter;
  engine_thread* bridge_observer{nullptr};
  explicit fixture(bool blocked_bridge = false) {
    require(engine.start([] { return true; }), "real engine startup failed");
    std::unique_ptr<engine_thread> bridge;
    try {
      if (blocked_bridge) {
        bridge = std::make_unique<engine_thread>();
        require(bridge->start([] { return true; }), "bridge worker startup failed");
        bridge_observer = bridge.get();
      }
      engine_admission::bind_unmount_fixture(adapter, engine, std::move(bridge));
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

void unused_adapter_can_cross_construction_thread() {
  dynamic_miniblink_adapter adapter;
  const auto result = std::async(std::launch::async, [&adapter] { return adapter.unmount(); }).get();
  require(result.ok, "unused adapter teardown incorrectly required its construction thread");
}
} // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "unused-transfer") {
      unused_adapter_can_cross_construction_thread();
    } else {
      require(argc == 1, "unexpected test argument");
      blocked_fifo_returns_pending_without_duplicate_work();
      blocked_bridge_worker_remains_owned();
    }
    std::cout << "Actual no-view Miniblink unmount continuation passed; no DLL or debugger pause qualified\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
