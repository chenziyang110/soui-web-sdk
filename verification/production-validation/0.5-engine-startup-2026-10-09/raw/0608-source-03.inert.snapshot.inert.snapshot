#include <soui/host/engine_thread.h>

#include "../../sdk/src/host/engine_startup_test_hooks.h"

#include <array>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using soui::sdk::engine_thread;
using soui::sdk::engine_thread_state;
constexpr auto completion_deadline = 5s;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

enum class fault { none, event_creation, launch_system_error, launch_bad_alloc,
                   blocked_launch_system_error };

struct launch_gate {
  std::promise<void> entered;
  std::future<void> entered_future{entered.get_future()};
  std::promise<void> release;
  std::shared_future<void> release_future{release.get_future().share()};
};

struct event_counts {
  unsigned attempts{};
  unsigned created{};
  unsigned closed{};
  unsigned outstanding{};
  unsigned close_errors{};
  unsigned launches{};
};

struct fixture_state {
  std::mutex mutex;
  fault selected{fault::none};
  std::shared_ptr<launch_gate> gate;
  std::array<HANDLE, 64> events{};
  event_counts counts;
};

fixture_state fixture;

event_counts counts() {
  std::lock_guard<std::mutex> lock(fixture.mutex);
  return fixture.counts;
}

void configure(fault selected, std::shared_ptr<launch_gate> gate = {}) {
  std::lock_guard<std::mutex> lock(fixture.mutex);
  require(fixture.counts.outstanding == 0, "previous case retained a wake event");
  fixture.selected = selected;
  fixture.gate = std::move(gate);
  fixture.counts = {};
}

void clear_fault() {
  std::lock_guard<std::mutex> lock(fixture.mutex);
  fixture.selected = fault::none;
  fixture.gate.reset();
}

void require_closed(unsigned expected_created, unsigned expected_launches) {
  const auto actual = counts();
  require(actual.created == expected_created, "unexpected actual event creation count");
  require(actual.closed == expected_created, "created wake event was not closed exactly once");
  require(actual.outstanding == 0 && actual.close_errors == 0,
          "wake event ownership or real CloseHandle failed");
  require(actual.launches == expected_launches, "unexpected worker launch call count");
}

void require_stopped_without_callbacks(engine_thread& engine, unsigned initialized,
                                       unsigned shutdowns) {
  require(engine.state() == engine_thread_state::stopped, "launch failure did not restore stopped");
  require(!engine.is_owner_thread(), "failed launch treated the caller as its worker owner");
  require(initialized == 0 && shutdowns == 0, "worker callback ran without a created worker");
  require(!engine.post([] {}), "failed launch admitted work");
}

class engine_stop_guard final {
 public:
  explicit engine_stop_guard(engine_thread& engine) : engine_(engine) {}
  engine_stop_guard(const engine_stop_guard&) = delete;
  engine_stop_guard& operator=(const engine_stop_guard&) = delete;
  ~engine_stop_guard() { engine_.stop(); }
 private:
  engine_thread& engine_;
};

void normal_retry(engine_thread& engine) {
  clear_fault();
  unsigned initialized = 0;
  unsigned shutdowns = 0;
  std::thread::id initialize_owner;
  std::thread::id shutdown_owner;
  bool tasks_on_owner = true;
  bool shutdown_on_owner = false;
  std::vector<unsigned> order;
  std::promise<void> completed;
  auto completion = completed.get_future();
  // Stop before these borrowed callback inputs leave scope on assertion failure.
  engine_stop_guard stop_before_inputs_expire(engine);
  const bool started = engine.start([&] {
    ++initialized;
    initialize_owner = std::this_thread::get_id();
    return true;
  }, [&] {
    ++shutdowns;
    shutdown_owner = std::this_thread::get_id();
    shutdown_on_owner = engine.is_owner_thread();
  });
  require(started && initialized == 1, "same-object retry did not create a real worker");
  require(initialize_owner != std::thread::id{} &&
          initialize_owner != std::this_thread::get_id(), "initializer did not run on a real worker");
  for (unsigned value = 1; value <= 3; ++value) {
    require(engine.post([&, value] {
      tasks_on_owner = tasks_on_owner && engine.is_owner_thread() &&
                       std::this_thread::get_id() == initialize_owner;
      order.push_back(value);
      if (value == 3) completed.set_value();
    }), "retry rejected FIFO work");
  }
  require(completion.wait_for(completion_deadline) == std::future_status::ready,
          "retry FIFO did not finish");
  completion.get();
  engine.stop();
  require(tasks_on_owner && order == std::vector<unsigned>({1, 2, 3}),
          "retry changed FIFO or worker identity");
  require(shutdowns == 1 && shutdown_on_owner && shutdown_owner == initialize_owner,
          "retry shutdown did not run once on its initializer owner");
  require(engine.state() == engine_thread_state::stopped &&
          !engine.is_owner_thread(initialize_owner), "normal stop retained a live owner");
}

