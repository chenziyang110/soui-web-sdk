#pragma once

#include <soui/host/webview_host.h>
#include <algorithm>
#include <chrono>
#include <functional>

namespace soui::sdk::detail {

// Shared by the real application wait and deterministic deadline/error tests.
// Wait services a bounded message batch; it must not reset this total budget.
template <class Attempt, class Wait, class Clock>
host_result wait_runtime_shutdown(std::chrono::milliseconds timeout,
                                  Attempt&& attempt, Wait&& wait, Clock&& now) {
  using namespace std::chrono;
  if (timeout.count() < 0)
    return {false, "E_RUNTIME_SHUTDOWN_ARGUMENT", "Shutdown timeout must not be negative."};
  const auto started = std::invoke(now);
  auto result = std::invoke(attempt); // A zero budget still permits one non-blocking observation.
  while (!result.ok && result.code == "E_RUNTIME_SHUTDOWN_PENDING") {
    const auto elapsed = duration_cast<milliseconds>(std::invoke(now) - started);
    if (elapsed >= timeout)
      return {false, "E_RUNTIME_SHUTDOWN_TIMEOUT", "Process runtime shutdown exceeded the caller's close budget."};
    const auto waited = std::invoke(wait, std::min(milliseconds{10}, timeout - elapsed));
    if (!waited.ok) return waited;
    if (duration_cast<milliseconds>(std::invoke(now) - started) >= timeout)
      return {false, "E_RUNTIME_SHUTDOWN_TIMEOUT", "Process runtime shutdown exceeded the caller's close budget."};
    result = std::invoke(attempt);
  }
  return result; // Preserve unsupported, wrong-owner and other actual shutdown failures.
}

} // namespace soui::sdk::detail
