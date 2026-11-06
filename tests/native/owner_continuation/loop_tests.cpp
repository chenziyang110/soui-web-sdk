#include <soui/host/windows_owner_loop.h>
#include "../../../sdk/src/host/owner_continuation.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>

int run_owner_policy_contracts();
namespace {
using soui::sdk::windows_owner_loop;
using soui::sdk::windows_owner_loop_callbacks;
constexpr UINT business_message = WM_APP + 0x370;
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
void post(UINT message = business_message, WPARAM value = 0) {
  require(PostThreadMessageW(GetCurrentThreadId(), message, value, 0) != FALSE, "thread message post");
}
struct owner_data {
  unsigned work{}, pumps{}, notifications{};
  std::function<void()> during_pump;
  unsigned* destroyed{};
  ~owner_data() { if (destroyed) ++*destroyed; }
};
struct model {
  std::shared_ptr<owner_data> data = std::make_shared<owner_data>();
  std::unique_ptr<soui::sdk::detail::owner_continuation_registration> registration;
  model() {
    const std::weak_ptr<owner_data> weak = data;
    registration = soui::sdk::detail::attach_current_owner_continuation(data, [weak] {
      const auto live = weak.lock();
      if (!live) return false;
      ++live->pumps;
      if (live->work) --live->work;
      const auto callback = live->during_pump;
      if (callback) callback();
      return live->work != 0;
    }, [weak] { if (const auto live = weak.lock()) ++live->notifications; });
    require(registration != nullptr, "actual Core registration");
  }
  void add(unsigned count) { data->work += count; registration->request(); }
};
unsigned cases{};
template<class F> void test(const char* name, F body) {
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {}
  body();
  ++cases;
  std::printf("PASS %s\n", name);
}
}

