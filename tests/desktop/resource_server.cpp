// Exercise the production listener without a renderer or application backend.
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
// Include the production dependencies before remapping socket calls so the
// injection macros do not change system or standard-library declarations.
#include "../../sdk/src/desktop/resource_server.h"
#include "../../sdk/src/runtime/platform/secure_random.h"
#include "../../sdk/src/runtime/platform/strict_utf8.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
std::atomic<bool> abort_next_accept{false};
enum class startup_fault { none, bind, listen, inspect };
struct startup_injection {
  startup_fault fault{startup_fault::none};
  int error{0};
  unsigned bind_calls{0}, listen_calls{0}, inspect_calls{0}, close_calls{0};
  bool cleanup_clobbered_error{false};
} startup;
#ifdef _WIN32
using test_socket = SOCKET;
using test_socket_length = int;
constexpr int cleanup_error = WSAEINVAL;
void set_socket_error(int error) { ::WSASetLastError(error); }
int socket_error() { return ::WSAGetLastError(); }
#else
using test_socket = int;
using test_socket_length = socklen_t;
constexpr int cleanup_error = EINVAL;
void set_socket_error(int error) { errno = error; }
int socket_error() { return errno; }
#endif

int injected_bind(test_socket socket, const sockaddr* address, test_socket_length size) {
  if (startup.fault == startup_fault::none) return ::bind(socket, address, size);
  ++startup.bind_calls;
  if (startup.fault != startup_fault::bind) return 0;
  set_socket_error(startup.error);
  return -1;
}
int injected_listen(test_socket socket, int backlog) {
  if (startup.fault == startup_fault::none) return ::listen(socket, backlog);
  ++startup.listen_calls;
  if (startup.fault != startup_fault::listen) return 0;
  set_socket_error(startup.error);
  return -1;
}
int injected_getsockname(test_socket socket, sockaddr* address, test_socket_length* size) {
  if (startup.fault == startup_fault::none) return ::getsockname(socket, address, size);
  ++startup.inspect_calls;
  set_socket_error(startup.error);
  return -1;
}
int injected_close(test_socket socket) {
#ifdef _WIN32
  const auto status = ::closesocket(socket);
#else
  const auto status = ::close(socket);
#endif
  if (startup.fault != startup_fault::none) {
    ++startup.close_calls;
    // Cleanup is allowed to overwrite the thread-local OS error. The returned
    // diagnostic must already have captured the failing operation's error.
    set_socket_error(cleanup_error);
    startup.cleanup_clobbered_error = socket_error() == cleanup_error;
  }
  return status;
}
#ifdef _WIN32
SOCKET interrupted_accept(SOCKET socket, sockaddr* address, int* size) {
  if (abort_next_accept.exchange(false)) {
    ::WSASetLastError(WSAECONNRESET);
    return INVALID_SOCKET;
  }
  return ::accept(socket, address, size);
}
#elif defined(__linux__)
int interrupted_accept(int socket, sockaddr* address, socklen_t* size, int flags) {
  if (abort_next_accept.exchange(false)) {
    errno = ECONNABORTED;
    return -1;
  }
  return ::accept4(socket, address, size, flags);
}
#else
int interrupted_accept(int socket, sockaddr* address, socklen_t* size) {
  if (abort_next_accept.exchange(false)) {
    errno = ECONNABORTED;
    return -1;
  }
  return ::accept(socket, address, size);
}
#endif
}  // namespace

// Startup injection never reaches real bind/listen/getsockname. Outside the
// focused cases, retain the real listener and connection-local accept test.
#define bind injected_bind
#define listen injected_listen
#define getsockname injected_getsockname
#ifdef _WIN32
#define closesocket injected_close
#else
#define close injected_close
#endif
#ifdef __linux__
#define accept4 interrupted_accept
#else
#define accept interrupted_accept
#endif
#include "../../sdk/src/desktop/resource_server.cpp"
#undef bind
#undef listen
#undef getsockname
#ifdef _WIN32
#undef closesocket
#else
#undef close
#endif
#ifdef __linux__
#undef accept4
#else
#undef accept
#endif

#include <iostream>
#include <stdexcept>

