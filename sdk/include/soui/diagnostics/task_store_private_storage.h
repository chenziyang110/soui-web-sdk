#pragma once
#include <soui/diagnostics/storage_observation.h>
#include <cstdint>

namespace soui::sdk::diagnostics {

// Separate native SDK DTO. Requested allocator storage only; old DTOs stay intact.
struct task_store_private_storage final {
  static constexpr std::uint32_t schema_version = 1;
  enum coverage : std::uint64_t {
    map_allocator_requests = 1,
    private_string_allocator_requests = 2,
    aggregate_allocator_requests = 4,
  };
  enum gaps : std::uint64_t {
    // Parameters, public prepared receipt/evicted_id, formatted ID and returned copies.
    public_temporary_copies_gap = 1,
    // Total requests include private preparation/retirement, without a stage split.
    phase_partition_gap = 2,
    // Service/jobs/results/subscriptions/handlers and shared pointees, among others.
    other_sdk_owners_gap = 4,
    // Object storage, allocator reserve/fragmentation, whole heap and kernel caches.
    allocator_heap_external_gap = 8,
    // No callable post-destruction public receipt; owner must still be alive to read.
    final_disposal_receipt_gap = 16,
  };
  sample observation;
  std::uint64_t coverage_mask{0}, gap_mask{0};
  allocation_requests map_requests, private_string_requests, aggregate_requests;
  std::uint64_t task_entries{0}, operation_entries{0}, scope_entries{0};
};

} // namespace soui::sdk::diagnostics
