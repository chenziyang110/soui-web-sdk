#include <soui/runtime/agent/windows_peer_identity.h>
#include <sddl.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cwchar>
#include <iostream>
#include <iterator>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// TEST_ONLY: bounded C++ allocation failures on the verifying thread. This is
// neither a Windows API failure seam nor a claim of global allocation closure.
namespace { thread_local int allocation_failure_after = -1; }
void* operator new(std::size_t bytes) {
  if (allocation_failure_after == 0) throw std::bad_alloc();
  if (allocation_failure_after > 0) --allocation_failure_after;
  if (void* storage = std::malloc(bytes ? bytes : 1)) return storage;
  throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* storage) noexcept { std::free(storage); }
void operator delete[](void* storage) noexcept { std::free(storage); }
void operator delete(void* storage, std::size_t) noexcept { std::free(storage); }
void operator delete[](void* storage, std::size_t) noexcept { std::free(storage); }

namespace {
using namespace soui::runtime::agent;
constexpr DWORD wait_ms = 5000;
static_assert(!std::is_default_constructible_v<windows_peer_identity>);
static_assert(!std::is_copy_constructible_v<windows_peer_identity>);
static_assert(!std::is_move_constructible_v<windows_peer_identity>);

void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
[[noreturn]] void test_fail_stop() noexcept {
  // An overlapped operation still pending after cancellation cannot outlive its
  // stack buffer. Root's Owned Job then closes remaining child processes.
  TerminateProcess(GetCurrentProcess(), 91);
  ExitProcess(91);
}
class handle final {
 public:
  explicit handle(HANDLE value = nullptr) noexcept : value_(value) {}
  ~handle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
  handle(const handle&) = delete;
  handle& operator=(const handle&) = delete;
  handle(handle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
  HANDLE get() const noexcept { return value_; }
 private:
  HANDLE value_;
};
struct local_free {
  void operator()(void* storage) const noexcept { if (storage) LocalFree(storage); }
};
using local_memory = std::unique_ptr<void, local_free>;

std::uint64_t creation_time(HANDLE process) {
  FILETIME creation{}, exit{}, kernel{}, user{};
  require(GetProcessTimes(process, &creation, &exit, &kernel, &user) != FALSE, "process time query failed");
  return (static_cast<std::uint64_t>(creation.dwHighDateTime) << 32U) | creation.dwLowDateTime;
}
DWORD handle_count() {
  DWORD count = 0;
  require(GetProcessHandleCount(GetCurrentProcess(), &count) != FALSE, "handle count query failed");
  return count;
}
void require_self_context() {
  HANDLE raw = nullptr;
  const BOOL opened = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &raw);
  const DWORD error = opened ? ERROR_SUCCESS : GetLastError();
  handle token(raw);
  require(!opened && error == ERROR_NO_TOKEN, "verification left a thread token");
}
struct primary_identity {
  std::vector<unsigned char> user;
  TOKEN_STATISTICS statistics{};
  DWORD session{0};
};
primary_identity query_primary(HANDLE process) {
  HANDLE raw = nullptr;
  require(OpenProcessToken(process, TOKEN_QUERY, &raw) != FALSE, "primary token query failed");
  handle token(raw);
  primary_identity value;
  DWORD bytes = 0;
  require(GetTokenInformation(token.get(), TokenStatistics, &value.statistics, sizeof(value.statistics), &bytes) != FALSE,
      "primary statistics query failed");
  require(GetTokenInformation(token.get(), TokenSessionId, &value.session, sizeof(value.session), &bytes) != FALSE,
      "primary session query failed");
  require(!GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes) && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
      "primary user size query failed");
  value.user.resize(bytes);
  require(GetTokenInformation(token.get(), TokenUser, value.user.data(), bytes, &bytes) != FALSE, "primary user query failed");
  return value;
}