namespace {
namespace detail = soui::desktop::detail;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

struct fixture {
  fs::path root;
  fixture() {
    std::array<unsigned char, 12> random{};
    if (!soui::runtime::detail::secure_random(random.data(), random.size()))
      throw std::runtime_error("Fixture entropy unavailable");
    std::string name = "soui-resource-server-";
    for (auto value : random) name += std::to_string(value) + "-";
    root = fs::temp_directory_path() / name;
    if (!fs::create_directory(root)) throw std::runtime_error("Fixture creation failed");
    root = fs::canonical(root);
    std::ofstream stream(root / "index.html", std::ios::binary);
    stream << "resource-ok";
    stream.close();
    if (!stream) throw std::runtime_error("Fixture write failed");
  }
  ~fixture() { std::error_code error; fs::remove_all(root, error); }
};

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

unsigned startup_error_cases() {
  fixture files;
  unsigned checks = 0;
  const auto check = [&checks](bool condition, const char* message) {
    require(condition, message);
    ++checks;
  };
  struct error_case { startup_fault fault; const char* operation; int error; };
#ifdef _WIN32
  constexpr const char* domain = "WSAGetLastError";
  const std::array<error_case, 3> cases{{
    {startup_fault::bind, "bind", WSAEACCES},
    {startup_fault::listen, "listen on", WSAEADDRINUSE},
    {startup_fault::inspect, "inspect", WSAENOTSOCK},
  }};
#else
  constexpr const char* domain = "errno";
  const std::array<error_case, 3> cases{{
    {startup_fault::bind, "bind", EACCES},
    {startup_fault::listen, "listen on", EADDRINUSE},
    {startup_fault::inspect, "inspect", ENOTSOCK},
  }};
#endif
  for (const auto& item : cases) {
    startup = {};
    startup.fault = item.fault;
    startup.error = item.error;
    struct reset_injection { ~reset_injection() { startup = {}; } } reset;
    const auto slots = detail::live_servers.load();
    detail::resource_server server;
    const auto status = server.start(files.root);
    const auto expected = std::string("Unable to ") + item.operation +
      " the local resource listener (" + domain + "=" + std::to_string(item.error) + ").";
    check(!status.ok, "Injected listener startup failure was accepted");
    check(status.code == "resource_server_unavailable", "Listener startup public error code changed");
    // Exact equality also excludes resource paths, the random prefix, and
    // cleanup's replacement error from the public diagnostic.
    check(status.message == expected, "Listener startup lost its operation, error domain or original code");
    check(server.base_uri().empty(), "Failed listener published a resource origin");
    check(detail::live_servers.load() == slots, "Failed listener retained its service slot");
    check(startup.bind_calls == 1, "Startup did not reach the injected bind exactly once");
    check(startup.listen_calls == (item.fault == startup_fault::bind ? 0U : 1U),
          "Startup reached an unexpected listen stage");
    check(startup.inspect_calls == (item.fault == startup_fault::inspect ? 1U : 0U),
          "Startup reached an unexpected endpoint-inspection stage");
    check(startup.close_calls == 1, "Failed listener socket was not released exactly once");
    check(startup.cleanup_clobbered_error, "Cleanup did not overwrite the injected native error");
  }
  return checks;
}

void cases() {
#ifdef _WIN32
  WSADATA winsock{};
  require(::WSAStartup(MAKEWORD(2, 2), &winsock) == 0, "Client Winsock startup failed");
  struct client_winsock { ~client_winsock() { ::WSACleanup(); } } network;
  ::WSASetLastError(WSAECONNRESET);
#else
  errno = ECONNABORTED;
#endif
  require(detail::retryable_accept_error(), "Aborted connection must be retried by accept");
  require(!detail::retryable_socket_error(), "Aborted connection must not retry ordinary I/O");
#ifdef _WIN32
  ::WSASetLastError(WSAENOTSOCK);
#else
  errno = EBADF;
#endif
  require(!detail::retryable_accept_error(), "Invalid listener must not busy-retry");
  fixture files;
  detail::resource_server server;
  require(server.start(files.root).ok, "Resource service did not start");
  const auto uri = server.base_uri();
  const auto path_begin = uri.find('/', 7);
  const auto host = uri.substr(7, path_begin - 7);
  const auto path = uri.substr(path_begin) + "index.html";
  detail::socket_owner client(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  require(client.value != detail::invalid_socket && detail::prepare_socket(client.value),
          "Client socket preparation failed");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(static_cast<unsigned short>(std::stoi(host.substr(10))));
  abort_next_accept.store(true);
  const auto connected = ::connect(client.value, reinterpret_cast<sockaddr*>(&address), sizeof(address));
#ifdef _WIN32
  require(connected == 0 || ::WSAGetLastError() == WSAEWOULDBLOCK, "Client connect failed");
#else
  require(connected == 0 || errno == EINPROGRESS, "Client connect failed");
#endif
  const std::atomic<bool> not_stopping{false};
  const auto deadline = detail::steady_clock::now() + 3s;
  const auto request = "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\n\r\n";
  require(detail::send_bytes(client.value, request, deadline, not_stopping), "Client send failed");
  std::string response;
  std::array<char, 2048> buffer{};
  while (detail::wait_for_socket(client.value, false, deadline, not_stopping) == detail::readiness::ready) {
    const auto count = ::recv(client.value, buffer.data(), static_cast<int>(buffer.size()), 0);
    if (count < 0 && detail::retryable_socket_error()) continue;
    if (count <= 0) break;
    response.append(buffer.data(), static_cast<std::size_t>(count));
    require(response.size() < 16 * 1024, "Unexpectedly large fixture response");
  }
  require(!abort_next_accept.load(), "Accept failure was not exercised");
  require(response.find("HTTP/1.1 200 OK\r\n") == 0,
          "Aborted incoming connection terminated the resource listener");
  require(response.find("\r\n\r\nresource-ok") != std::string::npos, "Resource body mismatch");
  server.stop();
  require(server.base_uri().empty(), "Stopped service still publishes its origin");
  const auto stopped_deadline = detail::steady_clock::now() + 3s;
  while (detail::live_servers.load() && detail::steady_clock::now() < stopped_deadline)
    std::this_thread::sleep_for(1ms);
  require(detail::live_servers.load() == 0, "Stopped listener retained its service slot");
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const bool startup_only = argc == 2 && std::string_view(argv[1]) == "--startup-errors";
    require(argc == 1 || startup_only, "Usage: resource_server [--startup-errors]");
    const auto startup_checks = startup_error_cases();
    std::cout << "Resource startup diagnostics: " << startup_checks
              << " checks, 0 failures; injected bind/listen/getsockname, no real listener\n";
    if (!startup_only) {
      cases();
      std::cout << "Resource accept-recovery and stop checks passed\n";
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
