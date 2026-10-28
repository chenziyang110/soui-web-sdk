#pragma once

#include <soui/host/engine_thread.h>
#include <soui/host/webview_host.h>
#include "kernel_abi.h"
#include <atomic>
#include <functional>
#include <exception>
#include <memory>

namespace soui::sdk::detail {

class miniblink_devtools_busy final : public std::exception {
 public:
  const char* what() const noexcept override { return "A controlled inspector already owns the shared MiniBlink Runtime."; }
};

// Only the adapter can compose this borrow from its retained mounted lifetime.
// Neither this identity nor the kernel ABI is an application permission.
struct miniblink_devtools_target {
  engine_thread* engine{};
  HMODULE module{};
  const wkeCursorBrokerApi* broker{};
  wkeCursorBrokerRealm realm{};
  wkeCursorBrokerEndpoint endpoint{};
  wkeCursorBrokerIdentity identity{};
  wkeWebView view{};
  std::uint64_t receipt{};
  HWND owner_window{};
};

class miniblink_devtools_session final {
 public:
  [[nodiscard]] static bool available(HMODULE) noexcept;
  miniblink_devtools_session(miniblink_devtools_target, const devtools::configuration&,
      std::function<bool()> target_current, std::function<void()> native_close);
  ~miniblink_devtools_session();
  miniblink_devtools_session(const miniblink_devtools_session&) = delete;
  miniblink_devtools_session& operator=(const miniblink_devtools_session&) = delete;

  // UI-owner only. Each call performs bounded work and polls one retained
  // operation. No ordinary Engine FIFO is pumped while JavaScript is paused.
  [[nodiscard]] host_result advance();
  [[nodiscard]] host_result revoke();
  [[nodiscard]] devtools::observation observe() const;
  [[nodiscard]] bool closing() const noexcept;
  [[nodiscard]] bool drained() const noexcept;

 private:
  struct state;
  std::shared_ptr<state> state_;
};

} // namespace soui::sdk::detail
