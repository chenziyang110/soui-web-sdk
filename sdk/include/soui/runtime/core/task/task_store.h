#pragma once

#include <soui/diagnostics/runtime_metrics.h>
#include <soui/diagnostics/storage_observation.h>
#include <soui/diagnostics/task_store_private_storage.h>
#include <soui/runtime/core/task/detail/storage_allocator.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <soui/runtime/core/result/result.h>

namespace soui::runtime {
#ifdef SOUI_TASK_TERMINAL_QUOTA_TEST_ONLY
namespace detail { struct task_terminal_quota_test_access; }
#endif

enum class task_state {
  queued,
  running,
  timeout_observed,
  cancel_requested,
  succeeded,
  failed,
  cancelled,
  partial,
};

struct task_snapshot {
  std::string task_id;
  std::string operation_id;
  task_state state{task_state::queued};
  std::uint32_t progress{0};
  std::uint64_t sequence{0};
  std::string message;
  operation_result result;
};

class task_store {
 public:
  static constexpr std::size_t max_tasks{1024};

  // Constructors without a policy use default_task_store_request_quota.
  // The explicit optional policy remains native-only and immutable per Store.
  explicit task_store(std::string id_prefix = "task-") : task_store(std::move(id_prefix), soui::sdk::diagnostics::mode::disabled) {}
  task_store(std::string id_prefix, soui::sdk::diagnostics::mode metrics);
  task_store(std::string id_prefix, soui::sdk::diagnostics::mode metrics,
             std::optional<task_store_request_quota_policy> quota);
  task_store(const task_store&) = delete;
  task_store& operator=(const task_store&) = delete;

  // One actual pool under the existing Store lock; independent of diagnostics.
  [[nodiscard]] task_store_request_quota_status read_request_quota(
      task_store_request_quota_snapshot&) const noexcept;

  [[nodiscard]] soui::sdk::diagnostics::status read_storage_metrics(
      soui::sdk::diagnostics::task_store_storage&) const noexcept;
  // Phase 1 native-only observation. The metadata retains the old map DTO scope;
  // service/facade/collector integration is a separate slice.
  [[nodiscard]] soui::sdk::diagnostics::status read_retained_string_storage(
      soui::sdk::diagnostics::task_store_storage& metadata,
      soui::sdk::diagnostics::allocation_requests& strings,
      soui::sdk::diagnostics::allocation_requests& aggregate) const noexcept;
  [[nodiscard]] soui::sdk::diagnostics::status read_private_storage_metrics(
      soui::sdk::diagnostics::task_store_private_storage&) const noexcept;

  [[nodiscard]] task_snapshot start(std::string operation_id);
  [[nodiscard]] task_snapshot start_scoped(std::string operation_id, std::string scope_key,
                                           bool* duplicate = nullptr, bool* conflict = nullptr,
                                           bool* capacity = nullptr);
  [[nodiscard]] bool mark_running(const std::string& task_id);
  [[nodiscard]] bool update_progress(const std::string& task_id, std::uint32_t progress,
                                     std::optional<std::string> message = std::nullopt);
  [[nodiscard]] bool observe_timeout(const std::string& task_id, std::string message = {});
  [[nodiscard]] bool request_cancel(const std::string& task_id);
  [[nodiscard]] bool confirm_cancelled(const std::string& task_id, std::string message = {});
  [[nodiscard]] bool complete(const std::string& task_id, result_kind kind, std::string message = {});
  [[nodiscard]] bool complete(operation_result result);
  [[nodiscard]] std::optional<task_snapshot> query(const std::string& task_id) const;
  [[nodiscard]] bool erase(const std::string& task_id);

