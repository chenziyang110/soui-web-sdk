#include "../../sdk/src/desktop/asset_executor.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
using soui::desktop::detail::asset_cancellation;
using soui::desktop::detail::asset_executor;
using soui::desktop::detail::asset_permit;
using soui::desktop::detail::asset_result;
using soui::desktop::result;
using namespace std::chrono_literals;

// These are watchdogs, not assertions about scheduler speed. Every ordering
// assertion below is established by a gate or a callback-lifetime signal.
constexpr auto observation_timeout = 5s;
constexpr auto gate_watchdog = 30s;
int checks = 0;
int failures = 0;
std::string context;

#define CHECK(condition) do { ++checks; if (!(condition)) { \
  ++failures; std::cerr << "FAIL line " << __LINE__ << " [" << context \
                       << "]: " #condition "\n"; } } while (false)
#define REQUIRE(condition) do { ++checks; if (!(condition)) { \
  throw std::runtime_error("line " + std::to_string(__LINE__) + ": " #condition); \
  } } while (false)

class signal {
 public:
  void set() {
    std::lock_guard<std::mutex> lock(mutex_);
    set_ = true;
    changed_.notify_all();
  }

  bool wait(std::chrono::milliseconds timeout = observation_timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, timeout, [&] { return set_; });
  }

 private:
  std::mutex mutex_;
  std::condition_variable changed_;
  bool set_{false};
};

struct retirement_signal {
  explicit retirement_signal(std::shared_ptr<signal> target)
      : target(std::move(target)) {}
  ~retirement_signal() { target->set(); }
  std::shared_ptr<signal> target;
};

asset_cancellation session() {
  return std::make_shared<std::atomic<bool>>(false);
}

asset_result success() {
  return {result::success(), "text/plain", "eA==", 1};
}

struct reader_snapshot {
  unsigned started{0};
  unsigned finished{0};
  unsigned active{0};
  unsigned peak{0};
  bool timed_out{false};
  std::map<std::string, std::filesystem::path> requests;
  std::set<std::thread::id> threads;
};

// Reader state is shared by value with detached workers. Even on assertion
// failure, no worker refers to a stack fixture that has gone out of scope.
class gated_reader {
 public:
  asset_result read(const std::filesystem::path& root, std::string_view path) {
    const std::string name(path);
    std::unique_lock<std::mutex> lock(mutex_);
    ++state_.started;
    ++state_.active;
    state_.peak = std::max(state_.peak, state_.active);
    state_.requests.emplace(name, root);
    state_.threads.insert(std::this_thread::get_id());
    changed_.notify_all();
    if (!changed_.wait_for(lock, gate_watchdog, [&] {
          return release_all_ || released_.count(name) != 0;
        })) {
      state_.timed_out = true;
    }
    --state_.active;
    ++state_.finished;
    const bool should_throw = throwing_.count(name) != 0;
    changed_.notify_all();
    lock.unlock();
    if (should_throw) throw std::runtime_error("Injected reader failure");
    return success();
  }

  void throw_for(std::string path) {
    std::lock_guard<std::mutex> lock(mutex_);
    throwing_.insert(std::move(path));
  }

  void release(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    released_.insert(path);
    changed_.notify_all();
  }

  void release_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    release_all_ = true;
    changed_.notify_all();
  }

  bool wait_started(unsigned count) {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, observation_timeout,
                             [&] { return state_.started >= count; });
  }

  reader_snapshot snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
  }

 private:
  std::mutex mutex_;
  std::condition_variable changed_;
  reader_snapshot state_;
  std::set<std::string> released_;
  std::set<std::string> throwing_;
  bool release_all_{false};
};

struct completed_asset {
  std::string name;
  asset_result value;
  asset_permit permit;
  std::thread::id thread;
};

class completions {
 public:
  void add(std::string name, asset_result value, asset_permit permit) {
    std::lock_guard<std::mutex> lock(mutex_);
    values_.push_back({std::move(name), std::move(value), std::move(permit),
                       std::this_thread::get_id()});
    changed_.notify_all();
  }

  bool wait(unsigned count) {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, observation_timeout,
                             [&] { return values_.size() >= count; });
  }

  std::vector<completed_asset> snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    return values_;
  }

  asset_permit take_permit(std::size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::move(values_.at(index).permit);
  }

 private:
  std::mutex mutex_;
  std::condition_variable changed_;
  std::vector<completed_asset> values_;
};

