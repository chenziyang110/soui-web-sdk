#pragma once

#include <soui/diagnostics/view_observation_abi.h>
#include <cstdint>
#include <cstring>

namespace soui::sdk::diagnostics::detail {
[[nodiscard]] inline bool view_api_supported(const wkeViewDiagnosticsApi* api) noexcept {
  return api && api->structSize == sizeof(*api) &&
      api->abiVersion == WKE_VIEW_DIAGNOSTICS_ABI_VERSION &&
      api->capabilities == WKE_VIEW_DIAGNOSTICS_CAP_DLL_LIFETIMES && api->readViewLifetimes;
}

// Runtime lock serializes this binding; it never owns or calls the DLL.
class view_observation_binding final {
 public:
  [[nodiscard]] bool observe(const wkeViewLifetimeSnapshot& value,
      std::uint64_t pid, std::uint64_t birth) noexcept {
    constexpr std::uint32_t required = WKE_RENDER_DIAGNOSTICS_IDENTITY_VALID |
        WKE_RENDER_DIAGNOSTICS_CLOCK_VALID | WKE_RENDER_DIAGNOSTICS_ENGINE_INITIALIZED;
    constexpr std::uint32_t faults = WKE_RENDER_DIAGNOSTICS_COUNTER_FAULT |
        WKE_RENDER_DIAGNOSTICS_IDENTITY_MISMATCH | WKE_RENDER_DIAGNOSTICS_CLOCK_FAILURE;
    constexpr std::uint32_t known = required | faults | WKE_RENDER_DIAGNOSTICS_ENGINE_FINALIZED;
    const bool finalized = (value.flags & WKE_RENDER_DIAGNOSTICS_ENGINE_FINALIZED) != 0;
    if (invalid_ || value.structSize != sizeof(value) ||
        value.abiVersion != WKE_VIEW_DIAGNOSTICS_ABI_VERSION ||
        (value.status != WKE_RENDER_DIAGNOSTICS_READY && value.status != WKE_RENDER_DIAGNOSTICS_CLOSED) ||
        (value.flags & required) != required || (value.flags & faults) || (value.flags & ~known) ||
        !pid || !birth || value.processId != pid || value.processCreationFileTime != birth ||
        !value.engineOwnerThreadId || !value.qpcFrequency || !value.recordingEpoch ||
        !value.mutationSequence || value.viewsDestroyedTotal > value.viewsCreatedTotal ||
        value.viewsLive != value.viewsCreatedTotal - value.viewsDestroyedTotal ||
        value.lastMutationQpc < value.recordingEpoch ||
        finalized != (value.status == WKE_RENDER_DIAGNOSTICS_CLOSED) ||
        (finalized && (value.viewsLive || value.engineFinalizedQpc != value.lastMutationQpc)) ||
        (!finalized && value.engineFinalizedQpc)) return reject();
    if (bound_ && (value.processId != previous_.processId ||
        value.processCreationFileTime != previous_.processCreationFileTime ||
        value.engineOwnerThreadId != previous_.engineOwnerThreadId ||
        value.qpcFrequency != previous_.qpcFrequency || value.recordingEpoch != previous_.recordingEpoch ||
        value.mutationSequence < previous_.mutationSequence ||
        value.viewsCreatedTotal < previous_.viewsCreatedTotal ||
        value.viewsDestroyedTotal < previous_.viewsDestroyedTotal ||
        value.lastMutationQpc < previous_.lastMutationQpc ||
        ((previous_.flags & WKE_RENDER_DIAGNOSTICS_ENGINE_FINALIZED) &&
          std::memcmp(&value, &previous_, sizeof(value)) != 0) ||
        (value.mutationSequence == previous_.mutationSequence &&
          std::memcmp(&value, &previous_, sizeof(value)) != 0))) return reject();
    previous_ = value;
    bound_ = true;
    return true;
  }
  [[nodiscard]] bool retain_terminal(const wkeViewLifetimeSnapshot& value, bool retired) noexcept {
    if (terminal_recorded_) return true;
    if (!bound_ || invalid_ || !retired || value.status != WKE_RENDER_DIAGNOSTICS_CLOSED ||
        std::memcmp(&value, &previous_, sizeof(value)) != 0) return false;
    terminal_ = value;
    terminal_recorded_ = true;
    return true;
  }
  [[nodiscard]] bool invalid() const noexcept { return invalid_; }
  [[nodiscard]] bool terminal_recorded() const noexcept { return terminal_recorded_; }
  [[nodiscard]] const wkeViewLifetimeSnapshot& terminal() const noexcept { return terminal_; }
 private:
  bool reject() noexcept { invalid_ = true; return false; }
  bool bound_{false}, invalid_{false}, terminal_recorded_{false};
  wkeViewLifetimeSnapshot previous_{}, terminal_{};
};
} // namespace soui::sdk::diagnostics::detail
