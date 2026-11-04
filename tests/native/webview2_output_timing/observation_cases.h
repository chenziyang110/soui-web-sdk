#pragma once
#include <array>
#include <memory>

namespace observation_fixture {
namespace input_trace = soui::sdk::diagnostics::input_frame_trace;
// Only the production guard is projected. These identities and note_output
// are explicit fakes: no Host queue, lease, recorder, serializer, or COM call.
struct state { struct output {}; };
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