class operation final {
 public:
  explicit operation(HANDLE file) : file_(file), event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
    require(event_.get() != nullptr, "I/O event creation failed");
    overlapped_.hEvent = event_.get();
  }
  ~operation() { cancel_pending(); }
  OVERLAPPED* data() noexcept { return &overlapped_; }
  void started(BOOL immediate, DWORD error) {
    pending_ = !immediate && error == ERROR_IO_PENDING;
    require(immediate || pending_, "overlapped I/O start failed");
  }
  DWORD finish(DWORD immediate_bytes = 0) {
    if (!pending_) return immediate_bytes;
    if (WaitForSingleObject(event_.get(), wait_ms) != WAIT_OBJECT_0) {
      cancel_pending();
      throw std::runtime_error("bounded I/O wait failed");
    }
    pending_ = false;
    DWORD transferred = 0;
    require(GetOverlappedResult(file_, &overlapped_, &transferred, FALSE) != FALSE, "overlapped I/O completion failed");
    return transferred;
  }
 private:
  void cancel_pending() noexcept {
    if (!pending_) return;
    CancelIoEx(file_, &overlapped_);
    if (WaitForSingleObject(event_.get(), wait_ms) != WAIT_OBJECT_0) test_fail_stop();
    // The signaled OVERLAPPED event ends storage use, including aborted I/O.
    pending_ = false;
  }
  HANDLE file_;
  handle event_;
  OVERLAPPED overlapped_{};
  bool pending_{false};
};
void write_exact(HANDLE file, const void* bytes, DWORD count) {
  operation io(file);
  DWORD written = 0;
  const BOOL immediate = WriteFile(file, bytes, count, &written, io.data());
  const DWORD error = immediate ? ERROR_SUCCESS : GetLastError();
  io.started(immediate, error);
  require(io.finish(written) == count, "short pipe write");
}
void read_exact(HANDLE file, void* bytes, DWORD count) {
  operation io(file);
  DWORD read = 0;
  const BOOL immediate = ReadFile(file, bytes, count, &read, io.data());
  const DWORD error = immediate ? ERROR_SUCCESS : GetLastError();
  io.started(immediate, error);
  require(io.finish(read) == count, "short pipe read");
}

std::wstring executable_path() {
  std::wstring path(32768, L'\0');
  const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  require(count > 0 && count < path.size(), "test executable path unavailable");
  path.resize(count);
  return path;
}
std::wstring pipe_name() {
  static unsigned sequence = 0;
  return L"\\\\.\\pipe\\soui-peer-identity-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
      std::to_wstring(creation_time(GetCurrentProcess())) + L"-" + std::to_wstring(++sequence);
}
local_memory pipe_security() {
  auto current = query_primary(GetCurrentProcess());
  const auto* user = reinterpret_cast<const TOKEN_USER*>(current.user.data());
  LPWSTR text = nullptr;
  require(ConvertSidToStringSidW(user->User.Sid, &text) != FALSE, "SID conversion failed");
  local_memory sid_text(text);
  const std::wstring sddl = L"D:P(A;;GA;;;" + std::wstring(text) + L")";
  PSECURITY_DESCRIPTOR raw = nullptr;
  require(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &raw, nullptr) != FALSE,
      "pipe DACL creation failed");
  return local_memory(raw);
}
handle create_server(const std::wstring& name) {
  auto security = pipe_security();
  SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), security.get(), FALSE};
  handle pipe(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 512, 512, wait_ms, &attributes));
  require(pipe.get() != INVALID_HANDLE_VALUE, "server pipe creation failed");
  return pipe;
}

class child_process final {
 public:
  child_process(const std::wstring& pipe, DWORD level) {
    const auto executable = executable_path();
    std::wstring command = L"\"" + executable + L"\" --client \"" + pipe + L"\" " + std::to_wstring(level);
    STARTUPINFOW startup{sizeof(STARTUPINFOW)};
    require(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &info_) != FALSE, "real client process creation failed");
  }
  ~child_process() {
    if (info_.hProcess) {
      if (WaitForSingleObject(info_.hProcess, 0) == WAIT_TIMEOUT) {
        TerminateProcess(info_.hProcess, 90);  // Failure cleanup, never a successful test receipt.
        if (WaitForSingleObject(info_.hProcess, wait_ms) != WAIT_OBJECT_0) test_fail_stop();
      }
      CloseHandle(info_.hProcess);
      CloseHandle(info_.hThread);
    }
  }
  child_process(const child_process&) = delete;
  child_process& operator=(const child_process&) = delete;
  HANDLE process() const noexcept { return info_.hProcess; }
  DWORD pid() const noexcept { return info_.dwProcessId; }
  void finished() {
    require(WaitForSingleObject(info_.hProcess, wait_ms) == WAIT_OBJECT_0, "client did not finish within bound");
    DWORD exit = 0;
    require(GetExitCodeProcess(info_.hProcess, &exit) != FALSE && exit == 0, "client returned a failure");
  }
 private:
  PROCESS_INFORMATION info_{};
};