int main() { try {
  require(run_owner_policy_contracts() == 0, "policy contracts failed");
  test("no-sdk-context-retains-original-notification-contract", [] {
    auto data = std::make_shared<int>(0);
    require(!soui::sdk::detail::attach_current_owner_continuation(data, [] { return false; }, [] {}),
      "legacy application unexpectedly registered");
  });
  test("quit-is-observed-before-return-without-business-dispatch", [] {
    windows_owner_loop loop;
    require(loop.status().ok, "dispatcher initialization");
    unsigned observations{}, dispatches{};
    windows_owner_loop_callbacks hooks;
    hooks.message_retrieved = [&](const MSG& message) {
      if (message.message == WM_QUIT) { ++observations; require(message.wParam == 17, "quit identity"); }
    };
    hooks.dispatch = [&](MSG&) { ++dispatches; };
    PostQuitMessage(17);
    require(loop.run(hooks) == 17 && observations == 1 && !dispatches, "quit observer ordering");
  });
  test("same-object-reentry-rejects-with-outer-run-intact", [] {
    windows_owner_loop loop;
    bool rejected{};
    windows_owner_loop_callbacks hooks;
    hooks.dispatch = [&](MSG& message) {
      require(message.message == business_message, "unexpected business message");
      try { loop.run(); } catch (const std::logic_error&) { rejected = true; }
      PostQuitMessage(4);
    };
    post();
    require(loop.run(hooks) == 4 && rejected && loop.status().ok, "reentry corrupted outer scope");
  });
  test("different-sdk-objects-nest-and-preserve-outer-pending", [] {
    windows_owner_loop outer, inner;
    model adapter;
    adapter.add(2);
    bool nested{}, finished{};
    windows_owner_loop_callbacks hooks;
    hooks.quit_requested = [&] { return finished; };
    hooks.dispatch = [&](MSG& message) {
      require(message.message == business_message, "nested entry message");
      windows_owner_loop_callbacks nested_hooks;
      nested_hooks.quit_requested = [&] { return adapter.data->pumps != 0; };
      require(inner.run(nested_hooks) == 0, "nested result");
      require(adapter.data->pumps == 1 && adapter.data->work == 1, "inner changed outer backlog");
      nested = true;
    };
    adapter.data->during_pump = [&] { if (adapter.data->pumps == 2) finished = true; };
    post();
    require(outer.run(hooks) == 0 && nested && finished && adapter.data->pumps == 2,
      "outer scope lost after nested SDK run");
  });
  test("private-poke-and-foreign-dispatch-do-not-pump-past-queued-work", [] {
    windows_owner_loop loop;
    model adapter;
    adapter.add(1);
    unsigned dispatched{};
    bool done{};
    windows_owner_loop_callbacks hooks;
    hooks.quit_requested = [&] { return done; };
    hooks.dispatch = [&](MSG& message) {
      require(message.message == business_message && adapter.data->pumps == 0,
        "ordinary output bypassed queued application work");
      ++dispatched;
    };
    adapter.data->during_pump = [&] { require(dispatched == 3, "queue not drained before output"); done = true; };
    post(); post(); post();
    require(loop.run(hooks) == 0 && done && adapter.data->pumps == 1, "idle service missing");
  });
  test("business-idle-post-rechecks-queue-and-false-does-not-disable-sdk", [] {
    windows_owner_loop loop;
    model adapter;
    adapter.add(1);
    bool idle_called{}, dispatched{}, done{};
    windows_owner_loop_callbacks hooks;
    hooks.quit_requested = [&] { return done; };
    hooks.idle = [&](std::uint32_t) {
      require(!idle_called, "false idle callback repeated"); idle_called = true; post(); return false;
    };
    hooks.dispatch = [&](MSG& message) {
      require(message.message == business_message && !adapter.data->pumps, "stale empty-queue permit");
      dispatched = true;
    };
    adapter.data->during_pump = [&] { require(idle_called && dispatched, "idle callback work bypassed"); done = true; };
    require(loop.run(hooks) == 0 && done, "business idle false starved SDK pending");
  });
  test("queue-generated-timer-is-selected-before-pending-output", [] {
    windows_owner_loop loop;
    model adapter;
    adapter.add(1);
    const auto timer = SetTimer(nullptr, 0, 1, nullptr);
    require(timer != 0, "actual thread timer");
    Sleep(20); // Make the actual OS timer due; no synthetic WM_TIMER post.
    bool observed{}, done{};
    windows_owner_loop_callbacks hooks;
    hooks.quit_requested = [&] { return done; };
    hooks.dispatch = [&](MSG& message) {
      require(message.message == WM_TIMER && message.wParam == timer && !adapter.data->pumps,
        "pending output bypassed actual OS timer");
      require(KillTimer(nullptr, timer) != FALSE, "thread timer cleanup"); observed = true;
    };
    adapter.data->during_pump = [&] { require(observed, "timer not delivered"); done = true; };
    require(loop.run(hooks) == 0 && done, "timer fairness boundary");
  });
  test("continuing-business-idle-cannot-starve-pending-sdk-work", [] {
    windows_owner_loop loop;
    model adapter;
    adapter.add(2);
    unsigned idle_calls{};
    bool done{};
    windows_owner_loop_callbacks hooks;
    hooks.quit_requested = [&] { return done; };
    hooks.idle = [&](std::uint32_t count) {
      require(count == idle_calls && ++idle_calls < 5, "continuing idle starved SDK work");
      return true;
    };
    adapter.data->during_pump = [&] {
      require(idle_calls != 0, "business idle was never offered");
      if (!adapter.data->work) done = true;
    };
    require(loop.run(hooks) == 0 && done && adapter.data->pumps == 2,
      "bounded continuing idle blocked ordinary SDK continuation");
  });
  test("return-from-uncooperative-modal-restores-sdk-continuation", [] {
    windows_owner_loop loop;
    model adapter;
    adapter.add(2);
    bool modal{}, done{};
    windows_owner_loop_callbacks hooks;
    hooks.quit_requested = [&] { return done; };
    hooks.dispatch = [&](MSG& message) {
      require(message.message == business_message, "modal entry");
      const auto before = adapter.data->notifications;
      const auto deadline = GetTickCount64() + 1000;
      while (adapter.data->notifications == before) {
        require(GetTickCount64() < deadline, "modal fallback deadline");
        MSG nested{};
        if (PeekMessageW(&nested, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&nested);
        else MsgWaitForMultipleObjectsEx(0, nullptr, 20, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
      }
      require(!adapter.data->pumps, "private WndProc reentered ordinary pump"); modal = true;
    };
    adapter.data->during_pump = [&] {
      require(modal, "pump before modal exit"); if (!adapter.data->work) done = true;
    };
    post();
    require(loop.run(hooks) == 0 && done && adapter.data->pumps == 2, "permanent modal legacy trap");
  });
  test("exception-unwinds-root-and-loop-can-run-again", [] {
    windows_owner_loop loop;
    model adapter;
    adapter.add(2);
    adapter.data->during_pump = [] { throw std::runtime_error("actual pump callout"); };
    const auto before = adapter.data->notifications;
    bool caught{};
    try { loop.run(); } catch (const std::runtime_error&) { caught = true; }
    require(caught && adapter.data->notifications > before, "exception lost original wake");
    require(!loop.status().ok, "pump exception hidden from optimizer status");
    // Re-enter with the failed registration still pending. The application
    // message must run; resuming SDK roots must not retry the poisoned pump.
    bool dispatched{};
    windows_owner_loop_callbacks second;
    second.dispatch = [&](MSG&) { dispatched = true; };
    second.quit_requested = [&] { return dispatched; };
    post();
    require(loop.run(second) == 0 && adapter.data->pumps == 1, "exception fallback was resumed");
    adapter.data->during_pump = {};
    require(adapter.registration->service_legacy(), "fallback registration lost");
    require(adapter.data->work == 0, "legacy did not drain after exception");
    PostQuitMessage(9);
    require(loop.run() == 9, "failed run retained root/running state");
  });
  test("actual-weak-owner-is-pinned-only-through-service", [] {
    windows_owner_loop loop;
    model adapter;
    unsigned destroyed{};
    adapter.data->destroyed = &destroyed;
    std::weak_ptr<owner_data> weak = adapter.data;
    adapter.data->during_pump = [&] {
      adapter.data.reset(); require(!weak.expired(), "owner freed inside in-flight service");
    };
    adapter.add(1);
    windows_owner_loop_callbacks hooks;
    hooks.quit_requested = [&] { return weak.expired(); };
    require(loop.run(hooks) == 0 && weak.expired() && destroyed == 1, "owner lease retained after service");
    adapter.registration->request(); // Expired owner: no callback or stale token.
    adapter.registration->reset();
  });
  test("wrong-thread-status-and-run-have-explicit-failure", [] {
    windows_owner_loop loop;
    bool rejected{}, wrong_status{};
    std::thread other([&] {
      const auto status = loop.status(); wrong_status = !status.ok && status.code == "E_WRONG_THREAD";
      try { loop.run(); } catch (const std::logic_error&) { rejected = true; }
    });
    other.join();
    require(wrong_status && rejected && loop.status().ok, "wrong-thread operation changed owner state");
  });
  std::printf("SDK owner loop: %u cases passed; no visible window/input/performance qualification\n", cases);
  return 0;
} catch (const std::exception& error) { std::fprintf(stderr, "FAIL %s\n", error.what()); return 1; } }
