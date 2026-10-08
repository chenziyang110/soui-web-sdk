#pragma once
#include <cstdint>
#include <utility>
namespace renewal_probe {
template<class Observation> struct clock_sample {
  Observation observed;
  std::uint64_t now;
};
template<class State> auto sample_clock(State& state) {
  const auto now = state.now();
  auto observed = state.snapshot();
  return clock_sample<decltype(observed)>{std::move(observed), now};
}
}
