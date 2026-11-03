#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "resource_server.h"
#include "../runtime/platform/secure_random.h"
#include "../runtime/platform/strict_utf8.h"

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

namespace soui::desktop::detail {
namespace {
constexpr std::size_t maximum_header_bytes = 16 * 1024;
constexpr std::size_t maximum_resource_bytes = 8 * 1024 * 1024;
constexpr unsigned maximum_live_servers = 16;
constexpr auto socket_deadline = std::chrono::seconds(2);
constexpr auto poll_interval = std::chrono::milliseconds(200);
using steady_clock = std::chrono::steady_clock;

// Trivially destructible and independent of any application group. A stopped
// server blocked inside the OS filesystem still occupies its slot.
std::atomic<unsigned> live_servers{0};

#ifdef _WIN32
using socket_handle = SOCKET;
using socket_length = int;
constexpr socket_handle invalid_socket = INVALID_SOCKET;
#else
using socket_handle = int;
using socket_length = socklen_t;
constexpr socket_handle invalid_socket = -1;
#endif

void close_socket(socket_handle socket) noexcept {
  if (socket == invalid_socket) return;
#ifdef _WIN32
  ::closesocket(socket);
#else
  ::close(socket);
#endif
}

struct socket_owner {
  socket_handle value{invalid_socket};
  socket_owner() = default;
  explicit socket_owner(socket_handle socket) noexcept : value(socket) {}
  ~socket_owner() { close_socket(value); }
  socket_owner(const socket_owner&) = delete;
  socket_owner& operator=(const socket_owner&) = delete;
};

bool retryable_socket_error() noexcept {
#ifdef _WIN32
  const auto error = ::WSAGetLastError();
  return error == WSAEWOULDBLOCK || error == WSAEINTR;
#else
  return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

bool retryable_accept_error() noexcept {
  // An aborted queued connection does not invalidate the listening socket.
  // Retry it only at the accept boundary; send/recv must still drop that peer.
#ifdef _WIN32
  if (::WSAGetLastError() == WSAECONNRESET) return true;
#else
  if (errno == ECONNABORTED) return true;
#endif
  return retryable_socket_error();
}

bool interrupted_socket_error() noexcept {
#ifdef _WIN32
  return ::WSAGetLastError() == WSAEINTR;
#else
  return errno == EINTR;
#endif
}

bool prepare_socket(socket_handle socket) noexcept {
#ifdef _WIN32
  u_long nonblocking = 1;
  if (::ioctlsocket(socket, FIONBIO, &nonblocking) != 0) return false;
  // Accepted sockets must not leak into child processes either.
  if (!::SetHandleInformation(reinterpret_cast<HANDLE>(socket),
                              HANDLE_FLAG_INHERIT, 0)) return false;
#else
  const auto flags = ::fcntl(socket, F_GETFL, 0);
  const auto descriptor_flags = ::fcntl(socket, F_GETFD, 0);
  if (flags < 0 || descriptor_flags < 0 ||
      ::fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0 ||
      ::fcntl(socket, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0) return false;
#ifdef __APPLE__
  const int suppress_sigpipe = 1;
  if (::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &suppress_sigpipe,
                   sizeof(suppress_sigpipe)) != 0) return false;
#endif
#endif
  return true;
}

enum class readiness { ready, elapsed, failed, stopped };

readiness wait_for_socket(socket_handle socket, bool writing,
                          steady_clock::time_point deadline,
                          const std::atomic<bool>& stopping) noexcept {
  for (;;) {
    if (stopping.load(std::memory_order_acquire)) return readiness::stopped;
    const auto now = steady_clock::now();
    if (now >= deadline) return readiness::elapsed;
    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    remaining = std::min(remaining, poll_interval);
    // Round the final sub-millisecond interval up rather than busy-poll.
    if (remaining.count() < 1) remaining = std::chrono::milliseconds(1);
#ifdef _WIN32
    fd_set descriptors;
    FD_ZERO(&descriptors);
    FD_SET(socket, &descriptors);
    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = static_cast<long>(remaining.count() * 1000);
    const auto selected = ::select(0, writing ? nullptr : &descriptors,
                                    writing ? &descriptors : nullptr, nullptr, &timeout);
#else
    pollfd descriptor{};
    descriptor.fd = socket;
    descriptor.events = writing ? POLLOUT : POLLIN;
    const auto selected = ::poll(&descriptor, 1, static_cast<int>(remaining.count()));
#endif
    if (selected < 0) {
      if (interrupted_socket_error()) continue;
      return readiness::failed;
    }
    if (selected > 0) {
      if (stopping.load(std::memory_order_acquire)) return readiness::stopped;
#ifndef _WIN32
      if (descriptor.revents & (POLLERR | POLLNVAL)) return readiness::failed;
      // POLLHUP on a reader can accompany remaining bytes. recv determines EOF.
      if (!(descriptor.revents & (descriptor.events | POLLHUP))) continue;
#endif
      return readiness::ready;
    }
  }
}

result failure(const char* code, const char* message) noexcept {
  try {
    return result::failure(code, message);
  } catch (...) {
    return {};
  }
}

// Capture the native error at the failing call site, before formatting or RAII
// teardown can replace it. Numeric diagnostics disclose no resource root or token.
int socket_error_code() noexcept {
#ifdef _WIN32
  return ::WSAGetLastError();
#else
  return errno;
#endif
}

result listener_failure(const char* operation, int error) noexcept {
  try {
#ifdef _WIN32
    constexpr auto domain = "WSAGetLastError";
#else
    constexpr auto domain = "errno";
#endif
    return result::failure("resource_server_unavailable",
        std::string("Unable to ") + operation + " the local resource listener (" +
        domain + "=" + std::to_string(error) + ").");
  } catch (...) {
    return failure("resource_server_unavailable", "Unable to start the local resource listener.");
  }
}

bool reserve_server() noexcept {
  auto current = live_servers.load(std::memory_order_relaxed);
  while (current < maximum_live_servers) {
    if (live_servers.compare_exchange_weak(current, current + 1,
                                          std::memory_order_relaxed)) return true;
  }
  return false;
}

bool header_token(std::string_view text) noexcept {
  if (text.empty()) return false;
  for (const unsigned char value : text) {
    if ((value >= '0' && value <= '9') || (value >= 'a' && value <= 'z') ||
        (value >= 'A' && value <= 'Z')) continue;
    switch (value) {
      case '!': case '#': case '$': case '%': case '&': case '\'':
      case '*': case '+': case '-': case '.': case '^': case '_':
      case '`': case '|': case '~': break;
      default: return false;
    }
  }
  return true;
}

bool same_header(std::string_view text, std::string_view expected) noexcept {
  if (text.size() != expected.size()) return false;
  for (std::size_t index = 0; index < text.size(); ++index) {
    auto value = text[index];
    if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
    if (value != expected[index]) return false;
  }
  return true;
}

std::string_view trim_ows(std::string_view value) noexcept {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
    value.remove_prefix(1);
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
    value.remove_suffix(1);
  return value;
}

int hex_digit(unsigned char value) noexcept {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

bool decode_path(std::string_view encoded, std::string& decoded) {
  if (encoded.empty() || encoded.size() > 12288) return false;
  decoded.reserve(encoded.size());
  for (std::size_t index = 0; index < encoded.size(); ++index) {
    auto value = static_cast<unsigned char>(encoded[index]);
    if (value == '%') {
      if (encoded.size() - index < 3) return false;
      const auto high = hex_digit(static_cast<unsigned char>(encoded[index + 1]));
      const auto low = hex_digit(static_cast<unsigned char>(encoded[index + 2]));
      if (high < 0 || low < 0) return false;
      value = static_cast<unsigned char>((high << 4) | low);
      index += 2;
    }
    // ':' excludes drive roots and Windows alternate data streams. A literal
    // '%' produced by %25 is a filename byte, never decoded a second time.
    if (value < 32 || value == 127 || value == '\\' || value == ':') return false;
    decoded += static_cast<char>(value);
    if (decoded.size() > 4096) return false;
  }
  if (!soui::runtime::detail::valid_utf8(decoded)) return false;
  for (std::size_t index = 0; index + 1 < decoded.size(); ++index) {
    if (static_cast<unsigned char>(decoded[index]) == 0xc2U &&
        static_cast<unsigned char>(decoded[index + 1]) <= 0x9fU) return false;
  }
  std::size_t begin = 0;
  for (;;) {
    const auto end = decoded.find('/', begin);
    const auto part = std::string_view(decoded).substr(
      begin, end == std::string::npos ? decoded.size() - begin : end - begin);
    if (part.empty() || part == "." || part == "..") return false;
    if (end == std::string::npos) return true;
    begin = end + 1;
  }
}

struct request {
  bool head{false};
  std::string path;
};

struct request_line {
  std::string_view method;
  std::string_view target;
};

bool parse_request_line(std::string_view bytes, request_line& parsed) noexcept {
  const auto line_end = bytes.find("\r\n");
  if (line_end == std::string_view::npos) return false;
  const auto line = bytes.substr(0, line_end);
  const auto first_space = line.find(' ');
  if (first_space == std::string_view::npos) return false;
  const auto second_space = line.find(' ', first_space + 1);
  if (second_space == std::string_view::npos ||
      line.find(' ', second_space + 1) != std::string_view::npos) return false;
  const auto method = line.substr(0, first_space);
  if (!header_token(method)) return false;
  const auto target = line.substr(first_space + 1, second_space - first_space - 1);
  const auto version = line.substr(second_space + 1);
  if (version != "HTTP/1.0" && version != "HTTP/1.1") return false;
  if (target.empty() || target.front() != '/') return false;
  for (const unsigned char value : target)
    if (value <= 32 || value >= 127 || value == '\\' || value == '#') return false;
  parsed = {method, target};
  return true;
}

// Only origin-form HTTP/1.0 and HTTP/1.1 are supported. The complete header and
// any already-received trailing bytes are inspected before filesystem access.
int parse_request(std::string_view bytes, std::string_view host,
                  std::string_view prefix, request& parsed) {
  request_line line;
  if (!parse_request_line(bytes, line)) return 400;
  parsed.head = line.method == "HEAD";
  const auto terminator = bytes.find("\r\n\r\n");
  if (terminator == std::string_view::npos || terminator + 4 != bytes.size())
    return 400;
  bool have_host = false, have_length = false;
  std::size_t position = bytes.find("\r\n") + 2;
  while (position < terminator) {
    const auto end = bytes.find("\r\n", position);
    if (end == std::string_view::npos || end > terminator) return 400;
    const auto field = bytes.substr(position, end - position);
    const auto colon = field.find(':');
    if (colon == std::string_view::npos || !header_token(field.substr(0, colon)))
      return 400;
    const auto name = field.substr(0, colon);
    const auto value = trim_ows(field.substr(colon + 1));
    for (const unsigned char character : value)
      if ((character < 32 && character != '\t') || character == 127) return 400;
    if (same_header(name, "host")) {
      if (have_host || value != host) return 400;
      have_host = true;
    } else if (same_header(name, "content-length")) {
      if (have_length || value != "0") return 400;
      have_length = true;
    } else if (same_header(name, "transfer-encoding") ||
               same_header(name, "expect") || same_header(name, "upgrade") ||
               same_header(name, "trailer")) {
      return 400;
    }
    position = end + 2;
  }
  if (!have_host) return 400;
  if (line.method != "GET" && line.method != "HEAD") return 405;
  const auto path = line.target.substr(0, line.target.find('?'));
  if (path.size() <= prefix.size() || path.substr(0, prefix.size()) != prefix)
    return 404;
  if (!decode_path(path.substr(prefix.size()), parsed.path)) return 400;
  return 200;
}

bool contained_by(const std::filesystem::path& root,
                  const std::filesystem::path& candidate) {
  auto selected = candidate.begin();
  for (const auto& part : root) {
    if (selected == candidate.end() || part != *selected) return false;
    ++selected;
  }
  return selected != candidate.end();
}

const char* mime_type(const std::filesystem::path& path) {
  auto extension = path.extension().u8string();
  for (auto& value : extension)
    if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
  struct entry { std::string_view extension; const char* mime; };
  constexpr entry types[] = {
    {".html", "text/html"}, {".htm", "text/html"}, {".css", "text/css"},
    {".js", "text/javascript"}, {".mjs", "text/javascript"},
    {".json", "application/json"}, {".map", "application/json"},
    {".txt", "text/plain"}, {".csv", "text/csv"}, {".xml", "application/xml"},
    {".svg", "image/svg+xml"}, {".png", "image/png"}, {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"}, {".gif", "image/gif"}, {".webp", "image/webp"},
    {".avif", "image/avif"}, {".ico", "image/x-icon"},
    {".wasm", "application/wasm"}, {".pdf", "application/pdf"},
    {".woff", "font/woff"}, {".woff2", "font/woff2"},
    {".ttf", "font/ttf"}, {".otf", "font/otf"}, {".mp3", "audio/mpeg"},
    {".wav", "audio/wav"}, {".ogg", "audio/ogg"},
    {".mp4", "video/mp4"}, {".webm", "video/webm"}
  };
  for (const auto& type : types)
    if (extension == type.extension) return type.mime;
  return "application/octet-stream";
}

struct resource {
  const char* mime{"application/octet-stream"};
  std::string bytes;
  std::size_t size{0};
};

int read_resource(const std::filesystem::path& root, const request& requested,
                  resource& loaded) {
  const auto relative = std::filesystem::u8path(requested.path);
  if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
    return 400;
  std::error_code error;
  const auto candidate = std::filesystem::canonical(root / relative, error);
  if (error || !contained_by(root, candidate)) return 404;
  if (!std::filesystem::is_regular_file(candidate, error) || error) return 404;
  const auto advertised_size = std::filesystem::file_size(candidate, error);
  if (error) return 404;
  if (advertised_size > maximum_resource_bytes) return 413;
  loaded.mime = mime_type(candidate);
  loaded.size = static_cast<std::size_t>(advertised_size);
  if (requested.head) return 200;

  std::ifstream stream(candidate, std::ios::binary);
  if (!stream) return 404;
  // The deployment directory must remain immutable. Still bound allocation and
  // detect growth instead of trusting the earlier size observation.
  loaded.bytes.resize(maximum_resource_bytes + 1);
  stream.read(loaded.bytes.data(), static_cast<std::streamsize>(loaded.bytes.size()));
  const auto count = static_cast<std::size_t>(stream.gcount());
  if (count > maximum_resource_bytes) return 413;
  if (stream.bad() || (stream.fail() && !stream.eof())) return 500;
  loaded.bytes.resize(count);
  loaded.size = count;
  return 200;
}

bool send_bytes(socket_handle socket, std::string_view bytes,
                steady_clock::time_point deadline,
                const std::atomic<bool>& stopping) noexcept {
  while (!bytes.empty()) {
    if (wait_for_socket(socket, true, deadline, stopping) != readiness::ready) return false;
    // Both Winsock and POSIX accept this bounded count without narrowing loss.
    const auto count = static_cast<int>(std::min<std::size_t>(bytes.size(), 64 * 1024));
#if defined(_WIN32)
    const auto sent = ::send(socket, bytes.data(), count, 0);
#elif defined(MSG_NOSIGNAL)
    const auto sent = ::send(socket, bytes.data(), static_cast<std::size_t>(count), MSG_NOSIGNAL);
#else
    const auto sent = ::send(socket, bytes.data(), static_cast<std::size_t>(count), 0);
#endif
    if (sent < 0) {
      if (retryable_socket_error()) continue;
      return false;
    }
    if (!sent) return false;
    bytes.remove_prefix(static_cast<std::size_t>(sent));
  }
  return true;
}

void respond(socket_handle socket, int status, const request& requested,
             const resource& loaded, const std::atomic<bool>& stopping) {
  const char* reason = "Internal Server Error";
  switch (status) {
    case 200: reason = "OK"; break;
    case 400: reason = "Bad Request"; break;
    case 404: reason = "Not Found"; break;
    case 405: reason = "Method Not Allowed"; break;
    case 408: reason = "Request Timeout"; break;
    case 413: reason = "Content Too Large"; break;
    case 431: reason = "Request Header Fields Too Large"; break;
    default: status = 500; break;
  }
  const std::string error_body = status == 200 ? "" : std::string(reason) + "\n";
  const auto size = status == 200 ? loaded.size : error_body.size();
  std::string header = "HTTP/1.1 " + std::to_string(status) + " " + reason +
    "\r\nContent-Type: " + (status == 200 ? loaded.mime : "text/plain; charset=utf-8") +
    "\r\nContent-Length: " + std::to_string(size) +
    "\r\nConnection: close\r\nCache-Control: no-store"
    "\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n";
  if (status == 405) header += "Allow: GET, HEAD\r\n";
  header += "\r\n";
  const auto deadline = steady_clock::now() + socket_deadline;
  if (!send_bytes(socket, header, deadline, stopping) || requested.head) return;
  send_bytes(socket, status == 200 ? std::string_view(loaded.bytes) : std::string_view(error_body),
             deadline, stopping);
}

void serve_connection(socket_handle socket, const std::filesystem::path& root,
                      std::string_view host, std::string_view prefix,
                      const std::atomic<bool>& stopping) {
  std::string header;
  header.reserve(2048);
  std::array<char, 2048> buffer{};
  int status = 200;
  const auto deadline = steady_clock::now() + socket_deadline;
  for (;;) {
    if (header.find("\r\n\r\n") != std::string::npos) break;
    if (header.size() >= maximum_header_bytes) { status = 431; break; }
    const auto available = wait_for_socket(socket, false, deadline, stopping);
    if (available == readiness::elapsed) { status = 408; break; }
    if (available != readiness::ready) return;
    const auto wanted = static_cast<int>(
      std::min(buffer.size(), maximum_header_bytes - header.size()));
    const auto count = ::recv(socket, buffer.data(), wanted, 0);
    if (count < 0) {
      if (retryable_socket_error()) continue;
      return;
    }
    if (!count) return;
    header.append(buffer.data(), static_cast<std::size_t>(count));
  }
  request requested;
  resource loaded;
  if (status == 200) status = parse_request(header, host, prefix, requested);
  else {
    request_line line;
    if (parse_request_line(header, line)) requested.head = line.method == "HEAD";
  }
  if (stopping.load(std::memory_order_acquire)) return;
  if (status == 200) status = read_resource(root, requested, loaded);
  if (stopping.load(std::memory_order_acquire)) return;
  respond(socket, status, requested, loaded, stopping);
}
}  // namespace

struct resource_server::shared_state {
  std::filesystem::path root;
  std::string host;
  std::string prefix;
  std::string uri;
  std::atomic<bool> stopping{false};
  std::atomic<bool> activated{false};
  socket_handle listener{invalid_socket};
  bool charged{false};
#ifdef _WIN32
  bool winsock_started{false};
#endif

  ~shared_state() { release(); }
  // Called by the worker at actual exit (or by destruction before launch).
  // Never concurrently invoked; object destruction follows the last owner.
  void release() noexcept {
    close_socket(listener);
    listener = invalid_socket;
#ifdef _WIN32
    if (winsock_started) {
      ::WSACleanup();
      winsock_started = false;
    }
#endif
    if (charged) {
      live_servers.fetch_sub(1, std::memory_order_relaxed);
      charged = false;
    }
  }
};

resource_server::~resource_server() { stop(); }

result resource_server::start(const std::filesystem::path& root) noexcept {
  if (state_) return failure("resource_server_started", "Resource service is already started.");
  try {
    const auto state = std::make_shared<shared_state>();
    if (!reserve_server())
      return failure("resource_server_limit", "Too many local resource services are still active.");
    state->charged = true;
    // The owning application already validated this canonical directory.
    // Repeat only lexical checks, not potentially blocking filesystem metadata.
    if (!root.is_absolute() || root.lexically_normal() != root)
      return failure("invalid_root", "Resource root must be a validated canonical absolute directory.");
    state->root = root;
    std::array<unsigned char, 32> random{};
    if (!soui::runtime::detail::secure_random(random.data(), random.size()))
      return failure("entropy_unavailable", "Cannot authorize a resource service without system entropy.");
    constexpr char hex[] = "0123456789abcdef";
    state->prefix = "/";
    for (const auto value : random) {
      state->prefix += hex[value >> 4U];
      state->prefix += hex[value & 15U];
    }
    state->prefix += "/";
#ifdef _WIN32
    WSADATA data{};
    if (::WSAStartup(MAKEWORD(2, 2), &data) != 0)
      return failure("resource_server_unavailable", "Unable to initialize the local resource service.");
    state->winsock_started = true;
    state->listener = ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
                                  WSA_FLAG_NO_HANDLE_INHERIT);
#elif defined(__linux__)
    state->listener = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, IPPROTO_TCP);
#else
    state->listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#endif
    if (state->listener == invalid_socket || !prepare_socket(state->listener))
      return failure("resource_server_unavailable", "Unable to create the local resource listener.");
#ifdef _WIN32
    const BOOL exclusive = TRUE;
    if (::setsockopt(state->listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                     reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) != 0)
      return failure("resource_server_unavailable", "Unable to protect the local resource listener.");
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(state->listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
      const int error = socket_error_code();
      return listener_failure("bind", error);
    }
    if (::listen(state->listener, 8) != 0) {
      const int error = socket_error_code();
      return listener_failure("listen on", error);
    }
    socket_length address_size = sizeof(address);
    if (::getsockname(state->listener, reinterpret_cast<sockaddr*>(&address), &address_size) != 0) {
      const int error = socket_error_code();
      return listener_failure("inspect", error);
    }
    // A successful system call with an unexpected address has no native error.
    // Do not attach an unrelated stale errno/WSA error to this invariant failure.
    if (address.sin_family != AF_INET || address.sin_addr.s_addr != htonl(INADDR_LOOPBACK) ||
        !address.sin_port)
      return failure("resource_server_unavailable", "The local resource listener returned an unexpected address.");
    state->host = "127.0.0.1:" + std::to_string(ntohs(address.sin_port));
    state->uri = "http://" + state->host + state->prefix;

    std::thread worker(&resource_server::work, state);
    try {
      worker.detach();
    } catch (...) {
      // The startup gate forbids filesystem/socket reads until detachment
      // succeeds, so this exceptional join cannot wait on resource I/O.
      state->stopping.store(true, std::memory_order_release);
      worker.join();
      throw;
    }
    state_ = state;
    state->activated.store(true, std::memory_order_release);
    return result::success();
  } catch (...) {
    return failure("resource_server_unavailable", "Unable to start the local resource service.");
  }
}

std::string resource_server::base_uri() const {
  const auto state = state_;
  return state && !state->stopping.load(std::memory_order_acquire) ? state->uri : "";
}

void resource_server::stop() noexcept {
  const auto state = std::move(state_);
  if (state) state->stopping.store(true, std::memory_order_release);
}

void resource_server::work(std::shared_ptr<shared_state> state) noexcept {
  try {
    while (!state->activated.load(std::memory_order_acquire) &&
           !state->stopping.load(std::memory_order_acquire))
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    while (!state->stopping.load(std::memory_order_acquire)) {
      const auto available = wait_for_socket(state->listener, false,
        steady_clock::now() + poll_interval, state->stopping);
      if (available == readiness::elapsed) continue;
      if (available != readiness::ready) break;
      sockaddr_in peer{};
      socket_length peer_size = sizeof(peer);
#ifdef __linux__
      socket_owner connection(::accept4(state->listener,
        reinterpret_cast<sockaddr*>(&peer), &peer_size, SOCK_CLOEXEC | SOCK_NONBLOCK));
#else
      socket_owner connection(::accept(state->listener,
        reinterpret_cast<sockaddr*>(&peer), &peer_size));
#endif
      if (connection.value == invalid_socket) {
        if (retryable_accept_error()) continue;
        break;
      }
      if (state->stopping.load(std::memory_order_acquire)) break;
      if (peer.sin_family != AF_INET || peer.sin_addr.s_addr != htonl(INADDR_LOOPBACK) ||
          !prepare_socket(connection.value)) continue;
      try {
        serve_connection(connection.value, state->root, state->host, state->prefix, state->stopping);
      } catch (...) {
        // Fail closed without echoing paths, exception text, or the URL prefix.
        // The connection RAII closes it even after allocation/filesystem errors.
      }
    }
  } catch (...) {
    // Nothing may escape a detached thread entry point.
  }
  state->stopping.store(true, std::memory_order_release);
  state->release();
}

}  // namespace soui::desktop::detail
