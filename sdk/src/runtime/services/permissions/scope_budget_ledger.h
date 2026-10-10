#pragma once

#include <atomic>
#include <cstddef>
#include <exception>
#include <memory>
#include <utility>

namespace soui::runtime::detail {

// Capacity state only. No issuer/store/authority pointer or callback.
class scope_budget_ledger final {
 public:
  static constexpr std::size_t limit = 512;
  [[nodiscard]] bool reserve() noexcept {
    auto count = outstanding_.load(std::memory_order_relaxed);
    while (count < limit) {
      if (outstanding_.compare_exchange_weak(count, count + 1,
          std::memory_order_relaxed)) return true;
    }
    return false;
  }
  void refund() noexcept {
    if (outstanding_.fetch_sub(1, std::memory_order_relaxed) == 0) std::terminate();
  }
  [[nodiscard]] std::size_t outstanding() const noexcept {
    return outstanding_.load(std::memory_order_relaxed);
  }
 private:
  std::atomic<std::size_t> outstanding_{0};
};

// An uncommitted reservation or one real scope owner owns exactly one slot.
// The ledger can outlive issuer destruction without preserving its authority.
class scope_budget_slot final {
 public:
  scope_budget_slot() noexcept = default;
  scope_budget_slot(const scope_budget_slot&) = delete;
  scope_budget_slot& operator=(const scope_budget_slot&) = delete;
  scope_budget_slot(scope_budget_slot&& other) noexcept : ledger_(std::move(other.ledger_)) {}
  scope_budget_slot& operator=(scope_budget_slot&& other) noexcept {
    if (this != &other) { reset(); ledger_ = std::move(other.ledger_); }
    return *this;
  }
  ~scope_budget_slot() { reset(); }
  [[nodiscard]] static scope_budget_slot acquire(
      const std::shared_ptr<scope_budget_ledger>& ledger) noexcept {
    scope_budget_slot slot;
    if (ledger->reserve()) slot.ledger_ = ledger;
    return slot;
  }
  [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(ledger_); }
 private:
  void reset() noexcept {
    if (ledger_) { auto retired = std::move(ledger_); retired->refund(); }
  }
  std::shared_ptr<scope_budget_ledger> ledger_;
};

} // namespace soui::runtime::detail
