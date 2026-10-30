#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include <soui/bridge_adapter.h>
#include <soui/devtools.h>
#include <soui/runtime/host/session/webview_session.h>

namespace soui::sdk {

struct host_config {
  std::string architecture;
  // Retained for existing Miniblink consumers; other adapters may ignore it.
  std::filesystem::path miniblink_binary;
  std::filesystem::path resource_root;
  void* owner_window{nullptr};
  soui::sdk::bridge_adapter* bridge_adapter{nullptr}; // borrowed for the mounted lifetime
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

// Native window-local request only. Forward to the same application policy as
// the menu; WPARAM is the virtual key, LPARAM bits 0/1/2 are Ctrl/Shift/Alt.
inline constexpr std::uint32_t devtools_request_message = 0x84D5U;

// Implementations own their renderer resources. A failed mount can still own
// partial resources; unmount must be idempotent and allow a retry on failure.
class webview_host_adapter {
 public:
  virtual ~webview_host_adapter() = default;
  [[nodiscard]] virtual host_result mount(const host_config& config) = 0;
  [[nodiscard]] virtual host_result unmount() = 0;
  [[nodiscard]] virtual host_result show(bool) {
    return {false, "E_NOT_SUPPORTED", "The renderer adapter does not support visibility changes."};
  }
  [[nodiscard]] virtual host_result reload() {
    return {false, "E_NOT_SUPPORTED", "The renderer adapter does not support reload."};
  }
  [[nodiscard]] virtual host_result resize(int, int) {
    return {false, "E_NOT_SUPPORTED", "The renderer adapter does not support resizing."};
  }
  [[nodiscard]] virtual host_result focus() {
    return {false, "E_NOT_SUPPORTED", "The renderer adapter does not support focus."};
  }
  virtual void pump() {}
  // Payloads must originate from this adapter. Once delivered, ownership is
  // transferred to the adapter, which must consume them exactly once, even
  // after unmount (discarding late events without touching a destroyed view).
  // Adapters that post payloads must override this hook; the default is only
  // for adapters that never produce posted events. Arbitrary pointers are invalid.
  virtual void deliver_posted_event(void*) {}
  [[nodiscard]] virtual bool document_ready() const noexcept { return false; }
  [[nodiscard]] virtual bool application_ready() { return false; }
  [[nodiscard]] virtual std::string application_diagnostics() { return {}; }
  // Optional notification after successful start/reload. Adapters with their
  // own document bootstrap must preserve any already-established session.
  virtual void set_session_id(std::string) {}

 protected:
  friend class soui_miniblink_host;
  [[nodiscard]] virtual host_result configure_devtools_native(const devtools::configuration& config) {
    return config.access == devtools::policy::disabled ? host_result{true, {}, {}}
        : host_result{false, "E_NOT_SUPPORTED", "The renderer does not support controlled DevTools."};
  }
  [[nodiscard]] virtual devtools::capabilities devtools_capabilities_native() const { return {}; }
  [[nodiscard]] virtual host_result prepare_devtools_grant(devtools::target_identity&) {
    return {false, "E_NOT_SUPPORTED", "The renderer does not support native DevTools grants."};
  }
  [[nodiscard]] virtual devtools::target_identity devtools_target_native() const { return {}; }
  [[nodiscard]] virtual host_result open_devtools_native() {
    return {false, "E_NOT_SUPPORTED", "The renderer does not support controlled DevTools."};
  }
  // Close admission immediately, then retry non-blocking until inspector
  // detach/release is complete. Target unmount follows in destroy_target mode.
  [[nodiscard]] virtual host_result revoke_devtools_native(devtools::revoke_reason) {
    return {false, "E_NOT_SUPPORTED", "The renderer does not support DevTools revocation."};
  }
  [[nodiscard]] virtual devtools::observation observe_devtools_native() const { return {}; }
  // After successful target unmount, prove any backend-specific final barrier.
  [[nodiscard]] virtual host_result finish_devtools_retirement() { return {true, {}, {}}; }
};

// Calls are serialized by the owning UI thread. This host owns its adapter;
// the bridge_adapter in host_config remains owned by the application.
// The historical class name is preserved for source compatibility.
class soui_miniblink_host {
 public:
  explicit soui_miniblink_host(std::unique_ptr<webview_host_adapter> adapter);
  soui_miniblink_host(const soui_miniblink_host&) = delete;
  soui_miniblink_host& operator=(const soui_miniblink_host&) = delete;
  ~soui_miniblink_host();

