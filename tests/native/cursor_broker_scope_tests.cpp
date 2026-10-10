#include "../../sdk/src/host/cursor_broker.h"

#include <iostream>
#include <memory>
#include <stdexcept>

using namespace soui::sdk::detail;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct owned_window {
  HWND handle{CreateWindowExW(0, L"STATIC", L"cursor scope regression", 0,
      0, 0, 1, 1, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr)};
  ~owned_window() { if (handle) DestroyWindow(handle); }
};
void first_endpoint_inside_modal_scope() {
  owned_window window; require(window.handle != nullptr, "owned HWND creation");
  std::unique_ptr<cursor_broker_endpoint> endpoint;
  {
    native_cursor_scope outer;
    require(outer.admitted(), "first native scope creates shared context");
    endpoint = cursor_broker_endpoint::create(window.handle, {sizeof(wkeCursorBrokerIdentity), 0, 1, 1, 1});
    require(endpoint && endpoint->native_scopes() == 1, "first endpoint inherits outer modal lease");
    {
      native_cursor_scope inner;
      require(inner.admitted() && endpoint->native_scopes() == 2, "nested native scope shares same context");
    }
    require(endpoint->native_scopes() == 1 && endpoint->begin_close() && !endpoint->finish_close(),
            "close cannot claim completion while the outer borrower lives");
  }
  require(endpoint->native_scopes() == 0 && endpoint->finish_close(), "real outer leave enables completed close");
  endpoint.reset();
}
void scope_capacity_before_any_endpoint() {
  std::unique_ptr<native_cursor_scope> scopes[65];
  for (unsigned i = 0; i < 65; ++i) scopes[i] = std::make_unique<native_cursor_scope>();
  for (unsigned i = 0; i < 64; ++i) require(scopes[i]->admitted(), "bounded lazy scope admitted");
  require(!scopes[64]->admitted(), "65th lazy scope rejected");
  for (auto& scope : scopes) scope.reset();
  native_cursor_scope recovered; require(recovered.admitted(), "capacity released by real scope destructors");
}
void document_and_geometry_revoke_old_callbacks() {
  owned_window window; require(window.handle != nullptr, "generation fixture HWND");
  const wkeCursorBrokerIdentity initial{sizeof(wkeCursorBrokerIdentity), 0, 1, 1, 1};
  auto endpoint = cursor_broker_endpoint::create(window.handle, initial);
  require(endpoint != nullptr, "generation fixture endpoint");
  // This fixture supplies an ABI receipt and synchronous callbacks. It does
  // not create a vendor WebView or prove that the vendor has drained.
  const wkeCursorBrokerEndpoint token{5, 6};
  wkeCursorBrokerEndpointReceipt receipt{};
  receipt.structSize = sizeof(receipt); receipt.ownerThreadId = GetCurrentThreadId();
  receipt.endpoint = token; receipt.identity = initial;
  receipt.webView = reinterpret_cast<wkeWebView>(1);
  require(endpoint->bind(receipt), "trusted fixture receipt binding");
  const auto hooks = endpoint->hooks();
  auto emit = [&](wkeCursorBrokerIdentity identity, unsigned sequence, unsigned type) {
    wkeCursorBrokerDescriptor descriptor{};
    descriptor.structSize = sizeof(descriptor); descriptor.kind = WKE_CURSOR_BROKER_STANDARD;
    descriptor.identity = identity; descriptor.publicationSequence = sequence; descriptor.cursorType = type;
    hooks.onDescriptor(hooks.userData, token, &descriptor);
    (void)endpoint->process_descriptor();
    require(endpoint->select(), "standard fixture selection");
  };
  emit(initial, 1, 3);
  require(GetCursor() == LoadCursorW(nullptr, IDC_IBEAM), "initial actual thread selection");
  require(endpoint->request_document_change() && !endpoint->input_allowed(), "request revokes input before actual context");
  emit(initial, 2, 3);
  require(GetCursor() == LoadCursorW(nullptr, IDC_ARROW), "late same-id request callback cannot restore old cursor");
  wkeCursorBrokerNativeReceipt native{};
  native.structSize = sizeof(native); native.kind = WKE_CURSOR_BROKER_NATIVE_MENU;
  native.endpoint = token; native.identity = initial; native.taskSequence = native.scopeSequence = 1;
  require(!hooks.onNativeEnter(hooks.userData, &native), "old native task rejected while document request pending");
  require(!endpoint->advance_document() && !endpoint->input_allowed() &&
              endpoint->identity().documentGeneration == 1,
          "old context before the engine issues reload cannot ACK the UI request");
  require(endpoint->arm_document_change(), "actual engine command arms the document request");
  require(endpoint->advance_document() && endpoint->input_allowed(), "actual context advances epoch");
  require(!endpoint->arm_document_change(), "completed request cannot be armed a second time");
  const auto document = endpoint->identity();
  require(document.documentGeneration == 2 && document.geometryGeneration == 1, "document advances only its dimension");
  emit(initial, 3, 3);
  require(GetCursor() == LoadCursorW(nullptr, IDC_ARROW), "old document callback remains stale after actual context");
  emit(document, 4, 2);
  require(GetCursor() == LoadCursorW(nullptr, IDC_HAND), "current document callback selected");
  require(endpoint->advance_geometry(), "UI geometry advances");
  const auto geometry = endpoint->identity();
  require(geometry.documentGeneration == 2 && geometry.geometryGeneration == 2, "geometry preserves actual document epoch");
  emit(document, 5, 3);
  require(GetCursor() == LoadCursorW(nullptr, IDC_ARROW), "old geometry callback cannot restore previous selection");
  emit(geometry, 6, 1);
  require(GetCursor() == LoadCursorW(nullptr, IDC_CROSS), "current full tuple selected");
  require(endpoint->begin_close() && endpoint->finish_close(), "synchronous fixture callback storage closed");
}
}  // namespace
int main() {
  try {
    first_endpoint_inside_modal_scope(); scope_capacity_before_any_endpoint(); document_and_geometry_revoke_old_callbacks();
    std::cout << "Cursor broker scopes: 3/3 passed\n"; return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
