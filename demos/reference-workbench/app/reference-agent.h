#pragma once
#include <soui/json_bridge.h>
#include <soui/runtime/agent/windows_local_endpoint.h>
#include <Windows.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace reference {
struct agent_options final {
  bool enabled{false};
  std::string pipe_component;
  DWORD approved_process{0}; // Trusted operator selection; never a wire claim.
};
struct command_line final { bool valid{false}, smoke{false}; agent_options agent; };
[[nodiscard]] command_line parse_command_line(int count, wchar_t* const* arguments) noexcept;

// App-only decorator. Host borrows this object; it and its held process survive
// Host destruction and the real Bridge task join. Never owns/deletes endpoint.
class agent final : public soui::sdk::bridge_adapter {
 public:
  agent(soui::sdk::json_bridge& bridge, agent_options options);
  ~agent() override;
  agent(const agent&) = delete;
  agent& operator=(const agent&) = delete;
  agent(agent&&) = delete;
  agent& operator=(agent&&) = delete;

  // Owner supplies a trusted current-document probe, not a page ready claim.
  // Main binds actual adapter receipt/digest AND application_ready. The finite
  // pipe fixture binds only its own native window; that is not a GUI proof.
  [[nodiscard]] bool start_bound_document(std::function<bool()> current);
  void pump_owner() noexcept;
  void request_close() noexcept; // Foreign thread: atomic close/stop/event only.
  [[nodiscard]] soui::runtime::agent::local_endpoint_stop stop_owner() noexcept;
  [[nodiscard]] bool enabled() const noexcept;
  [[nodiscard]] bool started() const noexcept; // owner only
  [[nodiscard]] bool closed() const noexcept;
  [[nodiscard]] DWORD selected_process_id() const noexcept;
  [[nodiscard]] std::uint64_t selected_creation_filetime() const noexcept;

  std::string handle_json(std::string_view request, bool gesture = false) override;
  void shutdown_session(const std::string& session) override;
  void set_owner_window(void* window) noexcept override;
  void set_event_sink(event_sink sink) override;
 private:
  struct impl;
  std::unique_ptr<impl> data_;
};
} // namespace reference
