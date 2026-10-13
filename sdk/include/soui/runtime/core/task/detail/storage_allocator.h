#pragma once
// Private TaskStore representation dependency. Public string allocators are unchanged.
#include <soui/diagnostics/storage_observation.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>

namespace soui::runtime::detail {
class task_store_allocation_ledger;
#ifdef SOUI_TASK_STORE_STORAGE_TEST_ONLY
namespace task_store_storage_test {
void before_allocate(task_store_allocation_ledger&, std::size_t);
void ledger_retired(const task_store_allocation_ledger&) noexcept;
}
#endif

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
