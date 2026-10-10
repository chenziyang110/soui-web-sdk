#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

namespace soui::sdk::detail {

// Zero is the legacy/inactive state. Odd receipts are pending; their reserved
// even successor is completed. Invalidation also advances an already-pending
// receipt, so a nested context cannot be completed by its outer callback.
// The odd ceiling is sticky fail-closed. The smaller ceiling used by tests
// exercises the same production overflow branch without forging atomic state.
template <std::uint64_t LastPending = (std::numeric_limits<std::uint64_t>::max)()>
class main_context_gate final {
 public:
  static_assert(LastPending >= 3 && (LastPending & 1U) != 0,
                "The exhausted receipt must be a nonzero odd value.");

  [[nodiscard]] static std::uint64_t capture(
      const std::atomic<std::uint64_t>& state) noexcept {
    return state.load(std::memory_order_acquire);
  }

  [[nodiscard]] static std::uint64_t invalidate(
      std::atomic<std::uint64_t>& state) noexcept {
    auto current = capture(state);
    for (;;) {
      if (current >= LastPending - 1) {
        if (state.compare_exchange_weak(current, LastPending,
                std::memory_order_acq_rel, std::memory_order_acquire)) return 0;
      } else {
        const auto next = current + ((current & 1U) ? 2U : 1U);
        if (state.compare_exchange_weak(current, next,
                std::memory_order_acq_rel, std::memory_order_acquire))
          return next == LastPending ? 0 : next;
      }
    }
  }

  [[nodiscard]] static bool pending(
      const std::atomic<std::uint64_t>& state, std::uint64_t receipt) noexcept {
    return receipt && receipt < LastPending && (receipt & 1U) && capture(state) == receipt;
  }

  [[nodiscard]] static bool complete(
      std::atomic<std::uint64_t>& state, std::uint64_t receipt) noexcept {
    if (!receipt || receipt >= LastPending || !(receipt & 1U)) return false;
    return state.compare_exchange_strong(receipt, receipt + 1,
        std::memory_order_acq_rel, std::memory_order_acquire);
  }

  [[nodiscard]] static bool accepts(
      const std::atomic<std::uint64_t>& state, std::uint64_t receipt) noexcept {
    return !(receipt & 1U) && receipt < LastPending && capture(state) == receipt;
  }

  // Revoke only our completed receipt after a failed final observation. A
  // nested callback's newer state must never be overwritten by the outer one.
  [[nodiscard]] static bool revoke_completed(
      std::atomic<std::uint64_t>& state, std::uint64_t receipt) noexcept {
    if (!receipt || (receipt & 1U) || receipt >= LastPending) return false;
    return state.compare_exchange_strong(receipt, receipt + 1,
        std::memory_order_acq_rel, std::memory_order_acquire);
  }

  // Only the owner may call this after workers, native callbacks and HWNDs
  // have actually drained. It is not a recovery from a failed context ACK.
  static void reset_after_teardown(std::atomic<std::uint64_t>& state) noexcept {
    state.store(0, std::memory_order_release);
  }
};

// Before jsGetWebView has identified a Host there is no per-Host receipt to
// capture. This process-wide revision closes that first opaque lookup window.
// It grants nothing: the discovered Host must still validate its own receipt,
// view, document and session. Changes in another Host conservatively reject
// the lookup. Its exhaustion is permanent for this process, including teardown.
template <std::uint64_t Ceiling = (std::numeric_limits<std::uint64_t>::max)()>
class context_lookup_revision final {
 public:
  static_assert(Ceiling > 0, "The lookup ceiling must be nonzero.");
  [[nodiscard]] static std::uint64_t capture(const std::atomic<std::uint64_t>& state) noexcept {
    return state.load(std::memory_order_acquire);
  }
  [[nodiscard]] static bool matches(const std::atomic<std::uint64_t>& state,
                                    std::uint64_t captured) noexcept {
    return captured < Ceiling && capture(state) == captured;
  }
  [[nodiscard]] static bool advance(std::atomic<std::uint64_t>& state) noexcept {
    auto current = capture(state);
    while (current < Ceiling) {
      if (state.compare_exchange_weak(current, current + 1,
              std::memory_order_acq_rel, std::memory_order_acquire)) return current + 1 < Ceiling;
    }
    return false;
  }
};

}  // namespace soui::sdk::detail
