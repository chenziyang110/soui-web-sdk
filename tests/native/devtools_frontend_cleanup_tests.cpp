#include "../../sdk/src/host/devtools/miniblink_frontend_cleanup.h"
#include <initializer_list>
#include <iostream>
#include <stdexcept>

// Exercise the production cleanup driver through the real Broker ABI table.
// This does not run a kernel, resource dispatcher, inspector or native HWND.
namespace {
using namespace soui::sdk::detail;
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
bool same(wkeCursorBrokerEndpoint a, wkeCursorBrokerEndpoint b) {
  return a.low == b.low && a.high == b.high;
}
struct broker_fixture {
  inline static broker_fixture* current{};
  const wkeCursorBrokerEndpoint original{17, 29};
  wkeCursorBrokerEndpoint endpoint{original};
  std::uint64_t sequence{};
  std::atomic<unsigned> first_failure{0};
  unsigned close_status{WKE_CURSOR_BROKER_OK}, inspect_status{WKE_CURSOR_BROKER_OK};
  unsigned destroy_status{WKE_CURSOR_BROKER_OK};
  unsigned close_calls{}, inspect_calls{}, destroy_calls{};
  bool write_inspect{true}, write_destroy{true};
  wkeCursorBrokerDrainReceipt inspected{}, destroyed{};
  wkeCursorBrokerApi api{};

