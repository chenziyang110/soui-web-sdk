#pragma once

// Native scalar diagnostics. Opt-in construction; snapshots do not grant authority.
// Native callers explicitly opt in at construction. No Bridge/wire method.
#include <array>
#include <cstdint>
#include <string>
#include <soui/diagnostics/renderer_observation_abi.h>
#include <soui/diagnostics/renderer_model_abi.h>

namespace soui::sdk::diagnostics {

enum class mode : std::uint32_t { disabled, scalar };
enum class status : std::uint32_t {
  disabled, ready, busy, not_started, wrong_owner_thread, unavailable,
};

// Each component is sampled separately under its actual owner lock. Epochs are
// local diagnostic correlations, never credentials or document identities.
struct sample final {
  status state{status::disabled};
  std::uint64_t owner_epoch{0};
  std::uint64_t begin_monotonic_us{0};
  std::uint64_t end_monotonic_us{0};
  bool counter_saturated{false};
  bool clock_invalid{false};
};

struct queue final {
  sample observation;
  std::uint64_t queued{0}, executing{0}, high_water{0};
  std::uint64_t oldest_queued_age_us{0}, current_callback_age_us{0};
  std::uint64_t posted_total{0}, stopped_rejections_total{0};
  // Actual full post/reservation attempts for this Engine only; default-off is zero.
  std::uint64_t full_rejections_total{0};
  std::uint64_t discarded_on_stop_total{0};
  // The actual generic Engine FIFO bound; reservations are not queued work.
  bool has_capacity_bound{false};
  std::uint64_t capacity_bound{0};
};

struct tasks final {
  sample observation;
  std::uint64_t worker_limit{0}, inflight_limit{0}, subscription_limit{0};
  std::uint64_t worker_slots{0}, active_handlers{0};
  std::uint64_t inflight{0}, queued{0}, subscriptions{0};
  std::uint64_t inflight_high_water{0}, queued_high_water{0};
  std::uint64_t subscription_high_water{0}, oldest_queued_age_us{0};
  std::uint64_t committed_starts_total{0}, duplicate_starts_total{0};
  std::uint64_t inflight_full_rejections_total{0};
  std::uint64_t store_full_rejections_total{0};
  std::uint64_t subscription_full_rejections_total{0};
  std::uint64_t finished_handlers_total{0}, cancelled_before_handler_total{0};
  std::uint64_t store_limit{0}, store_entries{0}, terminal_entries{0};
  std::uint64_t queued_entries{0}, running_entries{0};
  std::uint64_t timeout_observed_entries{0}, cancel_requested_entries{0};
  std::uint64_t committed_terminal_evictions_total{0};
  std::uint64_t owner_entries{0}, operation_entries{0}, result_cache_entries{0};
  // String size/capacity sums for the explicitly enumerated owned fields only.
  // These are NOT allocator, container-node, external-borrow, or heap totals.
  std::uint64_t store_string_size_bytes{0}, store_string_capacity_bytes{0};
  std::uint64_t result_string_size_bytes{0}, result_string_capacity_bytes{0};
  bool stopping{false};
};

struct lane final {
  std::uint64_t limit{0}, outstanding{0}, high_water{0};
  std::uint64_t oldest_outstanding_age_us{0};
  std::uint64_t acquired_total{0}, released_total{0};
  std::uint64_t full_rejections_total{0}, duplicate_rejections_total{0};
};

struct software_requests final {
  sample observation;
  lane ordinary, control, renewal; // actual limits 256 / 128 / 1
  std::uint64_t queue_expired_total{0}, stale_context_drop_total{0};
  std::uint64_t worker_post_rejections_total{0}, engine_post_rejections_total{0};
  // Outstanding leases extend through queued/executing/native resolver work.
  // Independent lane capacity is not FIFO scheduling priority or authorization.
};

struct permissions final {
  sample observation;
  std::uint64_t stored_scope_owners{0}, bearer_alias_entries{0};
  std::uint64_t stored_previous_aliases{0};
  std::uint64_t issued_scopes_total{0}, rotated_leases_total{0};
  std::uint64_t idempotent_renewals_total{0}, renewal_too_early_total{0};
  std::uint64_t checked_renewal_rejections_total{0}, revoked_scopes_total{0};
  // Store counts include expired grants until explicit revoke. Pending issuance
  // reserves one initial alias. Module slots also include other stores and
  // externally held revoked/issuer-dead owners; the module sample is non-atomic.
  std::uint64_t stored_scope_limit{0}, pending_scope_reservations{0};
  std::uint64_t pending_alias_reservations{0}, committed_alias_limit{0};
  std::uint64_t renewal_alias_transient_limit{0}, bearer_alias_high_water{0};
  std::uint64_t module_scope_limit{0}, module_scope_slots_used{0};
  std::uint64_t store_scope_capacity_rejections_total{0};
  std::uint64_t module_scope_capacity_rejections_total{0}, alias_capacity_rejections_total{0};
  // Stored previous aliases are not equivalent to valid overlap credentials.
  bool has_scope_capacity_bound{false};
};

struct gui final {
  sample observation;
  std::uint64_t context_limit{0}, context_slots_used{0};
  std::uint64_t pending_live_contexts{0}, retained_weak_slots{0};
  std::uint64_t effect_queue_limit{0}, queued_effects{0}, effect_borrows{0};
  std::uint64_t effect_queue_high_water{0}, oldest_queued_effect_age_us{0};
  bool closed{false}, owner_poll_active{false};
};

struct bridge_runtime final {
  static constexpr std::uint32_t schema_version{1};
  tasks task_service;
  permissions permission_store;
  gui gui_commands; // not_started if the optional GUI coordinator is absent
};

struct software_host_runtime final {
  static constexpr std::uint32_t schema_version{1};
  software_requests requests;
  queue bridge_worker_fifo; // exact adapter-owned worker
  queue process_engine_fifo; // shared process runtime, not one view's work
  bool bridge_accepting{false};
  std::uint64_t context_receipt_before{0}, context_receipt_after{0};
};

// The shared Engine is one process owner, not one queue per mounted view.
// A terminal snapshot retains its original timestamp after actual stop/join.
enum class renderer_status : std::uint32_t { disabled, not_started, busy, unknown, ready, closed };

// Completed compositor calls in one loaded DLL; path alone does not bind bytes.
struct renderer_observation final {
  static constexpr std::uint32_t schema_version{1};
  renderer_status state{renderer_status::unknown};
  std::string module_path;
  bool module_matches{false}, identity_bound{false}, terminal_snapshot{false};
  wkeRendererProcessSnapshot native{};
};

struct process_engine_runtime final {
  static constexpr std::uint32_t schema_version{1};
  queue engine_fifo;
  std::uint64_t mounted_hosts{0};
  bool initialized{false}, retired{false}, terminal_snapshot{false};
};

// Independent optional v2 DTO. The existing renderer/queue layouts are retained.
struct renderer_model_observation final {
  static constexpr std::uint32_t schema_version{2};
  renderer_status state{renderer_status::unknown};
  std::string module_path;
  bool module_matches{false}, identity_bound{false}, terminal_snapshot{false};
  wkeRendererModelSnapshot native{};
};

} // namespace soui::sdk::diagnostics
