// Exercise the production listener without a renderer or application backend.
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <atomic>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <sys/socket.h>
#endif

namespace {
std::atomic<bool> abort_next_accept{false};
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

// Inject one documented connection-local accept failure, not a listener error.
#ifdef __linux__
#define accept4 interrupted_accept
#else
#define accept interrupted_accept
#endif
#include "../../sdk/src/desktop/resource_server.cpp"
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

int main() {
  try {
    cases();
    std::cout << "Resource accept-recovery and stop checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