struct pipe_fixture {
  std::wstring name{pipe_name()};
  handle server{create_server(name)};
  std::unique_ptr<child_process> child;
  explicit pipe_fixture(DWORD level = SECURITY_IMPERSONATION) {
    operation connection(server.get());
    const BOOL immediate = ConnectNamedPipe(server.get(), connection.data());
    const DWORD error = immediate ? ERROR_SUCCESS : GetLastError();
    connection.started(immediate || error == ERROR_PIPE_CONNECTED, error);
    child = std::make_unique<child_process>(name, level);
    connection.finish();
    std::array<char, 128> request{};
    read_exact(server.get(), request.data(), static_cast<DWORD>(request.size()));
    // Actual last-message read precedes impersonation. Contents intentionally
    // claim another PID/SID and are never provided to the production factory.
    std::array<char, 128> expected{};
    const char forged[] = "{\"pid\":0,\"sid\":\"S-1-5-7\",\"gesture\":true}";
    std::copy(std::begin(forged), std::end(forged), expected.begin());
    require(request == expected, "client handshake mismatch");
  }
  void finish_client() {
    const unsigned char done = 1;
    write_exact(server.get(), &done, sizeof(done));
    child->finished();
  }
};

void compare_actual(const windows_peer_identity& identity, const child_process& child) {
  require(identity.process_id() == child.pid(), "identity did not bind actual child PID");
  require(identity.creation_filetime() == creation_time(child.process()), "identity did not bind actual child FILETIME");
  const auto expected = query_primary(child.process());
  const auto* user = reinterpret_cast<const TOKEN_USER*>(expected.user.data());
  require(EqualSid(user->User.Sid, const_cast<std::uint8_t*>(identity.user_sid().data())) != FALSE, "actual user SID mismatch");
  const auto auth = identity.authentication_id();
  require(auth.low_part == expected.statistics.AuthenticationId.LowPart &&
      auth.high_part == expected.statistics.AuthenticationId.HighPart, "actual logon LUID mismatch");
  require(identity.session_id() == expected.session, "actual token session mismatch");
}

