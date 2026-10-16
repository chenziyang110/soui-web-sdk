#pragma once

#include <cstdint>
#include <new>
#include <optional>

namespace soui::runtime {
// Native construction only. nullopt is unconfigured; zero refuses positive
// private requests. Neither value describes heap reservation or other owners.
struct task_store_request_quota_policy {
  std::uint64_t max_live_requested_bytes;
};
// Per Store requested bytes, not total heap or process memory. Explicit native
// nullopt/zero/finite policy constructors retain their independent meaning.
inline constexpr task_store_request_quota_policy default_task_store_request_quota{128ull * 1024 * 1024};

class task_store_request_quota_exceeded final : public std::bad_alloc {
 public:
  const char* what() const noexcept override { return "TaskStore private request quota exceeded"; }
};

enum class task_store_request_quota_status { available, busy, invalid, unavailable };

// Separate native-only observation, independent of diagnostics::mode and all
// existing storage DTOs. No strings, allocation, clock, or runtime setter.
// Fields describe a single successfully locked pool, not allocator heap bytes.
// On busy/unavailable they are cleared and must not be treated as evidence.
struct task_store_request_quota_snapshot {
  static constexpr std::uint32_t schema_version{1};
  task_store_request_quota_status status{task_store_request_quota_status::unavailable};
  std::optional<std::uint64_t> max_live_requested_bytes;
  std::uint64_t owner_epoch{0};
  bool owner_epoch_valid{false};
  // Includes old, candidates, retirement buffers and outstanding reservations.
  std::uint64_t charged_live_requested_bytes{0}, charged_peak_requested_bytes{0};
  // Added only at successful reservation.commit; removed after physical free.
  std::uint64_t successful_live_requested_bytes{0}, successful_peak_requested_bytes{0};
  // Quota refusal only: upstream length/heap/hook errors are not quota denials.
  std::uint64_t denied_requests{0};
  bool denied_requests_exact{false}, credit_valid{false};
};
}  // namespace soui::runtime
