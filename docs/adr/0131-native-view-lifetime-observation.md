# 0131: Native DLL view lifetime observation

Status: candidate; implementation and focused checks do not establish D13/D14 acceptance.

The current renderer cache counts `LayerTreeHost` compositors. SDK mounted-host
counts also omit the independent Inspector view. Neither is a WKE view count.

Add an independent, optional native getter for one loaded DLL's `CWebView`
lifetimes. Record creation only after successful insertion into `g_liveWebViews`,
and destruction after the base destructor's `WebPage::close()` returns. This
includes `CWebWindow` subclasses and controlled business/Inspector views. It does
not count every internal Blink page, frame, compositor, callback or OS process.

The existing DLL cache lock serializes fixed scalar updates and a try-shared-lock
getter. The getter only copies cached values, never visits a view, pumps messages,
invokes Engine/JS, changes permissions or touches lifecycle. Writers hold the lock
only across scalar operations. The table export is separate from existing WKE
tables, so their ABI and exact capability checks remain unchanged. No reset,
enumeration, raw pointer, HWND, command, or destruction API is added.

Bind samples to PID, process creation FILETIME, engine owner, QPC frequency and
the first initialization QPC epoch. Counts and mutation sequence never decrease;
overflow, underflow, wrong owner, invalid clock, pre-init/post-finalize mutation
or epoch replacement latch unavailable. `busy`, `not_started` and absent getter
remain unknown observations. Finalization publishes `closed` only with zero
live views; the SDK retains that exact terminal sample after actual runtime
retirement. Loaded module path matching is only one identity component; the run
manifest must independently bind the loaded DLL bytes.

Affected objects are the DLL lifetime cache, `CWebView` construction/destruction,
SDK optional observation binding and native acceptance consumer. Normal view
ownership and failed-close retention stay with their original owners. A failed
getter never authorizes cleanup or allows a gate to substitute zero. Recovery is
a separate process/candidate run, not a counter reset in the current epoch.

Focused validation covers actual hook placement, ABI parity, arithmetic faults,
identity/epoch/sequence regressions, busy and terminal retention. Runtime coverage
still requires current paired binaries in the task VM, real cycles, independently
frozen thresholds and original owned-process closure. Child-process ownership and
callback domains require their own receipts; this view getter cannot supply them.

For existing DevTools callback observations, expose explicitly scoped counters:
session dispatch/interrupt/protocol queue, Inspector resource active/queue, and
SDK frontend paint. Sum pending protocol, interrupt and resource queues with
checked arithmetic. These are session counters, not all process callbacks. Keep
authority/drain decisions in the original broker/session owners.
