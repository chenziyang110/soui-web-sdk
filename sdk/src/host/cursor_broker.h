#pragma once

#include <cstdint>
#include <memory>

#include <windows.h>
#include "../../../third_party/include/miniblink/wke.h"
#include "cursor_queue.h"

namespace soui::sdk::detail {

inline constexpr UINT cursor_descriptor_message = WM_APP + 0x4D4;
struct cursor_queue_context;

// Callback storage must outlive the vendor's successful destroyEndpoint drain
// receipt. Native hooks never invoke WKE or wait for the renderer owner.
class cursor_broker_endpoint final {
 public:
  [[nodiscard]] static std::unique_ptr<cursor_broker_endpoint> create(
      HWND owner, const wkeCursorBrokerIdentity& identity);
  ~cursor_broker_endpoint();
  cursor_broker_endpoint(const cursor_broker_endpoint&) = delete;
  cursor_broker_endpoint& operator=(const cursor_broker_endpoint&) = delete;
  [[nodiscard]] wkeCursorBrokerHooks hooks() const noexcept;
  [[nodiscard]] bool bind(const wkeCursorBrokerEndpointReceipt&) noexcept;
  [[nodiscard]] wkeCursorBrokerEndpoint token() const noexcept;
  [[nodiscard]] wkeCursorBrokerIdentity identity() const noexcept;
  [[nodiscard]] bool advance_document() noexcept;
  [[nodiscard]] bool request_document_change() noexcept;
  // Call on the engine owner immediately before issuing the actual reload.
  // A context callback already running when the UI requests reload cannot ACK it.
  [[nodiscard]] bool arm_document_change() noexcept;
  [[nodiscard]] bool input_allowed() const noexcept;
  [[nodiscard]] bool advance_geometry() noexcept;
  [[nodiscard]] bool process_descriptor() noexcept;
  [[nodiscard]] bool select() noexcept;
  [[nodiscard]] bool begin_close() noexcept;
  [[nodiscard]] bool finish_close() noexcept;
  [[nodiscard]] unsigned native_scopes() const noexcept;
  [[nodiscard]] cursor_queue_status last_status() const noexcept;

 private:
  struct implementation;
  explicit cursor_broker_endpoint(std::shared_ptr<implementation> state) noexcept;
  std::shared_ptr<implementation> impl_;
};

// Protect the actual native modal call, including any nested message dispatch.
// Establish the shared queue even before its first endpoint; a modal pump may
// create that endpoint while this lease is active. Failed admission is explicit.
class native_cursor_scope final {
 public:
  native_cursor_scope() noexcept;
  ~native_cursor_scope();
  native_cursor_scope(const native_cursor_scope&) = delete;
  native_cursor_scope& operator=(const native_cursor_scope&) = delete;
  [[nodiscard]] bool admitted() const noexcept { return admitted_; }
 private:
  std::shared_ptr<cursor_queue_context> impl_;
  std::uint64_t sequence_{0};
  bool admitted_{false};
};

}  // namespace soui::sdk::detail
