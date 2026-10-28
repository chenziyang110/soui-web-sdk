#include <soui/diagnostics/view_observation_binding.h>
#include "../../third_party/sources/miniblink/wke/ViewDiagnosticsState.h"
#include "../../sdk/src/host/devtools/callback_observation.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using wke::diagnostics::view_lifetime_state;
using soui::sdk::diagnostics::detail::view_observation_binding;
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
view_lifetime_state initialized() {
  view_lifetime_state state;
  state.initialized(10, 20, 30, 1000, 100, true, true);
  return state;
}
void lifecycle() {
  view_lifetime_state empty;
  require(empty.snapshot().status == WKE_RENDER_DIAGNOSTICS_NOT_STARTED, "pre-init is unknown");
  auto state = initialized();
  view_observation_binding binding;
  require(state.snapshot().status == WKE_RENDER_DIAGNOSTICS_READY, "ready before any draw or view");
  require(binding.observe(state.snapshot(), 10, 20), "initial identity");
  state.created(30, 110, true);
  state.created(30, 111, true);
  auto value = state.snapshot();
  require(value.viewsCreatedTotal == 2 && value.viewsDestroyedTotal == 0 && value.viewsLive == 2, "business and Inspector wrappers");
  require(binding.observe(value, 10, 20), "two views bound");
  state.destroyed(30, 120, true);
  state.destroyed(30, 121, true);
  value = state.snapshot();
  require(value.viewsLive == 0 && value.status == WKE_RENDER_DIAGNOSTICS_READY, "zero live is not runtime finalized");
  require(binding.observe(value, 10, 20), "retired views bound");
  require(!binding.retain_terminal(value, true), "cannot retain a nonterminal sample");
  state.finalized(30, 130, true);
  value = state.snapshot();
  require(value.status == WKE_RENDER_DIAGNOSTICS_CLOSED && value.engineFinalizedQpc == 130, "actual finalization closes cache");
  require(binding.observe(value, 10, 20), "finalized identity bound");
  require(!binding.retain_terminal(value, false), "runtime retirement still required");
  require(binding.retain_terminal(value, true), "terminal retained after runtime retirement");
  state.finalized(30, 999, true);
  require(state.snapshot().engineFinalizedQpc == 130, "reading/repeated finalization cannot retimestamp");
  require(binding.terminal().engineFinalizedQpc == 130, "retained sample timestamp unchanged");
}
void faults() {
  auto state = initialized();
  state.destroyed(30, 110, true);
  require(state.snapshot().status == WKE_RENDER_DIAGNOSTICS_UNAVAILABLE, "underflow latches");
  state.created(30, 111, true);
  require(state.snapshot().status == WKE_RENDER_DIAGNOSTICS_UNAVAILABLE, "valid later event cannot heal fault");
  state = initialized(); state.created(31, 110, true);
  require(state.snapshot().status == WKE_RENDER_DIAGNOSTICS_UNAVAILABLE, "wrong owner");
  state = initialized(); state.created(30, 99, true);
  require(state.snapshot().status == WKE_RENDER_DIAGNOSTICS_UNAVAILABLE, "clock regression");
  state = initialized(); state.created(30, 110, false);
  require(state.snapshot().status == WKE_RENDER_DIAGNOSTICS_UNAVAILABLE, "clock failure");
  state = initialized(); state.created(30, 110, true); state.finalized(30, 120, true);
  require(state.snapshot().status == WKE_RENDER_DIAGNOSTICS_UNAVAILABLE, "finalize cannot hide remaining views");
  state = initialized(); state.finalized(30, 110, true); state.created(30, 120, true);
  require(state.snapshot().status == WKE_RENDER_DIAGNOSTICS_UNAVAILABLE, "post-finalize mutation");
  state = initialized(); state.initialized(10, 20, 30, 1000, 200, true, true);
  require(state.snapshot().status == WKE_RENDER_DIAGNOSTICS_UNAVAILABLE, "no new epoch in same DLL cache");
}
void binding_guards() {
  auto state = initialized();
  auto first = state.snapshot();
  view_observation_binding binding;
  require(binding.observe(first, 10, 20), "bind initial");
  state.created(30, 110, true);
  const auto second = state.snapshot();
  require(binding.observe(second, 10, 20), "bind second");
  require(!binding.observe(first, 10, 20) && binding.invalid(), "counter and sequence regression rejected");
  require(!binding.observe(second, 10, 20), "invalid binding stays invalid");
  auto check = [&](auto mutate) {
    view_observation_binding candidate;
    auto invalid = second; mutate(invalid);
    require(!candidate.observe(invalid, 10, 20), "malformed sample rejected");
  };
  check([](auto& v) { v.processCreationFileTime++; });
  check([](auto& v) { v.viewsLive++; });
  check([](auto& v) { v.viewsDestroyedTotal = v.viewsCreatedTotal + 1; });
  check([](auto& v) { v.status = WKE_RENDER_DIAGNOSTICS_BUSY; });
  check([](auto& v) { v.flags |= WKE_RENDER_DIAGNOSTICS_OBSERVED_RASTER; });
  check([](auto& v) { v.engineFinalizedQpc = 200; });
  view_observation_binding same_sequence;
  require(same_sequence.observe(second, 10, 20), "bind immutable sequence");
  auto tampered = second; tampered.lastMutationQpc++;
  require(!same_sequence.observe(tampered, 10, 20), "same sequence cannot change cached values");
  view_observation_binding epoch;
  require(epoch.observe(second, 10, 20), "bind epoch");
  tampered = second; tampered.recordingEpoch++;
  require(!epoch.observe(tampered, 10, 20), "epoch replacement rejected");
  wkeViewDiagnosticsApi api{sizeof(api), WKE_VIEW_DIAGNOSTICS_ABI_VERSION, WKE_VIEW_DIAGNOSTICS_CAP_DLL_LIFETIMES,
    +[](wkeViewLifetimeSnapshot*) -> uint32_t { return WKE_RENDER_DIAGNOSTICS_BUSY; }};
  require(soui::sdk::diagnostics::detail::view_api_supported(&api), "exact optional API supported");
  api.capabilities |= 2;
  require(!soui::sdk::diagnostics::detail::view_api_supported(&api), "unrecognized capabilities rejected");
  require(!soui::sdk::diagnostics::detail::view_api_supported(nullptr), "old DLL absent API unsupported");
}
void callback_counts() {
  using namespace soui::sdk;
  devtools::observation output;
  devtools::callback_observation counts;
  counts.domain = devtools::callback_domain::miniblink_session;
  counts.sampled = true;
  counts.interrupt_pending = 1; counts.protocol_queued = 2; counts.resource_queued = 3;
  counts.dispatch_active = 4; counts.resource_active = 5; counts.frontend_paint_active = 6;
  require(detail::publish_callback_observation(output, counts), "session counters sampled");
  require(output.pending_callbacks == 6 && output.active_callbacks == 15, "all named scoped queues included");
  counts.protocol_queued = (std::numeric_limits<std::uint32_t>::max)();
  require(!detail::publish_callback_observation(output, counts), "sum cannot wrap to zero");
  require(output.pending_callbacks == 6, "invalid sample does not replace prior output");
  counts = {}; counts.sampled = true;
  require(!detail::publish_callback_observation(output, counts), "unknown domain not zero coverage");
}
}

int main() {
  static_assert(sizeof(wkeViewLifetimeSnapshot) == 104, "fixed POD ABI");
  try {
    lifecycle(); faults(); binding_guards(); callback_counts();
    std::cout << "4 view/session observation contract groups passed; no product or GUI executed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
