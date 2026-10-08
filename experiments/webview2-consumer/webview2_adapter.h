#pragma once

#include <soui/host/webview_host.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace soui::experiment {

// Experimental, UI-STA-confined consumer adapter; not an installed SDK backend.
class webview2_adapter final : public sdk::webview_host_adapter {
 public:
  struct state;
  webview2_adapter(void* owner, std::filesystem::path profile, std::string preload);
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

}  // namespace soui::experiment
