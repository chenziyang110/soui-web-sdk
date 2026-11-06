#pragma once

#include <cstdint>
#include <string_view>

namespace soui::sdk::detail {

// Host response bookkeeping only; this value never authorizes a Bridge call.
// unknown is reserved for callers which have not parsed the request method.
enum class miniblink_request_route : std::uint8_t { unknown, ordinary, bootstrap, probe };

[[nodiscard]] constexpr miniblink_request_route classify_miniblink_request_route(
    std::string_view method) noexcept {
  return method == "bridge.bootstrap" ? miniblink_request_route::bootstrap
      : method == "bridge.probe.report" ? miniblink_request_route::probe
      : miniblink_request_route::ordinary;
}

} // namespace soui::sdk::detail