asset_executor::completion collect(
    const std::shared_ptr<completions>& values, std::string name,
    const std::shared_ptr<signal>& retired) {
  return [values, name = std::move(name),
          lifetime = std::make_shared<retirement_signal>(retired)](
             asset_result value, asset_permit permit) {
    (void)lifetime;
    values->add(name, std::move(value), std::move(permit));
  };
}

struct fixture {
  explicit fixture(std::size_t workers = 2, std::size_t capacity = 4)
      : reader(std::make_shared<gated_reader>()),
        executor(workers, capacity, [state = reader](
            const std::filesystem::path& root, std::string_view path) {
          return state->read(root, path);
        }) {}

  // Open every read gate before destroying the executor, including on a
  // failed REQUIRE. Detached worker captures keep all other state alive.
  ~fixture() { reader->release_all(); }

  std::shared_ptr<gated_reader> reader;
  asset_executor executor;
};

// A thread performing cancellation/destruction must never be stranded when an
// assertion throws. Open its dependent gate before joining it.
class guarded_thread {
 public:
  guarded_thread(std::thread thread, std::function<void()> release)
      : thread_(std::move(thread)), release_(std::move(release)) {}
  ~guarded_thread() {
    release_();
    if (thread_.joinable()) thread_.join();
  }
  void join() {
    if (thread_.joinable()) thread_.join();
  }

 private:
  std::thread thread_;
  std::function<void()> release_;
};

void bounded_workers_and_saturation() {
  constexpr unsigned capacity = 16;
  fixture test(2, capacity);
  const auto caller = std::this_thread::get_id();
  const auto token = session();
  const auto values = std::make_shared<completions>();
  std::vector<std::shared_ptr<signal>> retired;
  for (unsigned index = 0; index != capacity; ++index) {
    retired.push_back(std::make_shared<signal>());
    const std::string name = "asset-" + std::to_string(index);
    REQUIRE(test.executor.submit("copied-root", name, token,
                                collect(values, name, retired.back())).ok);
  }
  REQUIRE(test.reader->wait_started(2));
  auto state = test.reader->snapshot();
  CHECK(state.started == 2);
  CHECK(state.active == 2);
  CHECK(state.peak == 2);
  CHECK(state.threads.size() == 2);
  CHECK(state.threads.count(caller) == 0);
  auto rejected_retired = std::make_shared<signal>();
  const auto rejected = test.executor.submit(
      "unused", "overflow", token, collect(values, "overflow", rejected_retired));
  CHECK(!rejected.ok);
  CHECK(rejected.code == "asset_queue_full");
  REQUIRE(rejected_retired->wait());

  // Keep one worker occupied while the other advances a queued request.
  // No scheduler delay or assumed FIFO order is needed.
  test.reader->release(state.requests.begin()->first);
  REQUIRE(test.reader->wait_started(3));
  state = test.reader->snapshot();
  CHECK(state.started == 3);
  CHECK(state.active == 2);
  CHECK(state.peak == 2);
  test.reader->release_all();
  REQUIRE(values->wait(capacity));
  for (const auto& item : retired) REQUIRE(item->wait());
  state = test.reader->snapshot();
  CHECK(state.started == capacity);
  CHECK(state.finished == capacity);
  CHECK(state.active == 0);
  CHECK(state.peak == 2);
  CHECK(!state.timed_out);
  CHECK(state.requests.count("overflow") == 0);
  for (const auto& item : state.requests) CHECK(item.second == "copied-root");
  for (const auto& item : values->snapshot()) {
    CHECK(item.value.status.ok);
    CHECK(item.value.mime == "text/plain");
    CHECK(item.value.base64 == "eA==");
    CHECK(item.value.size == 1);
    CHECK(static_cast<bool>(item.permit));
    CHECK(item.thread != caller);
  }
}

void completion_lease_holds_capacity() {
  fixture test(1, 1);
  const auto token = session();
  const auto values = std::make_shared<completions>();
  const auto first_retired = std::make_shared<signal>();
  REQUIRE(test.executor.submit({}, "first", token,
                              collect(values, "first", first_retired)).ok);
  REQUIRE(test.reader->wait_started(1));
  test.reader->release("first");
  REQUIRE(values->wait(1));
  REQUIRE(first_retired->wait());
  CHECK(test.reader->snapshot().active == 0);

  auto submit_next = [&] {
    return test.executor.submit({}, "next", token,
        collect(values, "next", std::make_shared<signal>()));
  };
  CHECK(submit_next().code == "asset_queue_full");
  auto lease = values->take_permit(0);
  REQUIRE(static_cast<bool>(lease));
  auto copy = lease;
  lease.reset();
  CHECK(submit_next().code == "asset_queue_full");
  copy.reset();
  REQUIRE(submit_next().ok);
  REQUIRE(test.reader->wait_started(2));
  test.reader->release_all();
  REQUIRE(values->wait(2));
  CHECK(test.reader->snapshot().started == 2);
  CHECK(!test.reader->snapshot().timed_out);
}

