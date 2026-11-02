#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

// Private application policy. Owned by the existing UI thread; no SDK/Win32 calls.
namespace workbench_tray_callbacks {

struct callback {
  std::uint32_t message{0};
  std::uintptr_t first{0};
  std::intptr_t second{0};
  std::uint64_t ordinal{0};
  std::uint64_t received_ms{0};
};

class queue final {
 public:
  using value_type = callback;
  enum class result { direct, queued, overflow, revoked };
  static constexpr std::size_t capacity = 64;
  static constexpr std::size_t batch_limit = 8;

  class call_scope final {
   public:
    explicit call_scope(queue& owner) noexcept : owner_(owner) { ++owner_.call_depth_; }
    ~call_scope() { --owner_.call_depth_; }
    call_scope(const call_scope&) = delete;
    call_scope& operator=(const call_scope&) = delete;
   private:
    queue& owner_;
  };

  class drain_scope final {
   public:
    explicit drain_scope(queue& owner) noexcept : owner_(owner) {
      if (!owner_.revoked_ && !owner_.call_depth_ && !owner_.draining_) {
        acquired_ = true;
        owner_.draining_ = true;
        remaining_ = owner_.size_ < batch_limit ? owner_.size_ : batch_limit;
      }
    }
    ~drain_scope() { if (acquired_) owner_.draining_ = false; }
    drain_scope(const drain_scope&) = delete;
    drain_scope& operator=(const drain_scope&) = delete;
    bool next(callback& value) noexcept {
      if (!acquired_ || !remaining_ || !owner_.size_ || owner_.revoked_ || owner_.call_depth_) return false;
      value = owner_.values_[owner_.head_];
      owner_.head_ = (owner_.head_ + 1) % capacity;
      --owner_.size_;
      --remaining_;
      return true;
    }
   private:
    queue& owner_;
    std::size_t remaining_{0};
    bool acquired_{false};
  };

  bool capture(std::uint32_t message, std::uintptr_t first, std::intptr_t second,
               std::uint64_t received_ms, callback& value) noexcept {
    if (!next_ordinal_) return false;
    value = {message, first, second, next_ordinal_, received_ms};
    next_ordinal_ = next_ordinal_ == (std::numeric_limits<std::uint64_t>::max)() ? 0 : next_ordinal_ + 1;
    return true;
  }
  result defer(std::uint32_t callback_message, const callback& value) noexcept {
    if (!callback_message || value.message != callback_message) return result::direct;
    if (revoked_) return result::revoked;
    if (!call_depth_) return result::direct;
    if (size_ == capacity) return result::overflow;
    values_[(head_ + size_) % capacity] = value;
    ++size_;
    return result::queued;
  }
  std::size_t revoke() noexcept {
    const auto discarded = size_;
    revoked_ = true;
    head_ = 0;
    size_ = 0;
    return discarded;
  }
  std::size_t size() const noexcept { return size_; }
  unsigned call_depth() const noexcept { return call_depth_; }
  bool draining() const noexcept { return draining_; }
  bool revoked() const noexcept { return revoked_; }

 private:
  std::array<callback, capacity> values_{};
  std::size_t head_{0}, size_{0};
  unsigned call_depth_{0};
  std::uint64_t next_ordinal_{1};
  bool draining_{false}, revoked_{false};
};

}  // namespace workbench_tray_callbacks
