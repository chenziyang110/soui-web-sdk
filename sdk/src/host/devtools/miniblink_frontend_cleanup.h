#pragma once

#include "kernel_abi.h"
#include <atomic>
#include <cstdint>

namespace soui::sdk::detail {
struct miniblink_frontend_cleanup_result {
  unsigned status{WKE_CURSOR_BROKER_OK};
  bool success{true}, waiting{false}, destroyed{false};
  const char* code{""};
};

// One Engine-owner advancement, not a retry loop. BUSY does not publish a
// receipt; only a successful destroy authorizes releasing the endpoint owner.
[[nodiscard]] inline miniblink_frontend_cleanup_result advance_miniblink_frontend_cleanup(
    const wkeCursorBrokerApi& api, wkeCursorBrokerEndpoint& endpoint,
    std::uint64_t& close_sequence, std::atomic<unsigned>& first_native_failure) {
  miniblink_frontend_cleanup_result result;
  const auto fail = [&result](const char* code) {
    result.success = false;
    if (!*result.code) result.code = code;
  };
  if (first_native_failure.load(std::memory_order_acquire)) fail("E_DEVTOOLS_FRONTEND");
  const auto remember_native_failure = [&](const char* code) {
    unsigned empty = 0;
    (void)first_native_failure.compare_exchange_strong(empty, result.status,
      std::memory_order_acq_rel, std::memory_order_acquire);
    fail(code);
  };
  const auto valid_receipt = [&endpoint](const wkeCursorBrokerDrainReceipt& receipt) {
    return receipt.structSize == sizeof(receipt) && receipt.endpoint.low == endpoint.low &&
      receipt.endpoint.high == endpoint.high && receipt.closing == 1 && receipt.closeSequence &&
      receipt.drained <= 1;
  };
  const auto drained = [](const wkeCursorBrokerDrainReceipt& receipt) {
    return receipt.drained == 1 && !receipt.queuedUiTasks && !receipt.runningUiTasks &&
      !receipt.nativeScopes && !receipt.engineCallbacks && !receipt.oleRegistered;
  };

  // A first FAILED may follow close publication but precede admitting the
  // close callout. Keep its failure sticky while continuing physical cleanup.
  result.status = api.requestClose(endpoint);
  if (result.status == WKE_CURSOR_BROKER_BUSY) { result.waiting = true; return result; }
  if (result.status == WKE_CURSOR_BROKER_FAILED) {
    remember_native_failure("E_DEVTOOLS_FRONTEND_CLOSE");
  } else if (result.status != WKE_CURSOR_BROKER_OK) {
    fail("E_DEVTOOLS_FRONTEND_CLOSE"); return result;
  }

  wkeCursorBrokerDrainReceipt receipt{}; receipt.structSize = sizeof(receipt);
  result.status = api.inspectDrain(endpoint, &receipt);
  if (result.status == WKE_CURSOR_BROKER_BUSY) { result.waiting = true; return result; }
  if (result.status == WKE_CURSOR_BROKER_FAILED) {
    remember_native_failure("E_DEVTOOLS_FRONTEND_DRAIN");
  } else if (result.status != WKE_CURSOR_BROKER_OK) {
    fail("E_DEVTOOLS_FRONTEND_DRAIN"); return result;
  }
  if (!valid_receipt(receipt)) { fail("E_DEVTOOLS_FRONTEND_DRAIN"); return result; }
  if (!close_sequence) close_sequence = receipt.closeSequence;
  if (close_sequence != receipt.closeSequence) {
    fail("E_DEVTOOLS_FRONTEND_CLOSE_SEQUENCE"); return result;
  }
  if (!drained(receipt)) { result.waiting = true; return result; }

  result.status = api.destroyEndpoint(endpoint, &receipt);
  // destroyEndpoint can return BUSY either before or after writing out. The
  // next owner advancement must request close again with the original token.
  if (result.status == WKE_CURSOR_BROKER_BUSY) { result.waiting = true; return result; }
  if (result.status == WKE_CURSOR_BROKER_FAILED) {
    remember_native_failure("E_DEVTOOLS_FRONTEND_DESTROY");
  } else if (result.status != WKE_CURSOR_BROKER_OK) {
    fail("E_DEVTOOLS_FRONTEND_DESTROY"); return result;
  }
  if (!valid_receipt(receipt) || receipt.closeSequence != close_sequence || !drained(receipt)) {
    fail("E_DEVTOOLS_FRONTEND_DESTROY"); return result;
  }
  // FAILED can contain the valid pre-destruction receipt when native destroy
  // threw. That receipt is not authorization to forget the still-owned view.
  if (result.status != WKE_CURSOR_BROKER_OK) return result;
  endpoint = {};
  result.destroyed = true;
  return result;
}
} // namespace soui::sdk::detail
