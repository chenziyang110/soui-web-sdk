#pragma once
#include <soui/runtime/agent/windows_local_endpoint.h>
#include <json.hpp>
#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace public_endpoint_tests {
using json = nlohmann::json;
using soui::runtime::bridge_transport;
inline void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
// The unchanged finite native client body is copied from the existing public
// endpoint fixture. It uses actual framed Windows I/O and held completion.
[[noreturn]] void native_pending_failure() noexcept {
  // A test failure cannot free a buffer/endpoint still borrowed by native I/O.
  ::TerminateProcess(::GetCurrentProcess(), 91); ::ExitProcess(91);
}
class handle final {
 public:
  explicit handle(HANDLE value = nullptr) noexcept : value_(value) {}
  ~handle() { if (*this) ::CloseHandle(value_); }
  handle(const handle&) = delete;
  handle& operator=(const handle&) = delete;
  HANDLE get() const noexcept { return value_; }
  explicit operator bool() const noexcept { return value_ && value_ != INVALID_HANDLE_VALUE; }
 private: HANDLE value_;
};
std::wstring pipe_name(const std::string& component) {
  return L"\\\\.\\pipe\\" + std::wstring(component.begin(), component.end());
}
std::string component() {
  static std::atomic<unsigned> serial{0};
  return "soui-endpoint-test-" + std::to_string(::GetCurrentProcessId()) + "-" +
      std::to_string(::GetTickCount64()) + "-" + std::to_string(serial.fetch_add(1));
}
class client final {
 public:
  explicit client(const std::string& component)
      : pipe_(open(pipe_name(component))), event_(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
    if (!event_) {
      const DWORD error = ::GetLastError();
      std::cerr << "Client CreateEventW failed: error=" << error << '\n';
    }
    require(pipe_ && event_, "Client pipe/event creation failed");
  }
  bool write_raw(std::string body) {
    const auto length = static_cast<std::uint32_t>(body.size());
    std::array<unsigned char, 4> header{{static_cast<unsigned char>(length),
        static_cast<unsigned char>(length >> 8U), static_cast<unsigned char>(length >> 16U),
        static_cast<unsigned char>(length >> 24U)}};
    return bytes(header.data(), header.size(), true) && bytes(body.data(), body.size(), true);
  }
  bool write_length(std::uint32_t length) {
    std::array<unsigned char, 4> header{{static_cast<unsigned char>(length),
        static_cast<unsigned char>(length >> 8U), static_cast<unsigned char>(length >> 16U),
        static_cast<unsigned char>(length >> 24U)}};
    return bytes(header.data(), header.size(), true);
  }
  bool write_fragmented(std::string body) {
    const auto length = static_cast<std::uint32_t>(body.size());
    std::array<unsigned char, 4> header{{static_cast<unsigned char>(length),
        static_cast<unsigned char>(length >> 8U), static_cast<unsigned char>(length >> 16U),
        static_cast<unsigned char>(length >> 24U)}};
    for (auto& byte : header) if (!bytes(&byte, 1, true)) return false;
    for (auto& byte : body) if (!bytes(&byte, 1, true)) return false;
    return true;
  }
  std::string read_raw() {
    std::array<unsigned char, 4> header{};
    if (!bytes(header.data(), header.size(), false)) return {};
    const auto length = static_cast<std::uint32_t>(header[0]) |
        (static_cast<std::uint32_t>(header[1]) << 8U) | (static_cast<std::uint32_t>(header[2]) << 16U) |
        (static_cast<std::uint32_t>(header[3]) << 24U);
    require(length && length <= bridge_transport::max_message_bytes, "Unbounded server frame");
    std::string body(length, '\0');
    return bytes(body.data(), body.size(), false) ? body : std::string{};
  }
  void hello() {
    require(write_raw(R"({"kind":"hello","schemaVersion":"soui.local/1"})"), "Handshake write failed");
    const auto raw = read_raw();
    require(!raw.empty() && json::parse(raw) == json{{"kind", "hello"},
        {"schemaVersion", "soui.local/1"}, {"ok", true}}, "Explicit native authorization failed");
  }
  json call(const char* method, json params = {{"n", 2}}) {
    require(write_raw(json{{"kind", "request"}, {"schemaVersion", "soui.bridge/1"},
        {"requestId", "request:one"}, {"method", method}, {"params", std::move(params)}}.dump()), "Request write failed");
    const auto raw = read_raw(); require(!raw.empty(), "Expected server response missing");
    const auto response = json::parse(raw);
    require(response.at("requestId") == "request:one" && !response.contains("token") &&
        !response.contains("sessionId") && !response.contains("origin") && !response.contains("subject") &&
        !response.contains("bearer"), "Response disclosed native authority/context");
    return response;
  }
 private:
  static HANDLE open(const std::wstring& name) {
    const auto deadline = ::GetTickCount64() + 4000;
    DWORD last_error = ERROR_SUCCESS;
    do {
      HANDLE value = ::CreateFileW(name.c_str(), FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE,
          0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IMPERSONATION, nullptr);
      if (value != INVALID_HANDLE_VALUE) return value;
      const DWORD error = ::GetLastError();
      last_error = error;
      if (error != ERROR_PIPE_BUSY && error != ERROR_FILE_NOT_FOUND) break;
      (void)::WaitNamedPipeW(name.c_str(), 10);
      ::Sleep(1);
    } while (::GetTickCount64() < deadline);
    std::cerr << "Client CreateFileW failed: error=" << last_error
        << " desiredAccess=0x00100003" << '\n';
    return INVALID_HANDLE_VALUE;
  }
  bool transfer(void* buffer, DWORD length, DWORD& transferred, bool write) {
    require(::ResetEvent(event_.get()) != FALSE, "Client event reset failed");
    OVERLAPPED operation{}; operation.hEvent = event_.get();
    const BOOL immediate = write ? ::WriteFile(pipe_.get(), buffer, length, &transferred, &operation) :
        ::ReadFile(pipe_.get(), buffer, length, &transferred, &operation);
    if (immediate) {
      if (::GetOverlappedResult(pipe_.get(), &operation, &transferred, FALSE)) return true;
      const DWORD completion_error = ::GetLastError();
      if (completion_error != ERROR_IO_INCOMPLETE && completion_error != ERROR_IO_PENDING) return false;
    } else if (::GetLastError() != ERROR_IO_PENDING) return false;
    const DWORD waited = ::WaitForSingleObject(event_.get(), 4000);
    if (waited != WAIT_OBJECT_0) {
      (void)::CancelIoEx(pipe_.get(), &operation);
      // Wait for real completion before operation/buffer storage can unwind.
      if (::WaitForSingleObject(event_.get(), 2000) != WAIT_OBJECT_0) native_pending_failure();
      DWORD ignored = 0;
      if (!::GetOverlappedResult(pipe_.get(), &operation, &ignored, FALSE)) {
        const DWORD error = ::GetLastError();
        if (error == ERROR_IO_INCOMPLETE || error == ERROR_IO_PENDING) native_pending_failure();
      }
      return false;
    }
    return ::GetOverlappedResult(pipe_.get(), &operation, &transferred, FALSE) != FALSE;
  }
  bool bytes(void* storage, std::size_t length, bool write) {
    auto* data = static_cast<unsigned char*>(storage);
    for (std::size_t offset = 0; offset < length;) {
      DWORD count = 0;
      if (!transfer(data + offset, static_cast<DWORD>(length - offset), count, write) ||
          !count || count > length - offset) return false;
      offset += count;
    }
    return true;
  }
  handle pipe_, event_;
};
}  // namespace public_endpoint_tests
