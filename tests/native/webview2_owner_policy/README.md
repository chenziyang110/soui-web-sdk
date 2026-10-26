# WebView2 owner policy SOURCE

No build, test or WebView2 runtime was executed for this package.

`fixture.cpp` uses the actual `owner_output_policy.h`. Target
`soui_webview2_owner_policy_tests`, CTest `sdk.native.webview2-owner-policy`;
Root must register this subdirectory in the SDK's global native test entry.
It covers the existing16-output cap, default/invalid elapsed policy, minimum
one-output progress rule, exact raw+UTF16 cache bytes and overflow boundaries,
and strict bounded decimal configuration. It does not instantiate COM or prove
real owner-loop progress, queue delivery or performance.

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