void queued_cancellation_removes_reader_calls() {
  fixture test;
  const auto active_session = session();
  const auto queued_session = session();
  const auto values = std::make_shared<completions>();
  std::vector<std::shared_ptr<signal>> running_retired;
  for (const auto* name : {"running-a", "running-b"}) {
    running_retired.push_back(std::make_shared<signal>());
    REQUIRE(test.executor.submit({}, name, active_session,
        collect(values, name, running_retired.back())).ok);
  }
  REQUIRE(test.reader->wait_started(2));
  std::vector<std::shared_ptr<signal>> cancelled_retired;
  for (const auto* name : {"cancelled-a", "cancelled-b"}) {
    cancelled_retired.push_back(std::make_shared<signal>());
    REQUIRE(test.executor.submit({}, name, queued_session,
        collect(values, name, cancelled_retired.back())).ok);
  }
  test.executor.cancel(queued_session);
  test.executor.cancel(queued_session);  // Cancellation is idempotent.
  CHECK(queued_session->load());
  for (const auto& item : cancelled_retired) REQUIRE(item->wait());
  CHECK(values->snapshot().empty());

  const auto rejected = test.executor.submit({}, "already-cancelled",
      queued_session, collect(values, "already-cancelled", std::make_shared<signal>()));
  CHECK(!rejected.ok);
  CHECK(rejected.code == "asset_cancelled");
  const auto replacement_retired = std::make_shared<signal>();
  REQUIRE(test.executor.submit({}, "replacement", active_session,
      collect(values, "replacement", replacement_retired)).ok);
  CHECK(test.reader->snapshot().started == 2);
  test.reader->release_all();
  REQUIRE(values->wait(3));
  REQUIRE(replacement_retired->wait());
  for (const auto& item : running_retired) REQUIRE(item->wait());
  const auto state = test.reader->snapshot();
  CHECK(state.started == 3);
  CHECK(state.requests.count("cancelled-a") == 0);
  CHECK(state.requests.count("cancelled-b") == 0);
  CHECK(state.requests.count("already-cancelled") == 0);
  CHECK(state.requests.count("replacement") == 1);
  CHECK(!state.timed_out);
}

void inflight_cancellation_drops_completion() {
  fixture test(1, 1);
  const auto old_session = session();
  const auto next_session = session();
  const auto values = std::make_shared<completions>();
  const auto old_retired = std::make_shared<signal>();
  REQUIRE(test.executor.submit({}, "old-page", old_session,
                              collect(values, "old-page", old_retired)).ok);
  REQUIRE(test.reader->wait_started(1));
  test.executor.cancel(old_session);
  CHECK(old_session->load());
  CHECK(test.reader->snapshot().active == 1);
  const auto blocked = test.executor.submit({}, "next-page", next_session,
      collect(values, "next-page", std::make_shared<signal>()));
  CHECK(!blocked.ok);
  CHECK(blocked.code == "asset_queue_full");

  test.reader->release("old-page");
  REQUIRE(old_retired->wait());
  CHECK(values->snapshot().empty());
  const auto next_retired = std::make_shared<signal>();
  REQUIRE(test.executor.submit({}, "next-page", next_session,
      collect(values, "next-page", next_retired)).ok);
  REQUIRE(test.reader->wait_started(2));
  test.reader->release_all();
  REQUIRE(values->wait(1));
  REQUIRE(next_retired->wait());
  const auto delivered = values->snapshot();
  REQUIRE(delivered.size() == 1);
  CHECK(delivered.front().name == "next-page");
  CHECK(!test.reader->snapshot().timed_out);
}

