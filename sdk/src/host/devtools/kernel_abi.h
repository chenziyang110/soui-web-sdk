#pragma once

// SDK-private mirror of the controlled inspector ABI in the matching kernel's
// wke/CursorDescriptorBroker.cpp. Controlled Inspector ABI 3; the separate
// frontend resource snapshot contract remains ABI 2. The portable kernel does
// not include SDK headers. Keep layout/export unchanged.
#ifndef ENABLE_WKE
#define ENABLE_WKE 0
#endif
#include "../../../../third_party/include/miniblink/wke.h"
#include "snapshot_callback_abi.h"

namespace soui::sdk::detail {
inline constexpr unsigned inspector_abi_version = 3;
#pragma pack(push, 8)
enum inspector_status : unsigned { inspector_ok, inspector_invalid, inspector_wrong_thread,
  inspector_stale, inspector_busy, inspector_closing, inspector_failed, inspector_limit };
enum inspector_input_kind : unsigned { inspector_mouse = 1, inspector_wheel,
  inspector_key_down, inspector_key_up, inspector_key_press, inspector_resize, inspector_focus };
struct inspector_input {
  unsigned structSize, kind, message, flags;
  int x, y, delta, key;
};
// Keyboard-only wire fields: message is the original WM_(SYS)KEY*/CHAR,
// flags is the original 32-bit lParam, x is this six-bit UI snapshot, and
// y/delta are zero. Mouse/wheel/resize fields retain their existing meaning.
enum inspector_keyboard_modifier : unsigned {
  inspector_shift = 1, inspector_control = 2, inspector_alt = 4,
  inspector_caps_lock = 8, inspector_num_lock = 16, inspector_meta = 32
};
inline inspector_input make_inspector_keyboard_input(unsigned message,
    unsigned key, unsigned key_data, unsigned modifiers) noexcept {
  inspector_input value{};
  value.structSize = sizeof(value);
  value.message = message;
  value.flags = key_data;
  value.x = static_cast<int>(modifiers);
  value.key = static_cast<int>(key);
  switch (message) {
    case WM_KEYDOWN: case WM_SYSKEYDOWN: value.kind = inspector_key_down; break;
    case WM_KEYUP: case WM_SYSKEYUP: value.kind = inspector_key_up; break;
    case WM_CHAR: case WM_SYSCHAR: value.kind = inspector_key_press; break;
  }
  return value;
}
inline bool inspector_input_requires_failure(unsigned status) noexcept {
  return status != inspector_ok && status != inspector_closing;
}
struct inspector_snapshot {
  unsigned structSize, abiVersion;
  unsigned __int64 session;
  unsigned attached, closing, detached, failed;
  unsigned pauseDepth, dispatchDepth, queued, pinsHeld, pendingCallbacks;
  unsigned __int64 pauseEntries, pauseExits, microtaskCheckpoints;
  unsigned __int64 posted, delivered, discarded, closeRequests;
  unsigned resourceActive, resourceQueued, resourceClosing, resourceDispatcherAlive;
  unsigned __int64 resourceHits, resourceDenied, resourceBytes, resourceManagerEscapes, resourceUnsupported;
};
struct inspector_api {
  unsigned structSize, abiVersion;
  unsigned (WKE_CALL_TYPE* open)(wkeCursorBrokerEndpoint, const wkeCursorBrokerIdentity*,
    wkeCursorBrokerEndpoint, const wkeCursorBrokerIdentity*, unsigned __int64*);
  unsigned (WKE_CALL_TYPE* snapshot)(unsigned __int64, inspector_snapshot*);
  unsigned (WKE_CALL_TYPE* input)(unsigned __int64, const inspector_input*);
  unsigned (WKE_CALL_TYPE* command)(unsigned __int64, unsigned);
  unsigned (WKE_CALL_TYPE* requestClose)(unsigned __int64);
  unsigned (WKE_CALL_TYPE* detach)(unsigned __int64);
  unsigned (WKE_CALL_TYPE* release)(unsigned __int64);
  unsigned (WKE_CALL_TYPE* createFrontendResources)(wkeCursorBrokerRealm,
    const wkeCursorBrokerEndpointConfig*, const Stage0SnapshotConfig*,
    wkeCursorBrokerEndpointReceipt*, Stage0SnapshotReceipt*);
};
using get_inspector_api = const inspector_api* (WKE_CALL_TYPE*)(unsigned);
#pragma pack(pop)
inline bool inspector_api_supported(const inspector_api* api) noexcept {
  return api && api->structSize == sizeof(*api) && api->abiVersion == inspector_abi_version &&
      api->open && api->snapshot && api->input && api->command && api->requestClose &&
      api->detach && api->release && api->createFrontendResources;
}
static_assert(sizeof(inspector_input) == 32);
static_assert(sizeof(inspector_snapshot) == 168);
static_assert(sizeof(inspector_api) == (sizeof(void*) == 8 ? 72 : 40));
} // namespace soui::sdk::detail
