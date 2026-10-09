#pragma once

#include <Windows.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace soui::runtime::agent {

enum class windows_peer_error {
  none,
  invalid_handle,
  caller_already_impersonating,
  thread_context_unavailable,
  pipe_duplicate_failed,
  not_server_named_pipe,
  pipe_identity_unavailable,
  process_open_failed,
  process_time_unavailable,
  process_exited,
  process_state_unavailable,
  primary_token_unavailable,
  impersonation_failed,
  impersonation_token_unavailable,
  token_information_unavailable,
  invalid_token_information,
  anonymous_peer,
  insufficient_impersonation_level,
  user_mismatch,
  authentication_mismatch,
  session_mismatch,
  pipe_identity_changed,
  out_of_memory,
  internal_failure
};

[[nodiscard]] const char* windows_peer_error_name(windows_peer_error error) noexcept;

enum class held_process_state { alive, exited, unavailable };

struct windows_authentication_id {
  std::uint32_t low_part{0};
  std::int32_t high_part{0};
};

struct verified_windows_peer;

// Snapshot minted only from actual native pipe/process/token queries. No rights
// are granted by this object. Never reconstruct it from wire/public fields.
class windows_peer_identity final {
 public:
  ~windows_peer_identity();
  windows_peer_identity(const windows_peer_identity&) = delete;
  windows_peer_identity& operator=(const windows_peer_identity&) = delete;
  windows_peer_identity(windows_peer_identity&&) = delete;
  windows_peer_identity& operator=(windows_peer_identity&&) = delete;

  [[nodiscard]] std::uint32_t process_id() const noexcept { return process_id_; }
  [[nodiscard]] std::uint64_t creation_filetime() const noexcept { return creation_filetime_; }
  [[nodiscard]] const std::vector<std::uint8_t>& user_sid() const noexcept { return user_sid_; }
  [[nodiscard]] windows_authentication_id authentication_id() const noexcept { return authentication_id_; }
  [[nodiscard]] std::uint32_t session_id() const noexcept { return session_id_; }
  // Queries the held process object, never a new process having the same PID.
  [[nodiscard]] held_process_state process_state() const noexcept;

 private:
  friend verified_windows_peer verify_windows_named_pipe_peer(HANDLE server_pipe) noexcept;
  windows_peer_identity(HANDLE process, std::uint32_t process_id,
      std::uint64_t creation_filetime, std::vector<std::uint8_t> user_sid,
      windows_authentication_id authentication_id, std::uint32_t session_id) noexcept;

  HANDLE const process_;
  const std::uint32_t process_id_;
  const std::uint64_t creation_filetime_;
  const std::vector<std::uint8_t> user_sid_;
  const windows_authentication_id authentication_id_;
  const std::uint32_t session_id_;
};

struct verified_windows_peer {
  windows_peer_error error{windows_peer_error::internal_failure};
  std::unique_ptr<const windows_peer_identity> identity;
  [[nodiscard]] bool verified() const noexcept { return error == windows_peer_error::none && identity != nullptr; }
};

// Borrowed, connected SERVER handle from a trusted native endpoint. Caller
// holds it and serializes read/disconnect/reconnect for the entire operation.
// A handshake message MUST already have been read: Windows impersonates the
// last message read, not wire-reported identity. Endpoint owns local-only pipe
// creation/DACL and connection generation; this leaf grants no permissions.
// Use a dedicated self-context server thread. Existing impersonation is
// rejected AND cleared. Every exit reverts; a failed revert stops the process.
[[nodiscard]] verified_windows_peer verify_windows_named_pipe_peer(HANDLE server_pipe) noexcept;

}  // namespace soui::runtime::agent
