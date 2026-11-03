// Execute the actual production function with narrow deterministic dependencies.
#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace blink {
struct WebCursorInfo {
  enum Type { TypePointer, TypeIBeam, TypeNone } type;
  explicit WebCursorInfo(Type value) : type(value) {}
};
}
struct FakeView {
  blink::WebCursorInfo m_cursor{blink::WebCursorInfo::TypePointer};
  FakeView* webPage() { return this; }
  FakeView* webPageImpl() { return this; }
};
using wkeWebView = FakeView*;
struct Resources {
  unsigned documents{0};
  bool is_closed{false};
  void documentCommitted() { if (++documents > 1) is_closed = true; }
  bool closed() const { return is_closed; }
};
struct Control {
  bool closing{false}, failed{false}, documentPending{false};
  unsigned inspectorDocuments{0};
  std::shared_ptr<Resources> inspectorResources;
  struct Identity { std::uint64_t documentGeneration{7}; } identity;
  std::uint64_t pendingDocumentGeneration{0};
  std::atomic<std::uint64_t> epoch{1};
  std::array<unsigned, 2> identityActions{};
  wkeWebView view{};
};
using BrokerActionOwner = unsigned;
constexpr std::size_t kIdentityActionsPerEndpoint = 2;
std::shared_ptr<Control> active;
bool on_engine = true, nested_identity_change = false, session_closing = false;
unsigned publications = 0, action_calls = 0;
blink::WebCursorInfo::Type published = blink::WebCursorInfo::TypePointer;
std::shared_ptr<Control> findView(wkeWebView view) { return active && active->view == view ? active : nullptr; }
bool ownerEngine() { return on_engine; }
struct ActionCalloutLease { explicit ActionCalloutLease(std::shared_ptr<Control>) {} bool entered() { return true; } };
bool snapshotActions(std::shared_ptr<Control>, const std::array<BrokerActionOwner, 2>&,
    std::array<BrokerActionOwner, 2>&) { return true; }
std::uint64_t nextSequence(std::atomic<std::uint64_t>& sequence) { return ++sequence; }
void invokeActions(std::shared_ptr<Control> value, const std::array<BrokerActionOwner, 2>&) {
  ++action_calls;
  if (nested_identity_change) { ++value->epoch; value->view->m_cursor.type = blink::WebCursorInfo::TypeIBeam; }
}
void stage0DocumentCommitted(wkeWebView, bool main) {
  if (main && active->inspectorResources && active->inspectorDocuments > 1) session_closing = true;
}
bool brokerPublishCursor(wkeWebView, const blink::WebCursorInfo& cursor) { ++publications; published = cursor.type; return true; }

// The extraction contains no duplicated implementation of the document transition.
#include "broker-document.inc"

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void reset(FakeView& view, bool inspector) {
  active = std::make_shared<Control>(); active->view = &view;
  view.m_cursor.type = blink::WebCursorInfo::TypePointer;
  if (inspector) active->inspectorResources = std::make_shared<Resources>();
  on_engine = true; nested_identity_change = session_closing = false;
  publications = action_calls = 0; published = blink::WebCursorInfo::TypePointer;
}
int main() {
  try {
    FakeView view;
    reset(view, true);
    brokerDocumentCommitted(&view, true);
    require(active->inspectorDocuments == 1 && active->inspectorResources->documents == 1,
        "the trusted Inspector's first actual document must still enter its resource fence");
    require(!active->documentPending && published == blink::WebCursorInfo::TypePointer,
        "REGRESSION: first trusted Inspector document must not wait for an absent business ACK and hide its cursor");
    require(!session_closing && action_calls == 1 && publications == 1,
        "first Inspector load retains session admission and identity-action invalidation");
    brokerDocumentCommitted(&view, true);
    require(active->documentPending && published == blink::WebCursorInfo::TypeNone &&
        active->inspectorResources->closed() && session_closing,
        "a later Inspector document must still revoke resources, close the session and hide the old cursor");

    reset(view, true); active->inspectorResources->is_closed = true;
    brokerDocumentCommitted(&view, true);
    require(active->documentPending && published == blink::WebCursorInfo::TypeNone,
        "closed Inspector resources cannot use the first-document exception");

    reset(view, false);
    brokerDocumentCommitted(&view, true);
    require(active->documentPending && active->pendingDocumentGeneration == 7 &&
        published == blink::WebCursorInfo::TypeNone,
        "business document transitions must still require a strictly newer SDK ACK");

    reset(view, true);
    brokerDocumentCommitted(&view, false);
    require(!active->inspectorDocuments && !active->documentPending && !publications && action_calls == 1,
        "subframes do not consume the first main document or hide the top-level cursor");

    reset(view, true); nested_identity_change = true;
    brokerDocumentCommitted(&view, true);
    require(!publications && view.m_cursor.type == blink::WebCursorInfo::TypeIBeam,
        "nested identity change cannot be overwritten by a stale outer cursor publication");

    reset(view, true); active->closing = true;
    brokerDocumentCommitted(&view, true);
    require(!publications && !active->inspectorDocuments, "closing endpoint cannot admit a document");
    reset(view, true); active->failed = true;
    brokerDocumentCommitted(&view, true);
    require(!publications && !active->inspectorDocuments, "failed endpoint cannot admit a document");
    reset(view, true); on_engine = false;
    brokerDocumentCommitted(&view, true);
    require(!publications && !active->inspectorDocuments, "wrong thread cannot admit a document");
    std::cout << "Inspector cursor document regression: 9 groups passed.\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
