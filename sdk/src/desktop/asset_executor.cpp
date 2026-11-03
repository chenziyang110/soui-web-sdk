#include "asset_executor.h"
#include <condition_variable>
#include <list>
#include <mutex>
#include <new>
#include <thread>

namespace soui::desktop::detail {
namespace {
result failure(const char* code, const char* message) noexcept {
  try {
    return result::failure(code, message);
  } catch (...) {
    return {};
  }
}
}  // namespace

struct asset_executor::shared_state {
  struct permit {
    explicit permit(std::shared_ptr<shared_state> state)
      : owner(std::move(state)) {}
    ~permit() {
      if (charged) owner->outstanding.fetch_sub(1, std::memory_order_relaxed);
    }
    std::shared_ptr<shared_state> owner;
    bool charged{false};
  };

  struct job {
    std::filesystem::path root;
    std::string path;
    asset_cancellation cancellation;
    completion complete;
    asset_permit capacity;
  };

  shared_state(std::size_t maximum, reader read)
    : maximum_outstanding(maximum), read(std::move(read)) {}

  std::mutex mutex;
  std::condition_variable ready;
  std::list<job> pending;
  const std::size_t maximum_outstanding;
  std::atomic<std::size_t> outstanding{0};
  std::atomic<bool> stopping{false};
  const reader read;
};

asset_executor::asset_executor(std::size_t worker_count,
                               std::size_t maximum_outstanding,
                               reader read) noexcept {
  if (!worker_count || !maximum_outstanding) return;
  try {
    state_ = std::make_shared<shared_state>(
      maximum_outstanding, read ? std::move(read) : reader{read_asset});
    for (std::size_t index = 0; index < worker_count; ++index) {
      std::thread worker(&asset_executor::work, state_);
      try {
        worker.detach();
      } catch (...) {
        // No submission can occur before construction returns, so this join
        // cannot wait on filesystem I/O. Do not unwind a joinable std::thread.
        stop(state_);
        worker.join();
        throw;
      }
    }
  } catch (...) {
    stop(state_);
    state_.reset();
  }
}

asset_executor::~asset_executor() {
  stop(state_);
}

void asset_executor::stop(const std::shared_ptr<shared_state>& state) noexcept {
  if (!state) return;
  std::list<shared_state::job> discarded;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->stopping.store(true, std::memory_order_release);
    discarded.splice(discarded.end(), state->pending);
  }
  state->ready.notify_all();
  // Destroy callbacks and their permits outside the queue lock. Workers own
  // state independently and finish without accessing the executor object.
}

result asset_executor::submit(const std::filesystem::path& root,
                              std::string_view relative_path,
                              const asset_cancellation& cancellation,
                              completion complete) noexcept {
  const auto state = state_;
  if (!state)
    return failure("asset_executor_unavailable", "Asset workers could not be started.");
  if (!cancellation || !complete)
    return failure("invalid_asset_request", "An asset session and completion are required.");
  try {
    auto capacity = std::make_shared<shared_state::permit>(state);
    shared_state::job next{
      root, std::string(relative_path), cancellation, std::move(complete), capacity};
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      if (state->stopping.load(std::memory_order_acquire))
        return failure("asset_executor_unavailable", "Asset workers have stopped.");
      if (cancellation->load(std::memory_order_acquire))
        return failure("asset_cancelled", "The asset session has been revoked.");
      if (state->outstanding.load(std::memory_order_relaxed) >=
          state->maximum_outstanding)
        return failure("asset_queue_full", "Too many asset reads are outstanding.");
      state->pending.emplace_back(std::move(next));
      state->outstanding.fetch_add(1, std::memory_order_relaxed);
      capacity->charged = true;
    }
    state->ready.notify_one();
    return result::success();
  } catch (...) {
    return failure("asset_executor_unavailable", "Unable to queue the asset read.");
  }
}

void asset_executor::cancel(const asset_cancellation& cancellation) noexcept {
  if (!cancellation) return;
  cancellation->store(true, std::memory_order_release);
  const auto state = state_;
  if (!state) return;
  std::list<shared_state::job> discarded;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    for (auto job = state->pending.begin(); job != state->pending.end();) {
      const auto current = job++;
      if (current->cancellation == cancellation)
        discarded.splice(discarded.end(), state->pending, current);
    }
  }
}

void asset_executor::work(std::shared_ptr<shared_state> state) noexcept {
  try {
    for (;;) {
      shared_state::job next;
      {
        std::unique_lock<std::mutex> lock(state->mutex);
        state->ready.wait(lock, [&] {
          return state->stopping.load(std::memory_order_acquire) ||
                 !state->pending.empty();
        });
        if (state->stopping.load(std::memory_order_acquire)) return;
        next = std::move(state->pending.front());
        state->pending.pop_front();
      }
      if (state->stopping.load(std::memory_order_acquire) ||
          next.cancellation->load(std::memory_order_acquire)) continue;

      asset_result loaded;
      try {
        loaded = state->read(next.root, next.path);
      } catch (...) {
        loaded.status = failure("asset_read_failed", "Unable to read the local asset.");
      }
      if (state->stopping.load(std::memory_order_acquire) ||
          next.cancellation->load(std::memory_order_acquire)) continue;
      try {
        next.complete(std::move(loaded), std::move(next.capacity));
      } catch (...) {
        // Dispatch allocation or a consumer callback must not terminate a
        // worker or leak capacity. The callback may already own the permit.
      }
    }
  } catch (...) {
    // Infrastructure failures also fail closed. In particular, never let an
    // exception escape a detached thread's entry point.
    stop(state);
  }
}

asset_executor& process_asset_executor() noexcept {
  // Deliberately do not register an exit destructor: static window/application
  // teardown may still revoke a session, and reads may still be in flight.
  // Placement construction also avoids a throwing singleton heap allocation.
  alignas(asset_executor) static unsigned char storage[sizeof(asset_executor)];
  static auto* executor = ::new (static_cast<void*>(storage)) asset_executor;
  return *executor;
}

}  // namespace soui::desktop::detail
