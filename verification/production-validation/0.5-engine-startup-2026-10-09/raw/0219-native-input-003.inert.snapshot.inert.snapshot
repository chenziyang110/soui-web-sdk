#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <soui/runtime/services/filesystem-readonly/directory_scanner.h>

namespace soui::sdk {

// Application extension point. The application must outlive its host and
// synchronize state accessed by the engine, bridge worker and UI threads.
// JSON envelopes and permission checks remain the adapter's responsibility.
class bridge_adapter {
 public:
  using event_sink = std::function<void(std::string_view)>;
  virtual ~bridge_adapter() = default;
  virtual std::string handle_json(std::string_view request, bool native_user_gesture = false) = 0;
  virtual void shutdown_session(const std::string& session_id) = 0;
  virtual void set_owner_window(void*) noexcept {}
  virtual void set_event_sink(event_sink) {}
  virtual void record_paint() noexcept {}
  virtual void set_present_count(std::uint64_t) noexcept {}
  virtual void set_draw_interval(unsigned) noexcept {}
  virtual void set_gpu_active(bool) noexcept {}
  virtual unsigned active_frame_rate() const noexcept { return 60; }
  // Optional diagnostic fixture support; production adapters may omit it.
  virtual std::string register_root(const soui::runtime::scan_request_context&,
                                    const std::filesystem::path&) { return {}; }
};
}  // namespace soui::sdk
