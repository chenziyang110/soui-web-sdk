#ifndef content_browser_InspectorKeyboardSnapshot_h
#define content_browser_InspectorKeyboardSnapshot_h

#include "third_party/WebKit/public/web/WebInputEvent.h"

// Private controlled Inspector ABI. No SDK/Win32 dependency or new language
// requirement; the event is Blink's existing fixed POD, with no runtime calls.
namespace content {

enum { InspectorControlledApiVersion = 3 };
enum InspectorKeyboardModifier {
    InspectorShift = 1, InspectorControl = 2, InspectorAlt = 4,
    InspectorCapsLock = 8, InspectorNumLock = 16, InspectorMeta = 32,
    InspectorKeyboardModifierMask = 63
};
struct InspectorKeyboardSnapshot {
    unsigned int modifiers;
    unsigned int keyData; // Original unsigned 32-bit Win32 lParam.
};

inline bool inspectorKeyboardRecordValid(unsigned int kind, unsigned int message,
    int modifiers, int reservedY, int reservedDelta)
{
    if (modifiers < 0 || (static_cast<unsigned int>(modifiers) & ~InspectorKeyboardModifierMask)
        || reservedY || reservedDelta)
        return false;
    switch (kind) {
    case 3: return message == 0x0100 || message == 0x0104; // KEYDOWN / SYSKEYDOWN
    case 4: return message == 0x0101 || message == 0x0105; // KEYUP / SYSKEYUP
    case 5: return message == 0x0102 || message == 0x0106; // CHAR / SYSCHAR
    default: return false;
    }
}
inline bool inspectorKeyboardIsSystem(unsigned int message)
{
    return message == 0x0104 || message == 0x0105 || message == 0x0106;
}
inline bool inspectorKeyboardIsRepeat(unsigned int message, unsigned int keyData)
{
    return (message == 0x0100 || message == 0x0104 || message == 0x0102 || message == 0x0106)
        && (keyData & 0x40000000U);
}
inline void applyInspectorKeyboardSnapshot(blink::WebKeyboardEvent& event,
    unsigned int message, const InspectorKeyboardSnapshot& snapshot)
{
    // Replace ambient modifier facts with the original UI event snapshot.
    const int mask = blink::WebInputEvent::ShiftKey | blink::WebInputEvent::ControlKey
        | blink::WebInputEvent::AltKey | blink::WebInputEvent::CapsLockOn
        | blink::WebInputEvent::NumLockOn | blink::WebInputEvent::MetaKey
        | blink::WebInputEvent::IsAutoRepeat;
    event.modifiers &= ~mask;
    if (snapshot.modifiers & InspectorShift) event.modifiers |= blink::WebInputEvent::ShiftKey;
    if (snapshot.modifiers & InspectorControl) event.modifiers |= blink::WebInputEvent::ControlKey;
    if (snapshot.modifiers & InspectorAlt) event.modifiers |= blink::WebInputEvent::AltKey;
    if (snapshot.modifiers & InspectorCapsLock) event.modifiers |= blink::WebInputEvent::CapsLockOn;
    if (snapshot.modifiers & InspectorNumLock) event.modifiers |= blink::WebInputEvent::NumLockOn;
    if (snapshot.modifiers & InspectorMeta) event.modifiers |= blink::WebInputEvent::MetaKey;
    if (inspectorKeyboardIsRepeat(message, snapshot.keyData)) event.modifiers |= blink::WebInputEvent::IsAutoRepeat;
    event.isSystemKey = inspectorKeyboardIsSystem(message);
    event.size = sizeof(blink::WebKeyboardEvent);
}

} // namespace content
#endif // content_browser_InspectorKeyboardSnapshot_h
