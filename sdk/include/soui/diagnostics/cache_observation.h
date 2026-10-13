#pragma once
#include <soui/diagnostics/cache_observation_abi.h>
#include <cstdint>
#include <string>

namespace soui::sdk::diagnostics {
enum class cache_status : std::uint32_t { disabled, not_started, pending, busy, unavailable, sampled };
struct cache_observation final {
  cache_status state{cache_status::not_started};
  std::string module_path;
  bool module_matches{false}, identity_bound{false}, has_completed_sample{false};
  bool inflight{false}, runtime_retired{false};
  wkeCacheSnapshot native{}; // Last completed copy retains its original sampleQpc.
};
namespace detail {
[[nodiscard]] inline bool cache_api_supported(const wkeCacheDiagnosticsApi* api) noexcept {
  return api && api->structSize == sizeof(*api) && api->abiVersion == WKE_CACHE_ABI_VERSION &&
      api->capabilities == WKE_CACHE_CAP_NAMED_SCALARS && api->sampleExistingCaches;
}
// Runtime mutex owns this admission state. No scheduling or native access here.
class cache_sample_slot final {
 public:
  [[nodiscard]] std::uint64_t begin() noexcept {
    if (retired_ || inflight_ || generation_ == UINT64_MAX) return 0;
    inflight_ = true; return ++generation_;
  }
  [[nodiscard]] bool finish(std::uint64_t token) noexcept {
    if (!inflight_ || retired_ || token != generation_) return false;
    inflight_ = false; return true;
  }
  void retire() noexcept { retired_ = true; inflight_ = false; }
  [[nodiscard]] bool inflight() const noexcept { return inflight_; }
 private:
  std::uint64_t generation_{0};
  bool inflight_{false}, retired_{false};
};
// Cache sizes/limits may decrease. Only identity and sampling time are stable.
class cache_identity_binding final {
 public:
  [[nodiscard]] bool observe(const wkeCacheSnapshot& value, std::uint64_t pid,
                              std::uint64_t creation) noexcept {
    if (invalid_ || value.structSize != sizeof(value) || value.abiVersion != 1 ||
        value.status != WKE_CACHE_READY || value.reserved || value.blinkReserved || value.glyphReserved ||
        value.processId != pid || !pid || value.processCreationFileTime != creation || !creation ||
        !value.engineOwnerThreadId || !value.qpcFrequency || !value.recordingEpoch ||
        value.recordingEpoch != value.initializedQpc || value.sampleQpc < value.initializedQpc ||
        value.blinkStatus > WKE_CACHE_UNAVAILABLE || value.glyphStatus > WKE_CACHE_UNAVAILABLE ||
        value.resourceStatus > WKE_CACHE_UNAVAILABLE || value.resourceDiscardable > 1 ||
        (value.blinkStatus == WKE_CACHE_READY && (value.blinkMinDeadCapacity > value.blinkMaxDeadCapacity ||
          value.blinkMaxDeadCapacity > value.blinkCapacity)) ||
        (bound_ && (value.processId != previous_.processId ||
          value.processCreationFileTime != previous_.processCreationFileTime ||
          value.engineOwnerThreadId != previous_.engineOwnerThreadId ||
          value.recordingEpoch != previous_.recordingEpoch || value.qpcFrequency != previous_.qpcFrequency ||
          value.sampleQpc < previous_.sampleQpc))) { invalid_ = true; return false; }
    if (value.resourceStatus == WKE_CACHE_READY) {
      const auto raw = value.resourceRawSingleLimit;
      const auto total = value.resourceByteLimit;
      const auto effective = value.resourceDiscardable ? raw : (!raw ? total : (raw < total ? raw : total));
      if (value.resourceEffectiveSingleLimit != effective || (value.resourceDiscardable && total != 0)) {
        invalid_ = true; return false;
      }
    }
    previous_ = value; bound_ = true; return true;
  }
  [[nodiscard]] bool invalid() const noexcept { return invalid_; }
 private:
  bool bound_{false}, invalid_{false};
  wkeCacheSnapshot previous_{};
};
} // namespace detail
} // namespace soui::sdk::diagnostics
