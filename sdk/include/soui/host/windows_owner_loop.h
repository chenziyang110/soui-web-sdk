#pragma once

#include <soui/host/webview_host.h>
#include <windows.h>
#include <cstdint>
#include <functional>
#include <memory>

namespace soui::sdk {

// Application hooks for a loop which the SDK actually owns. Empty hooks use
// ordinary TranslateMessage/DispatchMessage, no application idle callback,
// and WM_QUIT termination. They do not run on a worker or grant Bridge access.
struct windows_owner_loop_callbacks {
  // Passive observation immediately after GetMessage returns a message
  // (including WM_QUIT), before SDK work or other hooks. Do not retrieve,
  // dispatch or reenter a loop here; consumers can capture queue metadata
  // before the normal dispatch hook without changing message identity.
  std::function<void(const MSG&)> message_retrieved;
  std::function<void(MSG&)> dispatch;
  std::function<bool(std::uint32_t)> idle;
  std::function<bool(const MSG&)> is_idle_message;
  // Passive predicate: inspect application state without posting/retrieving
  // messages, dispatching callbacks or reentering a loop. It is also checked
  // after retrieval APIs which can dispatch sent messages internally.
  std::function<bool()> quit_requested;
  // Return true only to preserve an application's explicit retry policy.
  // Otherwise run() throws on the actual GetMessage error.
  std::function<bool(std::uint32_t)> get_message_error;
};

// Construct before preparing Hosts on this owner thread. Matching adapters
// register automatically; an application which keeps its own legacy loop
// retains the original notification contract. This class does not initialize
// COM, change thread priority/timer precision, or close application Hosts.
//
// All calls and destruction require the creating thread. One object cannot
// reenter run(); different SDK loop objects on the same thread may nest.
// Callbacks and individual COM calls remain cooperative, not preemptible.
class windows_owner_loop final {
 public:
  struct state;
  windows_owner_loop();
  ~windows_owner_loop();
  windows_owner_loop(const windows_owner_loop&) = delete;
  windows_owner_loop& operator=(const windows_owner_loop&) = delete;
  windows_owner_loop(windows_owner_loop&&) = delete;
  windows_owner_loop& operator=(windows_owner_loop&&) = delete;

  // Optimizer availability, separate from whether run() is active. Expected
  // dispatcher/resource failures retain a usable ordinary message loop and
  // original Host wakeups; status() reports the failure instead of success.
  [[nodiscard]] host_result status() const;

  // Returns WM_QUIT's wParam, or zero when quit_requested asks to stop.
  // Exception/quit unwinding releases this root scope and restores legacy
  // wakeups. Applications must still drain/close Hosts before ending the loop.
  int run(const windows_owner_loop_callbacks& callbacks = {});

 private:
  std::shared_ptr<state> state_;
};

}  // namespace soui::sdk
