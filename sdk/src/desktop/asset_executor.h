#pragma once

#include "assets.h"
#include <atomic>
#include <functional>
#include <memory>

namespace soui::desktop::detail {

using asset_cancellation = std::shared_ptr<std::atomic<bool>>;
using asset_permit = std::shared_ptr<void>;

// A permit accounts for queued work, a running read, and its eventual UI result.
// Completion must move the permit into its dispatch closure, retaining it until
// that closure is consumed or discarded. It must not capture a window owner.
class asset_executor final {
 public:
  using reader = std::function<asset_result(
    const std::filesystem::path&, std::string_view)>;
  using completion = std::function<void(asset_result, asset_permit)>;

  explicit asset_executor(std::size_t worker_count = 2,
                          std::size_t maximum_outstanding = 16,
                          reader read = {}) noexcept;
  ~asset_executor();
  asset_executor(const asset_executor&) = delete;
  asset_executor& operator=(const asset_executor&) = delete;

  // The root and path are copied before returning. A failed submission never
  // calls completion. Reader/completion callbacks run only on worker threads.
  [[nodiscard]] result submit(const std::filesystem::path& root,
                              std::string_view relative_path,
                              const asset_cancellation& cancellation,
                              completion complete) noexcept;

  // Removes queued reads for this token and suppresses late read results.
  // Already-running filesystem calls are not interrupted; already-dispatched
  // UI closures must also check their session token before delivering a result.
  void cancel(const asset_cancellation& cancellation) noexcept;

 private:
  struct shared_state;
  static void work(std::shared_ptr<shared_state> state) noexcept;
  static void stop(const std::shared_ptr<shared_state>& state) noexcept;
  std::shared_ptr<shared_state> state_;
};

// Production windows share one process-wide budget, even across application
// group destruction/recreation while an old filesystem call remains blocked.
asset_executor& process_asset_executor() noexcept;

}  // namespace soui::desktop::detail
