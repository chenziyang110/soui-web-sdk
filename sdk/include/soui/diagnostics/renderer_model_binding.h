#pragma once
#include <soui/diagnostics/renderer_model_abi.h>
#include <soui/diagnostics/renderer_observation_binding.h>
#include <cstring>

namespace soui::sdk::diagnostics::detail {
[[nodiscard]] inline bool renderer_model_api_supported(const wkeRendererModelDiagnosticsApi* api) noexcept {
  return api && api->structSize == sizeof(*api) && api->abiVersion == 2 &&
      api->capabilities == 7 && api->readProcessModel;
}
// Runtime mutex owns this state; no DLL/view invocation or clock query here.
class renderer_model_binding final {
 public:
  [[nodiscard]] bool observe(const wkeRendererModelSnapshot& value,
                             std::uint64_t pid, std::uint64_t creation) noexcept {
    if (invalid_ || value.structSize != sizeof(value) || value.abiVersion != 2 ||
        value.status != value.compositor.status || (value.flags & 19U) != 19U ||
        (value.flags & ~19U) != 0 || value.providerId != 1 || value.backendId != 1 ||
        value.compiledGpuSupport > 1 || value.factoryCoverageMask != 127 ||
        value.backendInitializationQpc == 0 ||
        value.backendInitializationQpc > value.compositor.initializedQpc ||
        !compositor_.observe(value.compositor, pid, creation)) {
      invalid_ = true; return false;
    }
    if (bound_ && (value.providerId != previous_.providerId || value.backendId != previous_.backendId ||
        value.compiledGpuSupport != previous_.compiledGpuSupport ||
        value.factoryCoverageMask != previous_.factoryCoverageMask ||
        value.backendInitializationQpc != previous_.backendInitializationQpc ||
        value.mediaFactoryAttempts < previous_.mediaFactoryAttempts ||
        value.pluginFactoryAttempts < previous_.pluginFactoryAttempts ||
        value.offscreenFactoryAttempts < previous_.offscreenFactoryAttempts ||
        value.sharedContextFactoryAttempts < previous_.sharedContextFactoryAttempts ||
        value.externalFactoryAttempts < previous_.externalFactoryAttempts ||
        value.platformMismatchTotal < previous_.platformMismatchTotal ||
        value.unknownRouteTotal < previous_.unknownRouteTotal)) {
      invalid_ = true; return false;
    }
    previous_ = value; bound_ = true; return true;
  }
  [[nodiscard]] bool retain_terminal(const wkeRendererModelSnapshot& value, bool retired) noexcept {
    if (terminal_recorded_) return true;
    if (invalid_ || !bound_ || std::memcmp(&value, &previous_, sizeof(value)) != 0 ||
        !compositor_.retain_terminal(value.compositor, retired)) return false;
    terminal_ = value; terminal_recorded_ = true; return true;
  }
  [[nodiscard]] bool invalid() const noexcept { return invalid_; }
  [[nodiscard]] bool terminal_recorded() const noexcept { return terminal_recorded_; }
  [[nodiscard]] const wkeRendererModelSnapshot& terminal() const noexcept { return terminal_; }
 private:
  renderer_observation_binding compositor_;
  bool invalid_{false}, bound_{false}, terminal_recorded_{false};
  wkeRendererModelSnapshot previous_{}, terminal_{};
};
} // namespace soui::sdk::diagnostics::detail
