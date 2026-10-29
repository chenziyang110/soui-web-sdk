#pragma once
#include "../../../sdk/src/host/input_mailbox.h"
#include "../../../sdk/src/host/engine_admission.h"
#include <condition_variable>
#include <stdexcept>
namespace input_wake_cases {
using namespace soui::sdk;
inline void run(engine_thread& engine) {
  std::mutex mutex; std::condition_variable changed;
  unsigned delivered = 0; bool empty_ack = false;
  bool owner_held = false, release_owner = false;
  std::atomic<bool> observe_empty{false};
  auto mailbox = std::make_shared<detail::input_mailbox>([&](const input_packet* packet) {
    if (packet) { std::lock_guard<std::mutex> lock(mutex); ++delivered; changed.notify_all(); }
  }, [&engine](std::atomic<std::uint64_t>& head, std::atomic<bool>& pending, bool first, bool recovery) {
    return detail::engine_admission::commit_input(engine, head, pending, first, recovery);
  }, [&engine] { return detail::engine_admission::input_cutoff(engine); });
  if (!detail::engine_admission::register_input(engine, mailbox) || !engine.invoke([&] {
    (void)engine.set_owner_opportunity([&] {
      if (observe_empty.load() && !mailbox->pending()) {
        std::lock_guard<std::mutex> lock(mutex); empty_ack = true; changed.notify_all();
      }
    });
  })) throw std::runtime_error("late wake source setup");
  const bool held_admitted = engine.post([&] {
    std::unique_lock<std::mutex> lock(mutex); owner_held = true; changed.notify_all();
    changed.wait(lock, [&] { return release_owner; });
  });
  bool held = false;
  { std::unique_lock<std::mutex> lock(mutex); held = changed.wait_for(lock, std::chrono::seconds(5), [&] { return owner_held; }); }
  input_packet input; input.kind = input_kind::character; input.sequence = 1;
  const bool first_admitted = mailbox->post(input) == input_admission_result::accepted;
  const bool p1_observed_pending = mailbox->pending(); // P1 pauses before its notification.
  const bool other_opportunity = engine.post([] {}); // Actual owner drains P1 via another opportunity.
  { std::lock_guard<std::mutex> lock(mutex); release_owner = true; changed.notify_all(); }
  bool first_drained = false, acknowledged = false, second_drained = false;
  {
    std::unique_lock<std::mutex> lock(mutex);
    first_drained = changed.wait_for(lock, std::chrono::seconds(5), [&] { return delivered == 1; });
  }
  observe_empty = true;
  if (p1_observed_pending) detail::engine_admission::notify_input(engine); // Delayed P1 wake arrives after data drained.
  {
    std::unique_lock<std::mutex> lock(mutex);
    acknowledged = changed.wait_for(lock, std::chrono::seconds(5), [&] { return empty_ack; });
  }
  input.sequence = 2;
  const bool second_admitted = mailbox->post(input) == input_admission_result::accepted;
  if (mailbox->pending()) detail::engine_admission::notify_input(engine); // P2 owns its ordinary native event notification.
  {
    std::unique_lock<std::mutex> lock(mutex);
    second_drained = changed.wait_for(lock, std::chrono::seconds(5), [&] { return delivered == 2; });
  }
  const bool closed = engine.invoke([&engine, mailbox] {
    (void)engine.set_owner_opportunity({}); detail::engine_admission::unregister_input(engine, mailbox);
  });
  if (!closed) engine.stop(false); // Never leave a callback borrowing the source case's stack after a failed fence.
  if (!(held_admitted && held && first_admitted && p1_observed_pending && other_opportunity && first_drained && acknowledged && second_admitted && second_drained && closed))
    throw std::runtime_error("delayed producer wake poisoned subsequent input progress");
}
} // namespace input_wake_cases
