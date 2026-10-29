#include "../../sdk/src/host/input_admission.h"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <vector>

using namespace std::chrono_literals;
using namespace soui::sdk;
using namespace soui::sdk::detail;

namespace {
void require(bool passed, const char* message) {
  if (!passed) {
    std::cerr << "input-admission.failed: " << message << '\n';
    ::ExitProcess(91); // Never unwind captures while a real owner is blocked.
  }
}
class blocked_engine final {
 public:
  blocked_engine() {
    require(engine.start([] { return true; }), "Engine startup");
    require(engine.post([this] {
      std::unique_lock<std::mutex> lock(mutex_);
      entered_ = true; condition_.notify_all();
      require(condition_.wait_for(lock, 8s, [this] { return released_; }), "owner release deadline");
    }), "blocker accepted");
    std::unique_lock<std::mutex> lock(mutex_);
    require(condition_.wait_for(lock, 2s, [this] { return entered_; }), "actual owner blocked");
  }
  ~blocked_engine() { release(); engine.stop(); }
  void release() { { std::lock_guard<std::mutex> lock(mutex_); released_ = true; } condition_.notify_all(); }
  void finish() { release(); require(engine.drain_for(5s), "actual queue drain"); engine.stop(); }
  engine_thread engine;
 private:
  std::mutex mutex_;
  std::condition_variable condition_;
  bool entered_{false}, released_{false};
};

input_packet packet(input_kind kind, unsigned key = 0, std::uint64_t document = 1,
    std::uint64_t geometry = 1) {
  input_packet result;
  result.kind = kind; result.key_code = key; result.generation = {1, document, geometry};
  return result;
}
void accepted(engine_admission_result result) {
  require(result == engine_admission_result::accepted, "expected accepted input");
}
void fill(engine_thread& engine) {
  while (engine_admission::post(engine, engine_lane::ordinary, [] {}) == engine_admission_result::accepted) {}
  const auto state = engine_admission::observe(engine);
  require(state.queued[0] + state.reserved[0] == engine_admission::ordinary_limit, "ordinary full with held endings");
  require(state.queued[1] == 0 && state.queued[2] == 0 && state.queued[3] == 0
      && state.reserved[1] == 0 && state.reserved[2] == 0 && state.reserved[3] == 0,
      "input never borrows other lanes");
}

void endings_survive_full_fifo() {
  input_admission input;
  blocked_engine owner;
  std::vector<unsigned> order;
  const auto post = [&](input_kind kind, unsigned marker, unsigned key = 0) {
    return input.post(owner.engine, packet(kind, key), [&, marker] { order.push_back(marker); });
  };
  accepted(post(input_kind::focus, 1));
  accepted(post(input_kind::key_down, 2, 65));
  accepted(post(input_kind::pointer_down, 3));
  accepted(post(input_kind::ime_start, 4));
  fill(owner.engine);
  accepted(post(input_kind::key_up, 5, 65));
  accepted(post(input_kind::pointer_up, 6));
  accepted(post(input_kind::ime_commit, 7));
  accepted(post(input_kind::ime_end, 8));
  accepted(post(input_kind::blur, 9));
  require(engine_admission::observe(owner.engine).reserved[0] == 0, "all ending leases consumed");
  owner.finish();
  require(order == std::vector<unsigned>{1,2,3,4,5,6,7,8,9}, "original starts/endings keep one FIFO order");
}

void repeat_geometry_and_invalid_packets() {
  input_admission input;
  blocked_engine owner;
  accepted(input.post(owner.engine, packet(input_kind::key_down, 65), [] {}));
  const auto reserved = engine_admission::observe(owner.engine).reserved[0];
  require(reserved == 2, "first down holds up plus session blur");
  accepted(input.post(owner.engine, packet(input_kind::key_down, 65), [] {}));
  require(engine_admission::observe(owner.engine).reserved[0] == reserved, "repeat does not reserve more");
  require(input.post(owner.engine, packet(input_kind::key_up, 999), [] {}) == engine_admission_result::invalid,
      "invalid key rejected without changing accepted gestures");
  require(engine_admission::observe(owner.engine).reserved[0] == reserved, "invalid packet retains old leases");
  fill(owner.engine);
  require(input.post(owner.engine, packet(input_kind::key_down, 65), [] {}) == engine_admission_result::full,
      "repeat full is reported");
  accepted(input.post(owner.engine, packet(input_kind::key_up, 65, 1, 99), [] {}));
  accepted(input.post(owner.engine, packet(input_kind::blur), [] {}));
  owner.finish();
}

void failed_start_rolls_back() {
  input_admission input;
  blocked_engine owner;
  for (unsigned i = 0; i != 510; ++i) require(owner.engine.post([] {}), "fill leaving only two credits");
  require(input.post(owner.engine, packet(input_kind::key_down, 65), [] {}) == engine_admission_result::full,
      "start needs completion credits plus its own FIFO slot");
  const auto state = engine_admission::observe(owner.engine);
  require(state.queued[0] == 510 && state.reserved[0] == 0, "new ending reservations roll back together");
  owner.finish();
}

void blur_retires_session() {
  input_admission input;
  blocked_engine owner;
  for (unsigned i = 0; i != 80; ++i) {
    accepted(input.post(owner.engine, packet(input_kind::focus), [] {}));
    accepted(input.post(owner.engine, packet(input_kind::key_down, 65), [] {}));
    accepted(input.post(owner.engine, packet(input_kind::blur), [] {}));
    require(engine_admission::observe(owner.engine).reserved[0] == 0,
        "accepted blur retires key leases when key-up belongs to another window");
  }
  fill(owner.engine);
  require(input.post(owner.engine, packet(input_kind::key_up, 65), [] {}) == engine_admission_result::full,
      "orphan release has no invented guarantee");
  owner.finish();
}

void ime_multiple_results_and_cancel() {
  input_admission input;
  blocked_engine owner;
  unsigned commits = 0;
  accepted(input.post(owner.engine, packet(input_kind::ime_start), [] {}));
  require(engine_admission::observe(owner.engine).reserved[0] == 3, "IME first result/end and session blur held");
  fill(owner.engine);
  accepted(input.post(owner.engine, packet(input_kind::ime_commit), [&] { ++commits; }));
  require(input.post(owner.engine, packet(input_kind::ime_commit), [&] { ++commits; }) == engine_admission_result::full,
      "additional result reports real full instead of claiming guaranteed delivery");
  accepted(input.post(owner.engine, packet(input_kind::ime_end), [] {}));
  accepted(input.post(owner.engine, packet(input_kind::blur), [] {}));
  owner.finish();
  require(commits == 1, "only accepted result executes");

  blocked_engine cancelled;
  accepted(input.post(cancelled.engine, packet(input_kind::ime_start), [] {}));
  accepted(input.post(cancelled.engine, packet(input_kind::ime_end), [] {}));
  require(engine_admission::observe(cancelled.engine).reserved[0] == 1, "end without result refunds unused result credit");
  accepted(input.post(cancelled.engine, packet(input_kind::blur), [] {}));
  cancelled.finish();
}

void generations_and_guarded_epoch() {
  input_admission input;
  blocked_engine owner;
  accepted(input.post(owner.engine, packet(input_kind::pointer_down), [] {}));
  accepted(input.post(owner.engine, packet(input_kind::character, 0, 2), [] {}));
  require(engine_admission::observe(owner.engine).reserved[0] == 0, "document change retires old gestures");
  input.reset();
  accepted(input.post(owner.engine, packet(input_kind::key_down, 66, 2), [] {}));
  require(engine_admission::observe(owner.engine).reserved[0] == 2, "new document has fresh ending credits");
  input.reset();
  require(engine_admission::observe(owner.engine).reserved[0] == 0, "unmount/reset refunds all held credits");
  owner.finish();

  engine_thread engine;
  require(engine.start([] { return true; }), "epoch Engine start");
  engine_reply_ticket stale;
  accepted(engine_admission::reserve_reply(engine, engine_lane::ordinary, stale));
  engine.stop();
  require(engine.start([] { return true; }), "epoch Engine restart");
  engine_thread::task retained = [] {};
  const engine_reply_ticket* guard[]{&stale};
  require(engine_admission::try_post_guarded(engine, retained, guard, 1) == engine_admission_result::invalid
      && static_cast<bool>(retained), "start cannot cross epoch while keeping an old ending credit");
  engine.stop();
}

struct reentrant_retirement final {
  reentrant_retirement(input_admission& owner, unsigned& count) : input(owner), retired(count) {}
  input_admission& input;
  unsigned& retired;
  ~reentrant_retirement() { input.reset(); ++retired; }
};
void rejection_and_callback_reentry() {
  input_admission input;
  blocked_engine owner;
  for (unsigned i = 0; i != 510; ++i) require(owner.engine.post([] {}), "full start precondition");
  unsigned retired = 0;
  auto capture = std::make_shared<reentrant_retirement>(input, retired);
  engine_thread::task work = [capture] {};
  capture.reset();
  require(input.post(owner.engine, packet(input_kind::key_down, 65), std::move(work)) == engine_admission_result::full,
      "reentrant captured start refused");
  require(retired == 1 && engine_admission::observe(owner.engine).reserved[0] == 0,
      "rejected capture reset executes outside input/Engine locks");
  owner.finish();

  blocked_engine callback;
  accepted(input.post(callback.engine, packet(input_kind::key_down, 65), [&] { input.reset(); }));
  callback.finish();
  require(engine_admission::observe(callback.engine).reserved[0] == 0, "accepted callback may reset without lock inversion");
}

void multiple_hosts_and_view_change() {
  input_admission first, second;
  blocked_engine owner;
  accepted(first.post(owner.engine, packet(input_kind::key_down, 65), [] {}));
  accepted(second.post(owner.engine, packet(input_kind::key_down, 66), [] {}));
  require(engine_admission::observe(owner.engine).reserved[0] == 4, "hosts hold independent endings in one ordinary ledger");
  fill(owner.engine);
  accepted(first.post(owner.engine, packet(input_kind::key_up, 65), [] {}));
  accepted(second.post(owner.engine, packet(input_kind::key_up, 66), [] {}));
  accepted(first.post(owner.engine, packet(input_kind::blur), [] {}));
  accepted(second.post(owner.engine, packet(input_kind::blur), [] {}));
  owner.finish();

  blocked_engine recreated;
  accepted(first.post(recreated.engine, packet(input_kind::pointer_down), [] {}));
  auto new_view = packet(input_kind::character);
  new_view.generation.view = 2;
  accepted(first.post(recreated.engine, new_view, [] {}));
  require(engine_admission::observe(recreated.engine).reserved[0] == 0, "view change retires old completion leases");
  recreated.finish();
}
} // namespace

int main() {
  endings_survive_full_fifo();
  repeat_geometry_and_invalid_packets();
  failed_start_rolls_back();
  blur_retires_session();
  ime_multiple_results_and_cancel();
  generations_and_guarded_epoch();
  rejection_and_callback_reentry();
  multiple_hosts_and_view_change();
  std::cout << "input-admission: real FIFO/held endings/reentry checks completed\n";
  return 0;
}
