#pragma once

#ifndef SOUI_HOST_STAGE0_DEVTOOLS_TEST_ONLY
#error "Private WebView2 Stage0 access is available only to the dedicated experiment"
#endif

#include <soui/host/webview2_host.h>

namespace soui::sdk::detail {

class stage0_webview2_grant final {
 public:
  stage0_webview2_grant() = default;
 private:
  std::weak_ptr<webview2_adapter::state> owner_;
  std::uint64_t generation_{}, epoch_{};
  std::uint32_t process_{};
  std::string document_;
  friend class stage0_webview2_access;
};

enum class stage0_scope_shutdown { not_started, pending, succeeded, failed, unknown };
enum class stage0_window_state { unknown };
#ifdef SOUI_HOST_PERFORMANCE_PROFILE_TEST_ONLY
enum class profile_page_action { setup, query, install, start, finish, abort, stop_workload };
class stage0_profile_subscription;
using stage0_profile_completion = std::function<void(host_result, std::string)>;
using stage0_profile_event = std::function<void(std::wstring, std::string)>;
#endif
struct stage0_webview2_observation final {
  bool wrong_thread{};
  bool policy_enabled{}, grant_valid{}, open_attempted{}, open_accepted{};
  bool retirement_required{}, admission_closed{}, worker_done{}, worker_joined{}, controller_closed{};
  bool debugger_enabled{}, paused{}, protocol_failed{};
  unsigned pending_callbacks{}, active_callbacks{}, pause_events{}, resume_events{};
  stage0_scope_shutdown scope_shutdown{stage0_scope_shutdown::not_started};
  stage0_window_state visible{stage0_window_state::unknown}, attached{stage0_window_state::unknown};
};

// Native-only pause/revoke experiment. It returns no COM pointer, arbitrary
// script or bearer. The additional performance-test macro below explicitly
// permits bounded profile/trace responses in the isolated measurement target.
class stage0_webview2_access final {
 public:
  static host_result configure(webview2_adapter&, bool enabled, bool accept_destroy_target);
  static host_result grant(webview2_adapter&, stage0_webview2_grant&);
  static host_result open(webview2_adapter&, const stage0_webview2_grant&);
  static host_result revoke_grant(webview2_adapter&);
  static host_result begin_retirement(webview2_adapter&);
  static host_result enable_debugger(webview2_adapter&, const stage0_webview2_grant&);
  static host_result request_debugger_pause(webview2_adapter&, const stage0_webview2_grant&);
  static stage0_webview2_observation observe(const webview2_adapter&);
#ifdef SOUI_HOST_PERFORMANCE_PROFILE_TEST_ONLY
  // Fixed, native-only diagnostic recipe. Definitions exist only in the
  // dedicated experiment, never in the normal SDK library or installation.
  static host_result profile_command(webview2_adapter&, const stage0_webview2_grant&,
                                    const std::wstring& method, stage0_profile_completion);
  static host_result profile_page(webview2_adapter&, const stage0_webview2_grant&,
                                 profile_page_action, stage0_profile_completion);
  static host_result profile_subscribe(webview2_adapter&, const stage0_webview2_grant&,
                                      const std::wstring& event, stage0_profile_event,
                                      std::shared_ptr<stage0_profile_subscription>&);
  static host_result profile_unsubscribe(std::shared_ptr<stage0_profile_subscription>&);
#endif
 private:
  static host_result validate(const webview2_adapter&, const stage0_webview2_grant&);
};

} // namespace soui::sdk::detail
