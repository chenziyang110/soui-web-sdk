#include "../../sdk/src/host/main_context_gate.h"

#include <atomic>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
using gate = soui::sdk::detail::main_context_gate<>;
using lookup = soui::sdk::detail::context_lookup_revision<>;

void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

void legacy_and_initial_mount() {
  std::atomic<std::uint64_t> state{0};
  require(gate::accepts(state, 0), "legacy starts inactive");
  const auto pending = gate::invalidate(state);
  require(pending == 1 && !gate::accepts(state, 0) && !gate::accepts(state, pending),
          "controlled mount must reject inactive and pending access");
  require(gate::complete(state, pending) && gate::accepts(state, 2), "own context completes");
}

void nested_context_pending() {
  std::atomic<std::uint64_t> state{0};
  const auto outer = gate::invalidate(state);
  const auto inner = gate::invalidate(state);
  require(inner > outer && !gate::complete(state, outer) && gate::pending(state, inner),
          "outer ACK cannot clear a nested pending context");
}

void nested_context_completed() {
  std::atomic<std::uint64_t> state{0};
  const auto outer = gate::invalidate(state);
  const auto inner = gate::invalidate(state);
  require(gate::complete(state, inner), "inner context completes");
  require(!gate::complete(state, outer) && gate::accepts(state, inner + 1),
          "outer ACK cannot overwrite a completed nested context");
  require(!gate::accepts(state, outer + 1), "completed receipts do not have boolean ABA");
}

void reload_revokes_completed_context() {
  std::atomic<std::uint64_t> state{0};
  const auto context = gate::invalidate(state);
  require(gate::complete(state, context), "first context completes");
  const auto old_access = gate::capture(state);
  const auto reload = gate::invalidate(state);
  require(gate::pending(state, reload) && !gate::accepts(state, old_access),
          "accepted reload revokes old admission before a new ACK");
  require(!gate::complete(state, context), "old callback cannot complete reload");
}

void failed_ack_requires_a_fresh_context() {
  std::atomic<std::uint64_t> state{0};
  const auto failed = gate::invalidate(state);
  // Failed opaque ACKs do not call complete. Read-only geometry/pump
  // observations cannot change or authorize the pending production state.
  for (unsigned i = 0; i < 8; ++i)
    require(gate::capture(state) == failed && !gate::accepts(state, failed), "reads cannot complete an ACK");
  const auto fresh = gate::invalidate(state);
  require(!gate::complete(state, failed) && gate::complete(state, fresh), "fresh actual context may recover");
}

void failed_final_observation_revokes_only_itself() {
  std::atomic<std::uint64_t> state{0};
  const auto first = gate::invalidate(state);
  require(gate::complete(state, first), "first ACK completes");
  require(gate::revoke_completed(state, first + 1) && !gate::accepts(state, first + 1),
          "failed final observation returns own context to pending");
  const auto fresh = gate::invalidate(state);
  require(gate::complete(state, fresh), "fresh context completes");
  require(!gate::revoke_completed(state, first + 1) && gate::accepts(state, fresh + 1),
          "old failure cannot revoke a newer completed context");
}

void overflow_is_sticky() {
  using bounded = soui::sdk::detail::main_context_gate<7>;
  std::atomic<std::uint64_t> state{0};
  for (unsigned i = 0; i < 3; ++i) {
    const auto pending = bounded::invalidate(state);
    require(pending && bounded::complete(state, pending), "receipts before ceiling complete");
  }
  require(!bounded::invalidate(state) && bounded::capture(state) == 7, "ceiling fails closed");
  require(!bounded::invalidate(state) && !bounded::complete(state, 7) &&
          !bounded::accepts(state, 0) && !bounded::accepts(state, 6), "no wrap or revival at ceiling");
}

void invalid_completion_receipts() {
  std::atomic<std::uint64_t> state{0};
  const auto pending = gate::invalidate(state);
  require(!gate::complete(state, 0) && !gate::complete(state, pending + 1) &&
          !gate::complete(state, (std::numeric_limits<std::uint64_t>::max)()) &&
          gate::pending(state, pending), "invalid ACKs preserve the pending owner");
}

void one_exact_completion_wins() {
  std::atomic<std::uint64_t> state{0};
  const auto pending = gate::invalidate(state);
  std::atomic<unsigned> completed{0};
  std::thread first([&] { if (gate::complete(state, pending)) ++completed; });
  // If creating another OS thread throws, the first must still be joined.
  try {
    std::thread second([&] { if (gate::complete(state, pending)) ++completed; });
    second.join();
  } catch (...) { first.join(); throw; }
  first.join();
  require(completed == 1 && gate::accepts(state, pending + 1), "exact CAS completes at most once");
}

