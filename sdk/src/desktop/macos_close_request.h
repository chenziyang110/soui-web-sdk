#pragma once

#include <atomic>
#include <memory>

namespace soui::desktop::detail {
// Native title-bar and WebKit script close requests share host approval. The
// state and callback survive approval that clears native handles/events. Missing
// handlers and exceptions never turn a request into a forced native shutdown.
template <typename State>
void request_macos_native_close(std::shared_ptr<State> state, bool sender_matches) noexcept {
  if (!state || !sender_matches || !state->accepting_work.load(std::memory_order_acquire)) return;
  try {
    const auto callback = state->events.close_requested;
    if (callback) callback();
  } catch (...) {}
}
}  // namespace soui::desktop::detail
