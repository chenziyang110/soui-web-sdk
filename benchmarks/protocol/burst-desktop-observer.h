#pragma once
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace soui::benchmark {
// Independent burst protocol. Borrows held target handle through finish();
// never waits for a per-input ACK. The legacy observer is unchanged.
class burst_desktop_observer final {
 public:
  burst_desktop_observer(HANDLE process, DWORD pid, std::uint64_t creation,
      HWND window, const std::filesystem::path& output,
      const std::string& run_id, std::uint32_t expected_nonce);
  ~burst_desktop_observer();
  burst_desktop_observer(const burst_desktop_observer&) = delete;
  burst_desktop_observer& operator=(const burst_desktop_observer&) = delete;
  void check();
  void wait_final_ack(std::uint32_t sequence);
  void finish();
 private:
  struct state;
  std::unique_ptr<state> state_;
};
}