void empty_initializer_creates_nothing() {
  configure(fault::none);
  engine_thread engine;
  unsigned shutdowns = 0;
  require(!engine.start({}, [&] { ++shutdowns; }), "empty initializer was accepted");
  require_stopped_without_callbacks(engine, 0, shutdowns);
  require(counts().attempts == 0, "empty initializer created a temporary event");
  engine.stop();
  require_closed(0, 0);
}

void event_creation_failure_can_retry() {
  configure(fault::event_creation);
  engine_thread engine;
  unsigned initialized = 0;
  unsigned shutdowns = 0;
  require(!engine.start([&] { ++initialized; return true; }, [&] { ++shutdowns; }),
          "controlled NULL event was accepted");
  require_stopped_without_callbacks(engine, initialized, shutdowns);
  require(counts().attempts == 1, "event failure bypassed the actual creation call site");
  require_closed(0, 0);
  normal_retry(engine);
  require_closed(1, 1);
}

void launch_failure_can_retry(fault selected) {
  configure(selected);
  engine_thread engine;
  unsigned initialized = 0;
  unsigned shutdowns = 0;
  require(!engine.start([&] { ++initialized; return true; }, [&] { ++shutdowns; }),
          "controlled launch exception became startup success");
  require_stopped_without_callbacks(engine, initialized, shutdowns);
  require_closed(1, 1);
  engine.stop();
  engine.stop();
  require_closed(1, 1);
  normal_retry(engine);
  require_closed(2, 2);
}

void system_error_rolls_back() { launch_failure_can_retry(fault::launch_system_error); }
void bad_alloc_rolls_back() { launch_failure_can_retry(fault::launch_bad_alloc); }

void repeated_launch_failures_have_no_event_growth() {
  configure(fault::launch_system_error);
  engine_thread engine;
  unsigned initialized = 0;
  unsigned shutdowns = 0;
  constexpr unsigned attempts = 32;
  for (unsigned index = 0; index < attempts; ++index) {
    require(!engine.start([&] { ++initialized; return true; }, [&] { ++shutdowns; }),
            "repeated launch failure was accepted");
    require_stopped_without_callbacks(engine, initialized, shutdowns);
    require_closed(index + 1, index + 1);
  }
  normal_retry(engine);
  require_closed(attempts + 1, attempts + 1);
}

void second_start_closes_only_its_temporary_event() {
  configure(fault::none);
  engine_thread engine;
  unsigned initialized = 0;
  unsigned shutdowns = 0;
  std::thread::id original_owner;
  engine_stop_guard stop_before_inputs_expire(engine);
  require(engine.start([&] { ++initialized; return true; }, [&] { ++shutdowns; }),
          "first startup failed");
  require(engine.invoke([&] { original_owner = std::this_thread::get_id(); }), "owner probe failed");
  bool second_callback = false;
  require(!engine.start([&] { second_callback = true; return true; }),
          "second start replaced a running worker");
  const auto actual = counts();
  require(actual.created == 2 && actual.closed == 1 && actual.outstanding == 1 &&
          actual.close_errors == 0 && actual.launches == 1,
          "rejected second start disturbed the original event");
  require(!second_callback && engine.is_owner_thread(original_owner) &&
          engine.invoke([] {}), "rejected second start disturbed the original worker");
  engine.stop();
  require(initialized == 1 && shutdowns == 1, "second start changed callback count");
  require_closed(2, 1);
}

