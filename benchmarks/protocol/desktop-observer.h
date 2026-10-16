#pragma once
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace soui::benchmark {
// A measurement-only thread. It borrows the held host process until finish().
// No HWND/SDK callback is executed on this thread.
class desktop_observer final {
 public:
  desktop_observer(HANDLE process, DWORD pid, std::uint64_t creation, HWND window,
                   const std::filesystem::path& output, unsigned scene, bool probe);
  ~desktop_observer();
  desktop_observer(const desktop_observer&) = delete;
  desktop_observer& operator=(const desktop_observer&) = delete;
  void check();
  std::uint32_t next_event();
  void wait_ack(std::uint32_t event);
  void finish();
 private:
  struct state;
  std::unique_ptr<state> state_;
};
} // namespace soui::benchmark