void shutdown_does_not_wait_for_read() {
  const auto reader = std::make_shared<gated_reader>();
  const auto values = std::make_shared<completions>();
  const auto token = session();
  const auto running_retired = std::make_shared<signal>();
  const auto queued_retired = std::make_shared<signal>();
  const auto stopped = std::make_shared<signal>();
  auto executor = std::make_unique<asset_executor>(1, 2,
      [reader](const std::filesystem::path& root, std::string_view path) {
        return reader->read(root, path);
      });
  // This guard exists before any assertion can fail with a read in flight.
  struct read_release {
    std::shared_ptr<gated_reader> reader;
    ~read_release() { reader->release_all(); }
  } release{reader};
  REQUIRE(executor->submit({}, "running", token,
                          collect(values, "running", running_retired)).ok);
  REQUIRE(reader->wait_started(1));
  REQUIRE(executor->submit({}, "queued", token,
                          collect(values, "queued", queued_retired)).ok);
  guarded_thread destroyer{
      std::thread([owned = std::move(executor), stopped]() mutable {
        owned.reset();
        stopped->set();
      }), [reader] { reader->release_all(); }};
  REQUIRE(stopped->wait());
  // Destruction returned before the test permits the underlying read to end.
  CHECK(reader->snapshot().active == 1);
  CHECK(reader->snapshot().finished == 0);
  REQUIRE(queued_retired->wait());
  CHECK(values->snapshot().empty());
  reader->release_all();
  destroyer.join();
  REQUIRE(running_retired->wait());
  CHECK(values->snapshot().empty());
  const auto state = reader->snapshot();
  CHECK(state.started == 1);
  CHECK(state.finished == 1);
  CHECK(state.requests.count("queued") == 0);
  CHECK(!state.timed_out);
}

void reader_exceptions_leave_worker_usable() {
  fixture test(1, 2);
  test.reader->throw_for("throws");
  const auto token = session();
  const auto values = std::make_shared<completions>();
  const auto failed_retired = std::make_shared<signal>();
  const auto good_retired = std::make_shared<signal>();
  REQUIRE(test.executor.submit({}, "throws", token,
                              collect(values, "throws", failed_retired)).ok);
  REQUIRE(test.executor.submit({}, "after-throw", token,
                              collect(values, "after-throw", good_retired)).ok);
  test.reader->release_all();
  REQUIRE(values->wait(2));
  REQUIRE(failed_retired->wait());
  REQUIRE(good_retired->wait());
  const auto delivered = values->snapshot();
  REQUIRE(delivered.size() == 2);
  CHECK(delivered[0].name == "throws");
  CHECK(!delivered[0].value.status.ok);
  CHECK(delivered[0].value.status.code == "asset_read_failed");
  CHECK(delivered[0].value.mime.empty());
  CHECK(delivered[0].value.base64.empty());
  CHECK(delivered[0].value.size == 0);
  CHECK(delivered[1].name == "after-throw");
  CHECK(delivered[1].value.status.ok);
  CHECK(test.reader->snapshot().threads.size() == 1);
  CHECK(!test.reader->snapshot().timed_out);
}

void completion_exceptions_release_capacity() {
  fixture test(1, 2);
  const auto token = session();
  const auto values = std::make_shared<completions>();
  const auto throwing_calls = std::make_shared<std::atomic<unsigned>>(0);
  const auto failed_retired = std::make_shared<signal>();
  const auto next_retired = std::make_shared<signal>();
  REQUIRE(test.executor.submit({}, "throws", token,
      [throwing_calls,
       lifetime = std::make_shared<retirement_signal>(failed_retired)](
          asset_result, asset_permit) {
        (void)lifetime;
        ++*throwing_calls;
        throw std::runtime_error("Injected completion failure");
      }).ok);
  REQUIRE(test.executor.submit({}, "after-throw", token,
      collect(values, "after-throw", next_retired)).ok);
  test.reader->release_all();
  REQUIRE(values->wait(1));
  REQUIRE(failed_retired->wait());
  REQUIRE(next_retired->wait());
  CHECK(throwing_calls->load() == 1);
  CHECK(test.reader->snapshot().threads.size() == 1);
  // The normal result still owns one permit. The throwing completion must
  // have released its other permit, allowing another submission.
  const auto final_retired = std::make_shared<signal>();
  REQUIRE(test.executor.submit({}, "still-usable", token,
      collect(values, "still-usable", final_retired)).ok);
  REQUIRE(values->wait(2));
  REQUIRE(final_retired->wait());
  CHECK(test.reader->snapshot().started == 3);
  CHECK(!test.reader->snapshot().timed_out);
}