void initializer_failure_preserves_failed(bool throws) {
  configure(fault::none);
  engine_thread engine;
  unsigned initialized = 0;
  unsigned shutdowns = 0;
  std::thread::id initialize_owner;
  std::thread::id shutdown_owner;
  require(!engine.start([&]() -> bool {
    ++initialized;
    initialize_owner = std::this_thread::get_id();
    if (throws) throw std::runtime_error("controlled initializer exception");
    return false;
  }, [&] {
    ++shutdowns;
    shutdown_owner = std::this_thread::get_id();
  }), "failed initializer became success");
  require(engine.state() == engine_thread_state::failed && initialized == 1 && shutdowns == 1,
          "worker initialization failure was incorrectly treated as launch rollback");
  require(initialize_owner != std::this_thread::get_id() && initialize_owner != std::thread::id{} &&
          shutdown_owner == initialize_owner, "failed initialization lost its real shutdown owner");
  require_closed(1, 1);
  bool retried = false;
  require(!engine.start([&] { retried = true; return true; }) && !retried,
          "initialization failure was made implicitly retryable");
  engine.stop();
  require(engine.state() == engine_thread_state::failed, "stop changed existing failed semantics");
  require_closed(2, 1);
}

void initializer_false_preserves_failed() { initializer_failure_preserves_failed(false); }
void initializer_exception_preserves_failed() { initializer_failure_preserves_failed(true); }

class release_guard final {
 public:
  explicit release_guard(std::shared_ptr<launch_gate> gate) : gate_(std::move(gate)) {}
  release_guard(const release_guard&) = delete;
  release_guard& operator=(const release_guard&) = delete;
  ~release_guard() { open(); }
  void open() {
    if (gate_) { gate_->release.set_value(); gate_.reset(); }
  }
 private:
  std::shared_ptr<launch_gate> gate_;
};

void concurrent_stop_does_not_wait_for_a_nonexistent_worker() {
  auto gate = std::make_shared<launch_gate>();
  configure(fault::blocked_launch_system_error, gate);
  unsigned initialized = 0;
  unsigned shutdowns = 0;
  std::promise<void> stop_entered;
  auto stop_entered_future = stop_entered.get_future();
  engine_thread engine;
  auto starter = std::async(std::launch::async, [&] {
    return engine.start([&] { ++initialized; return true; }, [&] { ++shutdowns; });
  });
  std::future<void> stopper;
  // Release before either future joins on assertion failure.
  release_guard release(gate);
  require(gate->entered_future.wait_for(completion_deadline) == std::future_status::ready,
          "controlled launch gate did not enter");
  stopper = std::async(std::launch::async, [&] {
    stop_entered.set_value();
    engine.stop();
  });
  require(stop_entered_future.wait_for(completion_deadline) == std::future_status::ready,
          "concurrent stop caller did not enter");
  release.open();
  require(starter.wait_for(completion_deadline) == std::future_status::ready,
          "failed start caller did not return");
  require(!starter.get(), "concurrent launch failure became success");
  require(stopper.wait_for(completion_deadline) == std::future_status::ready,
          "stop waited for a nonexistent worker");
  stopper.get();
  require_stopped_without_callbacks(engine, initialized, shutdowns);
  require_closed(1, 1);
  normal_retry(engine);
  require_closed(2, 2);
}

