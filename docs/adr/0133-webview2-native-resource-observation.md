# 0133: Optional WebView2 native resource observation

Status: candidate; source, compilation and finite checks cannot establish D14.

The current WebView2 DevTools observation deliberately cannot report attachment,
pause or Inspector-window lifetime. Its completion counters also omit preparation
and policy events. They cannot provide a complete native resource sample.

Add an independent native-only scalar observation, explicitly enabled by a new
five-argument adapter constructor. Preserve the existing four-argument constructor
and default-disabled behavior. The getter grants no authority and performs no
COM call, message pump, process wait or blocking lock acquisition. It only reads
owner-STA counters and tries the existing runtime-exit callback mutex. Wrong
thread, busy, invalid identity, arithmetic fault and incomplete coverage remain
unavailable observations. Observation never permits teardown.

The view domain is SDK-held Controller references, including a successfully
closed Controller still retained for native completions. Count the actual two
Controller assignments and two explicit successful-close Reset paths. A failed
Close cannot credit a release. This does not enumerate Chromium pages, frames,
Inspector windows or renderer processes. A/B samples are distinct adapter epochs
and are summed once each by the native consumer.

The callback domain is the existing preparation pending/completion accounting,
native asynchronous completion accounting (including the native shortcut), all
nine installed SDK policy event bodies, and the exclusive runtime-exit callback
borrow. The policy scope covers even cancellation/denial handlers which do not
use the shared exception boundary. These disjoint counters are not all callbacks
inside WebView2, all Engine work or application Bridge handlers. Outstanding
preparation and async operations are pending; actual borrowed handlers are active.

Scalar Controller history and policy borrows latch faults on invalid transitions,
wrong-owner updates, underflow or overflow. Samples bind PID, process creation
FILETIME, UI thread, a process-local never-reused adapter epoch, and real monotonic
timestamps. Terminal readiness requires the original unmount to have actually
released its Controller and all declared callbacks to have settled. There is no
reset, recovery, hidden close, synthetic zero or new COM resource owner.

This changes delivered SDK headers and WebView2 code and therefore requires a new
preview.6 source/native package identity. The frozen preview.5 delivery and its
historical failures/results remain unchanged. Focused validation must cover real
counter transitions, retained closed references, wrong thread, busy/unknown,
fault arithmetic, and hook placement. Actual current binaries still need the
task VM, independently frozen real calibration, eight actual hours per backend,
and original owned-process closure before any production acceptance.
