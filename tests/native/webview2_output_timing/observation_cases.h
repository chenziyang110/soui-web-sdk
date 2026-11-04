#pragma once
#include <array>
#include <memory>
#include <string>

namespace observation_fixture {
namespace input_trace = soui::sdk::diagnostics::input_frame_trace;
namespace output_timing = soui::sdk::detail::webview2_output_timing;
// Production initialization and cancellation helpers are projected. State,
// thread identity and note_output are fakes; no Host queue or COM call runs.
struct state {
  bool trace_outputs{false}, prepare_output{false};
  unsigned ui_thread{17};
  struct output { std::string raw; output_timing::metadata timing; };
};
inline unsigned thread_queries{}, current_thread{17};
inline unsigned GetCurrentThreadId() noexcept { ++thread_queries; return current_thread; }

#include "output-timing-initialization.inc"

inline void run_initialization() {
  const auto owner = std::make_shared<state>();
  state::output output;
  output.raw.assign(4096, 'x');
  const output_timing::metadata unchanged{output_timing::origin::event_sink,
      output_timing::executor::other, output_timing::preparation::cached, 64};
  output.timing = unchanged;
  thread_queries = 0;
  for (const bool prepare : {false, true}) {
    owner->prepare_output = prepare;
    for (const auto origin : {output_timing::origin::bridge_reply,
        output_timing::origin::admission_rejection, output_timing::origin::event_sink}) {
      initialize_output_timing(owner, output, origin);
      require(!thread_queries && output_timing::encode(output.timing) == output_timing::encode(unchanged),
          "disabled output timing queried executor or changed metadata");
    }
  }
  owner->trace_outputs = true;
  unsigned expected_queries = 0;
  for (const bool prepare : {false, true}) {
    owner->prepare_output = prepare;
    for (const auto origin : {output_timing::origin::bridge_reply,
        output_timing::origin::admission_rejection, output_timing::origin::event_sink}) {
      for (const bool on_owner : {false, true}) {
        current_thread = on_owner ? owner->ui_thread : owner->ui_thread + 1;
        initialize_output_timing(owner, output, origin);
        const auto executor = on_owner ? output_timing::executor::owner :
            origin == output_timing::origin::bridge_reply ? output_timing::executor::bridge_worker : output_timing::executor::other;
        require(thread_queries == ++expected_queries && output.timing.source == origin &&
            output.timing.thread == executor && output.timing.prepared == (prepare ?
                output_timing::preparation::limit_fallback : output_timing::preparation::disabled) &&
            output.timing.raw_bytes == output.raw.size(), "enabled output timing metadata changed");
      }
    }
  }
}
struct observed {
  const state* owner{};
  const state::output* output{};
  input_trace::stage point{};
  input_trace::reason why{};
};
inline std::array<observed, 16> notes{};
inline unsigned count{};
inline bool overflow{};
inline void note_output(const std::shared_ptr<state>& owner, const state::output& output,
    input_trace::stage point, input_trace::reason why) noexcept {
  if (count == notes.size()) { overflow = true; return; }
  notes[count++] = {owner.get(), &output, point, why};
}

#include "output-observation.inc"

inline void run() {
  const auto owner = std::make_shared<state>();
  const state::output first{}, second{};
  const auto verify = [&](unsigned expected, const state::output& output, input_trace::reason why) {
    require(!overflow && count == expected, "production observation terminal count differs");
    const auto& note = notes[expected - 1];
    require(note.owner == owner.get() && note.output == &output &&
        note.point == input_trace::stage::task_cancelled && note.why == why,
        "production observation identity or reason changed");
  };
  count = 0; overflow = false;
  try {
    output_observation guard{owner, first};
    throw std::runtime_error("explicit fake serialization boundary failure");
  } catch (const std::runtime_error&) {}
  verify(1, first, input_trace::reason::invalid);
  {
    output_observation guard{owner, second};
    guard.cancel(input_trace::reason::stale_generation);
    guard.cancel(input_trace::reason::stopped);
  }
  verify(2, second, input_trace::reason::stale_generation);
  try {
    output_observation guard{owner, first};
    guard.cancel(input_trace::reason::post_failed);
    throw std::runtime_error("explicit fake post HRESULT check failure");
  } catch (const std::runtime_error&) {}
  verify(3, first, input_trace::reason::post_failed);
  {
    output_observation guard{owner, second};
    guard.finished = true; // Same success dismissal used after production COM.
    guard.cancel(input_trace::reason::invalid);
  }
  require(count == 3 && !overflow, "successful observation invented cancellation");
  {
    output_observation guard{owner, second};
  }
  verify(4, second, input_trace::reason::invalid);
  {
    output_observation guard{owner, first};
    guard.cancel(input_trace::reason::stopped);
  }
  verify(5, first, input_trace::reason::stopped);
}
} // namespace observation_fixture
