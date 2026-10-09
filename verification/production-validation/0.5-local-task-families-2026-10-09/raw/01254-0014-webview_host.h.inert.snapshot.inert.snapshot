#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <soui/bridge_adapter.h>
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

// Implementations own their renderer resources. A failed mount must clean up
// its partial resources; a failed unmount must permit a later unmount retry.
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
  // Idempotent cleanup: never unmounts an already-closed/destroyed session twice.
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

  std::unique_ptr<webview_host_adapter> adapter_;
  std::unique_ptr<soui::runtime::webview_session> session_;
  bool lifecycle_operation_{false};
};

using webview_host = soui_miniblink_host;

}  // namespace soui::sdk
