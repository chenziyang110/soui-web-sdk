#pragma once
#include "../../../sdk/src/host/input_mailbox.h"
#include <stdexcept>
namespace input_terminal_cases {
using namespace soui::sdk;
inline void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
inline void run(std::atomic<bool>& fail_next_allocation) {
  namespace trace = diagnostics::input_frame_trace;
  struct no_independent final : independent_input_sink {
    bool submit_wheel(const input_packet&, scroll_sample&) override { return false; }
    bool capture_scroll_barrier(const input_packet&, scroll_sample&) override { return false; }
  } independent;
  struct recovery_sink final : engine_input_sink {
    detail::input_mailbox mailbox{[](const input_packet*) {}};
    std::uint64_t marker_sequence{123};
    bool post_input(const input_packet& packet) override {
      marker_sequence = packet.sequence; return mailbox.post(packet) == input_admission_result::accepted;
    }
  } engine;
  interaction_router router; router.activate({1, 2, 3});
  input_packet original; original.kind = input_kind::ime_commit; original.generation = {1, 2, 3};
  original.sequence = 900001; original.text.assign(128, u'x');
  fail_next_allocation = true;
  const auto rejected = router.route(original, independent, engine);
  fail_next_allocation = false;
  check(rejected.admission == input_admission_result::no_memory && engine.marker_sequence == 0,
      "router copy rejection gave internal recovery a second physical input identity");
  for (unsigned index = 0; index < 4 && engine.mailbox.pending(); ++index) engine.mailbox.service();
  check(!engine.mailbox.pending(), "anonymous recovery failed to drain");
  check(trace::record(detail::input_event(original, trace::stage::input_terminal, trace::reason::capacity,
      trace::terminal::rejected, 1)) == trace::record_result::recorded, "original rejected terminal publication");
  {
    detail::input_mailbox exception([](const input_packet* value) { if (value) throw std::runtime_error("handler failure"); }, {}, {}, 1);
    auto input = original; input.text.clear(); input.sequence = 900002;
    check(exception.post(input) == input_admission_result::accepted, "exception case admission");
    for (unsigned index = 0; index < 4 && exception.pending(); ++index) exception.service();
    check(!exception.pending(), "exception recovery did not drain");
  }
  {
    detail::input_mailbox closing([](const input_packet*) {}, {}, {}, 1);
    auto input = original; input.text.clear(); input.sequence = 900003;
    check(closing.post(input) == input_admission_result::accepted, "close case admission"); closing.close();
  }
  {
    detail::input_mailbox explicit_cancel([](const input_packet*) {}, {}, {}, 1);
    auto input = original; input.text.clear(); input.kind = input_kind::cancel; input.sequence = 900004;
    check(explicit_cancel.post(input) == input_admission_result::accepted, "explicit cancel admission");
    explicit_cancel.service();
  }
  std::array<unsigned, 4> terminals{};
  std::array<trace::event, 256> records{};
  for (unsigned batch = 0; batch < 32; ++batch) {
    const auto drained = trace::process_recorder().drain(records.data(), records.size());
    check(!drained.busy, "single source consumer unexpectedly busy");
    for (std::size_t index = 0; index < drained.count; ++index) {
      const auto& value = records[index];
      if (value.point == trace::stage::input_terminal && value.input_first >= 900001 && value.input_first <= 900004)
        ++terminals[static_cast<std::size_t>(value.input_first - 900001)];
    }
    if (!drained.count) break;
  }
  for (auto count : terminals) check(count == 1, "physical input terminal missing or duplicated by recovery");
}
} // namespace input_terminal_cases