  [[nodiscard]] host_result start(const host_config& config);
  [[nodiscard]] host_result hide();
  [[nodiscard]] host_result show();
  [[nodiscard]] host_result reload();
  [[nodiscard]] host_result close();
  // Also cleans a failed mount with no published session. A failed result keeps
  // adapter ownership: retain this host and its window, then retry before exit.
  // Destroying a host whose resources cannot be released terminates the process.
  [[nodiscard]] host_result destroy();
  [[nodiscard]] host_result resize(int width, int height);
  [[nodiscard]] host_result focus();
  void pump();
  // Always returns adapter-owned payloads to that adapter, including late
  // events after close/destroy. The caller must not reuse or free the payload.
  void deliver_posted_event(void* payload);
  [[nodiscard]] bool document_ready() const noexcept;
  [[nodiscard]] bool application_ready();
  [[nodiscard]] std::string application_diagnostics();
  // Configure on the constructing UI owner before start (WebView2: before prepare).
  [[nodiscard]] host_result configure_devtools(const devtools::configuration& config);
  [[nodiscard]] devtools::capabilities devtools_capabilities() const;
  [[nodiscard]] host_result authorize_devtools(devtools::grant& output,
      std::chrono::milliseconds validity = std::chrono::milliseconds::zero());
  [[nodiscard]] devtools::operation_result open_devtools(const devtools::grant& grant);
  [[nodiscard]] devtools::operation_result revoke_devtools(
      devtools::revoke_reason reason = devtools::revoke_reason::application);
  [[nodiscard]] devtools::observation observe_devtools() const;
  // Bounded history of the last 32 operations; unknown/evicted IDs fail explicitly.
  [[nodiscard]] devtools::operation_result devtools_operation(std::uint64_t id) const;
  // Matching only: this never issues a grant. Call only for this owner's key event.
  [[nodiscard]] bool devtools_shortcut(unsigned virtual_key, bool control, bool shift,
      bool alt, bool repeated = false) const noexcept;
#ifdef SOUI_PERFORMANCE_DIAGNOSTICS
  // Miniblink-only diagnostic extension, not a portable renderer capability.
  [[nodiscard]] host_result request_memory_recovery();
#endif
  [[nodiscard]] const soui::runtime::webview_session* session() const noexcept;
#ifdef SOUI_BRIDGE_BENCHMARK
  // Miniblink-only benchmark extensions.
  [[nodiscard]] std::string benchmark_result();
  void benchmark_gallery(bool all_components);
  [[nodiscard]] int configured_wake_interval() const noexcept;
#endif

 private:
  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] host_result require_running() const;
  [[nodiscard]] static host_result validate_local_resources(const std::filesystem::path& root);
  [[nodiscard]] static host_result validate_architecture(const host_config& config);
  [[nodiscard]] host_result require_devtools_owner() const;
  [[nodiscard]] host_result retire_devtools_before_unmount();
  [[nodiscard]] host_result finish_devtools_after_unmount();
  void invalidate_devtools_grants() noexcept;
  [[nodiscard]] host_result reset_devtools_for_mount();
  [[nodiscard]] bool devtools_resources_retired() const;
  [[nodiscard]] bool devtools_preparation_owned() const;
  void pump_devtools();
  void disarm_devtools_expiry() noexcept;

  std::unique_ptr<webview_host_adapter> adapter_;
  std::unique_ptr<soui::runtime::webview_session> session_;
  bool lifecycle_operation_{false};
  bool adapter_cleanup_pending_{false};
  const std::thread::id devtools_owner_{std::this_thread::get_id()};
  std::shared_ptr<detail::devtools_host_state> devtools_;
};

using webview_host = soui_miniblink_host;

}  // namespace soui::sdk
