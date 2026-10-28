#include <windows.h>
#include "sdk/src/host/devtools/kernel_abi.h"
#include "third_party/sources/miniblink/content/browser/InspectorKeyboardSnapshot.h"
#include <iostream>
#include <stdexcept>

// Fixed-record checks only; real UI/paused delivery awaits unified validation.
namespace {
using namespace soui::sdk::detail;
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
bool valid(const inspector_input& input) {
  return content::inspectorKeyboardRecordValid(input.kind, input.message, input.x, input.y, input.delta);
}
unsigned WKE_CALL_TYPE open_stub(wkeCursorBrokerEndpoint, const wkeCursorBrokerIdentity*,
    wkeCursorBrokerEndpoint, const wkeCursorBrokerIdentity*, unsigned __int64*) { return 0; }
unsigned WKE_CALL_TYPE snapshot_stub(unsigned __int64, inspector_snapshot*) { return 0; }
unsigned WKE_CALL_TYPE input_stub(unsigned __int64, const inspector_input*) { return 0; }
unsigned WKE_CALL_TYPE command_stub(unsigned __int64, unsigned) { return 0; }
unsigned WKE_CALL_TYPE session_stub(unsigned __int64) { return 0; }
unsigned WKE_CALL_TYPE resources_stub(wkeCursorBrokerRealm, const wkeCursorBrokerEndpointConfig*,
    const Stage0SnapshotConfig*, wkeCursorBrokerEndpointReceipt*, Stage0SnapshotReceipt*) { return 0; }
}

int main() {
  try {
    static_assert(sizeof(inspector_input) == 32, "fixed wire layout");
    static_assert(inspector_abi_version == content::InspectorControlledApiVersion, "controlled ABI parity");
    static_assert(inspector_shift == static_cast<unsigned>(content::InspectorShift) &&
        inspector_control == static_cast<unsigned>(content::InspectorControl) &&
        inspector_alt == static_cast<unsigned>(content::InspectorAlt) &&
        inspector_caps_lock == static_cast<unsigned>(content::InspectorCapsLock) &&
        inspector_num_lock == static_cast<unsigned>(content::InspectorNumLock) &&
        inspector_meta == static_cast<unsigned>(content::InspectorMeta), "modifier parity");
    for (unsigned modifiers = 0; modifiers <= 63; ++modifiers) {
      const auto event = make_inspector_keyboard_input(WM_KEYDOWN, 'P', 0x40190001U, modifiers);
      require(valid(event) && event.x == static_cast<int>(modifiers), "all modifier combinations retained");
      require(event.key == 'P' && event.flags == 0x40190001U, "scan/repeat data retained");
    }
    const auto extended = make_inspector_keyboard_input(WM_KEYDOWN, VK_RETURN, 0x011c0001U, inspector_control);
    require(valid(extended) && (HIWORD(extended.flags) & KF_EXTENDED), "extended keypad bit retained");
    require(!content::inspectorKeyboardIsRepeat(extended.message, extended.flags), "initial press is not repeat");
    const auto repeated = make_inspector_keyboard_input(WM_SYSKEYDOWN, VK_F10, 0x603e0002U, inspector_alt);
    require(valid(repeated) && content::inspectorKeyboardIsSystem(repeated.message) &&
        content::inspectorKeyboardIsRepeat(repeated.message, repeated.flags), "system repeat retained");
    const auto released = make_inspector_keyboard_input(WM_SYSKEYUP, VK_F10, 0xe03e0001U, 0);
    require(valid(released) && content::inspectorKeyboardIsSystem(released.message) &&
        !content::inspectorKeyboardIsRepeat(released.message, released.flags), "keyup is not autorepeat");
    const auto syschar = make_inspector_keyboard_input(WM_SYSCHAR, 'x', 0x20380001U, inspector_alt);
    require(valid(syschar) && syschar.kind == inspector_key_press &&
        content::inspectorKeyboardIsSystem(syschar.message), "system character message retained");
    const auto ime = make_inspector_keyboard_input(WM_CHAR, 0x4e2d, 0, 0);
    require(valid(ime) && ime.key == 0x4e2d, "committed UTF-16 code unit retained");
    // Use the actual production mapper and Blink POD, without a browser.
    blink::WebKeyboardEvent event;
    content::InspectorKeyboardSnapshot captured{inspector_control | inspector_shift, 0x40190001U};
    content::applyInspectorKeyboardSnapshot(event, WM_KEYDOWN, captured);
    require((event.modifiers & blink::WebInputEvent::ControlKey) &&
        (event.modifiers & blink::WebInputEvent::ShiftKey) &&
        (event.modifiers & blink::WebInputEvent::IsAutoRepeat), "UI snapshot survives absent ambient Engine modifiers");
    require(event.size == sizeof(blink::WebKeyboardEvent) && !event.isSystemKey,
        "controlled keyboard metadata has actual event size and message kind");
    captured = {inspector_alt | inspector_caps_lock | inspector_num_lock | inspector_meta, 0xe03e0001U};
    content::applyInspectorKeyboardSnapshot(event, WM_SYSKEYUP, captured);
    require(!(event.modifiers & (blink::WebInputEvent::ControlKey | blink::WebInputEvent::ShiftKey |
            blink::WebInputEvent::IsAutoRepeat)) && event.isSystemKey &&
        (event.modifiers & blink::WebInputEvent::AltKey) &&
        (event.modifiers & blink::WebInputEvent::CapsLockOn) &&
        (event.modifiers & blink::WebInputEvent::NumLockOn) &&
        (event.modifiers & blink::WebInputEvent::MetaKey), "snapshot replaces opposite ambient state, including toggles and Meta");
    captured = {0, 0};
    content::applyInspectorKeyboardSnapshot(event, WM_CHAR, captured);
    require(!event.modifiers && !event.isSystemKey, "released UI modifiers cannot remain stuck in Engine state");
    require(!make_inspector_keyboard_input(WM_DEADCHAR, '^', 0, 0).kind &&
        !make_inspector_keyboard_input(WM_SYSDEADCHAR, '^', 0, 0).kind, "dead keys are not committed characters");
    auto bad = ime;
    bad.x = 64; require(!valid(bad), "unknown modifier rejected");
    bad.x = -1; require(!valid(bad), "negative snapshot rejected");
    bad = ime; bad.y = 1; require(!valid(bad), "reserved y rejected");
    bad = ime; bad.delta = 1; require(!valid(bad), "reserved delta rejected");
    bad = ime; bad.message = WM_KEYDOWN; require(!valid(bad), "kind/message mismatch rejected");
    bad = ime; bad.message = WM_DEADCHAR; require(!valid(bad), "dead character rejected at producer boundary");
    require(!inspector_input_requires_failure(inspector_ok) &&
        !inspector_input_requires_failure(inspector_closing), "accepted/closing are nonfault paths");
    require(inspector_input_requires_failure(inspector_busy) &&
        inspector_input_requires_failure(inspector_limit) &&
        inspector_input_requires_failure(inspector_failed), "rejected discrete input cannot silently disappear");
    inspector_api api{sizeof(inspector_api), inspector_abi_version, open_stub, snapshot_stub,
        input_stub, command_stub, session_stub, session_stub, session_stub, resources_stub};
    require(inspector_api_supported(&api), "full current API accepted");
    api.abiVersion = 2; require(!inspector_api_supported(&api), "old ABI rejected instead of losing snapshots");
    api.abiVersion = inspector_abi_version; api.input = nullptr;
    require(!inspector_api_supported(&api) && !inspector_api_supported(nullptr), "missing input contract rejected");
    std::cout << "Controlled Inspector keyboard record checks passed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
