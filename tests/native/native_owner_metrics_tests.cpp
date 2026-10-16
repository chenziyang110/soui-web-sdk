#include "../acceptance-app/native-owner-metrics.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace soui::acceptance::metrics;
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void exact_native_precision() {
  diagnostics::queue queue;
  queue.observation.state = diagnostics::status::ready;
  queue.observation.owner_epoch = std::numeric_limits<std::uint64_t>::max();
  queue.queued = 9007199254740993ULL;
  queue.posted_total = std::numeric_limits<std::uint64_t>::max();
  const auto record = json::parse(serialize(queue).dump());
  require(record.at("queued").is_string() && record.at("queued") == "9007199254740993",
          "Queue precision above JavaScript safe integer must survive log round trip");
  require(record.at("posted_total") == "18446744073709551615" &&
          record.at("observation").at("owner_epoch") == "18446744073709551615",
          "Native uint64 maximum must remain exact decimal");
}

void incomplete_observations_remain_incomplete() {
  const diagnostics::status states[]{diagnostics::status::disabled, diagnostics::status::busy,
      diagnostics::status::not_started, diagnostics::status::wrong_owner_thread,
      diagnostics::status::unavailable};
  for (const auto state : states) {
    diagnostics::sample sample;
    sample.state = state;
    sample.clock_invalid = true;
    sample.counter_saturated = true;
    const auto record = serialize(sample);
    require(record.at("state") != "ready" && record.at("clock_invalid") == true &&
            record.at("counter_saturated") == true,
            "Missing/contended observations cannot turn into ready zero counts");
  }
  require(std::string(status_name(static_cast<diagnostics::status>(999))) == "unknown",
          "Unrecognized future status must remain unqualified");
}

void shared_engine_and_context_receipts() {
  diagnostics::software_host_runtime view;
  view.context_receipt_before = 9007199254740993ULL;
  view.context_receipt_after = 9007199254740994ULL;
  view.process_engine_fifo.queued = 83;
  view.bridge_worker_fifo.queued = 7;
  view.requests.renewal.limit = 1;
  view.requests.renewal.outstanding = 1;
  const auto record = serialize(view);
  require(!record.contains("process_engine_fifo") && record.at("process_engine_scope") == "process",
          "A shared process queue cannot be counted once per view");
  require(record.at("context_receipt_before") == "9007199254740993" &&
          record.at("context_receipt_after") == "9007199254740994" &&
          record.at("bridge_worker_fifo").at("queued") == "7",
          "View receipt transition and distinct worker ownership must survive");
  require(record.at("requests").at("renewal").at("outstanding") == "1",
          "Reserved renewal work cannot disappear from the owner record");
}

void retained_owners_and_capacity_fields() {
  diagnostics::bridge_runtime bridge;
  bridge.task_service.store_entries = 3;
  bridge.task_service.terminal_entries = 2;
  bridge.task_service.worker_slots = 1;
  bridge.task_service.store_string_capacity_bytes = 9007199254740993ULL;
  bridge.permission_store.has_scope_capacity_bound = true;
  bridge.permission_store.module_scope_slots_used = 4;
  bridge.permission_store.pending_alias_reservations = 1;
  bridge.permission_store.renewal_alias_transient_limit = 5;
  bridge.gui_commands.retained_weak_slots = 6;
  bridge.gui_commands.effect_borrows = 2;
  bridge.gui_commands.closed = true;
  const auto record = json::parse(serialize(bridge).dump());
  require(record.at("task_service").at("terminal_entries") == "2" &&
          record.at("task_service").at("worker_slots") == "1" &&
          record.at("task_service").at("store_string_capacity_bytes") == "9007199254740993",
          "Terminal records and still-owned workers/capacity must be observable");
  require(record.at("permission_store").at("has_scope_capacity_bound") == true &&
          record.at("permission_store").at("module_scope_slots_used") == "4" &&
          record.at("permission_store").at("pending_alias_reservations") == "1" &&
          record.at("permission_store").at("renewal_alias_transient_limit") == "5",
          "Module owners and reserved aliases cannot be inferred away after close");
  require(record.at("gui_commands").at("closed") == true &&
          record.at("gui_commands").at("retained_weak_slots") == "6" &&
          record.at("gui_commands").at("effect_borrows") == "2",
          "Closed GUI state does not imply that all borrowed owners have vanished");
}

void terminal_snapshot_keeps_original_observation() {
  diagnostics::process_engine_runtime process;
  process.retired = true;
  process.terminal_snapshot = true;
  process.engine_fifo.observation.state = diagnostics::status::ready;
  process.engine_fifo.observation.owner_epoch = 19;
  process.engine_fifo.observation.begin_monotonic_us = 12345;
  process.engine_fifo.observation.end_monotonic_us = 12349;
  process.engine_fifo.discarded_on_stop_total = 11;
  const auto record = serialize(process);
  require(record.at("scope") == "process" && record.at("terminal_snapshot") == true &&
          record.at("initialized") == false && record.at("mounted_hosts") == "0",
          "Terminal diagnostic readiness must not claim a running Engine");
  const auto observation = record.at("engine_fifo").at("observation");
  require(observation.at("owner_epoch") == "19" &&
          observation.at("begin_monotonic_us") == "12345" &&
          observation.at("end_monotonic_us") == "12349" &&
          record.at("engine_fifo").at("discarded_on_stop_total") == "11",
          "A terminal snapshot is retained evidence, not a freshly sampled clock");
}
} // namespace

int main() {
  try {
    exact_native_precision();
    incomplete_observations_remain_incomplete();
    shared_engine_and_context_receipts();
    retained_owners_and_capacity_fields();
    terminal_snapshot_keeps_original_observation();
    std::cout << "native owner serialization: 5 cases passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
