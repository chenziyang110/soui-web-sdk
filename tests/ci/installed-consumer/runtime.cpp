#include <soui/diagnostics/runtime_metrics.h>
#include <soui/diagnostics/view_observation_abi.h>
#include <soui/runtime/core/task/task_store.h>
#include <soui/runtime/host/session/webview_session.h>
#include <soui/runtime/services/permissions/permission_store.h>

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace diagnostics = soui::sdk::diagnostics;
static_assert(WKE_VIEW_DIAGNOSTICS_ABI_VERSION == 1U);
static_assert(std::is_standard_layout_v<wkeViewLifetimeSnapshot>);
static_assert(sizeof(wkeViewLifetimeSnapshot) == 104);
static_assert(offsetof(wkeViewLifetimeSnapshot, processId) == 16);
static_assert(offsetof(wkeViewLifetimeSnapshot, engineFinalizedQpc) == 96);
static_assert(diagnostics::view_observation::schema_version == 1);
static_assert(std::is_same_v<decltype(diagnostics::view_observation{}.native),
                             wkeViewLifetimeSnapshot>);

// Compiled/linked on every matrix target. Only the explicitly selected native
// Linux/macOS job may run this memory-only Runtime contract; it creates no UI,
// network endpoint, user data file or symbolic link. Token generation uses the
// Runtime's existing OS secure-entropy dependency.
int main() {
  try {
    soui::runtime::permission_store permissions;
    const auto token = permissions.issue("ci-consumer", "app://ci-consumer", {"app.read"});
    if (!permissions.authorize("ci-consumer", "app://ci-consumer", token, "app.read"))
      throw std::runtime_error("installed permission authorization failed");
    permissions.revoke_session("ci-consumer");
    if (permissions.authorize("ci-consumer", "app://ci-consumer", token, "app.read"))
      throw std::runtime_error("installed permission revocation failed");
    soui::runtime::task_store tasks;
    soui::runtime::task_store_request_quota_snapshot quota{};
    if (tasks.read_request_quota(quota) !=
        soui::runtime::task_store_request_quota_status::available)
      throw std::runtime_error("installed task quota unavailable");
    diagnostics::permissions metrics{};
    if (permissions.read_runtime_metrics(metrics) != diagnostics::status::disabled)
      throw std::runtime_error("default installed metrics must remain disabled");
    soui::runtime::webview_session session;
    if (session.terminal() || !session.close() || !session.terminal())
      throw std::runtime_error("installed Runtime lifecycle failed");
    std::cout << "Installed Runtime memory contract passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