 private:
  friend class task_service;
#ifdef SOUI_TASK_TERMINAL_QUOTA_TEST_ONLY
  friend struct detail::task_terminal_quota_test_access;
#endif
  struct allocation_owner {
    allocation_owner(bool observed, std::optional<task_store_request_quota_policy> policy) noexcept
        : quota(policy), aggregate(observed), maps(observed), strings(observed) {}
    // The pool retires after all buffers and both diagnostic domains.
    detail::task_store_request_quota quota;
    // aggregate retires last, after both domains and every actual buffer free.
    detail::task_store_allocation_ledger aggregate, maps, strings;
  };
  template <class T, bool StringDomain>
  class storage_allocator {
   public:
    using value_type = T;
    using propagate_on_container_copy_assignment = std::false_type;
    using propagate_on_container_move_assignment = std::false_type;
    using propagate_on_container_swap = std::false_type;
    using is_always_equal = std::false_type;
    template <class U> struct rebind { using other = storage_allocator<U, StringDomain>; };
    storage_allocator() noexcept = default;
    explicit storage_allocator(allocation_owner* owner) noexcept : owner_(owner) {}
    template <class U> storage_allocator(const storage_allocator<U, StringDomain>& other) noexcept : owner_(other.owner()) {}
    [[nodiscard]] allocation_owner* owner() const noexcept { return owner_; }
    [[nodiscard]] T* allocate(std::size_t count) {
      try {
        const auto bytes = detail::task_store_request_quota::checked_request_bytes(count, sizeof(T));
        detail::task_store_request_quota::reservation credit(owner_ ? &owner_->quota : nullptr, bytes);
#ifdef SOUI_TASK_STORE_STORAGE_TEST_ONLY
        if (owner_) {
          detail::task_store_storage_test::quota_before_allocate(owner_->quota);
          detail::task_store_storage_test::before_allocate(owner_->aggregate, bytes);
        }
#endif
        auto allocator = std::allocator<T>{};
        auto* result = std::allocator_traits<std::allocator<T>>::allocate(allocator, count);
        if (owner_) { domain().allocated(bytes); owner_->aggregate.allocated(bytes); }
        credit.commit();
        return result;
      } catch (...) {
        if (owner_) { domain().failed(); owner_->aggregate.failed(); }
        throw;
      }
    }
    void deallocate(T* pointer, std::size_t count) noexcept {
      // Empty STL cleanup did not acquire storage and must not refund a request.
      if (pointer == nullptr && count == 0) return;
      auto allocator = std::allocator<T>{};
      std::allocator_traits<std::allocator<T>>::deallocate(allocator, pointer, count);
      if (owner_) {
        owner_->quota.release(count * sizeof(T));
        domain().deallocated(count * sizeof(T)); owner_->aggregate.deallocated(count * sizeof(T));
      }
    }
    template <class U> bool operator==(const storage_allocator<U, StringDomain>& other) const noexcept { return owner_ == other.owner(); }
    template <class U> bool operator!=(const storage_allocator<U, StringDomain>& other) const noexcept { return !(*this == other); }
   private:
    detail::task_store_allocation_ledger& domain() const noexcept { return StringDomain ? owner_->strings : owner_->maps; }
    allocation_owner* owner_{nullptr};
  };
  class retained_string {
   public:
    using buffer_type = std::basic_string<char, std::char_traits<char>, storage_allocator<char, true>>;
    retained_string(std::string_view value, allocation_owner& owner, std::size_t capacity_floor = 0)
        : buffer_(std::in_place, storage_allocator<char, true>{&owner}) {
      if (capacity_floor) buffer_->reserve(capacity_floor);
      if (!value.empty()) buffer_->assign(value.data(), value.size());
    }
    // No basic_string is constructed for lookup, even on a checked STL.
    [[nodiscard]] static retained_string lookup(std::string_view value) noexcept { return retained_string(value); }
    [[nodiscard]] std::string_view view() const noexcept {
      return buffer_ ? std::string_view{buffer_->data(), buffer_->size()} : borrowed_;
    }
    [[nodiscard]] std::size_t size() const noexcept { return view().size(); }
    [[nodiscard]] std::size_t capacity() const noexcept { assert(buffer_); return buffer_->capacity(); }
    [[nodiscard]] std::string public_copy() const { return std::string{view()}; }
    void swap(retained_string& other) noexcept {
      static_assert(noexcept(std::declval<buffer_type&>().swap(std::declval<buffer_type&>())),
                    "The supported STL must provide non-throwing same-owner string swap");
      assert(buffer_ && other.buffer_ && buffer_->get_allocator() == other.buffer_->get_allocator());
      buffer_->swap(*other.buffer_);
    }
   private:
    explicit retained_string(std::string_view borrowed) noexcept : borrowed_(borrowed) {}
    // Owned views are calculated on access: an SSO move never leaves a cached pointer.
    std::optional<buffer_type> buffer_;
    std::string_view borrowed_;
  };
  struct retained_hash {
    std::size_t operator()(const retained_string& value) const noexcept { return std::hash<std::string_view>{}(value.view()); }
  };
  struct retained_equal {
    bool operator()(const retained_string& left, const retained_string& right) const noexcept { return left.view() == right.view(); }
  };
  struct retained_result {
    retained_result(allocation_owner& owner, result_kind kind, std::string_view id, std::string_view message = {})
        : kind(kind), task_id(id, owner), message(message, owner) {}
    void swap(retained_result& other) noexcept {
      task_id.swap(other.task_id); message.swap(other.message); std::swap(kind, other.kind);
    }
    result_kind kind;
    retained_string task_id, message;
  };
  struct retained_snapshot {
    struct funded_failure {
      static constexpr std::string_view text{"Native task terminal storage publication failed."};
      static constexpr std::size_t message_capacity{64};
      funded_failure(allocation_owner& owner, std::string_view id)
          : message(text, owner, message_capacity), result(owner, result_kind::failure, id) {
        static_assert(text.size() < message_capacity, "The funded failure text must fit its fixed reserve");
        retained_string funded_message(text, owner, message_capacity);
        result.message.swap(funded_message);
      }
      retained_string message;
      retained_result result;
    };
    retained_snapshot(const task_snapshot& value, allocation_owner& owner, bool fund_failure = false)
        : task_id(value.task_id, owner), operation_id(value.operation_id, owner), state(value.state),
          progress(value.progress), sequence(value.sequence), message(value.message, owner),
          result(owner, value.result.kind, value.result.task_id, value.result.message) {
      if (fund_failure) failure_reserve.emplace(owner, value.task_id);
    }
    [[nodiscard]] task_snapshot public_copy() const {
      return {task_id.public_copy(), operation_id.public_copy(), state, progress, sequence, message.public_copy(),
              operation_result{result.kind, result.task_id.public_copy(), result.message.public_copy()}};
    }
    retained_string task_id, operation_id;
    task_state state;
    std::uint32_t progress;
    std::uint64_t sequence;
    retained_string message;
    retained_result result;
    std::optional<funded_failure> failure_reserve;
  };
  template <class Value>
  using storage_map = std::unordered_map<retained_string, Value, retained_hash,
      retained_equal, storage_allocator<std::pair<const retained_string, Value>, false>>;
  struct prepared_start {
    prepared_start(std::mutex& mutex, allocation_owner& owner)
        : lock{mutex}, tasks{storage_map<retained_snapshot>::allocator_type{&owner}},
          operations{storage_map<retained_string>::allocator_type{&owner}},
          scopes{storage_map<retained_string>::allocator_type{&owner}} {}
    std::unique_lock<std::mutex> lock;
    storage_map<retained_snapshot> tasks;
    storage_map<retained_string> operations, scopes;
    // Public receipt/eviction copies remain outside the private allocator scope.
    task_snapshot snapshot;
    std::string evicted_id;
    bool duplicate{false}, conflict{false}, capacity{false}, publish{false};
  };
  [[nodiscard]] std::unique_ptr<prepared_start> prepare_start_scoped(
      std::string operation_id, std::string scope_key, bool fund_terminal_failure = false);
  void commit_start_scoped(prepared_start& prepared) noexcept;
  struct prepared_completion {
    explicit prepared_completion(std::mutex& mutex) : lock(mutex) {}
    std::unique_lock<std::mutex> lock;
    retained_snapshot* target{nullptr};
    std::optional<retained_string> message;
    std::optional<retained_result> result;
    task_state state{task_state::failed};
    bool publish{false};
  };
  void prepare_complete(prepared_completion&, const std::string& task_id,
                        result_kind kind, std::string_view message);
  void commit_complete(prepared_completion&) noexcept;
  // Requires admission's actual same-owner buffers. No allocation or retry.
  [[nodiscard]] bool commit_funded_failure(const std::string& task_id) noexcept;
  [[nodiscard]] static bool is_terminal(task_state state) noexcept;
  [[nodiscard]] static task_state state_for(result_kind kind) noexcept;

  [[nodiscard]] bool read_metrics_locked(soui::sdk::diagnostics::tasks&) const;
  void read_storage_locked(soui::sdk::diagnostics::task_store_storage&) const noexcept;
  const soui::sdk::diagnostics::mode metrics_mode_;
  const std::uint64_t metrics_epoch_;
  std::uint64_t metrics_evictions_{0};
  bool metrics_saturated_{false};
  // Declared before prefix/maps: one owner outlives every actual buffer free.
  allocation_owner allocation_owner_;
  mutable std::mutex mutex_;
  retained_string id_prefix_;
  std::uint64_t next_task_number_{1};
  std::uint64_t next_sequence_{1};
  storage_map<retained_snapshot> tasks_;
  storage_map<retained_string> operation_to_task_;
  storage_map<retained_string> operation_to_scope_;
};

}  // namespace soui::runtime
