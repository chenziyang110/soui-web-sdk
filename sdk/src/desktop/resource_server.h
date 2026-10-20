#pragma once

#include <soui/desktop/app.h>

namespace soui::desktop::detail {

// Private per-window origin. Call start/base_uri/stop on the owning UI thread.
// The root must remain application-owned, trusted and immutable until every
// background read finishes; canonical containment is not a filesystem sandbox.
class resource_server final {
 public:
  resource_server() noexcept = default;
  ~resource_server();
  resource_server(const resource_server&) = delete;
  resource_server& operator=(const resource_server&) = delete;

  // Precondition: application initialization already canonicalized and validated
  // this absolute directory. Startup deliberately performs no filesystem I/O.
  [[nodiscard]] result start(const std::filesystem::path& root) noexcept;
  // Includes the trailing slash; empty before start or after revocation.
  [[nodiscard]] std::string base_uri() const;

  // Does not join, close a worker-owned descriptor, or wait for filesystem I/O.
  // In-flight reads retain their process-wide capacity until the worker exits.
  void stop() noexcept;

 private:
  struct shared_state;
  static void work(std::shared_ptr<shared_state> state) noexcept;
  std::shared_ptr<shared_state> state_;
};

}  // namespace soui::desktop::detail
