#pragma once

#include <soui/host/runtime_profile.h>
#include <soui/host/webview_host.h>
#include <cstdint>
#include <functional>
#include <memory>

namespace soui::sdk {

struct auto_host_config {
  // resource_root is selected from the two explicit roots below. The other
  // host fields retain their existing meaning, including the borrowed Bridge.
  host_config host;
  runtime_profile requested{runtime_profile::automatic};
  bool requires_modern_web{false};
  std::filesystem::path modern_resource_root;
  std::filesystem::path compatibility_resource_root;
  std::filesystem::path webview2_user_data;
  // Empty uses installed discovery. A nonempty value must name an existing
  // absolute directory. An empty directory is a valid isolated search scope.
  std::filesystem::path webview2_fixed_runtime;
};

struct webview2_runtime_discovery {
  host_result result;
  bool attempted{false};
  bool available{false};
  std::int32_t hresult{};
  std::string version;
};

// Read-only loader discovery; does not create an environment or download a
// runtime. Only ERROR_FILE_NOT_FOUND means a successfully observed absence.
[[nodiscard]] webview2_runtime_discovery inspect_webview2_runtime(
    const std::filesystem::path& fixed_runtime = {});

// Optional Soui::AutoHost component. Construct, call, and destroy on the owner
// UI STA. It selects once before preparation and never switches after failure.
// Keep this object, its owner HWND and borrowed Bridge alive until shutdown()
// succeeds; an abandoned owning shutdown fail-stops like the underlying Host.
class auto_webview_host final {
 public:
  auto_webview_host();
  ~auto_webview_host();
  auto_webview_host(const auto_webview_host&) = delete;
  auto_webview_host& operator=(const auto_webview_host&) = delete;

  // The return reports admission only. An admitted invocation calls completed
  // exactly once; validation/MiniBlink completion may be synchronous. Do not
  // destroy this object inside completed. A second start is always rejected.
  [[nodiscard]] host_result start_async(const auto_host_config& config,
      std::function<void(host_result)> completed);
  // Cancels preparation and retries cleanup without blocking the UI. E_BUSY
  // retains ownership: continue dispatching messages and retry on this STA.
  // Includes the WebView2 exclusive browser exit barrier. MiniBlink process
  // shutdown remains the application's final call after every Host closes.
  [[nodiscard]] host_result shutdown();
  [[nodiscard]] bool shutdown_complete() const noexcept;
  [[nodiscard]] host_result status() const;
  [[nodiscard]] const runtime_selection& selection() const noexcept;
  [[nodiscard]] const webview2_runtime_discovery& discovery() const noexcept;
  [[nodiscard]] std::string runtime_version() const;
  void pump();
  [[nodiscard]] host_result resize(int width, int height);
  [[nodiscard]] host_result focus();
  [[nodiscard]] host_result show();
  [[nodiscard]] host_result hide();
  [[nodiscard]] host_result reload();
  void deliver_posted_event(void* payload);
  [[nodiscard]] bool document_ready() const noexcept;
  [[nodiscard]] bool application_ready();
  [[nodiscard]] std::string application_diagnostics();

 private:
  struct state;
  std::unique_ptr<state> state_;
};

} // namespace soui::sdk
