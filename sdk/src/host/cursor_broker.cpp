#include "cursor_broker.h"
#include "cursor_resources.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>
#include <utility>

namespace soui::sdk::detail {
namespace {

bool valid_identity(const wkeCursorBrokerIdentity& value) noexcept {
  return value.structSize == sizeof(value) && !value.reserved && value.viewGeneration &&
      value.documentGeneration && value.geometryGeneration;
}
bool same_identity(const wkeCursorBrokerIdentity& a, const wkeCursorBrokerIdentity& b) noexcept {
  return a.viewGeneration == b.viewGeneration && a.documentGeneration == b.documentGeneration &&
      a.geometryGeneration == b.geometryGeneration;
}
bool same_token(wkeCursorBrokerToken a, wkeCursorBrokerToken b) noexcept {
  return a.low == b.low && a.high == b.high;
}
bool valid_token(wkeCursorBrokerToken value) noexcept { return value.low || value.high; }

class win_cursor_resource_api final : public cursor_resource_api {
 public:
  void* color_bitmap(const cursor_descriptor& value, void*& pixels) noexcept override {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = static_cast<LONG>(value.width);
    info.bmiHeader.biHeight = -static_cast<LONG>(value.height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
  }
  void* mask_bitmap(const cursor_descriptor& value) noexcept override {
    // CreateBitmap's 1bpp scanlines are WORD aligned, including width 17.
    std::array<unsigned char, 8192> mask_bytes{};
    return CreateBitmap(static_cast<int>(value.width), static_cast<int>(value.height),
                        1, 1, mask_bytes.data());
  }
  void* cursor(const cursor_descriptor& value, void* color, void* mask) noexcept override {
    ICONINFO icon{};
    icon.fIcon = FALSE;
    icon.xHotspot = static_cast<DWORD>(value.hotspot_x);
    icon.yHotspot = static_cast<DWORD>(value.hotspot_y);
    icon.hbmColor = static_cast<HBITMAP>(color);
    icon.hbmMask = static_cast<HBITMAP>(mask);
    return CreateIconIndirect(&icon);
  }
  bool delete_bitmap(void* value) noexcept override { return DeleteObject(value) != FALSE; }
  bool destroy_unpublished_cursor(void* value) noexcept override {
    return DestroyCursor(static_cast<HCURSOR>(value)) != FALSE;
  }
};

class win_cursor_platform final : public cursor_platform {
 public:
  std::uint32_t thread_id() const noexcept override { return GetCurrentThreadId(); }
  void* standard(std::uint32_t type) noexcept override {
    LPCWSTR name = IDC_ARROW;
    switch (type) {
      case 1: case 31: name = IDC_CROSS; break;
      case 2: case 41: case 42: name = IDC_HAND; break;
      case 3: case 30: name = IDC_IBEAM; break;
      case 4: name = IDC_WAIT; break;
      case 5: name = IDC_HELP; break;
      case 6: case 13: case 15: case 18: name = IDC_SIZEWE; break;
      case 7: case 10: case 14: case 19: name = IDC_SIZENS; break;
      case 8: case 12: case 16: name = IDC_SIZENESW; break;
      case 9: case 11: case 17: name = IDC_SIZENWSE; break;
      case 20: case 21: case 22: case 23: case 24: case 25:
      case 26: case 27: case 28: case 29: name = IDC_SIZEALL; break;
      case 34: name = IDC_APPSTARTING; break;
      case 35: case 38: name = IDC_NO; break;
      default: break;
    }
    return LoadCursorW(nullptr, name); // shared, never destroyed
  }
  void* create(const cursor_descriptor& value) noexcept override {
    return resources_.create(value);
  }
  void select(void* value) noexcept override { (void)SetCursor(static_cast<HCURSOR>(value)); }
  void* local_selection() const noexcept override { return GetCursor(); }
  cursor_global_observation global_selection() const noexcept override {
    CURSORINFO info{}; info.cbSize = sizeof(info);
    if (!GetCursorInfo(&info)) return {};
    return {true, info.flags, info.hCursor};
  }
  bool destroy(void* value) noexcept override { return DestroyCursor(static_cast<HCURSOR>(value)) != FALSE; }
  bool cleanup_unpublished() noexcept override { return resources_.cleanup(); }
  unsigned unpublished_cursors() const noexcept override { return resources_.unpublished_cursors(); }
 private:
  win_cursor_resource_api resource_api_;
  cursor_resource_owner resources_{resource_api_};
};
struct cursor_wake_target {
  virtual ~cursor_wake_target() = default;
  virtual void post_wake() noexcept = 0;
};

}  // namespace

struct cursor_queue_context {
  win_cursor_platform platform;
  cursor_queue queue{platform};
  std::array<std::weak_ptr<cursor_wake_target>, 64> endpoints{};
  bool attach(const std::shared_ptr<cursor_wake_target>& endpoint) noexcept {
    if (!queue.on_owner_thread()) return false;
    for (auto& slot : endpoints) if (slot.expired()) { slot = endpoint; return true; }
    return false;
  }
  void wake_all() noexcept {
    if (!queue.on_owner_thread()) return;
    for (auto& slot : endpoints) if (const auto endpoint = slot.lock()) endpoint->post_wake();
  }
};
namespace {
thread_local std::weak_ptr<cursor_queue_context> current_queue;
std::shared_ptr<cursor_queue_context> obtain_queue() {
  auto queue = current_queue.lock();
  if (!queue) { queue = std::make_shared<cursor_queue_context>(); current_queue = queue; }
  return queue;
}
}

struct cursor_broker_endpoint::implementation final : cursor_wake_target {
  implementation(HWND window, wkeCursorBrokerIdentity value,
                 std::shared_ptr<cursor_queue_context> context)
      : owner(window), owner_thread(GetCurrentThreadId()), queue(std::move(context)), identity(value) {
    shown.pixels.reserve(256U * 256U * 4U);
  }
  const HWND owner;
  const DWORD owner_thread;
  std::shared_ptr<cursor_queue_context> queue;
  mutable std::mutex mutex;
  wkeCursorBrokerIdentity identity{};
  wkeCursorBrokerEndpoint endpoint{};
  wkeCursorBrokerDescriptor latest{};
  std::array<unsigned char, 256U * 256U * 4U> latest_pixels{};
  std::uint64_t received_sequence{0};
  bool latest_valid{false}, wake_posted{false}, closing{false}, receipt_bound{false};
  bool invalidate_shown{false};
  bool document_request_pending{false};
  bool document_request_armed{false};
  DWORD engine_thread{0};
  cursor_descriptor shown; // UI owner only; allocated before engine callbacks
  std::uint64_t shown_sequence{0};
  bool applying{false}, pending_withdraw{false}, finished{false};
  cursor_queue_status status{cursor_queue_status::selected};
  struct scope {
    wkeCursorBrokerNativeReceipt receipt{};
    std::uint64_t queue_sequence{0};
  };
  std::array<scope, 64> scopes{};
  unsigned scope_count{0};

