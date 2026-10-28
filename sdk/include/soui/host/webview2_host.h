#pragma once

#include <soui/host/webview_host.h>
#include <soui/diagnostics/runtime_metrics.h>
#include <filesystem>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace soui::sdk {

namespace detail { class stage0_webview2_access; }

// Forward this payload-free notification to webview_host::pump() on the owner
// thread. No periodic polling is needed for Bridge delivery.
inline constexpr std::uint32_t webview2_event_message = 0x84D4U;

enum class webview2_profile_access { shared, exclusive };
enum class webview2_runtime_exit_phase { pending, complete, finished, failed };
enum class webview2_runtime_exit_error {
  none, wrong_thread, unavailable, process_query, browser_changed,
  sender_identity, event_read, abnormal_exit, process_wait,
  registration, removal, process_handle_close, callback_exception
};
enum class webview2_browser_exit_kind { absent, normal, failed };

// Fixed observation; no authorization, callback, shutdown or recovery operation.
struct webview2_runtime_exit_observation {
  webview2_runtime_exit_phase phase{webview2_runtime_exit_phase::pending};
  webview2_runtime_exit_error error{webview2_runtime_exit_error::none};
  std::int32_t hresult{};
  std::uint32_t win32_error{};
  std::uint32_t browser_process_id{}, event_browser_process_id{}, active_callbacks{};
  std::uint64_t creation_file_time{};
  std::filesystem::path image_path;
  webview2_browser_exit_kind exit_kind{webview2_browser_exit_kind::absent};
  bool event_observed{}, process_signaled{}, registered{};
};

// Exclusive-profile application barrier, separate from controller unmount.
// Move/calls/destruction require the creating UI STA. An unfinished owning
// watch must be retained; destroying it fail-stops rather than releasing an
// environment which still owes the actual exit event.
class webview2_runtime_exit_watch final {
 public:
  struct state;
  ~webview2_runtime_exit_watch();
  webview2_runtime_exit_watch(webview2_runtime_exit_watch&&) noexcept;
  webview2_runtime_exit_watch& operator=(webview2_runtime_exit_watch&&) noexcept;
  webview2_runtime_exit_watch(const webview2_runtime_exit_watch&) = delete;
  webview2_runtime_exit_watch& operator=(const webview2_runtime_exit_watch&) = delete;
  [[nodiscard]] webview2_runtime_exit_observation observe() const;
  [[nodiscard]] host_result finish();
 private:
  explicit webview2_runtime_exit_watch(std::shared_ptr<state>);
  // Releases only a verified terminated process after an unsuccessful watch.
  // The observation remains failed; this is never a normal-exit receipt.
  [[nodiscard]] host_result dispose_failed();
  std::shared_ptr<state> state_;
  friend class webview2_adapter;
};

// Optional Soui::WebView2 component. Calls and destruction require the UI STA.
// Close/retry until unmount succeeds before destroying the adapter or Bridge.
// Native handlers must return promptly; use the task service for long work.
class webview2_adapter final : public sdk::webview_host_adapter {
 public:
  struct state;
  webview2_adapter(void* owner, std::filesystem::path profile, std::string preload = {},
                   std::filesystem::path fixed_runtime = {});
  webview2_adapter(void* owner, std::filesystem::path profile, std::string preload,
                   std::filesystem::path fixed_runtime, diagnostics::mode observation);
  ~webview2_adapter() override;
  webview2_adapter(const webview2_adapter&) = delete;
  webview2_adapter& operator=(const webview2_adapter&) = delete;

  // Exclusive mode is opt-in before prepare. It requires a successful owning
  // runtime watch finish before destruction. Controlled DevTools selects this
  // mode and retains its own watch through the native Host lifecycle hooks.
  [[nodiscard]] host_result configure_profile_access(webview2_profile_access);
  [[nodiscard]] host_result acquire_runtime_exit_watch(std::unique_ptr<webview2_runtime_exit_watch>& empty_output);
  // True only after a real failure before Environment ownership or a failed
  // no-Core Controller callback has settled and unmount released all failed
  // preparation ownership. Never a browser exit.
  [[nodiscard]] bool failed_preparation_cleanup_complete() const noexcept;
  void prepare_async(std::function<void(sdk::host_result)> completed);
  [[nodiscard]] sdk::host_result status() const;
  [[nodiscard]] std::string runtime_version() const;
  [[nodiscard]] std::string session_digest() const;
  // Owner-STA scalar copy only; no COM calls, waits, permissions or lifecycle.
  [[nodiscard]] diagnostics::webview2_resources observe_native_resources() const noexcept;
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

 protected:
  [[nodiscard]] host_result configure_devtools_native(const devtools::configuration&) override;
  [[nodiscard]] devtools::capabilities devtools_capabilities_native() const override;
  [[nodiscard]] host_result prepare_devtools_grant(devtools::target_identity&) override;
  [[nodiscard]] devtools::target_identity devtools_target_native() const override;
  [[nodiscard]] host_result open_devtools_native() override;
  [[nodiscard]] host_result revoke_devtools_native(devtools::revoke_reason) override;
  [[nodiscard]] devtools::observation observe_devtools_native() const override;
  [[nodiscard]] host_result finish_devtools_retirement() override;

 private:
  [[nodiscard]] host_result acquire_runtime_exit_watch_owned(
      std::unique_ptr<webview2_runtime_exit_watch>& empty_output);
  std::shared_ptr<state> state_;
  friend class detail::stage0_webview2_access;
};

}  // namespace soui::sdk
