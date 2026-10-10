#pragma once
#include "../runtime/diagnostics/runtime_metrics_internal.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace soui::sdk::detail {
// The existing RAII lease spans queues, handler and native resolver. Metrics
// attach one scalar timestamp to its existing identity; no retained history.
class bridge_request_budget {
 public:
  static constexpr std::size_t ordinary_limit = 256;
  static constexpr std::size_t control_limit = 128;
  static constexpr std::size_t renewal_limit = 1;
  enum class lane { ordinary, control, renewal };
  enum class rejection { none, duplicate, full };
  enum class drop { queue_expired, stale_context, worker_post, engine_post };
 private:
  struct record { lane category; std::uint64_t admitted_us; };
  struct state {
    explicit state(diagnostics::mode value) : mode(value),
        epoch(value == diagnostics::mode::scalar ? diagnostics::detail::epoch() : 0) {}
    std::mutex mutex;
    std::unordered_map<std::string, record> identities;
    std::size_t ordinary{0}, control{0}, renewal{0};
    const diagnostics::mode mode;
    const std::uint64_t epoch;
    diagnostics::software_requests metrics;
    bool saturated{false};
    std::atomic<bool> unavailable{false};
    diagnostics::lane& stats(lane category) noexcept {
      return category == lane::renewal ? metrics.renewal :
          category == lane::control ? metrics.control : metrics.ordinary;
    }
  };
 public:
  bridge_request_budget() : bridge_request_budget(diagnostics::mode::disabled) {}
  explicit bridge_request_budget(diagnostics::mode mode) : state_(std::make_shared<state>(mode)) {}
  struct lease {
    lease(std::shared_ptr<state> owner, std::string identity, lane category)
        : owner(std::move(owner)), identity(std::move(identity)), category(category) {}
    ~lease() { release(); }
    void release() {
      std::lock_guard<std::mutex> lock(owner->mutex);
      if (!active) return;
      active = false;
      owner->identities.erase(identity);
      --(category == lane::renewal ? owner->renewal :
          category == lane::control ? owner->control : owner->ordinary);
      if (owner->mode == diagnostics::mode::scalar)
        diagnostics::detail::increment(owner->stats(category).released_total,owner->saturated);
    }
    lease(const lease&) = delete;
    lease& operator=(const lease&) = delete;
    std::shared_ptr<state> owner;
    std::string identity;
    lane category;
    bool active{false};
  };
  std::shared_ptr<lease> acquire(const std::string& id, std::uint64_t generation,
                                 bool control, rejection& error) {
    return acquire(id, generation, control ? lane::control : lane::ordinary, error);
  }
  std::shared_ptr<lease> acquire(const std::string& id, std::uint64_t generation,
                                 lane category, rejection& error) {
    auto result = std::make_shared<lease>(state_, std::to_string(generation) + ":" + id, category);
    std::lock_guard<std::mutex> lock(state_->mutex);
    error = rejection::none;
    if (state_->identities.count(result->identity)) {
      error = rejection::duplicate;
      if (state_->mode == diagnostics::mode::scalar)
        diagnostics::detail::increment(state_->stats(category).duplicate_rejections_total,state_->saturated);
      return {};
    }
    auto& count = category == lane::renewal ? state_->renewal :
        category == lane::control ? state_->control : state_->ordinary;
    const auto limit = category == lane::renewal ? renewal_limit :
        category == lane::control ? control_limit : ordinary_limit;
    if (count >= limit) {
      error = rejection::full;
      if (state_->mode == diagnostics::mode::scalar)
        diagnostics::detail::increment(state_->stats(category).full_rejections_total,state_->saturated);
      return {};
    }
    state_->identities.emplace(result->identity,record{category,
        state_->mode == diagnostics::mode::scalar ? diagnostics::detail::now_us() : 0});
    ++count;
    result->active = true;
    if (state_->mode == diagnostics::mode::scalar) {
      auto& stats=state_->stats(category);
      diagnostics::detail::increment(stats.acquired_total,state_->saturated);
      diagnostics::detail::high_water(stats.high_water,count);
    }
    return result;
  }
  void record_drop(drop reason) noexcept {
    if (state_->mode != diagnostics::mode::scalar) return;
    try {
      std::lock_guard<std::mutex> lock(state_->mutex);
      auto& count = reason == drop::queue_expired ? state_->metrics.queue_expired_total :
          reason == drop::stale_context ? state_->metrics.stale_context_drop_total :
          reason == drop::worker_post ? state_->metrics.worker_post_rejections_total :
          state_->metrics.engine_post_rejections_total;
      diagnostics::detail::increment(count,state_->saturated);
    } catch (...) { state_->unavailable.store(true,std::memory_order_release); }
  }
  diagnostics::status read_runtime_metrics(diagnostics::software_requests& out) const noexcept {
    out={};
    if (state_->mode != diagnostics::mode::scalar) return diagnostics::status::disabled;
    try {
      auto observed=diagnostics::detail::begin(state_->mode,state_->epoch);
      std::unique_lock<std::mutex> lock(state_->mutex,std::try_to_lock);
      if (!lock.owns_lock()) { observed.state=diagnostics::status::busy; diagnostics::detail::end(observed); out.observation=observed; return observed.state; }
      out=state_->metrics; out.observation=observed; out.observation.counter_saturated=state_->saturated;
      if (state_->unavailable.load(std::memory_order_acquire)) out.observation.state=diagnostics::status::unavailable;
      out.ordinary.limit=ordinary_limit; out.control.limit=control_limit; out.renewal.limit=renewal_limit;
      out.ordinary.outstanding=state_->ordinary; out.control.outstanding=state_->control; out.renewal.outstanding=state_->renewal;
      const auto now=diagnostics::detail::now_us();
      for (const auto& item:state_->identities) {
        auto& stats=item.second.category == lane::renewal ? out.renewal :
            item.second.category == lane::control ? out.control : out.ordinary;
        diagnostics::detail::high_water(stats.oldest_outstanding_age_us,
            diagnostics::detail::age(item.second.admitted_us,now,out.observation));
      }
      diagnostics::detail::end(out.observation); return out.observation.state;
    } catch (...) { out={}; out.observation.state=diagnostics::status::unavailable; return out.observation.state; }
  }
 private:
  std::shared_ptr<state> state_;
};
} // namespace soui::sdk::detail
