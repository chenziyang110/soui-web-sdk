#include <soui/runtime/agent/windows_peer_identity.h>

#include <new>
#include <utility>

namespace soui::runtime::agent {
namespace {

class owned_handle final {
 public:
  explicit owned_handle(HANDLE handle = nullptr) noexcept : handle_(handle) {}
  ~owned_handle() { if (handle_ && handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); }
  owned_handle(const owned_handle&) = delete;
  owned_handle& operator=(const owned_handle&) = delete;
  HANDLE get() const noexcept { return handle_; }
  HANDLE release() noexcept { return std::exchange(handle_, nullptr); }
 private:
  HANDLE handle_;
};

[[noreturn]] void revert_failed() noexcept {
  // Microsoft requires process shutdown if self context cannot be restored.
  constexpr UINT status = 0xE0500044U;
  TerminateProcess(GetCurrentProcess(), status);
  ExitProcess(status);
}

class checked_revert final {
 public:
  ~checked_revert() { finish(); }
  checked_revert() = default;
  checked_revert(const checked_revert&) = delete;
  checked_revert& operator=(const checked_revert&) = delete;
  void finish() noexcept {
    if (!finished_) {
      if (!RevertToSelf()) revert_failed();
      finished_ = true;
    }
  }
 private:
  bool finished_{false};
};

struct token_snapshot {
  TOKEN_STATISTICS statistics{};
  DWORD session{0};
  std::vector<std::uint8_t> sid;
};

windows_peer_error read_token(HANDLE token, token_snapshot& snapshot) {
  DWORD returned = 0;
  if (!GetTokenInformation(token, TokenStatistics, &snapshot.statistics,
          sizeof(snapshot.statistics), &returned) || returned != sizeof(snapshot.statistics))
    return windows_peer_error::token_information_unavailable;
  if (!GetTokenInformation(token, TokenSessionId, &snapshot.session,
          sizeof(snapshot.session), &returned) || returned != sizeof(snapshot.session))
    return windows_peer_error::token_information_unavailable;

  DWORD required = 0;
  if (GetTokenInformation(token, TokenUser, nullptr, 0, &required) ||
      GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    return windows_peer_error::token_information_unavailable;
  constexpr DWORD max_user_bytes = 64U * 1024U;
  if (required < sizeof(TOKEN_USER) || required > max_user_bytes)
    return windows_peer_error::invalid_token_information;
  std::vector<std::uint8_t> user_buffer(required);
  if (!GetTokenInformation(token, TokenUser, user_buffer.data(), required, &returned))
    return windows_peer_error::token_information_unavailable;
  if (returned < sizeof(TOKEN_USER) || returned > user_buffer.size())
    return windows_peer_error::invalid_token_information;

  const auto* user = reinterpret_cast<const TOKEN_USER*>(user_buffer.data());
  const auto base = reinterpret_cast<std::uintptr_t>(user_buffer.data());
  const auto sid_address = reinterpret_cast<std::uintptr_t>(user->User.Sid);
  if (sid_address < base || sid_address - base < sizeof(TOKEN_USER) || sid_address - base > returned)
    return windows_peer_error::invalid_token_information;
  const auto remaining = returned - static_cast<std::size_t>(sid_address - base);
  // Check the fixed header and claimed subauthorities before OS validation can
  // read a SID. No untrusted pointer escapes this returned buffer.
  if (remaining < 8) return windows_peer_error::invalid_token_information;
  const auto* sid_bytes = reinterpret_cast<const std::uint8_t*>(user->User.Sid);
  const auto subauthorities = sid_bytes[1];
  if (subauthorities > SID_MAX_SUB_AUTHORITIES ||
      remaining < 8U + 4U * subauthorities || !IsValidSid(user->User.Sid))
    return windows_peer_error::invalid_token_information;
  const DWORD sid_length = GetLengthSid(user->User.Sid);
  if (sid_length > remaining || sid_length > SECURITY_MAX_SID_SIZE)
    return windows_peer_error::invalid_token_information;
  if (IsWellKnownSid(user->User.Sid, WinAnonymousSid)) return windows_peer_error::anonymous_peer;
  snapshot.sid.assign(sid_bytes, sid_bytes + sid_length);
  return windows_peer_error::none;
}

held_process_state query_process(HANDLE process) noexcept {
  const DWORD state = WaitForSingleObject(process, 0);
  if (state == WAIT_TIMEOUT) return held_process_state::alive;
  if (state == WAIT_OBJECT_0) return held_process_state::exited;
  return held_process_state::unavailable;
}

windows_peer_error require_alive(HANDLE process) noexcept {
  const auto state = query_process(process);
  if (state == held_process_state::alive) return windows_peer_error::none;
  return state == held_process_state::exited ? windows_peer_error::process_exited :
      windows_peer_error::process_state_unavailable;
}

verified_windows_peer rejected(windows_peer_error error) noexcept { return {error, nullptr}; }

}  // namespace

windows_peer_identity::windows_peer_identity(HANDLE process, std::uint32_t process_id,
    std::uint64_t creation_filetime, std::vector<std::uint8_t> user_sid,
    windows_authentication_id authentication_id, std::uint32_t session_id) noexcept
    : process_(process), process_id_(process_id), creation_filetime_(creation_filetime),
      user_sid_(std::move(user_sid)), authentication_id_(authentication_id), session_id_(session_id) {}

windows_peer_identity::~windows_peer_identity() { CloseHandle(process_); }
held_process_state windows_peer_identity::process_state() const noexcept { return query_process(process_); }

verified_windows_peer verify_windows_named_pipe_peer(HANDLE server_pipe) noexcept {
  try {
    checked_revert revert;  // Covers invalid input, API failures and allocation unwind too.
    HANDLE existing_raw = nullptr;
    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &existing_raw)) {
      owned_handle existing(existing_raw);
      return rejected(windows_peer_error::caller_already_impersonating);
    }
    const DWORD context_error = GetLastError();
    if (context_error == ERROR_CANT_OPEN_ANONYMOUS)
      return rejected(windows_peer_error::caller_already_impersonating);
    if (context_error != ERROR_NO_TOKEN)
      return rejected(windows_peer_error::thread_context_unavailable);
    if (!server_pipe || server_pipe == INVALID_HANDLE_VALUE)
      return rejected(windows_peer_error::invalid_handle);

