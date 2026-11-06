# WebView2 owner policy SOURCE

No build, test or WebView2 runtime was executed for this package.

ADR-0167 adds `owner_output_wake.h` and ten new source contracts, directly
using the production lazy `owner_output_drain_scope`. These additions have not
yet been built or executed. All existing budget/callout assertions remain.
The scope takes `(bool& active, std::mutex&, Queue&, noexcept notify)` by C++17
CTAD, and `enter_locked()` is called only in the original pop critical section.
An unused scope does not lock; a participating scope clears active and checks
the same queue under that mutex on all exits, notifying once if items remain.
`entered()` lets the caller preserve the old normal empty-pump tail check for
an unused scope; it replaces that tail with RAII only after actual output work.
Navigation/stop never owns or resets the active bit; non-output notifications
remain independent.

The new cases cover full same-slice consumption, budget remainder, actual
producer-thread enqueue before/after scope exit, exception exit, rejected
reentry preserving the outer guard, navigation clear/refill, stop clearing,
and independent lifecycle notification. Thread ordering uses a promise and join,
not sleeps; the unchanged CTest timeout is 15 seconds. The empty-scope case
holds the actual mutex while constructing/destroying the lazy scope, so an
unexpected lock fails by the existing bounded timeout. They test notification
responsibility, not real PostMessage/COM delivery, OS wake counts, whole owner
fairness, or application performance. The producer-side `!active` gate is the
caller boundary; the production Host wiring is reviewed separately.

The tenth case uses a real producer thread after an empty first pop and a
notification dependency seam that fails its first attempt. The unchanged
normal tail, selected through the production `entered()` accessor, retries
once and recovers that simulated transient failure. This is source coverage,
not an executed Win32 test or a guarantee for arbitrary PostMessage failures.

`fixture.cpp` uses the actual `owner_output_policy.h`. Target
`soui_webview2_owner_policy_tests`, CTest `sdk.native.webview2-owner-policy`;
Root must register this subdirectory in the SDK's global native test entry.
It covers the existing16-output cap, default/invalid elapsed policy, minimum
one-output progress rule, exact raw+UTF16 cache bytes and overflow boundaries,
and strict bounded decimal configuration. It does not instantiate COM or prove
real owner-loop progress, queue delivery or performance.

The extracted actual `owner_callout_policy.h` additionally has fake-callout
SOURCE for get_Source reentering close/navigation, keeping the selected target
alive and preserving new-document diagnostic flags. The same owner-pump guard
rejects nested drain entry, so opaque COM reentry cannot multiply the16-item
batch or deliver later output ahead of the active call. These are dependency
fakes, with no real COM execution.

Candidates remain off by default:

- `SOUI_WEBVIEW2_OWNER_BUDGET_US=100..5000`: optional elapsed deadline for the
  output batch, with the existing16-item cap and first output opportunity.
- `SOUI_WEBVIEW2_PREPARE_OUTPUT=1`: pure JSON/UTF16 preparation outside the
  output mutex; all WebView2 COM delivery remains on its actual owner STA.

Raw and prepared representation together must fit the unchanged64KiB payload
credit for that item and its existing lane budget. If caching cannot fit or
preparation fails, the same shared serializer remains on the legacy owner path.
Generation/document/source checks and authenticated request/lane selection are
preserved. Prepared wire data is immutable and bound to its original document;
stale output is rejected again before owner delivery. Owner COM targets stay
pinned through calls, and reentrant navigation/close cannot mark a new document
application-ready from an old response.

The specialist must record the actual runtime version and compare default,
elapsed-only, preparation-only and combined settings on identical workloads.
Check event/response JSON equivalence, stale navigation/close, OOM fallback,
byte/lane saturation, UI owner reentry, and queue/serialization/owner QPC costs.
No profile currently selects either candidate, and the browser's internal
input/render scheduling remains opaque to this owner-loop measurement.
