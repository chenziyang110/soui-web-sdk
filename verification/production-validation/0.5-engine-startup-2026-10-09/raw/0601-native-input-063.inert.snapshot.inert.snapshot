#include <soui/host/engine_thread.h>

#include "../../sdk/src/host/diagnostic_result.h"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {

using namespace std::chrono_literals;
using soui::sdk::engine_thread;
using soui::sdk::host_result;
using soui::sdk::detail::invoke_diagnostic_result;

void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

constexpr auto completion_deadline = 5s;
constexpr auto diagnostic_deadline = 1ms;

struct signal {
  std::promise<void> promise;
  std::shared_future<void> future{promise.get_future().share()};
};

class release_guard final {
 public:
  explicit release_guard(std::shared_ptr<signal> release) : release_(std::move(release)) {}
  ~release_guard() { open(); }
  void open() {
    if (release_) { release_->promise.set_value(); release_.reset(); }
  }
 private:
  std::shared_ptr<signal> release_;
};

host_result timed_out() { return {false, "E_ENGINE_THREAD", "diagnostic did not complete"}; }

struct owned_work {
  explicit owned_work(std::shared_ptr<signal> destroyed) : destroyed_(std::move(destroyed)) {}
  ~owned_work() { destroyed_->promise.set_value(); }
  std::shared_ptr<signal> destroyed_;
};

void test_queued_work_outlives_timed_out_caller() {
  engine_thread engine;
  require(engine.start([] { return true; }), "engine startup failed");
  auto blocker_entered = std::make_shared<signal>();
  auto blocker_release = std::make_shared<signal>();
  // Declared after engine so a failed assertion releases work before join.
  release_guard unblock(blocker_release);
  require(engine.post([blocker_entered, blocker_release] {
    blocker_entered->promise.set_value();
    blocker_release->future.wait();
  }), "blocking FIFO task was rejected");
  require(blocker_entered->future.wait_for(completion_deadline) == std::future_status::ready,
          "FIFO blocker did not enter");

  auto late_entered = std::make_shared<signal>();
  auto work_destroyed = std::make_shared<signal>();
  std::weak_ptr<owned_work> retained;
  const host_result response = [&] {
    auto owned = std::make_shared<owned_work>(work_destroyed);
    retained = owned;
    return invoke_diagnostic_result(engine, [owned, late_entered] {
      late_entered->promise.set_value();
      return host_result{true, {}, "late completion"};
    }, timed_out(), diagnostic_deadline);
  }();
  require(!response.ok && response.code == "E_ENGINE_THREAD", "queued timeout became success");
  require(late_entered->future.wait_for(0ms) != std::future_status::ready,
          "blocked FIFO executed the diagnostic before caller returned");
  require(!retained.expired(), "accepted diagnostic lost work ownership on timeout");

  unblock.open();
  require(late_entered->future.wait_for(completion_deadline) == std::future_status::ready,
          "timed-out accepted diagnostic was unexpectedly cancelled");
  require(engine.drain_for(completion_deadline), "late diagnostic did not drain");
  require(work_destroyed->future.wait_for(completion_deadline) == std::future_status::ready && retained.expired(),
          "late diagnostic retained work after completion");
  require(!response.ok && response.code == "E_ENGINE_THREAD" && response.message == "diagnostic did not complete",
          "late result changed the already-returned failure");
}

void test_executing_work_outlives_timed_out_caller() {
  engine_thread engine;
  require(engine.start([] { return true; }), "engine startup failed");
  auto entered = std::make_shared<signal>();
  auto release = std::make_shared<signal>();
  auto work_destroyed = std::make_shared<signal>();
  auto owner = std::make_shared<owned_work>(work_destroyed);
  std::weak_ptr<owned_work> retained = owner;
  auto caller_returned = std::make_shared<std::atomic<bool>>(false);
  auto entered_before_return = std::make_shared<std::atomic<bool>>(false);
  auto response = std::async(std::launch::async, [&engine, owner, entered, release,
                                                caller_returned, entered_before_return] {
    auto result = invoke_diagnostic_result(engine, [owner, entered, release,
                                                  caller_returned, entered_before_return] {
      entered_before_return->store(!caller_returned->load());
      entered->promise.set_value();
      release->future.wait();
      return host_result{true, {}, "late running completion"};
    }, timed_out(), 1s);
    caller_returned->store(true);
    return result;
  });
  // Release before the async future joins if any assertion throws.
  release_guard unblock(release);
  owner.reset();
  require(entered->future.wait_for(completion_deadline) == std::future_status::ready,
          "running diagnostic did not enter");
  require(entered_before_return->load(), "diagnostic had not entered before the caller returned");
  require(response.wait_for(completion_deadline) == std::future_status::ready,
          "running diagnostic ignored the caller deadline");
  const auto result = response.get();
  require(!result.ok && result.code == "E_ENGINE_THREAD", "running timeout became success");
  require(!retained.expired(), "running diagnostic lost ownership after caller return");
  unblock.open();
  require(engine.drain_for(completion_deadline), "running late diagnostic did not drain");
  require(work_destroyed->future.wait_for(completion_deadline) == std::future_status::ready && retained.expired(),
          "running diagnostic retained work after completion");
}

void test_success_failure_exception_and_rejection() {
  engine_thread engine;
  require(engine.start([] { return true; }), "engine startup failed");
  const auto success = invoke_diagnostic_result(engine, [] {
    return host_result{true, {}, "confirmed success"};
  }, timed_out(), completion_deadline);
  require(success.ok && success.message == "confirmed success", "completed diagnostic lost success");
  const auto failed = invoke_diagnostic_result(engine, [] {
    return host_result{false, "E_GC_API_MISSING", "native failure"};
  }, timed_out(), completion_deadline);
  require(!failed.ok && failed.code == "E_GC_API_MISSING" && failed.message == "native failure",
          "completed native failure was replaced or became success");
  const auto threw = invoke_diagnostic_result(engine, []() -> host_result {
    throw std::runtime_error("diagnostic failure");
  }, timed_out(), completion_deadline);
  require(!threw.ok && threw.code == "E_ENGINE_THREAD", "throwing diagnostic became success");
  require(engine.invoke([] {}, completion_deadline), "throwing diagnostic stopped the FIFO owner");
  engine.stop();
  bool ran = false;
  const auto rejected = invoke_diagnostic_result(engine, [&ran] {
    ran = true;
    return host_result{true, {}, {}};
  }, timed_out(), completion_deadline);
  require(!ran && !rejected.ok && rejected.code == "E_ENGINE_THREAD", "stopped engine ran a diagnostic");
}

}  // namespace

int main() {
  try {
    test_queued_work_outlives_timed_out_caller();
    test_executing_work_outlives_timed_out_caller();
    test_success_failure_exception_and_rejection();
    std::cout << "Engine diagnostics: owned late results and failure semantics passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