  bool on_ui() const noexcept { return GetCurrentThreadId() == owner_thread; }
  bool window_matches() const noexcept {
    DWORD process = 0;
    return IsWindow(owner) && GetWindowThreadProcessId(owner, &process) == owner_thread &&
        process == GetCurrentProcessId();
  }
  void post_locked() noexcept {
    if (!wake_posted && !finished && window_matches())
      wake_posted = PostMessageW(owner, cursor_descriptor_message, 0, 0) != FALSE;
  }
  void post_wake() noexcept override {
    std::lock_guard<std::mutex> lock(mutex);
    post_locked();
  }
  static void WKE_CALL_TYPE descriptor(void* context, wkeCursorBrokerEndpoint token,
                                        const wkeCursorBrokerDescriptor* value) noexcept {
    auto& self = *static_cast<implementation*>(context);
    if (!value || value->structSize != sizeof(*value) || value->reserved ||
        !valid_identity(value->identity) || !value->publicationSequence || value->kind > 3) return;
    if (value->kind == WKE_CURSOR_BROKER_CUSTOM_BGRA8) {
      if (value->cursorType != 43 || !value->width || !value->height || value->width > 256 ||
          value->height > 256 || value->stride != value->width * 4 ||
          value->pixelBytes != value->stride * value->height || !value->pixels ||
          value->hotspotX < 0 || value->hotspotY < 0 ||
          static_cast<unsigned>(value->hotspotX) >= value->width ||
          static_cast<unsigned>(value->hotspotY) >= value->height) return;
      for (unsigned i = 0; i < value->pixelBytes; i += 4)
        if (value->pixels[i] > value->pixels[i + 3] || value->pixels[i + 1] > value->pixels[i + 3] ||
            value->pixels[i + 2] > value->pixels[i + 3]) return;
    } else if (value->pixels || value->pixelBytes || value->width || value->height ||
               value->stride || value->hotspotX || value->hotspotY ||
               (value->kind == WKE_CURSOR_BROKER_STANDARD && (value->cursorType > 42 || value->cursorType == 37)) ||
               (value->kind == WKE_CURSOR_BROKER_NONE && value->cursorType != 37)) return;
    std::lock_guard<std::mutex> lock(self.mutex);
    if (self.closing || self.document_request_pending || !valid_token(token) || !same_identity(self.identity, value->identity) ||
        value->publicationSequence <= self.received_sequence ||
        (self.engine_thread && self.engine_thread != GetCurrentThreadId())) return;
    if (!valid_token(self.endpoint)) self.endpoint = token;
    if (!same_token(self.endpoint, token)) return;
    self.engine_thread = GetCurrentThreadId();
    self.latest = *value;
    self.latest.pixels = nullptr;
    if (value->pixelBytes) std::memcpy(self.latest_pixels.data(), value->pixels, value->pixelBytes);
    self.received_sequence = value->publicationSequence;
    self.latest_valid = true;
    self.post_locked();
  }
  static int WKE_CALL_TYPE enter(void* context, const wkeCursorBrokerNativeReceipt* receipt) noexcept {
    auto& self = *static_cast<implementation*>(context);
    if (!receipt || receipt->structSize != sizeof(*receipt) || !self.on_ui() ||
        !self.window_matches() || !receipt->taskSequence || !receipt->scopeSequence ||
        receipt->kind < WKE_CURSOR_BROKER_NATIVE_MENU || receipt->kind > WKE_CURSOR_BROKER_NATIVE_OLE_REVOKE ||
        !valid_identity(receipt->identity) || self.scope_count == self.scopes.size()) return 0;
    {
      std::lock_guard<std::mutex> lock(self.mutex);
      if (!valid_token(receipt->endpoint)) return 0;
      const bool revoke = receipt->kind == WKE_CURSOR_BROKER_NATIVE_OLE_REVOKE;
      if ((!same_identity(self.identity, receipt->identity) && !revoke) ||
          receipt->identity.viewGeneration != self.identity.viewGeneration ||
          receipt->identity.documentGeneration > self.identity.documentGeneration ||
          receipt->identity.geometryGeneration > self.identity.geometryGeneration ||
          ((self.closing || self.document_request_pending) && !revoke)) return 0;
      if (!valid_token(self.endpoint)) self.endpoint = receipt->endpoint;
      if (!same_token(self.endpoint, receipt->endpoint)) return 0;
    }
    for (const auto& slot : self.scopes) if (slot.queue_sequence &&
        slot.receipt.scopeSequence == receipt->scopeSequence) return 0;
    for (auto& slot : self.scopes) if (!slot.queue_sequence) {
      const auto sequence = self.queue->queue.enter_scope(&self);
      if (!sequence) return 0;
      slot = {*receipt, sequence};
      ++self.scope_count;
      return 1;
    }
    return 0;
  }
  static void WKE_CALL_TYPE leave(void* context, const wkeCursorBrokerNativeReceipt* receipt) noexcept {
    auto& self = *static_cast<implementation*>(context);
    if (!receipt || receipt->structSize != sizeof(*receipt) || !self.on_ui()) return;
    for (auto& slot : self.scopes) if (slot.queue_sequence &&
        same_token(slot.receipt.endpoint, receipt->endpoint) &&
        slot.receipt.taskSequence == receipt->taskSequence && slot.receipt.scopeSequence == receipt->scopeSequence &&
        slot.receipt.kind == receipt->kind && same_identity(slot.receipt.identity, receipt->identity)) {
      if (!self.queue->queue.leave_scope(&self, slot.queue_sequence)) std::terminate();
      slot = {};
      --self.scope_count;
      if (!self.queue->queue.native_scopes()) self.queue->wake_all();
      return;
    }
  }
  static void WKE_CALL_TYPE complete(void* context, const wkeCursorBrokerNativeReceipt*, unsigned int) noexcept {
    auto& self = *static_cast<implementation*>(context);
    if (self.on_ui()) self.post_wake();
  }
};

cursor_broker_endpoint::cursor_broker_endpoint(std::shared_ptr<implementation> value) noexcept
    : impl_(std::move(value)) {}
std::unique_ptr<cursor_broker_endpoint> cursor_broker_endpoint::create(
    HWND owner, const wkeCursorBrokerIdentity& identity) {
  DWORD process = 0;
  if (!valid_identity(identity) || !IsWindow(owner) ||
      GetWindowThreadProcessId(owner, &process) != GetCurrentThreadId() || process != GetCurrentProcessId()) return {};
  auto queue = obtain_queue();
  auto state = std::make_shared<implementation>(owner, identity, queue);
  if (!queue->attach(state)) return {};
  return std::unique_ptr<cursor_broker_endpoint>(new cursor_broker_endpoint(std::move(state)));
}
cursor_broker_endpoint::~cursor_broker_endpoint() {
  // Releasing callback storage before a vendor drain is a use-after-free.
  // The host's teardown owns the drain; no timeout or HWND disappearance can
  // convert outstanding callbacks into a completed lifetime.
  if (valid_token(impl_->endpoint) && !impl_->finished) std::terminate();
}
wkeCursorBrokerHooks cursor_broker_endpoint::hooks() const noexcept {
  return {sizeof(wkeCursorBrokerHooks), 0, impl_.get(), &implementation::descriptor,
      &implementation::enter, &implementation::leave, &implementation::complete};
}
bool cursor_broker_endpoint::bind(const wkeCursorBrokerEndpointReceipt& receipt) noexcept {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (receipt.structSize != sizeof(receipt) || !valid_token(receipt.endpoint) ||
      receipt.ownerThreadId != impl_->owner_thread || !valid_identity(receipt.identity) ||
      !same_identity(impl_->identity, receipt.identity) || impl_->receipt_bound || impl_->closing ||
      (valid_token(impl_->endpoint) && !same_token(impl_->endpoint, receipt.endpoint))) return false;
  impl_->endpoint = receipt.endpoint;
  impl_->receipt_bound = true;
  impl_->engine_thread = GetCurrentThreadId();
  // A failed creation may hand off a live endpoint with no completed view.
  // Retain its token so the caller can still request and finish real cleanup.
  return receipt.webView != nullptr;
}
wkeCursorBrokerEndpoint cursor_broker_endpoint::token() const noexcept {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->endpoint;
}
wkeCursorBrokerIdentity cursor_broker_endpoint::identity() const noexcept {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->identity;
}
bool cursor_broker_endpoint::advance_document() noexcept {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->closing || GetCurrentThreadId() != impl_->engine_thread ||
      impl_->identity.documentGeneration == std::numeric_limits<std::uint64_t>::max() ||
      (impl_->document_request_pending && !impl_->document_request_armed)) return false;
  ++impl_->identity.documentGeneration;
  impl_->document_request_pending = false;
  impl_->document_request_armed = false;
  impl_->latest_valid = false;
  impl_->invalidate_shown = true;
  impl_->post_locked();
  return true;
}
bool cursor_broker_endpoint::request_document_change() noexcept {
  if (!impl_->on_ui()) return false;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->closing) return false;
  impl_->document_request_pending = true;
  impl_->document_request_armed = false;
  impl_->latest_valid = false;
  impl_->invalidate_shown = true;
  impl_->post_locked();
  return true;
}
bool cursor_broker_endpoint::arm_document_change() noexcept {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->closing || GetCurrentThreadId() != impl_->engine_thread ||
      !impl_->document_request_pending) return false;
  impl_->document_request_armed = true;
  return true;
}
bool cursor_broker_endpoint::input_allowed() const noexcept {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return !impl_->closing && !impl_->document_request_pending;
}
bool cursor_broker_endpoint::advance_geometry() noexcept {
  if (!impl_->on_ui()) return false;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->closing || impl_->identity.geometryGeneration == std::numeric_limits<std::uint64_t>::max()) return false;
  ++impl_->identity.geometryGeneration;
  impl_->latest_valid = false;
  impl_->invalidate_shown = true;
  impl_->post_locked();
  return true;
}
bool cursor_broker_endpoint::process_descriptor() noexcept {
  if (!impl_->on_ui() || impl_->applying) return false;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->wake_posted = false;
    if (impl_->closing) return false;
    if (impl_->invalidate_shown) {
      impl_->shown.kind = cursor_kind::standard; impl_->shown.type = 0;
      impl_->shown.width = impl_->shown.height = impl_->shown.stride = 0;
      impl_->shown.hotspot_x = impl_->shown.hotspot_y = 0;
      impl_->shown.pixels.clear(); impl_->shown_sequence = 0;
      impl_->pending_withdraw = true;
      impl_->invalidate_shown = false;
    }
    if (impl_->latest_valid) {
      const auto& value = impl_->latest;
      impl_->shown.kind = value.kind == WKE_CURSOR_BROKER_STANDARD ? cursor_kind::standard :
          value.kind == WKE_CURSOR_BROKER_NONE ? cursor_kind::none :
          value.kind == WKE_CURSOR_BROKER_CUSTOM_BGRA8 ? cursor_kind::custom : cursor_kind::unsupported;
      impl_->shown.type = value.cursorType;
      impl_->shown.width = value.width; impl_->shown.height = value.height; impl_->shown.stride = value.stride;
      impl_->shown.hotspot_x = value.hotspotX; impl_->shown.hotspot_y = value.hotspotY;
      impl_->shown.pixels.assign(impl_->latest_pixels.data(), impl_->latest_pixels.data() + value.pixelBytes);
      impl_->shown_sequence = value.publicationSequence;
      impl_->latest_valid = false;
    }
  }
  if (impl_->pending_withdraw) {
    if (!impl_->queue->queue.withdraw(impl_.get())) return false;
    impl_->pending_withdraw = false;
  }
  POINT point{};
  return GetCursorPos(&point) && WindowFromPoint(point) == impl_->owner ? select() : true;
}
bool cursor_broker_endpoint::select() noexcept {
  if (!impl_->on_ui() || impl_->applying || !impl_->window_matches()) return false;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->closing) return false;
    // A context callback can revoke the previous descriptor before the posted
    // wake is dispatched. WM_SETCURSOR must not reselect that stale bitmap.
    if (impl_->invalidate_shown) {
      impl_->shown.kind = cursor_kind::standard; impl_->shown.type = 0;
      impl_->shown.width = impl_->shown.height = impl_->shown.stride = 0;
      impl_->shown.hotspot_x = impl_->shown.hotspot_y = 0;
      impl_->shown.pixels.clear(); impl_->shown_sequence = 0;
      impl_->pending_withdraw = true;
      impl_->invalidate_shown = false;
    }
  }
  if (impl_->pending_withdraw) {
    if (!impl_->queue->queue.withdraw(impl_.get())) return false;
    impl_->pending_withdraw = false;
  }
  impl_->applying = true;
  // An initial/default descriptor uses a reserved local publication until the
  // renderer emits its first actual sequence. No custom handle is invented.
  const auto publication = impl_->shown_sequence ? impl_->shown_sequence : std::numeric_limits<std::uint64_t>::max();
  const auto result = impl_->queue->queue.apply(impl_.get(), publication, impl_->shown);
  impl_->applying = false;
  impl_->status = result.status;
  return result.selected || result.status == cursor_queue_status::deferred;
}
bool cursor_broker_endpoint::begin_close() noexcept {
  if (!impl_->on_ui()) return false;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->closing = true;
    impl_->latest_valid = false;
  }
  impl_->shown_sequence = 0;
  impl_->pending_withdraw = !impl_->queue->queue.withdraw(impl_.get());
  return true;
}
bool cursor_broker_endpoint::finish_close() noexcept {
  if (!impl_->on_ui() || impl_->scope_count || !impl_->queue->queue.withdraw(impl_.get())) return false;
  { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->finished = true; }
  return true;
}
unsigned cursor_broker_endpoint::native_scopes() const noexcept {
  return impl_->on_ui() ? impl_->queue->queue.native_scopes() : 1;
}
cursor_queue_status cursor_broker_endpoint::last_status() const noexcept { return impl_->status; }

native_cursor_scope::native_cursor_scope() noexcept {
  try { impl_ = obtain_queue(); }
  catch (...) { admitted_ = false; return; }
  sequence_ = impl_->queue.enter_scope(this);
  admitted_ = sequence_ != 0;
}
native_cursor_scope::~native_cursor_scope() {
  if (sequence_ && (!impl_ || !impl_->queue.leave_scope(this, sequence_))) std::terminate();
  if (sequence_ && !impl_->queue.native_scopes()) impl_->wake_all();
}

}  // namespace soui::sdk::detail
