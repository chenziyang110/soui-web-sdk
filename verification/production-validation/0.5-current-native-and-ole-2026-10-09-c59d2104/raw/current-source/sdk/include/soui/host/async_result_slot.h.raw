#pragma once

#include <soui/host/engine_thread.h>

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <utility>

namespace soui::sdk {

// Keeps diagnostic JavaScript reads off the UI thread. The owning engine must
// be stopped before this slot is destroyed because queued work captures it.
class async_result_slot final {
 public:
  using producer = std::function<std::string()>;

  [[nodiscard]] std::string poll(engine_thread* executor, producer produce) {
    if (executor == nullptr || !produce) return {};

    std::uint64_t request_generation = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!result_.empty()) return result_;
      if (pending_generation_ == generation_) return {};
      request_generation = generation_;
      pending_generation_ = request_generation;
    }

    const bool posted = executor->post(
        [this, request_generation, produce = std::move(produce)]() mutable {
          std::string result;
          try {
            result = produce();
          } catch (...) {
            result.clear();
          }
          std::lock_guard<std::mutex> lock(mutex_);
          if (request_generation != generation_) return;
          result_ = std::move(result);
          if (pending_generation_ == request_generation)
            pending_generation_ = no_generation;
        });
    if (!posted) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (pending_generation_ == request_generation)
        pending_generation_ = no_generation;
    }
    return {};
  }

  void reset() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    ++generation_;
    if (generation_ == no_generation) ++generation_;
    result_.clear();
    pending_generation_ = no_generation;
  }

 private:
  static constexpr std::uint64_t no_generation = ~std::uint64_t{0};

  mutable std::mutex mutex_;
  std::string result_;
  std::uint64_t generation_{0};
  std::uint64_t pending_generation_{no_generation};
};

}  // namespace soui::sdk
