#pragma once

#include <soui/diagnostics/renderer_observation_abi.h>
#include <cstdint>
#include <cstring>

namespace soui::sdk::diagnostics::detail {
[[nodiscard]] inline bool renderer_api_supported(const wkeRendererDiagnosticsApi* api) noexcept {
  constexpr auto required = WKE_RENDER_DIAGNOSTICS_CAP_PROCESS_CACHE | WKE_RENDER_DIAGNOSTICS_CAP_COMPLETED_DRAW_CALLS;
  return api && api->structSize == sizeof(*api) && api->abiVersion == WKE_RENDER_DIAGNOSTICS_ABI_VERSION &&
      api->capabilities == required && api->readProcessSnapshot;
}
// Called only under the SDK Runtime mutex. This does not own or call the DLL.
class renderer_observation_binding {
 public:
  [[nodiscard]] bool observe(const wkeRendererProcessSnapshot& value,
                             std::uint64_t pid, std::uint64_t creation) noexcept {
    constexpr std::uint32_t required = WKE_RENDER_DIAGNOSTICS_IDENTITY_VALID |
        WKE_RENDER_DIAGNOSTICS_CLOCK_VALID | WKE_RENDER_DIAGNOSTICS_ENGINE_INITIALIZED;
    constexpr std::uint32_t faults = WKE_RENDER_DIAGNOSTICS_COUNTER_FAULT |
        WKE_RENDER_DIAGNOSTICS_IDENTITY_MISMATCH | WKE_RENDER_DIAGNOSTICS_CLOCK_FAILURE;
    if (invalid_ || value.structSize != sizeof(value) ||
        value.abiVersion != WKE_RENDER_DIAGNOSTICS_ABI_VERSION || value.reserved != 0 ||
        (value.status != WKE_RENDER_DIAGNOSTICS_READY && value.status != WKE_RENDER_DIAGNOSTICS_CLOSED) ||
        (value.flags & required) != required || (value.flags & faults) != 0 ||
        (value.flags & ~0x1ffU) != 0 || value.processId != pid || pid == 0 ||
        value.processCreationFileTime != creation || creation == 0 ||
        value.engineOwnerThreadId == 0 || value.qpcFrequency == 0 ||
        value.recordingEpoch == 0 || value.recordingEpoch != value.initializedQpc ||
        value.renderersClosedTotal > value.renderersCreatedTotal ||
        value.renderersLive != value.renderersCreatedTotal - value.renderersClosedTotal ||
        value.rasterDrawsTotal > value.drawEventsTotal ||
        value.gpuSkiaDrawsTotal != value.drawEventsTotal - value.rasterDrawsTotal ||
        (value.drawEventsTotal != 0 && (value.lastDrawProcessId != pid ||
          value.lastDrawThreadId == 0 || value.lastDrawQpc < value.initializedQpc))) {
      // Busy/not_started never enter observe(); malformed valid samples latch.
      invalid_ = true;
      return false;
    }
    if (bound_ && (value.processId != previous_.processId ||
        value.processCreationFileTime != previous_.processCreationFileTime ||
        value.engineOwnerThreadId != previous_.engineOwnerThreadId ||
        value.qpcFrequency != previous_.qpcFrequency ||
        value.recordingEpoch != previous_.recordingEpoch ||
        value.initializedQpc != previous_.initializedQpc ||
        value.renderersCreatedTotal < previous_.renderersCreatedTotal ||
        value.renderersClosedTotal < previous_.renderersClosedTotal ||
        value.rasterDrawsTotal < previous_.rasterDrawsTotal ||
        value.gpuSkiaDrawsTotal < previous_.gpuSkiaDrawsTotal ||
        value.drawEventsTotal < previous_.drawEventsTotal ||
        value.lastDrawQpc < previous_.lastDrawQpc)) {
      invalid_ = true;
      return false;
    }
    previous_ = value;
    bound_ = true;
    return true;
  }
  [[nodiscard]] bool retain_terminal(const wkeRendererProcessSnapshot& value, bool retired) noexcept {
    if (terminal_recorded_) return true;
    if (!bound_ || invalid_ || !retired || std::memcmp(&value, &previous_, sizeof(value)) != 0 ||
        value.status != WKE_RENDER_DIAGNOSTICS_CLOSED ||
        value.renderersLive != 0 || value.renderersCreatedTotal == 0 ||
        (value.flags & WKE_RENDER_DIAGNOSTICS_ENGINE_FINALIZED) == 0 ||
        value.closedSnapshotQpc < value.initializedQpc ||
        value.engineFinalizedQpc < value.closedSnapshotQpc) return false;
    terminal_ = value;
    terminal_recorded_ = true;
    return true;
  }
  [[nodiscard]] bool terminal_recorded() const noexcept { return terminal_recorded_; }
  [[nodiscard]] bool invalid() const noexcept { return invalid_; }
  [[nodiscard]] const wkeRendererProcessSnapshot& terminal() const noexcept { return terminal_; }
 private:
  bool bound_{false};
  bool invalid_{false};
  bool terminal_recorded_{false};
  wkeRendererProcessSnapshot previous_{};
  wkeRendererProcessSnapshot terminal_{};
};
} // namespace soui::sdk::diagnostics::detail
