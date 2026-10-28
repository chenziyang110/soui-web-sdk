#pragma once
#include <windows.h>
#include <filesystem>
#include <chrono>
#include <memory>
#include <string>
#include <soui/diagnostics/input_frame_trace.h>

namespace soui::benchmark {
// Owns only the nonengine exporter thread/files. Both sources outlive finish().
class trace_capture final {
 public:
  trace_capture(const std::filesystem::path& output,
      sdk::diagnostics::input_frame_trace::trace_source& sdk_source,
      sdk::diagnostics::input_frame_trace::trace_source& kernel_source);
  ~trace_capture();
  trace_capture(const trace_capture&) = delete;
  trace_capture& operator=(const trace_capture&) = delete;
  HANDLE failure_event() const noexcept;
  std::string failure_message() const;
  // Call after product producers stop/release. Never closes a live source.
  bool finish(std::chrono::milliseconds budget = std::chrono::seconds(3)) noexcept;
 private:
  struct state;
  std::unique_ptr<state> state_;
};
}
