#pragma once

#include <filesystem>
#include <soui/bridge_adapter.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include <soui/runtime/host/session/webview_session.h>
#include <soui/host/async_result_slot.h>
#include <soui/host/engine_thread.h>
#include <soui/host/interaction_protocol.h>
#include <soui/host/window_chrome.h>
#ifdef SOUI_TEST_KIT
#include "../../test-kit/native/pipe_agent.h"
#endif

namespace soui::sdk {

namespace detail { class bridge_request_budget; }

inline constexpr std::uint32_t bridge_event_message = 0x84D2U;
inline constexpr std::uint32_t ime_position_message = 0x84D3U;

struct host_config {
  std::string architecture;
  std::filesystem::path miniblink_binary;
  std::filesystem::path resource_root;
  void* owner_window{nullptr};
  soui::sdk::bridge_adapter* bridge_adapter{nullptr};
  bool self_test{false};
  bool managed_frame_timing{true};
#ifdef SOUI_BRIDGE_BENCHMARK
  std::filesystem::path benchmark_dataset;
  std::filesystem::path benchmark_output_directory;
  unsigned engine_timing_experiment{0}; // bit 1: wake=0; bit 2: draw=0; diagnostics only
  bool noopt_experiment{false};
#endif
};

struct host_result {
  bool ok{false};
  std::string code;
  std::string message;
};

// Final application exit, after every Host has been destroyed. Window teardown
// keeps the process runtime available for subsequent windows. Shutdown is final:
// this Miniblink version cannot initialize again in the same process.
[[nodiscard]] host_result shutdown_miniblink_runtime();

[[nodiscard]] bool binary_matches_architecture(const std::filesystem::path& binary,
                                                std::string_view architecture);

class webview_host_adapter {
 public:
  virtual ~webview_host_adapter() = default;
  [[nodiscard]] virtual host_result mount(const host_config& config) = 0;
  [[nodiscard]] virtual host_result unmount() = 0;
};

class dynamic_miniblink_adapter final : public webview_host_adapter,
                                        private independent_input_sink,
                                        private engine_input_sink {
 public:
  ~dynamic_miniblink_adapter() override;
  [[nodiscard]] host_result mount(const host_config& config) override;
  [[nodiscard]] host_result unmount() override;
  [[nodiscard]] host_result show(bool visible);
  [[nodiscard]] host_result reload();
  [[nodiscard]] host_result resize(int width, int height);
  [[nodiscard]] host_result focus();
  void pump();
  void deliver_posted_event(void* payload);
  [[nodiscard]] bool document_ready() const noexcept;
  [[nodiscard]] bool application_ready();
  [[nodiscard]] std::string application_diagnostics();
#ifdef SOUI_PERFORMANCE_DIAGNOSTICS
  [[nodiscard]] host_result request_memory_recovery();
#endif
  void set_session_id(std::string session_id);
  void notify_document_ready();
  void notify_main_context_created();
  [[nodiscard]] bool allow_document_navigation(std::string_view url) const noexcept;
  [[nodiscard]] std::string handle_message(std::string_view request, bool native_user_gesture);
  [[nodiscard]] bool enqueue_bridge_message(std::string request,
                                            bool native_user_gesture);
  [[nodiscard]] host_result try_enqueue_bridge_message(std::string request,
                                                        bool native_user_gesture);
  [[nodiscard]] bool processing_user_gesture() const noexcept;
  [[nodiscard]] bool owns_execution_context(void* execution_state) const noexcept;
  [[nodiscard]] bool update_window_chrome(void* execution_state, std::string_view json);
  void dispatch_event_now(const std::string& event_json);
  [[nodiscard]] bool handle_native_window_message(unsigned int message,
                                                  std::uintptr_t wparam,
                                                  std::intptr_t lparam,
                                                  std::intptr_t& result);
#ifdef SOUI_BRIDGE_BENCHMARK
  [[nodiscard]] std::string benchmark_result();
  void benchmark_motion(unsigned index);
  double benchmark_click(int x, int y);
  void benchmark_text();
  void benchmark_gallery(bool all_components);
  [[nodiscard]] int configured_wake_interval() const noexcept { return configured_wake_interval_; }
#endif

 private:
  [[nodiscard]] host_result mount_on_engine(const host_config& config, int width, int height);
  void unmount_on_engine();
  void pump_on_engine();
  [[nodiscard]] bool create_input_window();
  void destroy_input_window();
  void refresh_wheel_settings() noexcept;
  void dispatch_input_on_engine(input_packet packet);
  [[nodiscard]] bool submit_wheel(const input_packet& packet,
                                  scroll_sample& sample) override;
  [[nodiscard]] bool capture_scroll_barrier(
      const input_packet& packet, scroll_sample& sample) override;
  [[nodiscard]] bool post_input(const input_packet& packet) override;
  [[nodiscard]] input_packet make_input_packet(input_kind kind) noexcept;
  void resolve_bridge_response(std::string request_id, std::string response,
                               std::uint64_t generation);
  void on_document_ready();
  void install_js_bindings();
  void dispatch_event(const std::string& event_json);
#ifdef SOUI_TEST_KIT
  [[nodiscard]] soui::test_kit::json test_kit_request(const soui::test_kit::json& request);
  std::unique_ptr<soui::test_kit::pipe_agent> test_agent_;
#endif

  void* module_{nullptr};
  void* webview_{nullptr};
  void* webview_window_{nullptr};
  void* owner_window_{nullptr};
  soui::sdk::bridge_adapter* bridge_adapter_{nullptr};
  struct event_dispatch_lifetime;
  std::shared_ptr<event_dispatch_lifetime> event_lifetime_;
  engine_thread* engine_thread_{nullptr}; // borrowed from the process runtime
  std::unique_ptr<engine_thread> bridge_thread_;
  std::shared_ptr<detail::bridge_request_budget> bridge_budget_;
  std::atomic<bool> bridge_accepting_{false};
  bool bridge_com_initialized_{false};
  std::atomic<std::uint64_t> bridge_generation_{0};
  interaction_router interaction_router_;
  const void* interaction_api_{nullptr};
  void* interaction_endpoint_{nullptr};
  interaction_generation interaction_generation_;
  std::atomic<std::uint64_t> next_input_sequence_{0};
  wheel_smoothing_policy wheel_smoothing_;
  std::atomic<std::uint32_t> wheel_scroll_lines_{3};
  std::atomic<std::uint32_t> wheel_scroll_characters_{1};
  std::atomic<bool> pump_pending_{false};
  bool ime_composing_{false};
  window_chrome window_chrome_;
  chrome_action pressed_chrome_{chrome_action::none}; // UI-thread only
  bool ime_committed_{false};
  std::atomic<int> ime_caret_x_{0};
  std::atomic<int> ime_caret_y_{0};
  std::filesystem::path resource_root_;
  std::string session_id_;
  std::atomic<bool> document_ready_{false};
  std::atomic<bool> application_ready_{false};
  mutable std::mutex state_mutex_;
  std::string probe_report_;
  bool managed_frame_timing_{true};
  bool host_managed_pump_{false};
  bool experimental_other_thread_paint_{false};
  bool experimental_compositor_rate_{false};
  std::atomic<int> configured_wake_interval_{-1};
  std::atomic<int> configured_draw_min_interval_{-1};
  std::atomic<int> configured_main_frame_rate_{-1};
  bool experimental_main_frame_scheduler_{false};
  bool gpu_trace_active_{false};
  std::filesystem::path gpu_trace_path_;
#ifdef SOUI_BRIDGE_BENCHMARK
  [[nodiscard]] std::string collect_benchmark_result_on_engine();
  std::filesystem::path benchmark_dataset_;
  async_result_slot benchmark_result_slot_;
  int benchmark_x_{0}, benchmark_y_{0}, benchmark_width_{0}, benchmark_height_{0};
#endif
};

class soui_miniblink_host {
 public:
  explicit soui_miniblink_host(std::unique_ptr<webview_host_adapter> adapter);
  soui_miniblink_host(const soui_miniblink_host&) = delete;
  soui_miniblink_host& operator=(const soui_miniblink_host&) = delete;
  ~soui_miniblink_host();