  broker_fixture() {
    require(!current, "fixture must own the callback table exclusively");
    current = this;
    inspected.structSize = sizeof(inspected); inspected.endpoint = original;
    inspected.closing = inspected.drained = 1; inspected.closeSequence = 83;
    destroyed = inspected;
    api.structSize = sizeof(api); api.abiVersion = WKE_CURSOR_BROKER_ABI_VERSION;
    api.requestClose = close; api.inspectDrain = inspect; api.destroyEndpoint = destroy;
  }
  ~broker_fixture() { current = nullptr; }
  static unsigned WKE_CALL_TYPE close(wkeCursorBrokerEndpoint token) {
    auto& self = *current; ++self.close_calls;
    require(same(token, self.original), "close retry must keep the original endpoint");
    return self.close_status;
  }
  static unsigned WKE_CALL_TYPE inspect(wkeCursorBrokerEndpoint token, wkeCursorBrokerDrainReceipt* out) {
    auto& self = *current; ++self.inspect_calls;
    require(same(token, self.original), "inspect must retain the endpoint owner");
    require(out && out->structSize == sizeof(*out), "inspect requires a caller-sized receipt");
    if (self.write_inspect) *out = self.inspected;
    return self.inspect_status;
  }
  static unsigned WKE_CALL_TYPE destroy(wkeCursorBrokerEndpoint token, wkeCursorBrokerDrainReceipt* out) {
    auto& self = *current; ++self.destroy_calls;
    require(same(token, self.original), "destroy must use the original endpoint");
    if (self.write_destroy) *out = self.destroyed;
    return self.destroy_status;
  }
  miniblink_frontend_cleanup_result advance() {
    return advance_miniblink_frontend_cleanup(api, endpoint, sequence, first_failure);
  }
  void retained() const { require(same(endpoint, original), "pending/failed cleanup must retain endpoint ownership"); }
};

void busy_does_not_consume_receipts() {
  for (unsigned stage = 0; stage != 3; ++stage) {
    for (bool poison : {false, true}) {
      broker_fixture self;
      if (stage == 0) self.close_status = WKE_CURSOR_BROKER_BUSY;
      if (stage == 1) {
        self.inspect_status = WKE_CURSOR_BROKER_BUSY;
        self.write_inspect = poison; self.inspected = {};
      }
      if (stage == 2) {
        self.destroy_status = WKE_CURSOR_BROKER_BUSY;
        self.write_destroy = poison; self.destroyed = {};
      }
      const auto result = self.advance();
      require(result.success && result.waiting && !result.destroyed, "BUSY must be pending without a receipt failure");
      require(!*result.code && !self.first_failure.load(), "BUSY must not latch a native failure");
      require(self.close_calls == 1 && self.inspect_calls == (stage ? 1U : 0U) &&
        self.destroy_calls == (stage == 2 ? 1U : 0U), "one advancement must stop at its first BUSY");
      require(self.sequence == (stage == 2 ? 83U : 0U), "BUSY must not adopt an unpublished close sequence");
      self.retained();
    }
  }
  broker_fixture self;
  self.sequence = 83; self.inspect_status = WKE_CURSOR_BROKER_BUSY; self.inspected = {};
  const auto result = self.advance();
  require(result.waiting && self.sequence == 83, "BUSY must not replace a previously accepted sequence");
  self.retained();
}

void fences_and_retry() {
  using field = unsigned wkeCursorBrokerDrainReceipt::*;
  const field fences[]{&wkeCursorBrokerDrainReceipt::queuedUiTasks, &wkeCursorBrokerDrainReceipt::runningUiTasks,
    &wkeCursorBrokerDrainReceipt::nativeScopes, &wkeCursorBrokerDrainReceipt::engineCallbacks,
    &wkeCursorBrokerDrainReceipt::oleRegistered};
  for (const auto fence : fences) {
    broker_fixture self; self.inspected.*fence = 1;
    const auto result = self.advance();
    require(result.success && result.waiting && !self.destroy_calls, "every native fence must prevent destruction");
    self.retained();
  }
  broker_fixture self;
  self.inspected.drained = 0;
  require(self.advance().waiting && !self.destroy_calls, "undrained owner must stay pending");
  self.inspect_status = WKE_CURSOR_BROKER_BUSY; self.inspected = {};
  require(self.advance().waiting && self.sequence == 83, "later unpublished BUSY must retain the original sequence");
  self.inspect_status = WKE_CURSOR_BROKER_OK; self.inspected = self.destroyed;
  self.destroy_status = WKE_CURSOR_BROKER_BUSY;
  require(self.advance().waiting, "destroy BUSY must retain the owner for another advancement"); self.retained();
  self.destroy_status = WKE_CURSOR_BROKER_OK;
  const auto result = self.advance();
  require(result.success && result.destroyed && !result.waiting && !self.endpoint.low && !self.endpoint.high,
    "only a validated successful destroy may clear ownership");
  require(self.close_calls == 4 && self.inspect_calls == 4 && self.destroy_calls == 2,
    "each retry must request close once with no internal retry loop");
}

void reject_bad_receipts() {
  for (unsigned corruption = 0; corruption != 7; ++corruption) {
    broker_fixture self;
    if (corruption == 0) self.inspected.structSize = 0;
    if (corruption == 1) ++self.inspected.endpoint.low;
    if (corruption == 2) self.inspected.closing = 0;
    if (corruption == 3) self.inspected.closeSequence = 0;
    if (corruption == 4) self.inspected.drained = 2;
    if (corruption == 5) { self.sequence = 82; }
    if (corruption == 6) ++self.inspected.endpoint.high;
    const auto result = self.advance();
    require(!result.success && !result.waiting && !result.destroyed && !self.destroy_calls,
      "invalid or changed inspect receipt must fail before destruction");
    require(self.sequence == (corruption == 5 ? 82U : 0U), "invalid receipt must not replace the original sequence");
    self.retained();
  }
  for (unsigned corruption = 0; corruption != 10; ++corruption) {
    broker_fixture self;
    if (corruption == 0) self.destroyed.structSize = 0;
    if (corruption == 1) ++self.destroyed.endpoint.low;
    if (corruption == 2) self.destroyed.closing = 0;
    if (corruption == 3) ++self.destroyed.closeSequence;
    if (corruption == 4) self.destroyed.drained = 0;
    if (corruption == 5) self.destroyed.queuedUiTasks = 1;
    if (corruption == 6) self.destroyed.runningUiTasks = 1;
    if (corruption == 7) self.destroyed.nativeScopes = 1;
    if (corruption == 8) self.destroyed.engineCallbacks = 1;
    if (corruption == 9) self.destroyed.oleRegistered = 1;
    const auto result = self.advance();
    require(!result.success && !result.destroyed && self.sequence == 83,
      "successful status alone must not authorize owner release"); self.retained();
  }
}

void native_failure_stays_sticky() {
  {
    broker_fixture self;
    self.close_status = WKE_CURSOR_BROKER_FAILED;
    self.inspect_status = WKE_CURSOR_BROKER_BUSY; self.write_inspect = false;
    const auto pending = self.advance();
    require(!pending.success && pending.waiting && self.sequence == 0 && !self.destroy_calls,
      "close FAILED followed by BUSY must keep cleanup pending and the failure visible"); self.retained();
    self.close_status = self.inspect_status = WKE_CURSOR_BROKER_OK;
    self.write_inspect = true;
    const auto done = self.advance();
    require(done.destroyed && !done.success && self.first_failure.load() == WKE_CURSOR_BROKER_FAILED,
      "later successful physical cleanup must not erase the original failure");
  }
  {
    broker_fixture self; self.inspect_status = WKE_CURSOR_BROKER_FAILED;
    const auto result = self.advance();
    require(result.destroyed && !result.success && self.first_failure.load() == WKE_CURSOR_BROKER_FAILED,
      "inspect FAILED after close OK must stay sticky even if destruction succeeds");
  }
  {
    broker_fixture self; self.destroy_status = WKE_CURSOR_BROKER_FAILED;
    const auto failed = self.advance();
    require(!failed.success && !failed.destroyed && self.sequence == 83 &&
      self.first_failure.load() == WKE_CURSOR_BROKER_FAILED,
      "destroy FAILED with a valid pre-destruction receipt must retain the owner"); self.retained();
    self.destroy_status = WKE_CURSOR_BROKER_OK;
    const auto done = self.advance();
    require(done.destroyed && !done.success && self.close_calls == 2 && self.destroy_calls == 2,
      "failed destroy must be retried only by the next advancement without clearing its failure");
  }
  for (unsigned stage = 1; stage != 3; ++stage) {
    broker_fixture self;
    if (stage == 1) { self.inspect_status = WKE_CURSOR_BROKER_FAILED; self.inspected.structSize = 0; }
    if (stage == 2) { self.destroy_status = WKE_CURSOR_BROKER_FAILED; self.destroyed.structSize = 0; }
    const auto result = self.advance();
    require(!result.success && !result.destroyed && self.first_failure.load() == WKE_CURSOR_BROKER_FAILED,
      "FAILED must still validate its published receipt and retain the owner"); self.retained();
    require(self.sequence == (stage == 1 ? 0U : 83U), "malformed FAILED receipt must not adopt a sequence");
  }
}

void reject_nonbusy_statuses() {
  for (const auto status : {WKE_CURSOR_BROKER_INVALID, WKE_CURSOR_BROKER_STALE, WKE_CURSOR_BROKER_WRONG_THREAD}) {
    for (unsigned stage = 0; stage != 3; ++stage) {
      broker_fixture self;
      if (stage == 0) self.close_status = status;
      if (stage == 1) self.inspect_status = status;
      if (stage == 2) self.destroy_status = status;
      const auto result = self.advance();
      require(!result.success && !result.waiting && !result.destroyed,
        "invalid/stale/thread status must remain a hard failure"); self.retained();
      require(self.close_calls == 1 && self.inspect_calls == (stage ? 1U : 0U) &&
        self.destroy_calls == (stage == 2 ? 1U : 0U), "a failed stage must not call a later stage");
    }
  }
}
} // namespace

int main() {
  try {
    busy_does_not_consume_receipts(); fences_and_retry(); reject_bad_receipts();
    native_failure_stays_sticky(); reject_nonbusy_statuses();
    std::cout << "DevTools frontend cleanup contract passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
