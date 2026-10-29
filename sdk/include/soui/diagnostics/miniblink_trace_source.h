#pragma once

#include <soui/diagnostics/input_frame_trace.h>

namespace soui::sdk::diagnostics::input_frame_trace {

enum class backend_status : std::uint32_t {
  ready, module_unavailable, api_unavailable, incompatible, configure_rejected, no_memory,
};

// Consumer-side adapter only. The DLL reference outlives every call and export;
// no SDK/C++ object or stream is passed into the kernel. Attach/load are cold.
class miniblink_trace_source final : public trace_source {
 public:
  static std::unique_ptr<miniblink_trace_source> load(const wchar_t* absolute_dll_path,
      const identity& session, mode requested, backend_status& result) noexcept;
  static std::unique_ptr<miniblink_trace_source> attach(void* module,
      backend_status& result) noexcept;
  ~miniblink_trace_source() override;
  miniblink_trace_source(const miniblink_trace_source&) = delete;
  miniblink_trace_source& operator=(const miniblink_trace_source&) = delete;
  bool stop() noexcept;
  scalar_snapshot snapshot() const noexcept override;
  drain_result drain(event* output, std::size_t capacity) noexcept override;
  void note_causal_loss(std::uint64_t amount = 1) noexcept override;

 private:
  struct implementation;
  explicit miniblink_trace_source(std::unique_ptr<implementation> value) noexcept;
  std::unique_ptr<implementation> implementation_;
};

} // namespace soui::sdk::diagnostics::input_frame_trace