    HANDLE duplicate_raw = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), server_pipe, GetCurrentProcess(),
            &duplicate_raw, 0, FALSE, DUPLICATE_SAME_ACCESS))
      return rejected(windows_peer_error::pipe_duplicate_failed);
    owned_handle pipe(duplicate_raw);
    DWORD flags = 0;
    if (GetFileType(pipe.get()) != FILE_TYPE_PIPE ||
        !GetNamedPipeInfo(pipe.get(), &flags, nullptr, nullptr, nullptr) || !(flags & PIPE_SERVER_END))
      return rejected(windows_peer_error::not_server_named_pipe);
    ULONG pid = 0, pipe_session = 0;
    if (!GetNamedPipeClientProcessId(pipe.get(), &pid) || pid == 0 ||
        !GetNamedPipeClientSessionId(pipe.get(), &pipe_session))
      return rejected(windows_peer_error::pipe_identity_unavailable);
    owned_handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
    if (!process.get()) return rejected(windows_peer_error::process_open_failed);
    auto error = require_alive(process.get());
    if (error != windows_peer_error::none) return rejected(error);
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process.get(), &created, &exited, &kernel, &user))
      return rejected(windows_peer_error::process_time_unavailable);
    const auto creation = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32U) | created.dwLowDateTime;
    if (creation == 0) return rejected(windows_peer_error::process_time_unavailable);

    HANDLE primary_raw = nullptr;
    if (!OpenProcessToken(process.get(), TOKEN_QUERY, &primary_raw))
      return rejected(windows_peer_error::primary_token_unavailable);
    owned_handle primary(primary_raw);
    token_snapshot primary_snapshot;
    error = read_token(primary.get(), primary_snapshot);
    if (error != windows_peer_error::none) return rejected(error);
    if (primary_snapshot.statistics.TokenType != TokenPrimary)
      return rejected(windows_peer_error::invalid_token_information);

    if (!ImpersonateNamedPipeClient(pipe.get())) return rejected(windows_peer_error::impersonation_failed);
    HANDLE impersonation_raw = nullptr;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &impersonation_raw))
      return rejected(GetLastError() == ERROR_CANT_OPEN_ANONYMOUS ? windows_peer_error::anonymous_peer :
          windows_peer_error::impersonation_token_unavailable);
    owned_handle impersonation(impersonation_raw);
    token_snapshot impersonation_snapshot;
    error = read_token(impersonation.get(), impersonation_snapshot);
    if (error != windows_peer_error::none) return rejected(error);
    if (impersonation_snapshot.statistics.TokenType != TokenImpersonation)
      return rejected(windows_peer_error::invalid_token_information);
    const auto level = impersonation_snapshot.statistics.ImpersonationLevel;
    if (level == SecurityAnonymous) return rejected(windows_peer_error::anonymous_peer);
    if (level != SecurityImpersonation && level != SecurityDelegation)
      return rejected(windows_peer_error::insufficient_impersonation_level);
    if (primary_snapshot.sid != impersonation_snapshot.sid) return rejected(windows_peer_error::user_mismatch);
    const auto& primary_auth = primary_snapshot.statistics.AuthenticationId;
    const auto& impersonation_auth = impersonation_snapshot.statistics.AuthenticationId;
    if (primary_auth.LowPart != impersonation_auth.LowPart || primary_auth.HighPart != impersonation_auth.HighPart)
      return rejected(windows_peer_error::authentication_mismatch);
    if (primary_snapshot.session != impersonation_snapshot.session || primary_snapshot.session != pipe_session)
      return rejected(windows_peer_error::session_mismatch);
    revert.finish();  // Publish only in the server process context.

    ULONG final_pid = 0, final_session = 0;
    if (!GetNamedPipeClientProcessId(pipe.get(), &final_pid) ||
        !GetNamedPipeClientSessionId(pipe.get(), &final_session))
      return rejected(windows_peer_error::pipe_identity_unavailable);
    if (final_pid != pid || final_session != pipe_session) return rejected(windows_peer_error::pipe_identity_changed);
    error = require_alive(process.get());
    if (error != windows_peer_error::none) return rejected(error);
    auto identity = std::unique_ptr<const windows_peer_identity>(new windows_peer_identity(process.get(), pid,
        creation, std::move(primary_snapshot.sid), {primary_auth.LowPart, primary_auth.HighPart}, pipe_session));
    process.release();
    return {windows_peer_error::none, std::move(identity)};
  } catch (const std::bad_alloc&) {
    return rejected(windows_peer_error::out_of_memory);
  } catch (...) {
    return rejected(windows_peer_error::internal_failure);
  }
}