void cancellation_races_entered_completion() {
  fixture test(1, 2);
  const auto token = session();
  const auto entered = std::make_shared<signal>();
  const auto release = std::make_shared<signal>();
  const auto cancelled = std::make_shared<signal>();
  const auto retired = std::make_shared<signal>();
  const auto callback_count = std::make_shared<std::atomic<unsigned>>(0);
  const auto callback_timeout = std::make_shared<std::atomic<bool>>(false);
  struct callback_release {
    std::shared_ptr<signal> target;
    ~callback_release() { target->set(); }
  } release_on_exit{release};
  REQUIRE(test.executor.submit({}, "racing", token,
      [entered, release, callback_count, callback_timeout,
       lifetime = std::make_shared<retirement_signal>(retired)](
          asset_result, asset_permit) {
        (void)lifetime;
        ++*callback_count;
        entered->set();
        if (!release->wait(gate_watchdog)) callback_timeout->store(true);
      }).ok);
  test.reader->release_all();
  REQUIRE(entered->wait());
  // Cancellation cannot retract a completion already entered, and must not
  // hold a worker/callback lock while waiting for that completion to return.
  guarded_thread canceller{
      std::thread([&executor = test.executor, token, cancelled] {
        executor.cancel(token);
        cancelled->set();
      }), [release] { release->set(); }};
  REQUIRE(cancelled->wait());
  CHECK(token->load());
  CHECK(callback_count->load() == 1);
  const auto rejected = test.executor.submit({}, "same-session", token,
      [](asset_result, asset_permit) {});
  CHECK(!rejected.ok);
  CHECK(rejected.code == "asset_cancelled");
  release->set();
  canceller.join();
  REQUIRE(retired->wait());
  CHECK(callback_count->load() == 1);
  CHECK(!callback_timeout->load());

  const auto values = std::make_shared<completions>();
  const auto next_retired = std::make_shared<signal>();
  REQUIRE(test.executor.submit({}, "new-session", session(),
      collect(values, "new-session", next_retired)).ok);
  REQUIRE(values->wait(1));
  REQUIRE(next_retired->wait());
  CHECK(test.reader->snapshot().requests.count("same-session") == 0);
  CHECK(!test.reader->snapshot().timed_out);
}

void queued_request_owns_input_copies() {
  fixture test(1, 2);
  const auto token = session();
  const auto values = std::make_shared<completions>();
  const auto first_retired = std::make_shared<signal>();
  const auto queued_retired = std::make_shared<signal>();
  REQUIRE(test.executor.submit({}, "blocker", token,
      collect(values, "blocker", first_retired)).ok);
  REQUIRE(test.reader->wait_started(1));
  std::filesystem::path root = "original-root";
  std::string path = "original-path";
  REQUIRE(test.executor.submit(root, path, token,
      collect(values, "copied-input", queued_retired)).ok);
  root = "changed-root";
  path.assign("changed-path");
  test.reader->release_all();
  REQUIRE(values->wait(2));
  REQUIRE(first_retired->wait());
  REQUIRE(queued_retired->wait());
  const auto state = test.reader->snapshot();
  REQUIRE(state.requests.count("original-path") == 1);
  CHECK(state.requests.at("original-path") == "original-root");
  CHECK(state.requests.count("changed-path") == 0);
  CHECK(!state.timed_out);
}

void run(const char* name, void (*test)()) {
  context = name;
  try {
    test();
  } catch (const std::exception& error) {
    ++failures;
    std::cerr << "FAIL [" << context << "]: " << error.what() << '\n';
  } catch (...) {
    ++failures;
    std::cerr << "FAIL [" << context << "]: unexpected non-standard exception\n";
  }
}
}  // namespace

int main() {
  run("two real bounded workers and saturation", bounded_workers_and_saturation);
  run("UI completion lease retains capacity", completion_lease_holds_capacity);
  run("queued session cancellation", queued_cancellation_removes_reader_calls);
  run("inflight cancellation suppresses completion", inflight_cancellation_drops_completion);
  run("nonblocking destruction with detached reader", shutdown_does_not_wait_for_read);
  run("reader exception recovery", reader_exceptions_leave_worker_usable);
  run("completion exception recovery", completion_exceptions_release_capacity);
  run("cancellation racing an entered completion", cancellation_races_entered_completion);
  run("queued input value ownership", queued_request_owns_input_copies);
  std::cout << "Desktop production bounded asset executor: " << checks << " checks, "
            << failures << " failures. Actual worker threads with deterministic gates; "
            << "no native GUI/system WebView verification.\n";
  return failures ? 1 : 0;
}
