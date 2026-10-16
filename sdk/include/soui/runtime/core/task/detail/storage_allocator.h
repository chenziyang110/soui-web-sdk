#pragma once
// Private TaskStore representation dependency. Public string allocators are unchanged.
#include <soui/diagnostics/storage_observation.h>
#include <soui/runtime/core/task/task_store_quota.h>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>

namespace soui::runtime::detail {
class task_store_allocation_ledger;
class task_store_request_quota;
#ifdef SOUI_TASK_STORE_STORAGE_TEST_ONLY
namespace task_store_storage_test {
void before_allocate(task_store_allocation_ledger&, std::size_t);
void ledger_retired(const task_store_allocation_ledger&) noexcept;
void quota_before_allocate(const task_store_request_quota&) noexcept;
void quota_retired(const task_store_request_quota&) noexcept;
}
#endif

// Used only under the Store's existing lock, or during construction/destruction.
// This pool is independent of the optional diagnostic ledgers and their clocks.
class task_store_request_quota final {
 public:
  explicit task_store_request_quota(std::optional<task_store_request_quota_policy> policy = std::nullopt) noexcept
      : policy_(policy), owner_epoch_(next_owner_epoch()) {}
  ~task_store_request_quota() {
    assert(!valid_ || live_bytes_ == 0);
#ifdef SOUI_TASK_STORE_STORAGE_TEST_ONLY
    task_store_storage_test::quota_retired(*this);
#endif
  }
  task_store_request_quota(const task_store_request_quota&) = delete;
  task_store_request_quota& operator=(const task_store_request_quota&) = delete;
  [[nodiscard]] std::uint64_t live_requested_bytes() const noexcept { return live_bytes_; }
  [[nodiscard]] bool valid() const noexcept { return valid_; }
  [[nodiscard]] const std::optional<task_store_request_quota_policy>& policy() const noexcept { return policy_; }
  [[nodiscard]] task_store_request_quota_snapshot snapshot() const noexcept {
    task_store_request_quota_snapshot out;
    if (policy_) out.max_live_requested_bytes = policy_->max_live_requested_bytes;
    out.owner_epoch = owner_epoch_; out.owner_epoch_valid = owner_epoch_ != 0;
    out.charged_live_requested_bytes = live_bytes_; out.charged_peak_requested_bytes = peak_bytes_;
    out.successful_live_requested_bytes = successful_bytes_;
    out.successful_peak_requested_bytes = successful_peak_bytes_;
    out.denied_requests = denied_requests_; out.denied_requests_exact = denied_requests_exact_;
    out.credit_valid = valid_;
    out.status = valid_ && out.owner_epoch_valid && denied_requests_exact_
        ? task_store_request_quota_status::available : task_store_request_quota_status::invalid;
    return out;
  }
  [[nodiscard]] static std::size_t checked_request_bytes(std::size_t count, std::size_t element_bytes) {
    if (!element_bytes || count > (std::numeric_limits<std::size_t>::max)() / element_bytes ||
        count > (std::numeric_limits<std::uint64_t>::max)() / element_bytes)
      throw std::bad_array_new_length();
    return count * element_bytes;
  }

  class reservation final {
   public:
    reservation(task_store_request_quota* owner, std::uint64_t bytes) : owner_(owner), bytes_(bytes) {
      if (owner_) owner_->acquire(bytes_);
    }
    ~reservation() { if (owner_) owner_->cancel_reservation(bytes_); }
    reservation(const reservation&) = delete;
    reservation& operator=(const reservation&) = delete;
    // A successful allocator now owns the credit until its physical free.
    void commit() noexcept {
      if (owner_) owner_->commit_reservation(bytes_);
      owner_ = nullptr;
    }
   private:
    task_store_request_quota* owner_;
    const std::uint64_t bytes_;
  };

  void release(std::uint64_t bytes) noexcept {
    if (bytes > live_bytes_ || bytes > successful_bytes_) { valid_ = false; return; }
    live_bytes_ -= bytes; successful_bytes_ -= bytes;
  }
 private:
  // Process-local pool identity. Exhaustion stays at the sentinel, never wraps
  // or reuses an identity. Identity invalidity changes evidence, not admission.
  [[nodiscard]] static std::uint64_t next_owner_epoch() noexcept {
    static std::atomic<std::uint64_t> next{1};
    auto value = next.load(std::memory_order_relaxed);
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    while (value != maximum) {
      if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) return value;
    }
    return 0;
  }
  void denied() noexcept {
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    if (denied_requests_ == maximum) denied_requests_exact_ = false;
    else ++denied_requests_;
  }
  void cancel_reservation(std::uint64_t bytes) noexcept {
    if (successful_bytes_ > live_bytes_ || bytes > live_bytes_ - successful_bytes_) {
      valid_ = false; return;
    }
    live_bytes_ -= bytes;
  }
  void commit_reservation(std::uint64_t bytes) noexcept {
    if (successful_bytes_ > live_bytes_ || bytes > live_bytes_ - successful_bytes_) {
      valid_ = false; return;
    }
    successful_bytes_ += bytes;
    if (successful_bytes_ > successful_peak_bytes_) successful_peak_bytes_ = successful_bytes_;
  }
  void acquire(std::uint64_t bytes) {
    if (!valid_) { denied(); throw task_store_request_quota_exceeded{}; }
    if (bytes > (std::numeric_limits<std::uint64_t>::max)() - live_bytes_) {
      valid_ = false; denied();
      throw task_store_request_quota_exceeded{};
    }
    if (policy_ && (live_bytes_ > policy_->max_live_requested_bytes ||
                   bytes > policy_->max_live_requested_bytes - live_bytes_)) {
      if (live_bytes_ > policy_->max_live_requested_bytes) valid_ = false;
      denied();
      throw task_store_request_quota_exceeded{};
    }
    live_bytes_ += bytes;
    if (live_bytes_ > peak_bytes_) peak_bytes_ = live_bytes_;
  }
  const std::optional<task_store_request_quota_policy> policy_;
  const std::uint64_t owner_epoch_;
  std::uint64_t live_bytes_{0}, peak_bytes_{0}, successful_bytes_{0}, successful_peak_bytes_{0};
  std::uint64_t denied_requests_{0};
  bool denied_requests_exact_{true};
  bool valid_{true};
};

