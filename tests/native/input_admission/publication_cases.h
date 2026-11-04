#pragma once
#include "../../../sdk/src/host/input_mailbox.h"
#include "../../../sdk/src/host/engine_admission.h"
#include <condition_variable>
#include <stdexcept>
#include <vector>

namespace input_publication_cases {
using namespace soui::sdk;
using detail::input_mailbox;
inline void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
inline input_packet character(std::uint64_t sequence) {
  input_packet result; result.kind = input_kind::character; result.sequence = sequence; return result;
}
inline void drain(input_mailbox& mailbox) {
  for (unsigned turn = 0; turn < 128 && mailbox.pending(); ++turn) mailbox.service();
  check(!mailbox.pending(), "publication receipt fixture did not reach idle");
}
inline void run_receipts(std::atomic<bool>& fail_next_allocation) {
  input_mailbox mailbox([](const input_packet*) {});
  const auto first = mailbox.post_with_notification(character(1));
  const auto second = mailbox.post_with_notification(character(2));
  check(first.result == input_admission_result::accepted && first.wake_required
      && second.result == input_admission_result::accepted && !second.wake_required,
      "publication receipt did not distinguish first and already-pending inputs");
  input_packet cancel; cancel.kind = input_kind::cancel;
  const auto queued_recovery = mailbox.post_with_notification(cancel);
  const auto recovering = mailbox.post_with_notification(character(3));
  check(queued_recovery.result == input_admission_result::accepted && !queued_recovery.wake_required
      && recovering.result == input_admission_result::recovering && !recovering.wake_required,
      "existing pending recovery gained another notification");
  drain(mailbox);
  const auto recovery = mailbox.post_with_notification(cancel);
  check(recovery.result == input_admission_result::accepted && recovery.wake_required,
      "empty cancel publication omitted its notification");
  drain(mailbox);
  mailbox.quiesce();
  const auto quiescing = mailbox.post_with_notification(character(4));
  mailbox.close();
  const auto closed = mailbox.post_with_notification(character(5));
  check(quiescing.result == input_admission_result::stopped && !quiescing.wake_required
      && closed.result == input_admission_result::stopped && !closed.wake_required,
      "stopped receipt reopened input");

  input_mailbox limited([](const input_packet*) {});
  auto text = character(1); text.text.assign(input_mailbox::event_byte_limit / sizeof(char16_t) + 1, u'x');
  const auto oversized = limited.post_with_notification(text);
  check(oversized.result == input_admission_result::capacity && oversized.wake_required && limited.pending(),
      "rejected oversized input did not notify its first recovery publication");
  input_mailbox allocation([](const input_packet*) {});
  text.text.resize(32768);
  fail_next_allocation.store(true);
  const auto no_memory = allocation.post_with_notification(text);
  fail_next_allocation.store(false);
  check(no_memory.result == input_admission_result::no_memory && no_memory.wake_required && allocation.pending(),
      "allocation rejection did not notify its first recovery publication");

  input_mailbox refused_order([](const input_packet*) {},
      [](std::atomic<std::uint64_t>& head, std::atomic<bool>& pending, bool, bool recovery) {
        if (!recovery) return std::uint64_t{0};
        head.store(1); pending.store(true); return std::uint64_t{1};
      });
  const auto stopped_recovery = refused_order.post_with_notification(character(1));
  check(stopped_recovery.result == input_admission_result::stopped && stopped_recovery.wake_required,
      "failed ordinary ordinal omitted its accepted recovery notification");
  input_mailbox rejected_order([](const input_packet*) {},
      [](std::atomic<std::uint64_t>&, std::atomic<bool>&, bool, bool) { return std::uint64_t{0}; });
  const auto stopped = rejected_order.post_with_notification(character(1));
  check(stopped.result == input_admission_result::stopped && !stopped.wake_required && !rejected_order.pending(),
      "failed recovery claimed a publication");
  input_mailbox blocked([](const input_packet*) {});
  auto old_document = character(1); old_document.generation = {1, 2, 3};
  blocked.block_document(old_document.generation);
  const auto denied = blocked.post_with_notification(old_document);
  check(denied.result == input_admission_result::stopped && !denied.wake_required && !blocked.pending(),
      "blocked document claimed a new publication");
}

struct held_callback final {
  std::mutex mutex;
  std::condition_variable changed;
  bool entered{false}, released{false};
  void hold() {
    std::unique_lock<std::mutex> lock(mutex);
    entered = true; changed.notify_all();
    changed.wait(lock, [&] { return released; });
  }
  bool wait_for_entry() {
    std::unique_lock<std::mutex> lock(mutex);
    return changed.wait_for(lock, std::chrono::seconds(5), [&] { return entered; });
  }
  void release() { std::lock_guard<std::mutex> lock(mutex); released = true; changed.notify_all(); }
};
// Release even if allocation/admission fails after the owner enters a test gate.
struct release_guard final {
  std::shared_ptr<held_callback> gate;
  ~release_guard() { gate->release(); }
};
inline std::shared_ptr<input_mailbox> make_mailbox(engine_thread& engine, input_mailbox::handler handler) {
  return std::make_shared<input_mailbox>(std::move(handler),
      [&engine](std::atomic<std::uint64_t>& head, std::atomic<bool>& pending, bool first, bool recovery) {
        return detail::engine_admission::commit_input(engine, head, pending, first, recovery);
      }, [&engine] { return detail::engine_admission::input_cutoff(engine); });
}
inline bool unregister(engine_thread& engine, std::shared_ptr<detail::engine_input_service> service) {
  const bool result = engine.invoke([&engine, service] { detail::engine_admission::unregister_input(engine, service); });
  if (!result) engine.stop(false);
  return result;
}
inline void run_empty_boundary(engine_thread& engine) {
  auto gate = std::make_shared<held_callback>();
  release_guard release{gate};
  auto delivered = std::make_shared<std::atomic<unsigned>>(0);
  auto mailbox = make_mailbox(engine, [gate, delivered](const input_packet* packet) {
    if (packet) { ++*delivered; if (packet->sequence == 1) gate->hold(); }
  });
  check(detail::engine_admission::register_input(engine, mailbox), "empty boundary registration");
  const auto first = mailbox->post_with_notification(character(1));
  if (first.wake_required) detail::engine_admission::notify_input(engine);
  const bool entered = gate->wait_for_entry();
  // The last slot was popped and empty published before the callback. P2 must
  // own a new wake even while P1's callback is still running on the Engine.
  const bool empty = !mailbox->pending();
  const auto second = mailbox->post_with_notification(character(2));
  if (second.wake_required) detail::engine_admission::notify_input(engine);
  gate->release();
  const bool drained = engine.drain_for(std::chrono::seconds(5));
  const bool removed = unregister(engine, mailbox);
  detail::engine_admission::notify_input(engine); // Harmless late wake after close/unregister.
  const auto stopped = mailbox->post_with_notification(character(3));
  check(first.result == input_admission_result::accepted && first.wake_required && entered && empty
      && second.result == input_admission_result::accepted && second.wake_required
      && drained && removed && *delivered == 2 && !mailbox->pending()
      && stopped.result == input_admission_result::stopped && !stopped.wake_required,
      "publication after the last pop was lost or closed input reopened");
}
inline void run_rejected_recovery(engine_thread& engine) {
  auto recovered = std::make_shared<std::atomic<unsigned>>(0);
  auto delivered = std::make_shared<std::atomic<unsigned>>(0);
  auto mailbox = make_mailbox(engine, [recovered, delivered](const input_packet* packet) {
    if (packet) ++*delivered; else ++*recovered;
  });
  check(detail::engine_admission::register_input(engine, mailbox), "rejected recovery registration");
  auto oversized = character(1);
  oversized.text.assign(input_mailbox::event_byte_limit / sizeof(char16_t) + 1, u'x');
  const auto receipt = mailbox->post_with_notification(oversized);
  if (receipt.wake_required) detail::engine_admission::notify_input(engine);
  const bool drained = engine.drain_for(std::chrono::seconds(5));
  const bool removed = unregister(engine, mailbox);
  check(receipt.result == input_admission_result::capacity && receipt.wake_required
      && drained && removed && *recovered == 1 && *delivered == 0,
      "rejected first publication failed to wake actual Engine recovery");
}
inline void run_deadline_continuation(engine_thread& engine) {
  struct one_item_service final : detail::engine_input_service {
    std::shared_ptr<input_mailbox> mailbox;
    std::atomic<unsigned> calls{0};
    bool pending() const noexcept override { return mailbox->pending(); }
    std::uint64_t oldest_order() const noexcept override { return mailbox->oldest_order(); }
    void service() noexcept override { service_before_until(0, {}); }
    void service_before_until(std::uint64_t cutoff, std::chrono::steady_clock::time_point) noexcept override {
      ++calls;
      mailbox->service_before_until(cutoff, (std::chrono::steady_clock::time_point::min)());
    }
    void quiesce() noexcept override { mailbox->quiesce(); }
    void close() noexcept override { mailbox->close(); }
  };
  auto seen = std::make_shared<std::vector<std::uint64_t>>(); seen->reserve(32);
  auto mailbox = make_mailbox(engine, [seen](const input_packet* packet) { if (packet) seen->push_back(packet->sequence); });
  auto service = std::make_shared<one_item_service>(); service->mailbox = mailbox;
  check(detail::engine_admission::register_input(engine, service), "deadline publication registration");
  auto gate = std::make_shared<held_callback>(); release_guard release{gate};
  const bool held = engine.post([gate] { gate->hold(); });
  const bool entered = gate->wait_for_entry();
  unsigned notifications = 0, admitted = 0;
  for (unsigned sequence = 1; sequence <= 32; ++sequence) {
    auto packet = character(sequence); packet.kind = input_kind::pointer_move;
    const auto receipt = mailbox->post_with_notification(packet);
    if (receipt.result == input_admission_result::accepted) ++admitted;
    if (receipt.wake_required) { ++notifications; detail::engine_admission::notify_input(engine); }
  }
  gate->release();
  // No further producer, task or notification may be needed for the other
  // expired-deadline turns. drain_for observes; it does not notify the Engine.
  const bool drained = engine.drain_for(std::chrono::seconds(5));
  const bool removed = unregister(engine, service);
  bool ordered = seen->size() == 32;
  for (std::size_t index = 0; index < seen->size(); ++index) ordered = ordered && (*seen)[index] == index + 1;
  check(held && entered && admitted == 32 && notifications == 1 && drained && removed
      && ordered && service->calls == 32 && !mailbox->pending(),
      "one publication wake did not preserve every expired-deadline continuation");
}
inline void run_engine(engine_thread& engine) {
  run_empty_boundary(engine);
  run_rejected_recovery(engine);
  run_deadline_continuation(engine);
}
} // namespace input_publication_cases