const char* windows_peer_error_name(windows_peer_error error) noexcept {
  switch (error) {
    case windows_peer_error::none: return "none";
    case windows_peer_error::invalid_handle: return "invalid_handle";
    case windows_peer_error::caller_already_impersonating: return "caller_already_impersonating";
    case windows_peer_error::thread_context_unavailable: return "thread_context_unavailable";
    case windows_peer_error::pipe_duplicate_failed: return "pipe_duplicate_failed";
    case windows_peer_error::not_server_named_pipe: return "not_server_named_pipe";
    case windows_peer_error::pipe_identity_unavailable: return "pipe_identity_unavailable";
    case windows_peer_error::process_open_failed: return "process_open_failed";
    case windows_peer_error::process_time_unavailable: return "process_time_unavailable";
    case windows_peer_error::process_exited: return "process_exited";
    case windows_peer_error::process_state_unavailable: return "process_state_unavailable";
    case windows_peer_error::primary_token_unavailable: return "primary_token_unavailable";
    case windows_peer_error::impersonation_failed: return "impersonation_failed";
    case windows_peer_error::impersonation_token_unavailable: return "impersonation_token_unavailable";
    case windows_peer_error::token_information_unavailable: return "token_information_unavailable";
    case windows_peer_error::invalid_token_information: return "invalid_token_information";
    case windows_peer_error::anonymous_peer: return "anonymous_peer";
    case windows_peer_error::insufficient_impersonation_level: return "insufficient_impersonation_level";
    case windows_peer_error::user_mismatch: return "user_mismatch";
    case windows_peer_error::authentication_mismatch: return "authentication_mismatch";
    case windows_peer_error::session_mismatch: return "session_mismatch";
    case windows_peer_error::pipe_identity_changed: return "pipe_identity_changed";
    case windows_peer_error::out_of_memory: return "out_of_memory";
    case windows_peer_error::internal_failure: return "internal_failure";
  }
  return "internal_failure";
}

}  // namespace soui::runtime::agent
