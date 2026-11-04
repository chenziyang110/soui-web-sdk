#pragma once

#include <soui/host/webview_host.h>
#include <functional>

namespace soui::acceptance {
// The caller supplies its existing until/service and original deadline.
// Only pending is retried. Non-pending failures remain the original result;
// deadline exceptions propagate and cannot manufacture a completed step.
template <class Until, class Attempt>
soui::sdk::host_result poll_runtime_shutdown(Until&& until, Attempt&& attempt) {
  soui::sdk::host_result result{false, "E_RUNTIME_SHUTDOWN_PENDING", "Shutdown has not completed."};
  std::invoke(until, [&] {
    result = std::invoke(attempt);
    return result.ok || result.code != "E_RUNTIME_SHUTDOWN_PENDING";
  });
  return result;
}
} // namespace soui::acceptance
