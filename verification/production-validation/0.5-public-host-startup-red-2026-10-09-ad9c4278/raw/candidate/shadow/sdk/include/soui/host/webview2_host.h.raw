#pragma once

#include <soui/host/webview_host.h>
#include <filesystem>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace soui::sdk {

// Forward this payload-free notification to webview_host::pump() on the owner
// thread. No periodic polling is needed for Bridge delivery.
inline constexpr std::uint32_t webview2_event_message = 0x84D4U;

// Optional Soui::WebView2 component. Calls and destruction require the UI STA.
// Close/retry until unmount succeeds before destroying the adapter or Bridge.
// Native handlers must return promptly; use the task service for long work.
class webview2_adapter final : public sdk::webview_host_adapter {
 public:
  struct state;
  webview2_adapter(void* owner, std::filesystem::path profile, std::string preload = {},
                   std::filesystem::path fixed_runtime = {});
  ~webview2_adapter() override;
  webview2_adapter(const webview2_adapter&) = delete;
  webview2_adapter& operator=(const webview2_adapter&) = delete;

  void prepare_async(std::function<void(sdk::host_result)> completed);
  [[nodiscard]] sdk::host_result status() const;
  [[nodiscard]] std::string runtime_version() const;
  [[nodiscard]] std::string session_digest() const;
  [[nodiscard]] sdk::host_result mount(const sdk::host_config& config) override;
  [[nodiscard]] sdk::host_result unmount() override;
  [[nodiscard]] sdk::host_result show(bool visible) override;
  [[nodiscard]] sdk::host_result reload() override;
  [[nodiscard]] sdk::host_result resize(int width, int height) override;
  [[nodiscard]] sdk::host_result focus() override;
  void pump() override;
  [[nodiscard]] bool document_ready() const noexcept override;
  [[nodiscard]] bool application_ready() override;
  [[nodiscard]] std::string application_diagnostics() override;
  void set_session_id(std::string) override {} // actual Bridge bootstrap owns the document session

 private:
  std::shared_ptr<state> state_;
};

}  // namespace soui::sdk
