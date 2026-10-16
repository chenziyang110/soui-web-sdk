#pragma once
#include <soui/diagnostics/runtime_metrics.h>
#include <cstdint>

namespace soui::sdk::diagnostics {

struct allocation_requests final {
  std::uint64_t current_bytes{0}, high_water_bytes{0};
  std::uint64_t current_allocations{0}, high_water_allocations{0};
  std::uint64_t allocations_total{0}, deallocations_total{0}, failed_allocations_total{0};
  std::uint64_t allocated_bytes_total{0}, deallocated_bytes_total{0};
  bool current_bytes_exact{false}, current_allocations_exact{false};
};

// Capacity is a string diagnostic, not an allocator request or heap total.
struct storage_strings final {
  std::uint64_t fields{0}, size_bytes{0}, capacity_bytes{0};
};

// Independent native SDK observation; existing runtime/renderer/cache DTOs stay unchanged.
struct task_store_storage final {
  static constexpr std::uint32_t schema_version = 1;
  static constexpr std::uint64_t map_allocator_requests = 1;
  static constexpr std::uint64_t committed_string_diagnostics = 2;
  static constexpr std::uint64_t string_allocator_gap = 1;
  static constexpr std::uint64_t phase_partition_gap = 2;
  static constexpr std::uint64_t prepared_retiring_strings_gap = 4;
  static constexpr std::uint64_t other_sdk_owners_gap = 8;
  static constexpr std::uint64_t allocator_heap_external_gap = 16;
  static constexpr std::uint64_t final_disposal_receipt_gap = 32;
  sample observation;
  std::uint64_t coverage_mask{0}, gap_mask{0};
  // All three maps, including prepare/retirement allocations; not phase-partitioned.
  allocation_requests map_requests;
  std::uint64_t task_entries{0}, operation_entries{0}, scope_entries{0};
  storage_strings snapshot_strings, task_keys, operation_strings, scope_strings, id_prefix;
};

} // namespace soui::sdk::diagnostics
