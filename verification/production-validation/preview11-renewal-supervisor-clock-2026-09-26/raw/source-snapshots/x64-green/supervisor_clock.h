#pragma once
#include <cstdint>
#include <utility>
namespace renewal_probe {
template<class Observation> struct clock_sample {
  Observation observed;
  std::uint64_t now;
};
template<class State> auto sample_clock(State& state) {
  auto observed = state.snapshot();
  // Sample the monotonic clock after the locked state snapshot. Callback
  // timestamps in that snapshot cannot then be newer than this sample.
  const auto now = state.now();
  return clock_sample<decltype(observed)>{std::move(observed), now};
}
}
