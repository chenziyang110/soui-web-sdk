#pragma once

// Regression SOURCE only in this package. The specialist runs these against
// the actual Engine FIFO and mailbox; no copied admission algorithm is used.
#include "../../../sdk/src/host/input_mailbox.h"
#include "../../../sdk/src/host/engine_admission.h"
#include "../../../sdk/src/host/input_sequence.h"
#include <condition_variable>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace input_ordinal_cases {
using namespace soui::sdk;
inline void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
inline input_packet edge(std::uint64_t sequence) {
  input_packet result; result.kind = input_kind::character; result.sequence = sequence; return result;
}
inline detail::input_mailbox::order_committer committer(engine_thread& engine) {
  return [&engine](std::atomic<std::uint64_t>& head, std::atomic<bool>& pending, bool first, bool recovery) {
    return detail::engine_admission::commit_input(engine, head, pending, first, recovery);
  };
}
inline detail::input_mailbox::cutoff_reader cutoff(engine_thread& engine) {
  return [&engine] { return detail::engine_admission::input_cutoff(engine); };
}
inline void run(engine_thread& engine) {
  constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
  std::atomic<std::uint64_t> sequence{maximum - 1};
  check(detail::claim_input_sequence(sequence) == maximum && detail::claim_input_sequence(sequence) == 0
      && sequence.load() == maximum, "input sequence wrapped/reused at exhaustion");
  static_assert(detail::input_order_precedes(maximum, maximum), "MAX recovery must precede MAX cleanup");
  static_assert(!detail::input_order_precedes(2, 1) && detail::input_order_precedes(1, 2), "normal exclusive fence");
  static_assert(!detail::input_mailbox::valid_native_text_bytes(-1)
      && !detail::input_mailbox::valid_native_text_bytes(-2)
      && detail::input_mailbox::valid_native_text_bytes(0)
      && !detail::input_mailbox::valid_native_text_bytes(3)
      && !detail::input_mailbox::valid_native_text_bytes(65538), "IMM error/size must reject, empty must remain valid");
  std::vector<std::uint64_t> observed; observed.reserve(128);
  {
    std::mutex mutex; std::condition_variable changed;
    bool owner_waiting = false, release_owner = false, published = false, release_producer = false, entered = false;
    std::atomic<bool> fifo_ran{false};
    check(engine.post([&] {
      std::unique_lock<std::mutex> lock(mutex); owner_waiting = true; changed.notify_all();
      changed.wait(lock, [&] { return release_owner; });
    }), "owner gate admission");
    { std::unique_lock<std::mutex> lock(mutex); check(changed.wait_for(lock, std::chrono::seconds(5), [&] { return owner_waiting; }), "owner gate timeout"); }
    auto mailbox = std::make_shared<detail::input_mailbox>([&](const input_packet* value) {
      observed.push_back(value ? value->sequence : 0);
    }, [&](std::atomic<std::uint64_t>& head, std::atomic<bool>& pending, bool first, bool recovery) {
      const auto order = detail::engine_admission::commit_input(engine, head, pending, first, recovery);
      // Deliberately pause AFTER assignment/publication, BEFORE mailbox count
      // mutation and before returning to the producer. FIFO must stay fenced.
      std::unique_lock<std::mutex> lock(mutex); published = true; changed.notify_all();
      changed.wait(lock, [&] { return release_producer; }); return order;
    }, cutoff(engine));
    struct watched_service final : detail::engine_input_service {
      std::shared_ptr<detail::input_mailbox> mailbox; std::function<void()> on_entry;
      bool pending() const noexcept override { return mailbox->pending(); }
      std::uint64_t oldest_order() const noexcept override { return mailbox->oldest_order(); }
      void service() noexcept override { service_before(0); }
      void service_before(std::uint64_t order) noexcept override { on_entry(); mailbox->service_before(order); }
      void quiesce() noexcept override { mailbox->quiesce(); }
      void close() noexcept override { mailbox->close(); }
    };
    auto service = std::make_shared<watched_service>(); service->mailbox = mailbox;
    service->on_entry = [&] { std::lock_guard<std::mutex> lock(mutex); entered = true; changed.notify_all(); };
    check(detail::engine_admission::register_input(engine, service), "watched mailbox registration");
    input_admission_result accepted = input_admission_result::stopped;
    std::thread producer([&] { accepted = mailbox->post(edge(1)); });
    bool publication_seen = false;
    { std::unique_lock<std::mutex> lock(mutex); publication_seen = changed.wait_for(lock, std::chrono::seconds(5), [&] { return published; }); }
    const bool head_published = mailbox->pending() && mailbox->oldest_order() != 0;
    const bool fifo_accepted = engine.post([&] { observed.push_back(2); fifo_ran = true; });
    bool service_entered = false, fifo_stayed_fenced = false;
    {
      std::unique_lock<std::mutex> lock(mutex); release_owner = true; changed.notify_all();
      service_entered = changed.wait_for(lock, std::chrono::seconds(5), [&] { return entered; });
      fifo_stayed_fenced = !fifo_ran.load(); release_producer = true; changed.notify_all();
    }
    producer.join();
    detail::engine_admission::notify_input(engine);
    const bool drained = engine.drain_for(std::chrono::seconds(5));
    check(engine.invoke([&] { detail::engine_admission::unregister_input(engine, service); }), "watched mailbox unregister");
    check(publication_seen && head_published && fifo_accepted && service_entered && fifo_stayed_fenced && drained
        && accepted == input_admission_result::accepted && observed == std::vector<std::uint64_t>({1, 2}), "first-head publication gap");
  }
  observed.clear();
  {
    std::shared_ptr<detail::input_mailbox> mailbox;
    std::atomic<bool> nested_admitted{true};
    mailbox = std::make_shared<detail::input_mailbox>([&](const input_packet* value) {
      if (!value) { observed.push_back(0); return; }
      observed.push_back(value->sequence);
      if (value->sequence == 1) {
        nested_admitted = engine.post([&] { observed.push_back(2); })
            && mailbox->post(edge(3)) == input_admission_result::accepted;
      }
    }, committer(engine), cutoff(engine));
    check(detail::engine_admission::register_input(engine, mailbox), "batch mailbox registration");
    check(mailbox->post(edge(1)) == input_admission_result::accepted, "first input admission");
    detail::engine_admission::notify_input(engine);
    const bool drained = engine.drain_for(std::chrono::seconds(5));
    check(engine.invoke([&] { detail::engine_admission::unregister_input(engine, mailbox); }), "batch mailbox unregister");
    check(drained && nested_admitted && observed == std::vector<std::uint64_t>({1, 2, 3}), "FIFO appended during no-cutoff batch was bypassed");
  }
  observed.clear();
  {
    // Deterministically exercise >one actor batch before resize, then newer
    // input after resize; the fence must work in both directions.
    interaction_generation applied{1, 2, 3}, ui_latest = applied;
    check(engine.invoke([&] {
      auto mailbox = std::make_shared<detail::input_mailbox>([&](const input_packet* value) {
        observed.push_back(value ? (input_generation_applies(value->kind, value->generation, applied) ? value->sequence : 999) : 0);
      }, committer(engine), cutoff(engine));
      check(detail::engine_admission::register_input(engine, mailbox), "resize mailbox registration");
      for (unsigned index = 1; index <= 10; ++index) {
        auto input = edge(index); input.kind = input_kind::pointer_down; input.generation = applied;
        check(mailbox->post(input) == input_admission_result::accepted, "older edge admission");
      }
      ++ui_latest.geometry; // UI has published intent; old accepted edges remain applicable.
      check(engine.post([&, geometry = ui_latest.geometry] { applied.geometry = geometry; observed.push_back(20); }), "resize FIFO admission");
      for (unsigned index = 21; index <= 25; ++index) {
        auto input = edge(index); input.kind = input_kind::pointer_up; input.generation = ui_latest;
        check(mailbox->post(input) == input_admission_result::accepted, "newer edge admission");
      }
      check(engine.post([&, mailbox] { detail::engine_admission::unregister_input(engine, mailbox); }), "close FIFO admission");
    }), "resize setup owner call");
    check(engine.drain_for(std::chrono::seconds(5)), "resize order drain");
    const std::vector<std::uint64_t> expected{1,2,3,4,5,6,7,8,9,10,20,21,22,23,24,25};
    check(observed == expected, "resize/close crossed accepted input ordinal");
  }
  observed.clear();
  {
    detail::input_mailbox mailbox([&](const input_packet* value) { observed.push_back(value ? value->sequence : 0); },
      [](std::atomic<std::uint64_t>& head, std::atomic<bool>& pending, bool first, bool recovery) {
        const auto order = recovery ? maximum : 1;
        if (first) head.store(order, std::memory_order_release); pending.store(true, std::memory_order_release); return order;
      });
    check(mailbox.post(edge(1)) == input_admission_result::accepted, "MAX case older input");
    auto cancel = edge(2); cancel.kind = input_kind::cancel;
    check(mailbox.post(cancel) == input_admission_result::accepted, "MAX recovery admission");
    for (unsigned index = 0; index < 4 && mailbox.pending(); ++index) mailbox.service_before(maximum);
    check(!mailbox.pending() && observed == std::vector<std::uint64_t>({1, 0}), "MAX cleanup excluded recovery marker");
    detail::input_mailbox rejected([](const input_packet*) {},
      [](std::atomic<std::uint64_t>&, std::atomic<bool>&, bool, bool) { return std::uint64_t{0}; });
    check(rejected.post(edge(3)) == input_admission_result::stopped && !rejected.pending(), "failed ordinal accepted hidden payload");
    rejected.close();
  }
}
} // namespace input_ordinal_cases