  [[nodiscard]] host_result start(const host_config& config);
  [[nodiscard]] host_result hide();
  [[nodiscard]] host_result reload();
  [[nodiscard]] host_result close();
  [[nodiscard]] host_result destroy();
  [[nodiscard]] host_result resize(int width, int height);
  [[nodiscard]] host_result focus();
  void pump();
  void deliver_posted_event(void* payload);
  [[nodiscard]] bool document_ready() const noexcept;
  [[nodiscard]] bool application_ready();
  [[nodiscard]] std::string application_diagnostics();
#ifdef SOUI_PERFORMANCE_DIAGNOSTICS
  [[nodiscard]] host_result request_memory_recovery();
#endif
  [[nodiscard]] const soui::runtime::webview_session* session() const noexcept;
#ifdef SOUI_BRIDGE_BENCHMARK
  [[nodiscard]] std::string benchmark_result();
  void benchmark_gallery(bool all_components);
  [[nodiscard]] int configured_wake_interval() const noexcept;
#endif

 private:
  [[nodiscard]] static host_result validate_local_resources(const std::filesystem::path& root);
  [[nodiscard]] static host_result validate_architecture(const host_config& config);

  std::unique_ptr<webview_host_adapter> adapter_;
  std::unique_ptr<soui::runtime::webview_session> session_;
};

}  // namespace soui::sdk
