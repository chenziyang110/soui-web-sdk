#pragma once

#include <filesystem>
#include <soui/bridge_adapter.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include <soui/runtime/host/session/webview_session.h>
#include <soui/host/webview_host.h>
#include <soui/host/async_result_slot.h>
#include <soui/host/engine_thread.h>
#include <soui/host/interaction_protocol.h>
#include <soui/host/window_chrome.h>
#include <soui/diagnostics/cache_observation.h>
#ifdef SOUI_TEST_KIT
#include "../../test-kit/native/pipe_agent.h"
#endif

namespace soui::sdk {

namespace detail { class bridge_request_budget; class cursor_broker_endpoint; class trace_thread_registration; }

inline constexpr std::uint32_t bridge_event_message = 0x84D2U;
inline constexpr std::uint32_t ime_position_message = 0x84D3U;

// Final application exit, after every Host has been destroyed. Window teardown
// keeps the process runtime available for subsequent windows. Shutdown is final:
// this Miniblink version cannot initialize again in the same process.
// Poll from outside the engine owner after every Host is destroyed. Pending
// keeps the engine pumping; retry within the caller's finite close deadline.
// Failed/Unsupported retain the runtime/module and must not be treated as closed.
[[nodiscard]] host_result shutdown_miniblink_runtime();

// Application convenience for the same final shutdown, on the caller's UI
// thread after every Host is destroyed. Pumps bounded Windows message batches,
// retries only pending, and preserves WM_QUIT for the outer loop. Nonnegative
// timeout is a total monotonic budget; zero makes one non-blocking attempt.
// Timeout/failure retains the runtime and is never evidence of completed exit.
[[nodiscard]] host_result wait_for_miniblink_runtime_shutdown(std::chrono::milliseconds timeout);

// Native-only, non-blocking observation. No pump, work posting or kernel
// initialization. Disabled/busy/not_started are incomplete data, not zeros.
[[nodiscard]] diagnostics::status read_miniblink_process_runtime_metrics(
    diagnostics::process_engine_runtime&) noexcept;
// Optional, read-only observations from the already loaded provider DLL.
// Existing owner metrics DTO layout and queue getter remain unchanged.
[[nodiscard]] diagnostics::renderer_status read_miniblink_renderer_metrics(
    diagnostics::renderer_observation&) noexcept;
[[nodiscard]] diagnostics::renderer_status read_miniblink_renderer_model_metrics(
    diagnostics::renderer_model_observation&) noexcept;
// Explicit, single-flight asynchronous Engine ordinary-lane request. No wait.
[[nodiscard]] diagnostics::cache_status request_miniblink_cache_sample() noexcept;
// Pure completed-copy getter: no posting, pump, initialization or DLL invocation.
[[nodiscard]] diagnostics::cache_status read_miniblink_cache_metrics(
    diagnostics::cache_observation&) noexcept;

[[nodiscard]] bool binary_matches_architecture(const std::filesystem::path& binary,
                                                std::string_view architecture);

class dynamic_miniblink_adapter final : public webview_host_adapter,
                                        private independent_input_sink,
                                        private engine_input_sink {
 public:
  // Lifecycle runs on the HWND owner. A close requested from an active native
  // modal callback can return E_CURSOR_CLOSE_PENDING; keep this adapter and
  // both HWNDs alive until the callback returns and unmount completes. Do not
  // destroy an adapter from its own active member/native callback stack.
  dynamic_miniblink_adapter();
  // Diagnostics mode is immutable; construct on the retained lifecycle owner.
  explicit dynamic_miniblink_adapter(diagnostics::mode metrics);
  [[nodiscard]] diagnostics::status read_runtime_metrics(diagnostics::software_host_runtime&) const noexcept;
  ~dynamic_miniblink_adapter() override;
  [[nodiscard]] host_result mount(const host_config& config) override;
  [[nodiscard]] host_result unmount() override;
  [[nodiscard]] host_result show(bool visible) override;
  [[nodiscard]] host_result reload() override;
  [[nodiscard]] host_result resize(int width, int height) override;
  [[nodiscard]] host_result focus() override;
  void pump() override;
  void deliver_posted_event(void* payload) override;
  [[nodiscard]] bool document_ready() const noexcept override;
  [[nodiscard]] bool application_ready() override;
  [[nodiscard]] std::string application_diagnostics() override;
#ifdef SOUI_PERFORMANCE_DIAGNOSTICS
  [[nodiscard]] host_result request_memory_recovery();
#endif
  void set_session_id(std::string session_id) override;
  void notify_document_ready();
  [[nodiscard]] bool notify_main_context_created();
  [[nodiscard]] bool notify_main_context_created(std::uint64_t& completed_receipt);
  [[nodiscard]] bool notify_main_context_created(void* originating_view,
                                                 std::uint64_t& completed_receipt);
  // Read-only document receipts do not grant native permissions. Native
  // callers retain the receipt across opaque getters and asynchronous work.
  [[nodiscard]] std::uint64_t execution_context_receipt() const noexcept;
  [[nodiscard]] bool accepts_execution_context_receipt(std::uint64_t receipt) const noexcept;
  [[nodiscard]] bool allow_document_navigation(std::string_view url) const noexcept;
  [[nodiscard]] std::string handle_message(std::string_view request, bool native_user_gesture);
  [[nodiscard]] std::string handle_message(std::string_view request, bool native_user_gesture,
                                          std::uint64_t context_receipt);
  [[nodiscard]] bool enqueue_bridge_message(std::string request,
                                            bool native_user_gesture);
  [[nodiscard]] host_result try_enqueue_bridge_message(std::string request,
                                                        bool native_user_gesture);
  [[nodiscard]] host_result try_enqueue_bridge_message(std::string request,
      bool native_user_gesture, std::uint64_t context_receipt);
  [[nodiscard]] bool processing_user_gesture() const noexcept;
  [[nodiscard]] bool owns_execution_context(void* execution_state) const noexcept;
  [[nodiscard]] bool owns_execution_context(void* execution_state,
                                             std::uint64_t context_receipt) const noexcept;
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
  friend class detail::engine_admission;
  [[nodiscard]] host_result mount_on_engine(const host_config& config, int width, int height);
  [[nodiscard]] host_result finish_mount_on_engine(const host_config& config, int width, int height);
  [[nodiscard]] host_result wait_for_cursor_registration();
  [[nodiscard]] bool startup_window_matches() const noexcept;
  void unmount_on_engine();
  [[nodiscard]] host_result close_cursor_endpoint();
  [[nodiscard]] bool advance_cursor_document_on_engine();
  [[nodiscard]] bool update_cursor_identity_on_engine();
  void publish_view_trace_identity_on_engine() noexcept;
  void sync_cursor_input_generation() noexcept;
  void pump_on_engine();
  [[nodiscard]] bool create_input_window();
  [[nodiscard]] bool destroy_input_window();
  void refresh_wheel_settings() noexcept;
  void dispatch_input_on_engine(input_packet packet);
  void cancel_input_on_engine();
  [[nodiscard]] bool submit_wheel(const input_packet& packet,
                                  scroll_sample& sample) override;
  [[nodiscard]] bool capture_scroll_barrier(
      const input_packet& packet, scroll_sample& sample) override;
  [[nodiscard]] bool post_input(const input_packet& packet) override;
  [[nodiscard]] input_admission_result post_input_typed(const input_packet& packet) override;
  [[nodiscard]] input_packet make_input_packet(input_kind kind) noexcept;
  void resolve_bridge_response(std::string request_id, std::string response,
                               std::uint64_t generation, std::uint64_t context_receipt);
  void on_document_ready();
  [[nodiscard]] bool install_js_bindings(std::uint64_t context_receipt);
  void dispatch_event(const std::string& event_json);
  void dispatch_event(const std::string& event_json, std::uint64_t context_receipt);
  void dispatch_bridge_event(const std::string& event_json, std::uint64_t context_receipt);
  [[nodiscard]] bool install_bridge_event_sink(std::uint64_t context_receipt);
  void dispatch_event_now(const std::string& event_json, std::uint64_t context_receipt);
  [[nodiscard]] std::uint64_t invalidate_main_context() noexcept;
#ifdef SOUI_TEST_KIT
  [[nodiscard]] soui::test_kit::json test_kit_request(const soui::test_kit::json& request);
  std::unique_ptr<soui::test_kit::pipe_agent> test_agent_;
#endif

  const diagnostics::mode metrics_mode_;
  const std::thread::id metrics_owner_thread_{std::this_thread::get_id()};
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
  std::shared_ptr<detail::engine_input_service> input_mailbox_;
  std::unique_ptr<detail::trace_thread_registration> input_trace_owner_;
  std::array<bool, 256> input_keys_on_engine_{};
  bool input_pointer_on_engine_{false};
  int input_pointer_x_{0}, input_pointer_y_{0};
  std::atomic<bool> input_focus_requested_{false};
  std::atomic<bool> input_recovery_limited_{false};
  std::uint32_t input_injection_tag_{0}; // UI diagnostic context; never authorization.
  std::atomic<std::uint64_t> input_trace_view_id_{0};
  bool trace_document_confirmed_{false}, trace_geometry_applied_{false}; // Engine owner, actual callbacks/resize only.
  const void* interaction_api_{nullptr};
  void* interaction_endpoint_{nullptr};
  const void* cursor_api_{nullptr};
  std::unique_ptr<detail::cursor_broker_endpoint> cursor_broker_;
  std::atomic<bool> cursor_transition_pending_{false};
  // Odd = unique pending context, even = completed; zero is legacy/inactive.
  // Only the corresponding main-context callback can complete its receipt.
  std::atomic<std::uint64_t> cursor_context_receipt_{0};
  bool startup_window_protected_{false}; // UI owner; retained after failed mount until actual cleanup.
  std::atomic<bool> startup_cancelled_{false};
  bool startup_dispatch_refused_{false}; // UI owner; permanently retains a rejected timer attempt.
  struct startup_refused_message final {
    void* hwnd{nullptr};
    unsigned message{0};
    std::uintptr_t wparam{0};
    std::intptr_t lparam{0};
    std::uint32_t time{0};
    long x{0}, y{0};
    bool retained{false};
  } startup_refused_message_; // One actual removed MSG, never dispatched or reposted.
  bool unmount_running_{false}; // UI owner only
  bool cursor_close_pending_{false};
  bool cursor_vendor_destroyed_{false};
  interaction_generation interaction_generation_;
  interaction_generation engine_input_generation_; // Engine owner: actually applied context/resize.
  std::uint64_t engine_requested_document_generation_{1}; // Captured by the actual owner reload; confirmed at context creation.
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
  std::uint64_t session_context_receipt_{0}; // protected by state_mutex_
  std::atomic<bool> document_ready_{false};
  std::atomic<bool> application_ready_{false};
  std::atomic<std::uint64_t> document_ready_receipt_{0};
  std::atomic<std::uint64_t> application_ready_receipt_{0};
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

}  // namespace soui::sdk
