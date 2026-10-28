#include <soui/diagnostics/renderer_observation_binding.h>
#include "../acceptance-app/native-owner-metrics.h"
#include "../acceptance-app/shutdown-poll.h"
#include "../../sdk/src/host/runtime_shutdown_poll.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using soui::sdk::diagnostics::detail::renderer_observation_binding;
void require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
wkeRendererProcessSnapshot sample() {
  wkeRendererProcessSnapshot value{};
  value.structSize = sizeof(value); value.abiVersion = 1; value.status = 1; value.flags = 0x27;
  value.lastDrawKind = 1; value.processId = 9; value.processCreationFileTime = 123;
  value.engineOwnerThreadId = 10; value.qpcFrequency = 1000; value.recordingEpoch = 100;
  value.initializedQpc = 100; value.renderersCreatedTotal = 1; value.renderersLive = 1;
  value.rasterDrawsTotal = 1; value.drawEventsTotal = 1; value.lastDrawQpc = 120;
  value.lastDrawThreadId = 11; value.lastDrawProcessId = 9;
  return value;
}
std::uint32_t WKE_RENDER_DIAGNOSTICS_CALL inert_reader(wkeRendererProcessSnapshot*) { return 3; }
void api_shape() {
  using soui::sdk::diagnostics::detail::renderer_api_supported;
  wkeRendererDiagnosticsApi api{sizeof(api), 1, 3, inert_reader};
  require(renderer_api_supported(&api), "exact API refused");
  require(!renderer_api_supported(nullptr), "missing API accepted");
  api.abiVersion = 2; require(!renderer_api_supported(&api), "foreign version accepted");
  api.abiVersion = 1; api.structSize -= 1; require(!renderer_api_supported(&api), "short API accepted");
  api.structSize = sizeof(api); api.capabilities = 1; require(!renderer_api_supported(&api), "missing capability accepted");
  api.capabilities = 3; api.readProcessSnapshot = nullptr; require(!renderer_api_supported(&api), "missing reader accepted");
}
void stable_identity_and_history() {
  auto value = sample(); renderer_observation_binding binding;
  require(binding.observe(value, 9, 123), "first actual shape refused");
  value.rasterDrawsTotal = value.drawEventsTotal = 2; value.lastDrawQpc = 130;
  require(binding.observe(value, 9, 123), "monotone observed history refused");
  value.recordingEpoch = value.initializedQpc = 101;
  require(!binding.observe(value, 9, 123) && binding.invalid(), "reload epoch accepted");
  value.recordingEpoch = value.initializedQpc = 100;
  require(!binding.observe(value, 9, 123), "invalid binding recovered silently");
}
void malformed_or_regressed() {
  for (unsigned kind = 0; kind != 7; ++kind) {
    auto value = sample(); renderer_observation_binding binding;
    if (kind == 0) value.processCreationFileTime = 124;
    if (kind == 1) value.flags |= 0x10;
    if (kind == 2) value.structSize = 160;
    if (kind == 3) value.qpcFrequency = 0;
    if (kind == 4) value.renderersLive = 0;
    if (kind == 5) value.lastDrawProcessId = 8;
    if (kind == 6) value.reserved = 1;
    require(!binding.observe(value, 9, 123), "bad actual shape accepted");
  }
  auto value = sample(); renderer_observation_binding binding;
  require(binding.observe(value, 9, 123), "initial shape refused");
  value.rasterDrawsTotal = value.drawEventsTotal = 0;
  require(!binding.observe(value, 9, 123), "counter rollback accepted");
}
void terminal_once() {
  auto value = sample(); renderer_observation_binding binding;
  value.status = 4; value.flags |= 8; value.renderersLive = 0; value.renderersClosedTotal = 1;
  value.lastRendererCloseQpc = value.closedSnapshotQpc = 140; value.engineFinalizedQpc = 150;
  require(binding.observe(value, 9, 123), "closed shape refused");
  require(!binding.retain_terminal(value, false), "pre-retirement terminal accepted");
  require(binding.retain_terminal(value, true), "real retired terminal refused");
  const auto retained = binding.terminal();
  value.closedSnapshotQpc = 200; value.engineFinalizedQpc = 210;
  require(binding.retain_terminal(value, true), "repeated terminal call failed");
  require(std::memcmp(&retained, &binding.terminal(), sizeof(retained)) == 0, "terminal timestamp overwritten");
  renderer_observation_binding empty;
  require(!empty.retain_terminal(value, true) && !empty.terminal_recorded(), "unstarted terminal fabricated");
}
void exact_serialization() {
  soui::sdk::diagnostics::renderer_observation value;
  value.native = sample(); value.native.rasterDrawsTotal = std::numeric_limits<std::uint64_t>::max();
  const auto json = soui::acceptance::metrics::serialize(value);
  require(json.at("rasterDrawsTotal") == "18446744073709551615", "uint64 lost precision");
  require(json.at("state") == "unknown" && json.at("identityBound") == false, "raw DTO implies qualification");
}
void shutdown_success() {
  unsigned attempts = 0;
  const auto result = soui::acceptance::poll_runtime_shutdown(
      [](auto ready) { require(ready(), "immediate success must complete"); },
      [&] { ++attempts; return soui::sdk::host_result{true, "", "complete"}; });
  require(result.ok && attempts == 1, "success was retried");
}
void shutdown_pending_then_success() {
  unsigned attempts = 0;
  const auto result = soui::acceptance::poll_runtime_shutdown(
      [](auto ready) { require(!ready(), "pending completed early"); require(!ready(), "pending completed early");
        require(ready(), "completed stop remained pending"); },
      [&] { ++attempts; return attempts == 3 ? soui::sdk::host_result{true, "", "complete"}
          : soui::sdk::host_result{false, "E_RUNTIME_SHUTDOWN_PENDING", "pending"}; });
  require(result.ok && attempts == 3, "pending decision did not preserve attempts");
}
void shutdown_nonpending_rejected() {
  unsigned attempts = 0;
  const auto result = soui::acceptance::poll_runtime_shutdown(
      [](auto ready) { require(ready(), "non-pending failure was retried"); },
      [&] { ++attempts; return soui::sdk::host_result{false, "E_RUNTIME_SHUTDOWN_FAILED", "original failure"}; });
  require(!result.ok && result.code == "E_RUNTIME_SHUTDOWN_FAILED" && result.message == "original failure" && attempts == 1,
      "non-pending failure was suppressed");
}
void shutdown_deadline_propagates() {
  unsigned attempts = 0; bool deadline_failed = false;
  // Controlled Until models the unchanged caller deadline; no sleep/Native.
  constexpr unsigned original_capture_timeout_ms = 20000;
  static_assert(original_capture_timeout_ms == 20000);
  try {
    (void)soui::acceptance::poll_runtime_shutdown(
        [](auto ready) { require(!ready(), "permanent pending completed");
          throw std::runtime_error("original deadline"); },
        [&] { ++attempts; return soui::sdk::host_result{false, "E_RUNTIME_SHUTDOWN_PENDING", "pending"}; });
  } catch (const std::runtime_error& error) { deadline_failed = std::string(error.what()) == "original deadline"; }
  require(deadline_failed && attempts == 1, "deadline was replaced with success or a wider retry");
}
void application_shutdown_budget() {
  using namespace std::chrono;
  using soui::sdk::host_result;
  using soui::sdk::detail::wait_runtime_shutdown;
  milliseconds elapsed{0}; unsigned attempts = 0, waits = 0;
  const auto now = [&] { return steady_clock::time_point{} + elapsed; };
  const auto pending = [&] { ++attempts; return host_result{false, "E_RUNTIME_SHUTDOWN_PENDING", "pending"}; };
  const auto wait = [&](milliseconds quantum) {
    require(quantum.count() > 0 && quantum <= milliseconds{10}, "unbounded message wait");
    ++waits; elapsed += quantum; return host_result{true, {}, {}};
  };
  auto result = wait_runtime_shutdown(milliseconds{-1}, pending, wait, now);
  require(result.code == "E_RUNTIME_SHUTDOWN_ARGUMENT" && attempts == 0 && waits == 0, "negative budget started shutdown");
  result = wait_runtime_shutdown(milliseconds{0}, pending, wait, now);
  require(result.code == "E_RUNTIME_SHUTDOWN_TIMEOUT" && attempts == 1 && waits == 0, "zero budget waited");
  attempts = 0;
  result = wait_runtime_shutdown(milliseconds{0}, [&] { ++attempts; return host_result{true, {}, "complete"}; }, wait, now);
  require(result.ok && attempts == 1 && waits == 0, "immediate completion was changed");
  attempts = 0;
  result = wait_runtime_shutdown(milliseconds{25}, pending, wait, now);
  require(result.code == "E_RUNTIME_SHUTDOWN_TIMEOUT" && attempts == 3 && waits == 3 && elapsed == milliseconds{25},
      "pending extended deadline or retried after expiry");
  elapsed = milliseconds{0}; attempts = waits = 0;
  result = wait_runtime_shutdown(milliseconds{25}, [&] {
    ++attempts;
    return attempts == 3 ? host_result{true, {}, "complete"} : host_result{false, "E_RUNTIME_SHUTDOWN_PENDING", "pending"};
  }, wait, now);
  require(result.ok && attempts == 3 && waits == 2, "pending did not advance to real completion");
  for (const auto* code : {"E_RUNTIME_IN_USE", "E_RUNTIME_THREAD", "E_WORKER_SHUTDOWN_UNSUPPORTED", "E_RUNTIME_SHUTDOWN_FAILED"}) {
    attempts = waits = 0;
    result = wait_runtime_shutdown(milliseconds{25}, [&] { ++attempts; return host_result{false, code, "original"}; }, wait, now);
    require(result.code == code && result.message == "original" && attempts == 1 && waits == 0, "real failure was retried or replaced");
  }
  attempts = waits = 0;
  result = wait_runtime_shutdown(milliseconds{25}, pending, [&](milliseconds) {
    ++waits; return host_result{false, "E_RUNTIME_SHUTDOWN_WAIT", "original wait failure"};
  }, now);
  require(result.code == "E_RUNTIME_SHUTDOWN_WAIT" && result.message == "original wait failure" && attempts == 1 && waits == 1,
      "message wait failure was suppressed");
  attempts = waits = 0;
  result = wait_runtime_shutdown(milliseconds{25}, pending, [&](milliseconds) {
    ++waits; elapsed += milliseconds{100}; return host_result{true, {}, {}};
  }, now);
  require(result.code == "E_RUNTIME_SHUTDOWN_TIMEOUT" && attempts == 1 && waits == 1, "long callback bypassed total budget");
}
} // namespace
static_assert(sizeof(wkeRendererProcessSnapshot) == 168);
static_assert(offsetof(wkeRendererProcessSnapshot, processId) == 24);
static_assert(offsetof(wkeRendererProcessSnapshot, initializedQpc) == 160);
int main() {
  try { api_shape(); stable_identity_and_history(); malformed_or_regressed(); terminal_once(); exact_serialization();
    shutdown_success(); shutdown_pending_then_success(); shutdown_nonpending_rejected(); shutdown_deadline_propagates(); application_shutdown_budget(); }
  catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
  std::cout << "renderer consumer data contracts passed\n";
  return 0;
}
