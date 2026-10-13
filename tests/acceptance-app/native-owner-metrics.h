#pragma once

#include <soui/diagnostics/runtime_metrics.h>
#include <json.hpp>
#include <string>

// Internal acceptance serialization, never a Web-supplied observation.
namespace soui::acceptance::metrics {
using json = nlohmann::json;
namespace diagnostics = soui::sdk::diagnostics;

inline const char* status_name(diagnostics::status state) noexcept {
  switch (state) {
    case diagnostics::status::disabled: return "disabled";
    case diagnostics::status::ready: return "ready";
    case diagnostics::status::busy: return "busy";
    case diagnostics::status::not_started: return "not_started";
    case diagnostics::status::wrong_owner_thread: return "wrong_owner_thread";
    case diagnostics::status::unavailable: return "unavailable";
  }
  return "unknown";
}

inline json serialize(const diagnostics::sample& value) {
  return {{"state", status_name(value.state)},
          {"owner_epoch", std::to_string(value.owner_epoch)},
          {"begin_monotonic_us", std::to_string(value.begin_monotonic_us)},
          {"end_monotonic_us", std::to_string(value.end_monotonic_us)},
          {"counter_saturated", value.counter_saturated},
          {"clock_invalid", value.clock_invalid}};
}

// Keep SDK field names and exact decimal uint64 values. JSON numbers would lose
// precision in downstream JavaScript well before a native uint64 saturates.
#define SOUI_ACCEPTANCE_COUNT(field) result[#field] = std::to_string(value.field)

inline json serialize(const diagnostics::queue& value) {
  json result{{"observation", serialize(value.observation)},
              {"has_capacity_bound", value.has_capacity_bound}};
  SOUI_ACCEPTANCE_COUNT(queued); SOUI_ACCEPTANCE_COUNT(executing);
  SOUI_ACCEPTANCE_COUNT(high_water); SOUI_ACCEPTANCE_COUNT(oldest_queued_age_us);
  SOUI_ACCEPTANCE_COUNT(current_callback_age_us); SOUI_ACCEPTANCE_COUNT(posted_total);
  SOUI_ACCEPTANCE_COUNT(stopped_rejections_total); SOUI_ACCEPTANCE_COUNT(full_rejections_total);
  SOUI_ACCEPTANCE_COUNT(discarded_on_stop_total); SOUI_ACCEPTANCE_COUNT(capacity_bound);
  return result;
}

inline json serialize(const diagnostics::tasks& value) {
  json result{{"observation", serialize(value.observation)}, {"stopping", value.stopping}};
  SOUI_ACCEPTANCE_COUNT(worker_limit); SOUI_ACCEPTANCE_COUNT(inflight_limit);
  SOUI_ACCEPTANCE_COUNT(subscription_limit); SOUI_ACCEPTANCE_COUNT(worker_slots);
  SOUI_ACCEPTANCE_COUNT(active_handlers); SOUI_ACCEPTANCE_COUNT(inflight);
  SOUI_ACCEPTANCE_COUNT(queued); SOUI_ACCEPTANCE_COUNT(subscriptions);
  SOUI_ACCEPTANCE_COUNT(inflight_high_water); SOUI_ACCEPTANCE_COUNT(queued_high_water);
  SOUI_ACCEPTANCE_COUNT(subscription_high_water); SOUI_ACCEPTANCE_COUNT(oldest_queued_age_us);
  SOUI_ACCEPTANCE_COUNT(committed_starts_total); SOUI_ACCEPTANCE_COUNT(duplicate_starts_total);
  SOUI_ACCEPTANCE_COUNT(inflight_full_rejections_total); SOUI_ACCEPTANCE_COUNT(store_full_rejections_total);
  SOUI_ACCEPTANCE_COUNT(subscription_full_rejections_total); SOUI_ACCEPTANCE_COUNT(finished_handlers_total);
  SOUI_ACCEPTANCE_COUNT(cancelled_before_handler_total); SOUI_ACCEPTANCE_COUNT(store_limit);
  SOUI_ACCEPTANCE_COUNT(store_entries); SOUI_ACCEPTANCE_COUNT(terminal_entries);
  SOUI_ACCEPTANCE_COUNT(queued_entries); SOUI_ACCEPTANCE_COUNT(running_entries);
  SOUI_ACCEPTANCE_COUNT(timeout_observed_entries); SOUI_ACCEPTANCE_COUNT(cancel_requested_entries);
  SOUI_ACCEPTANCE_COUNT(committed_terminal_evictions_total); SOUI_ACCEPTANCE_COUNT(owner_entries);
  SOUI_ACCEPTANCE_COUNT(operation_entries); SOUI_ACCEPTANCE_COUNT(result_cache_entries);
  SOUI_ACCEPTANCE_COUNT(store_string_size_bytes); SOUI_ACCEPTANCE_COUNT(store_string_capacity_bytes);
  SOUI_ACCEPTANCE_COUNT(result_string_size_bytes); SOUI_ACCEPTANCE_COUNT(result_string_capacity_bytes);
  return result;
}

inline json serialize(const diagnostics::lane& value) {
  json result = json::object();
  SOUI_ACCEPTANCE_COUNT(limit); SOUI_ACCEPTANCE_COUNT(outstanding);
  SOUI_ACCEPTANCE_COUNT(high_water); SOUI_ACCEPTANCE_COUNT(oldest_outstanding_age_us);
  SOUI_ACCEPTANCE_COUNT(acquired_total); SOUI_ACCEPTANCE_COUNT(released_total);
  SOUI_ACCEPTANCE_COUNT(full_rejections_total); SOUI_ACCEPTANCE_COUNT(duplicate_rejections_total);
  return result;
}

inline json serialize(const diagnostics::software_requests& value) {
  json result{{"observation", serialize(value.observation)},
              {"ordinary", serialize(value.ordinary)},
              {"control", serialize(value.control)}, {"renewal", serialize(value.renewal)}};
  SOUI_ACCEPTANCE_COUNT(queue_expired_total); SOUI_ACCEPTANCE_COUNT(stale_context_drop_total);
  SOUI_ACCEPTANCE_COUNT(worker_post_rejections_total); SOUI_ACCEPTANCE_COUNT(engine_post_rejections_total);
  return result;
}

inline json serialize(const diagnostics::permissions& value) {
  json result{{"observation", serialize(value.observation)},
              {"has_scope_capacity_bound", value.has_scope_capacity_bound}};
  SOUI_ACCEPTANCE_COUNT(stored_scope_owners); SOUI_ACCEPTANCE_COUNT(bearer_alias_entries);
  SOUI_ACCEPTANCE_COUNT(stored_previous_aliases); SOUI_ACCEPTANCE_COUNT(issued_scopes_total);
  SOUI_ACCEPTANCE_COUNT(rotated_leases_total); SOUI_ACCEPTANCE_COUNT(idempotent_renewals_total);
  SOUI_ACCEPTANCE_COUNT(renewal_too_early_total); SOUI_ACCEPTANCE_COUNT(checked_renewal_rejections_total);
  SOUI_ACCEPTANCE_COUNT(revoked_scopes_total); SOUI_ACCEPTANCE_COUNT(stored_scope_limit);
  SOUI_ACCEPTANCE_COUNT(pending_scope_reservations); SOUI_ACCEPTANCE_COUNT(pending_alias_reservations);
  SOUI_ACCEPTANCE_COUNT(committed_alias_limit); SOUI_ACCEPTANCE_COUNT(renewal_alias_transient_limit);
  SOUI_ACCEPTANCE_COUNT(bearer_alias_high_water); SOUI_ACCEPTANCE_COUNT(module_scope_limit);
  SOUI_ACCEPTANCE_COUNT(module_scope_slots_used); SOUI_ACCEPTANCE_COUNT(store_scope_capacity_rejections_total);
  SOUI_ACCEPTANCE_COUNT(module_scope_capacity_rejections_total); SOUI_ACCEPTANCE_COUNT(alias_capacity_rejections_total);
  return result;
}

inline json serialize(const diagnostics::gui& value) {
  json result{{"observation", serialize(value.observation)}, {"closed", value.closed},
              {"owner_poll_active", value.owner_poll_active}};
  SOUI_ACCEPTANCE_COUNT(context_limit); SOUI_ACCEPTANCE_COUNT(context_slots_used);
  SOUI_ACCEPTANCE_COUNT(pending_live_contexts); SOUI_ACCEPTANCE_COUNT(retained_weak_slots);
  SOUI_ACCEPTANCE_COUNT(effect_queue_limit); SOUI_ACCEPTANCE_COUNT(queued_effects);
  SOUI_ACCEPTANCE_COUNT(effect_borrows); SOUI_ACCEPTANCE_COUNT(effect_queue_high_water);
  SOUI_ACCEPTANCE_COUNT(oldest_queued_effect_age_us);
  return result;
}

inline json serialize(const diagnostics::bridge_runtime& value) {
  return {{"schema_version", diagnostics::bridge_runtime::schema_version},
          {"task_service", serialize(value.task_service)},
          {"permission_store", serialize(value.permission_store)},
          {"gui_commands", serialize(value.gui_commands)}};
}

inline json serialize(const diagnostics::software_host_runtime& value) {
  json result{{"schema_version", diagnostics::software_host_runtime::schema_version},
              {"requests", serialize(value.requests)},
              {"bridge_worker_fifo", serialize(value.bridge_worker_fifo)},
              {"bridge_accepting", value.bridge_accepting},
              {"process_engine_scope", "process"}};
  SOUI_ACCEPTANCE_COUNT(context_receipt_before); SOUI_ACCEPTANCE_COUNT(context_receipt_after);
  // The shared Engine is serialized once by the process owner, never per view.
  return result;
}

inline const char* renderer_status_name(diagnostics::renderer_status value) noexcept {
  switch (value) {
    case diagnostics::renderer_status::disabled: return "disabled";
    case diagnostics::renderer_status::not_started: return "not_started";
    case diagnostics::renderer_status::busy: return "busy";
    case diagnostics::renderer_status::ready: return "ready";
    case diagnostics::renderer_status::closed: return "closed";
    default: return "unknown";
  }
}

inline json serialize(const diagnostics::renderer_observation& value) {
  const auto& raw = value.native;
  json result{{"schemaVersion", 1}, {"scope", "bound-dll-completed-compositor-calls"},
              {"state", renderer_status_name(value.state)}, {"modulePath", value.module_path},
              {"moduleMatches", value.module_matches}, {"identityBound", value.identity_bound},
              {"terminalSnapshot", value.terminal_snapshot}, {"structSize", raw.structSize},
              {"abiVersion", raw.abiVersion}, {"status", raw.status}, {"flags", raw.flags},
              {"lastDrawKind", raw.lastDrawKind}, {"reserved", raw.reserved}};
#define SOUI_RENDERER_COUNT(name) result[#name] = std::to_string(raw.name)
  SOUI_RENDERER_COUNT(processId); SOUI_RENDERER_COUNT(processCreationFileTime);
  SOUI_RENDERER_COUNT(engineOwnerThreadId); SOUI_RENDERER_COUNT(qpcFrequency);
  SOUI_RENDERER_COUNT(recordingEpoch); SOUI_RENDERER_COUNT(renderersCreatedTotal);
  SOUI_RENDERER_COUNT(renderersClosedTotal); SOUI_RENDERER_COUNT(renderersLive);
  SOUI_RENDERER_COUNT(rasterDrawsTotal); SOUI_RENDERER_COUNT(gpuSkiaDrawsTotal);
  SOUI_RENDERER_COUNT(drawEventsTotal); SOUI_RENDERER_COUNT(lastDrawQpc);
  SOUI_RENDERER_COUNT(lastDrawThreadId); SOUI_RENDERER_COUNT(lastDrawProcessId);
  SOUI_RENDERER_COUNT(lastRendererCloseQpc); SOUI_RENDERER_COUNT(closedSnapshotQpc);
  SOUI_RENDERER_COUNT(engineFinalizedQpc); SOUI_RENDERER_COUNT(initializedQpc);
#undef SOUI_RENDERER_COUNT
  return result;
}

inline json serialize(const diagnostics::renderer_model_observation& value) {
  const auto& raw = value.native;
  diagnostics::renderer_observation compositor;
  compositor.state = value.state; compositor.module_path = value.module_path;
  compositor.module_matches = value.module_matches; compositor.identity_bound = value.identity_bound;
  compositor.terminal_snapshot = value.terminal_snapshot; compositor.native = raw.compositor;
  json result{{"schemaVersion", 2}, {"scope", "bound-compatibility-backend-workload"},
      {"state", renderer_status_name(value.state)}, {"modulePath", value.module_path},
      {"moduleMatches", value.module_matches}, {"identityBound", value.identity_bound},
      {"terminalSnapshot", value.terminal_snapshot}, {"structSize", raw.structSize},
      {"abiVersion", raw.abiVersion}, {"status", raw.status}, {"flags", raw.flags},
      {"providerId", raw.providerId}, {"backendId", raw.backendId},
      {"compiledGpuSupport", raw.compiledGpuSupport}, {"factoryCoverageMask", raw.factoryCoverageMask},
      {"compositor", serialize(compositor)}};
#define SOUI_MODEL_COUNT(name) result[#name] = std::to_string(raw.name)
  SOUI_MODEL_COUNT(backendInitializationQpc); SOUI_MODEL_COUNT(mediaFactoryAttempts);
  SOUI_MODEL_COUNT(pluginFactoryAttempts); SOUI_MODEL_COUNT(offscreenFactoryAttempts);
  SOUI_MODEL_COUNT(sharedContextFactoryAttempts); SOUI_MODEL_COUNT(externalFactoryAttempts);
  SOUI_MODEL_COUNT(platformMismatchTotal); SOUI_MODEL_COUNT(unknownRouteTotal);
#undef SOUI_MODEL_COUNT
  return result;
}

inline json serialize(const diagnostics::process_engine_runtime& value) {
  json result{{"schema_version", diagnostics::process_engine_runtime::schema_version},
              {"scope", "process"}, {"engine_fifo", serialize(value.engine_fifo)},
              {"initialized", value.initialized}, {"retired", value.retired},
              {"terminal_snapshot", value.terminal_snapshot}};
  SOUI_ACCEPTANCE_COUNT(mounted_hosts);
  return result;
}

#undef SOUI_ACCEPTANCE_COUNT
} // namespace soui::acceptance::metrics