void actual_identity_and_forged_wire() {
  pipe_fixture pipe;
  const DWORD before = handle_count();
  {
    const auto result = verify_windows_named_pipe_peer(pipe.server.get());
    require(result.verified(), "actual peer verification failed");
    compare_actual(*result.identity, *pipe.child);
    require(result.identity->process_id() != 0, "wire PID was trusted");
    require(result.identity->process_state() == held_process_state::alive, "held process was not alive");
    require_self_context();
  }
  require(handle_count() == before, "verified identity retained handles after destruction");
  pipe.finish_client();
}
void rejected_level(DWORD level, windows_peer_error expected) {
  pipe_fixture pipe(level);
  const DWORD before = handle_count();
  const auto result = verify_windows_named_pipe_peer(pipe.server.get());
  require(!result.verified() && !result.identity && result.error == expected, "low impersonation level was admitted");
  require_self_context();
  require(handle_count() == before, "rejected token retained handles");
  pipe.finish_client();
}
void invalid_and_client_handles() {
  require(verify_windows_named_pipe_peer(nullptr).error == windows_peer_error::invalid_handle, "null handle admitted");
  require(verify_windows_named_pipe_peer(INVALID_HANDLE_VALUE).error == windows_peer_error::invalid_handle, "invalid handle admitted");
  handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  require(event.get() != nullptr, "test event creation failed");
  require(verify_windows_named_pipe_peer(event.get()).error == windows_peer_error::not_server_named_pipe, "event handle admitted");
  const auto name = pipe_name();
  auto server = create_server(name);
  operation connection(server.get());
  const BOOL immediate = ConnectNamedPipe(server.get(), connection.data());
  const DWORD error = immediate ? ERROR_SUCCESS : GetLastError();
  connection.started(immediate || error == ERROR_PIPE_CONNECTED, error);
  handle client(CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
      FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IMPERSONATION, nullptr));
  require(client.get() != INVALID_HANDLE_VALUE, "real client end creation failed");
  connection.finish();
  require(verify_windows_named_pipe_peer(client.get()).error == windows_peer_error::not_server_named_pipe, "client end admitted");
  require_self_context();
}
void existing_impersonation_is_cleared() {
  pipe_fixture pipe;
  const DWORD before = handle_count();
  require(ImpersonateNamedPipeClient(pipe.server.get()) != FALSE, "existing impersonation setup failed");
  const auto result = verify_windows_named_pipe_peer(pipe.server.get());
  require(result.error == windows_peer_error::caller_already_impersonating && !result.identity, "existing context was admitted");
  require_self_context();
  require(handle_count() == before, "existing context rejection retained handles");
  pipe.finish_client();
}
void held_process_survives_pipe_close() {
  verified_windows_peer result;
  {
    pipe_fixture pipe;
    result = verify_windows_named_pipe_peer(pipe.server.get());
    require(result.verified() && result.identity->process_state() == held_process_state::alive, "held identity setup failed");
    pipe.finish_client();
    require(result.identity->process_state() == held_process_state::exited, "held process did not report actual exit");
  }
  require(result.identity->process_state() == held_process_state::exited, "identity reopened PID after pipe closure");
}
void disconnected_peer_is_rejected() {
  pipe_fixture pipe;
  pipe.finish_client();
  require(DisconnectNamedPipe(pipe.server.get()) != FALSE, "real disconnect failed");
  const auto result = verify_windows_named_pipe_peer(pipe.server.get());
  require(!result.verified() && !result.identity && result.error == windows_peer_error::pipe_identity_unavailable,
      "disconnected instance was admitted");
  require_self_context();
}
void bounded_allocation_failures() {
  pipe_fixture pipe;
  const DWORD before = handle_count();
  bool reached_success = false;
  for (int ordinal = 0; ordinal < 16; ++ordinal) {
    allocation_failure_after = ordinal;
    auto result = verify_windows_named_pipe_peer(pipe.server.get());
    allocation_failure_after = -1;
    require_self_context();
    if (result.verified()) {
      compare_actual(*result.identity, *pipe.child);
      result.identity.reset();
      require(handle_count() == before, "successful allocation walk retained handles");
      reached_success = true;
      break;
    }
    require(result.error == windows_peer_error::out_of_memory && !result.identity, "allocation failure escaped stable error");
    require(handle_count() == before, "allocation failure retained native handles");
  }
  require(reached_success, "allocation walk bound did not reach success");
  pipe.finish_client();
}

int client_main(const wchar_t* name, DWORD level) {
  handle pipe(CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
      FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | level, nullptr));
  require(pipe.get() != INVALID_HANDLE_VALUE, "client pipe open failed");
  std::array<char, 128> request{};
  const char forged[] = "{\"pid\":0,\"sid\":\"S-1-5-7\",\"gesture\":true}";
  std::copy(std::begin(forged), std::end(forged), request.begin());
  write_exact(pipe.get(), request.data(), static_cast<DWORD>(request.size()));
  unsigned char done = 0;
  read_exact(pipe.get(), &done, sizeof(done));
  require(done == 1, "client completion marker mismatch");
  return 0;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  try {
    if (argc == 4 && std::wcscmp(argv[1], L"--client") == 0) {
      wchar_t* end = nullptr;
      const unsigned long level = std::wcstoul(argv[3], &end, 10);
      require(end && *end == L'\0' && (level == SECURITY_ANONYMOUS || level == SECURITY_IDENTIFICATION ||
          level == SECURITY_IMPERSONATION), "invalid internal client level");
      return client_main(argv[2], static_cast<DWORD>(level));
    }
    require(argc == 1, "unexpected test argument");
    actual_identity_and_forged_wire();
    rejected_level(SECURITY_ANONYMOUS, windows_peer_error::anonymous_peer);
    rejected_level(SECURITY_IDENTIFICATION, windows_peer_error::insufficient_impersonation_level);
    invalid_and_client_handles();
    existing_impersonation_is_cleared();
    held_process_survives_pipe_close();
    disconnected_peer_is_rejected();
    bounded_allocation_failures();
    require_self_context();
    std::cout << "windows_peer_identity: 8 real-pipe cases passed\n";
    return 0;
  } catch (...) {
    allocation_failure_after = -1;
    if (!RevertToSelf()) test_fail_stop();
    std::cerr << "windows_peer_identity: native test failed\n";
    return 1;
  }
}