class task_store_allocation_ledger final {
 public:
  explicit task_store_allocation_ledger(bool observed = false) noexcept : enabled(observed) {}
#ifdef SOUI_TASK_STORE_STORAGE_TEST_ONLY
  ~task_store_allocation_ledger() { task_store_storage_test::ledger_retired(*this); }
#endif
  task_store_allocation_ledger(const task_store_allocation_ledger&) = delete;
  task_store_allocation_ledger& operator=(const task_store_allocation_ledger&) = delete;

  void add(std::uint64_t& value, std::uint64_t amount) noexcept {
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    if (amount > maximum - value) { value = maximum; saturated = true; }
    else value += amount;
  }
  void allocated(std::size_t bytes) noexcept {
    if (!enabled) return;
    add(totals.allocations_total, 1); add(totals.allocated_bytes_total, bytes);
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    if (bytes > maximum - totals.current_bytes || totals.current_allocations == maximum)
      current_inexact = saturated = true;
    if (current_inexact) totals.current_bytes = totals.current_allocations = maximum;
    else { totals.current_bytes += bytes; ++totals.current_allocations; }
    if (totals.current_bytes > totals.high_water_bytes) totals.high_water_bytes = totals.current_bytes;
    if (totals.current_allocations > totals.high_water_allocations) totals.high_water_allocations = totals.current_allocations;
  }
  void deallocated(std::size_t bytes) noexcept {
    if (!enabled) return;
    add(totals.deallocations_total, 1); add(totals.deallocated_bytes_total, bytes);
    if (!current_inexact && (bytes > totals.current_bytes || !totals.current_allocations)) {
      current_inexact = saturated = invalid = true;
    }
    if (current_inexact) totals.current_bytes = totals.current_allocations = (std::numeric_limits<std::uint64_t>::max)();
    else { totals.current_bytes -= bytes; --totals.current_allocations; }
  }
  void failed() noexcept { if (enabled) add(totals.failed_allocations_total, 1); }
  soui::sdk::diagnostics::allocation_requests snapshot() const noexcept {
    auto result = totals;
    result.current_bytes_exact = result.current_allocations_exact = enabled && !current_inexact;
    return result;
  }
  const bool enabled;
  bool saturated{false}, current_inexact{false}, invalid{false};
  soui::sdk::diagnostics::allocation_requests totals;
};

template <class T>
class task_store_allocator {
 public:
  using value_type = T;
  using propagate_on_container_copy_assignment = std::false_type;
  using propagate_on_container_move_assignment = std::false_type;
  using propagate_on_container_swap = std::false_type;
  using is_always_equal = std::false_type;
  template <class U> struct rebind { using other = task_store_allocator<U>; };

  task_store_allocator() noexcept = default;
  explicit task_store_allocator(task_store_allocation_ledger* ledger) noexcept : ledger_(ledger) {}
  template <class U> task_store_allocator(const task_store_allocator<U>& other) noexcept : ledger_(other.owner()) {}
  [[nodiscard]] task_store_allocation_ledger* owner() const noexcept { return ledger_; }
  [[nodiscard]] T* allocate(std::size_t count) {
    try {
      // std::allocator's count bound prevents request-byte multiplication overflow.
      if (count > (std::numeric_limits<std::size_t>::max)() / sizeof(T)) throw std::bad_array_new_length();
#ifdef SOUI_TASK_STORE_STORAGE_TEST_ONLY
      if (ledger_) task_store_storage_test::before_allocate(*ledger_, count * sizeof(T));
#endif
      auto allocator = std::allocator<T>{};
      auto* result = std::allocator_traits<std::allocator<T>>::allocate(allocator, count);
      if (ledger_) ledger_->allocated(count * sizeof(T));
      return result;
    } catch (...) { if (ledger_) ledger_->failed(); throw; }
  }
  void deallocate(T* pointer, std::size_t count) noexcept {
    auto allocator = std::allocator<T>{};
    std::allocator_traits<std::allocator<T>>::deallocate(allocator, pointer, count);
    if (ledger_) ledger_->deallocated(count * sizeof(T));
  }
  template <class U> bool operator==(const task_store_allocator<U>& other) const noexcept { return ledger_ == other.owner(); }
  template <class U> bool operator!=(const task_store_allocator<U>& other) const noexcept { return !(*this == other); }
 private:
  task_store_allocation_ledger* ledger_{nullptr};
};
} // namespace soui::runtime::detail
