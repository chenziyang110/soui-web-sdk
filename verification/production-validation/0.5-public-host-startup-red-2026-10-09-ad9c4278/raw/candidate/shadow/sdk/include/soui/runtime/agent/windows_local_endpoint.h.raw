#pragma once

#include <soui/runtime/agent/windows_peer_identity.h>
#include <soui/runtime/bridge/bridge_transport.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace soui::runtime { class task_service; }
namespace soui::runtime::agent {

// Native application correlation, never a wire credential or an HWND claim.
// The application verifies these IDs/generation in its owner-thread callback.
struct local_endpoint_scope final {
  std::uint64_t application{0};
  std::uint64_t window{0};
  std::uint64_t document{0};
  std::uint64_t generation{0};
};

struct local_endpoint_policy final {
  local_endpoint_scope scope;
  std::string session_id;
  std::string origin;
  std::vector<std::string> capabilities;
  std::vector<std::string> methods;
};

struct local_endpoint_options final {
  // Only [A-Za-z0-9._-], 1..128 bytes; fixed local pipe prefix is added.
  // Application chooses a unique name and distributes it through trusted UI.
  std::string pipe_component;
  // Includes idle header waiting as well as a full frame. Idle expiry closes
  // this connection; a reconnect always needs a new native authorization.
  std::uint32_t io_timeout_ms{5000};
  std::uint32_t owner_timeout_ms{10000};
};

enum class local_endpoint_error {
  none, wrong_owner_thread, already_started, invalid_options,
  security_unavailable, pipe_unavailable, native_start_failed,
  io_failure, out_of_memory, callback_failure, internal_failure
};
enum class local_endpoint_state { stopped, listening, connected, stopping, failed };
enum class local_endpoint_stop { stopped, pending, wrong_owner_thread };

class windows_local_endpoint final {
 public:
  using authorizer = std::function<std::optional<local_endpoint_policy>(const windows_peer_identity&)>;
  using scope_check = std::function<bool(const local_endpoint_scope&)>;

  // Borrowed frozen registry/transport/store/services outlive actual stop.
  // Constructing this object starts nothing and grants nothing.
  windows_local_endpoint(bridge_transport& transport, permission_store& permissions,
      task_service* tasks = nullptr);
  // Requires poll_stop()==stopped before destruction; does not detach or wait
  // indefinitely on the UI thread. Destruction while live is fail-stop.
  ~windows_local_endpoint();
  windows_local_endpoint(const windows_local_endpoint&) = delete;
  windows_local_endpoint& operator=(const windows_local_endpoint&) = delete;
  windows_local_endpoint(windows_local_endpoint&&) = delete;
  windows_local_endpoint& operator=(windows_local_endpoint&&) = delete;

  // Captures the actual owner thread; empty callbacks mean default deny.
  [[nodiscard]] local_endpoint_error start(const local_endpoint_options& options,
      authorizer authorize = {}, scope_check current = {}) noexcept;
  // Single request slot plus reserved cleanup control; no callback under locks.
  // No reentrant pumping. Budget does not preempt an already started callback.
  [[nodiscard]] std::size_t pump_owner(std::size_t max_count = 8,
      std::uint32_t time_budget_ms = 4) noexcept;
  // Owner-thread navigation/window teardown entry. Immediate admission close;
  // precise grant/task cleanup is retained for the owner control slot.
  [[nodiscard]] bool revoke_scope(const local_endpoint_scope& scope) noexcept;
  // Thread-safe admission/I/O stop request; does not cancel executed business.
  void request_stop() noexcept;
  // Owner only: cleanup, real thread completion observation, join, release.
  [[nodiscard]] local_endpoint_stop poll_stop() noexcept;
  [[nodiscard]] local_endpoint_state state() const noexcept;
  [[nodiscard]] local_endpoint_error last_error() const noexcept;

 private:
  struct impl;
  std::unique_ptr<impl> data_;
};

}  // namespace soui::runtime::agent
