#pragma once
#include <windows.h>
#include <filesystem>
#include <chrono>
#include <memory>
#include <string>
#include <soui/diagnostics/input_frame_trace.h>

namespace soui::benchmark {
struct scalar_boundary final {
  sdk::diagnostics::input_frame_trace::identity expected;
  sdk::diagnostics::input_frame_trace::scalar_snapshot sdk, kernel;
  std::uint64_t capture_begin_qpc{}, capture_end_qpc{};
  bool kernel_present{false};
};
struct scalar_export_result final {
  bool io_complete{false}, usable{false}, sdk_usable{false}, kernel_usable{false}, ok{false};
};
scalar_boundary capture_scalar_boundary(
    sdk::diagnostics::input_frame_trace::identity expected,
    sdk::diagnostics::input_frame_trace::trace_source& sdk_source,
    sdk::diagnostics::input_frame_trace::trace_source* kernel_source);
void write_scalar_initial(const std::filesystem::path& output,const scalar_boundary& boundary,
    const std::string& cold_profile_json);
// No worker or event-mode attachment. Call only after normal source shutdown.
scalar_export_result finish_scalar_capture(const std::filesystem::path& output,
    const scalar_boundary& initial,sdk::diagnostics::input_frame_trace::trace_source& sdk_source,
    sdk::diagnostics::input_frame_trace::trace_source* kernel_source,std::chrono::milliseconds remaining);
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