#ifdef SOUI_ENGINE_STARTUP_RED_FIXTURE
int run_old_body_red() {
  configure(fault::launch_system_error);
  unsigned initialized = 0;
  unsigned shutdowns = 0;
  {
    engine_thread engine;
    bool threw = false;
    try {
      (void)engine.start([&] { ++initialized; return true; }, [&] { ++shutdowns; });
    } catch (const std::system_error&) { threw = true; }
    require(threw && initialized == 0 && shutdowns == 0,
            "RED did not reach the controlled constructor exception");
    require(engine.state() == engine_thread_state::starting, "RED old body was changed");
    const auto actual = counts();
    require(actual.created == 1 && actual.closed == 0 && actual.outstanding == 1,
            "RED did not retain the old published event");
    std::cout << "Controlled launch exception observed; old-body destructor follows\n" << std::flush;
  }
  std::cerr << "RED unexpectedly completed destruction\n";
  return 1;
}
#endif

}  // namespace

namespace soui::sdk::detail::engine_startup_test {

HANDLE create_wake_event() noexcept {
  {
    std::lock_guard<std::mutex> lock(fixture.mutex);
    ++fixture.counts.attempts;
    if (fixture.selected == fault::event_creation) {
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return nullptr;
    }
  }
  const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (event == nullptr) return nullptr;
  std::lock_guard<std::mutex> lock(fixture.mutex);
  for (auto& slot : fixture.events) {
    if (slot == nullptr) {
      slot = event;
      ++fixture.counts.created;
      ++fixture.counts.outstanding;
      return event;
    }
  }
  ++fixture.counts.close_errors;
  (void)CloseHandle(event);
  return nullptr;
}

void close_wake_event(HANDLE event) noexcept {
  std::lock_guard<std::mutex> lock(fixture.mutex);
  for (auto& slot : fixture.events) {
    if (slot == event && event != nullptr) {
      if (CloseHandle(event) != FALSE) {
        slot = nullptr;
        ++fixture.counts.closed;
        --fixture.counts.outstanding;
      } else { ++fixture.counts.close_errors; }
      return;
    }
  }
  ++fixture.counts.close_errors;
}

void before_worker_launch() {
  fault selected;
  std::shared_ptr<launch_gate> gate;
  {
    std::lock_guard<std::mutex> lock(fixture.mutex);
    ++fixture.counts.launches;
    selected = fixture.selected;
    gate = fixture.gate;
  }
  if (selected == fault::blocked_launch_system_error) {
    gate->entered.set_value();
    gate->release_future.wait();
  }
  if (selected == fault::launch_system_error || selected == fault::blocked_launch_system_error)
    throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again),
                            "TEST_ONLY worker construction failure");
  if (selected == fault::launch_bad_alloc) throw std::bad_alloc{};
}

}  // namespace soui::sdk::detail::engine_startup_test

int main(int argc, char** argv) {
  try {
#ifdef SOUI_ENGINE_STARTUP_RED_FIXTURE
    require(argc == 2 && std::string_view(argv[1]) == "--red-launch-system-error",
            "RED fixture requires its explicit controlled-fault argument");
    return run_old_body_red();
#else
    (void)argv;
    require(argc == 1, "startup regression accepts no arguments");
    const std::array<std::pair<const char*, void (*)()>, 9> tests{{
      {"empty_initializer_creates_nothing", empty_initializer_creates_nothing},
      {"event_creation_failure_can_retry", event_creation_failure_can_retry},
      {"system_error_rolls_back", system_error_rolls_back},
      {"bad_alloc_rolls_back", bad_alloc_rolls_back},
      {"repeated_launch_failures_have_no_event_growth", repeated_launch_failures_have_no_event_growth},
      {"second_start_closes_only_its_temporary_event", second_start_closes_only_its_temporary_event},
      {"initializer_false_preserves_failed", initializer_false_preserves_failed},
      {"initializer_exception_preserves_failed", initializer_exception_preserves_failed},
      {"concurrent_stop_does_not_wait_for_a_nonexistent_worker", concurrent_stop_does_not_wait_for_a_nonexistent_worker},
    }};
    for (const auto& test : tests) {
      test.second();
      require(counts().outstanding == 0 && counts().close_errors == 0,
              "completed case retained an event or failed real closure");
      std::cout << "PASS " << test.first << '\n';
    }
    std::cout << "Engine startup controlled-fault regression: 9/9 passed\n";
    return 0;
#endif
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
