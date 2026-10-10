#include <soui/host/engine_thread.h>
#include "engine_admission.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using soui::sdk::engine_thread;
using soui::sdk::engine_thread_state;
using soui::sdk::detail::engine_admission;
using soui::sdk::detail::engine_admission_result;
using soui::sdk::detail::engine_lane;
using soui::sdk::detail::engine_reply_ticket;

namespace {
[[noreturn]] void failed(const char* message) {
  std::cerr << "engine-capacity.failed " << message << std::endl;
  // A failed assertion must not unwind a still-blocked real Engine owner.
  ExitProcess(91);
}
void require(bool value, const char* message) { if (!value) failed(message); }
void accepted(engine_admission_result value) {
  require(value == engine_admission_result::accepted, "unexpected admission rejection");
}

class blocked_engine final {
 public:
  explicit blocked_engine(soui::sdk::diagnostics::mode mode = soui::sdk::diagnostics::mode::disabled)
      : engine(mode) {
    require(engine.start([] { return true; }), "real Engine startup");
    require(engine.post([this] {
      std::unique_lock<std::mutex> lock(mutex_);
      started_ = true;
      condition_.notify_all();
      if (!condition_.wait_for(lock, 8s, [this] { return released_; }))
        failed("real executing callback release deadline");
    }), "real blocking callback admission");
    std::unique_lock<std::mutex> lock(mutex_);
    require(condition_.wait_for(lock, 2s, [this] { return started_; }), "real callback started");
  }
  ~blocked_engine() { release(); engine.stop(); }
  void release() {
    { std::lock_guard<std::mutex> lock(mutex_); released_ = true; }
    condition_.notify_all();
  }
  void finish() { release(); require(engine.drain_for(5s), "real queue drained"); engine.stop(); }
  engine_thread engine;
 private:
  std::mutex mutex_;
  std::condition_variable condition_;
  bool started_{false};
  bool released_{false};
};

soui::sdk::diagnostics::queue read_queue(engine_thread& engine) {
  soui::sdk::diagnostics::queue out;
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  for (;;) {
    const auto state = engine.read_runtime_metrics(out);
    if (state == soui::sdk::diagnostics::status::ready) return out;
    require(state == soui::sdk::diagnostics::status::busy &&
            std::chrono::steady_clock::now() < deadline, "real scalar queue getter");
    std::this_thread::yield();
  }
}

void ordinary_limit() {
  blocked_engine owner(soui::sdk::diagnostics::mode::scalar);
  std::atomic<unsigned> calls{0};
  for (std::size_t i = 0; i < engine_admission::ordinary_limit; ++i)
    require(owner.engine.post([&] { ++calls; }), "ordinary available credit");
  require(!owner.engine.post([&] { ++calls; }), "ordinary 513 must be refused");
  const auto full = engine_admission::observe(owner.engine);
  require(full.queued[0] == 512 && full.executing == 1 && calls == 0,
          "512 queued plus actual executing owner, no refused execution");
  require(read_queue(owner.engine).full_rejections_total == 1, "one generic ordinary full attempt counted");
  require(engine_admission::post(owner.engine, static_cast<engine_lane>(4), [] {}) == engine_admission_result::invalid &&
          engine_admission::post(owner.engine, engine_lane::ordinary, {}) == engine_admission_result::invalid,
          "invalid post remains distinct from full");
  require(read_queue(owner.engine).full_rejections_total == 1, "invalid post never counted as full");
  owner.finish();
  require(calls == 512, "only accepted ordinary callbacks ran");
  require(!owner.engine.post([] {}), "stopped post rejected");
  const auto stopped = read_queue(owner.engine);
  require(stopped.full_rejections_total == 1 && stopped.stopped_rejections_total == 1,
          "stopped post does not increment full");
}

void all_lanes_fifo() {
  blocked_engine owner(soui::sdk::diagnostics::mode::scalar);
  std::vector<unsigned> order;
  order.reserve(engine_admission::queued_limit);
  unsigned sequence = 0;
  const engine_lane lanes[]{engine_lane::ordinary, engine_lane::control,
                            engine_lane::renewal, engine_lane::cleanup};
  const std::size_t counts[]{512, 32, 4, 32};
  for (unsigned lane = 0; lane != 4; ++lane) {
    for (std::size_t i = 0; i != counts[lane]; ++i) {
      const auto expected = sequence++;
      accepted(engine_admission::post(owner.engine, lanes[lane], [&, expected] { order.push_back(expected); }));
    }
    require(engine_admission::post(owner.engine, lanes[lane], [] {}) == engine_admission_result::full,
            "each lane has its frozen bound");
  }
  const auto full = engine_admission::observe(owner.engine);
  require(full.queued == std::array<std::size_t, 4>{512, 32, 4, 32} && full.executing == 1,
          "580 real queued plus one actual callback");
  require(read_queue(owner.engine).full_rejections_total == 4, "four actual lane-full post attempts counted once each");
  owner.finish();
  require(order.size() == 580, "all accepted work ran");
  for (unsigned i = 0; i != order.size(); ++i) require(order[i] == i, "one FIFO, no cleanup priority");
}

void reply_reservation() {
  blocked_engine owner(soui::sdk::diagnostics::mode::scalar);
  std::vector<engine_reply_ticket> tickets(engine_admission::ordinary_limit);
  for (auto& ticket : tickets) accepted(engine_admission::reserve_reply(owner.engine, engine_lane::ordinary, ticket));
  engine_reply_ticket rejected;
  std::atomic<unsigned> business{0}, replies{0};
  const auto request = [&] {
    if (engine_admission::reserve_reply(owner.engine, engine_lane::ordinary, rejected)
        != engine_admission_result::accepted) return false;
    ++business;
    return true;
  };
  require(!request() && business == 0, "no business before reply capacity is reserved");
  require(!owner.engine.post([] {}), "reservation blocks ordinary displacement");
  require(read_queue(owner.engine).full_rejections_total == 2, "one full reserve and one full post are distinct attempts");
  require(engine_admission::reserve_reply(owner.engine, engine_lane::ordinary, tickets[1]) == engine_admission_result::invalid &&
          engine_admission::reserve_reply(owner.engine, engine_lane::cleanup, rejected) == engine_admission_result::invalid,
          "active ticket and cleanup reply reservation rejected as invalid");
  require(read_queue(owner.engine).full_rejections_total == 2, "invalid reservations do not increment full");
  engine_reply_ticket moved(std::move(tickets[0]));
  require(moved.active() && !tickets[0].active(), "move transfers one non-authority credit");
  accepted(engine_admission::submit_reply(owner.engine, moved, [&] { ++replies; }));
  require(!moved.active(), "commit consumes ticket once");
  require(engine_admission::submit_reply(owner.engine, moved, [&] { ++replies; }) == engine_admission_result::invalid,
          "double commit refused");
  require(read_queue(owner.engine).full_rejections_total == 2, "successful and double-ticket submission are not full attempts");
  for (std::size_t i = 1; i != tickets.size(); ++i)
    accepted(engine_admission::submit_reply(owner.engine, tickets[i], [&] { ++replies; }));
  owner.finish();
  require(replies == 512 && business == 0, "each pre-reserved reply exactly once; refused business zero");
}

void ticket_refund_generation() {
  engine_reply_ticket surviving;
  {
    engine_thread engine(soui::sdk::diagnostics::mode::scalar);
    require(engine.start([] { return true; }), "ticket Engine startup");
    accepted(engine_admission::reserve_reply(engine, engine_lane::renewal, surviving));
    const auto generation = engine_admission::observe(engine).generation;
    {
      engine_reply_ticket temporary;
      accepted(engine_admission::reserve_reply(engine, engine_lane::renewal, temporary));
      require(engine_admission::observe(engine).reserved[2] == 2, "two actual renewal reservations");
    }
    require(engine_admission::observe(engine).reserved[2] == 1, "destruction refunds exactly one");
    engine.stop();
    require(engine.start([] { return true; }), "same Engine restart");
    require(engine_admission::observe(engine).generation > generation, "new startup generation");
    require(engine_admission::submit_reply(engine, surviving, [] {}) == engine_admission_result::invalid,
            "old generation does not revive");
    require(engine_admission::observe(engine).reserved[2] == 0, "old ledger cannot debit new run");
    require(read_queue(engine).full_rejections_total == 0, "stale ticket is not counted as full");
    engine.stop();
    engine_reply_ticket stopped;
    require(engine_admission::reserve_reply(engine, engine_lane::ordinary, stopped) == engine_admission_result::stopped &&
            read_queue(engine).full_rejections_total == 0, "stopped reservation is not counted as full");
  }
  require(surviving.active(), "old ticket is only an old capacity ledger after Engine death");
  surviving = engine_reply_ticket{};
  require(!surviving.active(), "safe refund after actual Engine destruction");
}

struct reenter_on_retire final {
  engine_thread& engine;
  std::atomic<unsigned>& observed;
  std::atomic<unsigned>& unexpected;
  reenter_on_retire(engine_thread& owner, std::atomic<unsigned>& seen, std::atomic<unsigned>& wrong)
      : engine(owner), observed(seen), unexpected(wrong) {}
  ~reenter_on_retire() {
    const auto state = engine.state();
    if (state != engine_thread_state::running && state != engine_thread_state::stopping) ++unexpected;
    ++observed;
  }
};

void rejection_retirement() {
  blocked_engine owner;
  for (unsigned i = 0; i != 512; ++i) require(owner.engine.post([] {}), "fill rejection queue");
  std::atomic<unsigned> observed{0}, unexpected{0};
  engine_thread::task rejected = [capture = std::make_shared<reenter_on_retire>(
      owner.engine, observed, unexpected)] {};
  const auto before = observed.load();
  require(!owner.engine.post(std::move(rejected)), "full rejection");
  require(observed == before + 1 && unexpected == 0, "rejected last capture reenters Engine outside lock");
  owner.finish();
}

void stop_retirement() {
  blocked_engine owner;
  std::atomic<unsigned> observed{0}, unexpected{0}, ran{0};
  for (unsigned i = 0; i != 32; ++i) {
    auto capture = std::make_shared<reenter_on_retire>(owner.engine, observed, unexpected);
    require(owner.engine.post([capture, &ran] { ++ran; }), "discard capture admission");
  }
  const auto before = observed.load();
  std::atomic<bool> stopped{false};
  std::thread stopper([&] { owner.engine.stop(false); stopped = true; });
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (observed.load() != before + 32 && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
  require(observed == before + 32 && unexpected == 0 && ran == 0 && !stopped,
          "discard captures retire before real worker join and outside lock");
  owner.release();
  stopper.join();
  require(stopped && owner.engine.state() == engine_thread_state::stopped, "real stop joins executing owner");
}

void executing_retirement() {
  engine_thread engine;
  require(engine.start([] { return true; }), "capture-retirement Engine startup");
  std::mutex mutex;
  std::condition_variable changed;
  bool destructor_started = false, release = false;
  struct retirement final {
    engine_thread& engine; std::mutex& mutex; std::condition_variable& changed;
    bool& started; bool& release;
    ~retirement() {
      require(engine.state() == engine_thread_state::running, "executing capture may reenter Engine");
      std::unique_lock<std::mutex> lock(mutex);
      started = true; changed.notify_all();
      if (!changed.wait_for(lock, 5s, [&] { return release; })) failed("capture retirement release");
    }
  };
  // Direct aggregate construction avoids a temporary with its own destructor.
  std::shared_ptr<retirement> capture(new retirement{engine, mutex, changed, destructor_started, release});
  require(engine.post([hold = std::move(capture)] {}), "capture retirement actual work");
  {
    std::unique_lock<std::mutex> lock(mutex);
    require(changed.wait_for(lock, 2s, [&] { return destructor_started; }), "actual capture destructor entered");
  }
  require(!engine.drain_for(10ms) && engine_admission::observe(engine).executing == 1,
          "drain cannot finish before capture owner retires");
  { std::lock_guard<std::mutex> lock(mutex); release = true; }
  changed.notify_all();
  require(engine.drain_for(2s), "capture retirement completes before quiet");
  engine.stop();
}

void invoke_timeout_and_metrics() {
  blocked_engine owner(soui::sdk::diagnostics::mode::scalar);
  std::atomic<unsigned> called{0};
  require(!owner.engine.invoke([&] { ++called; }, 1ms), "accepted invoke may time out");
  require(called == 0, "timeout is not early execution");
  soui::sdk::diagnostics::queue metrics;
  require(owner.engine.read_runtime_metrics(metrics) == soui::sdk::diagnostics::status::ready &&
          metrics.has_capacity_bound && metrics.capacity_bound == 580 && metrics.queued == 1 && metrics.executing == 1,
          "existing scalar getter observes real bounded queue");
  owner.finish();
  require(called == 1, "timeout never cancels accepted work");
  require(!owner.engine.post([] {}), "stopped remains rejected");
  blocked_engine disabled;
  for (std::size_t i = 0; i != engine_admission::ordinary_limit; ++i)
    require(disabled.engine.post([] {}), "default-off actual capacity fill");
  engine_reply_ticket refused;
  require(!disabled.engine.post([] {}) &&
          engine_admission::reserve_reply(disabled.engine, engine_lane::ordinary, refused) == engine_admission_result::full,
          "default-off actual post and reserve still enforce capacity");
  metrics.capacity_bound = metrics.full_rejections_total = metrics.posted_total = 999;
  require(disabled.engine.read_runtime_metrics(metrics) == soui::sdk::diagnostics::status::disabled &&
          metrics.capacity_bound == 0 && !metrics.has_capacity_bound && metrics.full_rejections_total == 0 &&
          metrics.posted_total == 0 && metrics.queued == 0 && metrics.executing == 0 &&
          metrics.stopped_rejections_total == 0 && metrics.observation.owner_epoch == 0,
          "default-off getter clears full and every actual queue scalar");
  disabled.finish();
}
} // namespace

int main(int argc, char** argv) {
  require(argc == 2, "one fixed case argument");
  const std::string selected(argv[1]);
  if (selected == "ordinary-limit") ordinary_limit();
  else if (selected == "all-lanes-fifo") all_lanes_fifo();
  else if (selected == "reply-reservation") reply_reservation();
  else if (selected == "ticket-refund-generation") ticket_refund_generation();
  else if (selected == "rejection-retirement") rejection_retirement();
  else if (selected == "stop-retirement") stop_retirement();
  else if (selected == "executing-retirement") executing_retirement();
  else if (selected == "invoke-timeout-metrics") invoke_timeout_and_metrics();
  else failed("unknown fixed case");
  std::cout << "engine-capacity.passed " << selected << std::endl;
  return 0;
}