void resumed_outer_after_nested_completion() {
  std::atomic<std::uint64_t> state{0};
  const auto outer = gate::invalidate(state);
  std::promise<void> resume;
  auto resumed = resume.get_future();
  std::atomic<bool> outer_completed{true};
  std::thread old([&] { resumed.wait(); outer_completed = gate::complete(state, outer); });
  const auto inner = gate::invalidate(state);
  const bool inner_completed = gate::complete(state, inner);
  resume.set_value();
  old.join();
  require(inner_completed && !outer_completed && gate::accepts(state, inner + 1),
          "a late outer callback cannot clear a new context across threads");
}

void stale_async_receipt_after_new_completion() {
  std::atomic<std::uint64_t> state{0};
  const auto initial = gate::invalidate(state);
  require(gate::complete(state, initial), "initial context completes");
  const auto queued_receipt = gate::capture(state);
  const auto replacement = gate::invalidate(state);
  require(gate::complete(state, replacement) && !gate::accepts(state, queued_receipt),
          "queued work and replies keep their old receipt after new completion");
}

void first_lookup_window_cannot_adopt_new_receipt() {
  std::atomic<std::uint64_t> revision{0};
  std::atomic<std::uint64_t> state{0};
  const auto before_lookup = lookup::capture(revision);
  require(lookup::advance(revision), "context changes before Host selection");
  const auto newer = gate::invalidate(state);
  require(gate::complete(state, newer), "nested context completes before lookup returns");
  require(!lookup::matches(revision, before_lookup),
          "even a newly completed Host receipt cannot validate the old first lookup");
  // This exercises the actual guard, not jsGetWebView or a fabricated Host.
}

void second_getter_window_keeps_original_receipt() {
  std::atomic<std::uint64_t> state{0};
  const auto first = gate::invalidate(state);
  require(gate::complete(state, first), "first context completes");
  const auto before_getter = gate::capture(state);
  const auto replacement = gate::invalidate(state);
  require(gate::complete(state, replacement) && !gate::accepts(state, before_getter),
          "returning the same view cannot repair the stale second-getter receipt");
}

void lookup_exhaustion_survives_host_teardown() {
  using bounded_lookup = soui::sdk::detail::context_lookup_revision<3>;
  std::atomic<std::uint64_t> revision{0};
  std::atomic<std::uint64_t> state{0};
  require(bounded_lookup::advance(revision) && bounded_lookup::advance(revision), "lookup before ceiling");
  const auto old_lookup = bounded_lookup::capture(revision);
  require(!bounded_lookup::advance(revision) && !bounded_lookup::matches(revision, old_lookup), "lookup ceiling rejects");
  gate::reset_after_teardown(state);
  require(gate::accepts(state, 0) && !bounded_lookup::advance(revision) &&
          !bounded_lookup::matches(revision, bounded_lookup::capture(revision)),
          "Host reset does not reset the process lookup revision");
}
}  // namespace

int main() {
  struct test { const char* name; void (*run)(); };
  const test cases[] = {
      {"legacy/initial-mount", legacy_and_initial_mount},
      {"nested-pending", nested_context_pending},
      {"nested-completed-ABA", nested_context_completed},
      {"reload-revocation", reload_revokes_completed_context},
      {"failed-ACK", failed_ack_requires_a_fresh_context},
      {"failed-final-observation", failed_final_observation_revokes_only_itself},
      {"receipt-overflow", overflow_is_sticky},
      {"invalid-ACK", invalid_completion_receipts},
      {"exact-CAS-once", one_exact_completion_wins},
      {"late-threaded-outer", resumed_outer_after_nested_completion},
      {"stale-async-receipt", stale_async_receipt_after_new_completion},
      {"first-lookup-window", first_lookup_window_cannot_adopt_new_receipt},
      {"second-getter-window", second_getter_window_keeps_original_receipt},
      {"lookup-overflow-after-teardown", lookup_exhaustion_survives_host_teardown}};
  for (const auto& item : cases) {
    try { item.run(); std::cout << "PASS " << item.name << '\n'; }
    catch (const std::exception& error) { std::cerr << "FAIL " << item.name << ": " << error.what() << '\n'; return 1; }
  }
  return 0;
}
