# ADR 0121: Correlate WebView2 transport replies with one invocation

Status: Implemented locally; focused regression, x64 Release component compilation
and independent source review completed. Real Runtime integration remains unverified.

## Problem

The WebView2 preload removed a timed-out request from its pending map but allowed
the same document to reuse its requestId. A delayed response matched only
documentId/requestId and could settle the new invocation with the old result.
Native generation/requestId admission prevents simultaneous native duplicates;
it cannot identify the page Promise that a delayed response belongs to.

## Decision

Keep the public `window.__souiNativePostMessage(requestJSON)` contract and Bridge
request/response JSON unchanged. Change only the paired WebView2 transport:

- Request envelope has exactly `documentId`, `invocationId`, and `request`.
- Response envelope has `kind`, `documentId`, `requestId`, `invocationId`, and `raw`.
- Events retain their existing documentId/data envelope and do not settle requests.

The preload allocates a canonical decimal invocationId in 1..9007199254740991
for each admitted local send. It never resets or reuses that counter within the
document, including after timeout or synchronous post failure. Exhaustion rejects
new sends without wrapping; existing replies can still settle their exact waits.
Only currently pending entries are stored, so no document-lifetime ID set grows.
Native code validates the canonical 1..16-byte value and echoes it through normal,
timeout, unauthorized, exception, duplicate, busy and oversized-response paths.
The preload requires exact documentId/requestId/invocationId equality before
releasing a wait. There is no ID-only fallback.

This identifier is correlation, not authorization, a native gesture, or a bearer
credential. Existing native generation/session checks, generation/requestId
duplicate rejection, admission priorities and capacity remain unchanged. Reusing
a completed or locally timed-out requestId is still allowed; native admission
may reject a retry whose original native lease is still outstanding.

## Compatibility and bounds

Native code and the generated embedded preload must be rebuilt and delivered as
a pair. The old native receiver rejects the new three-field envelope. The new
receiver rejects the old two-field envelope, and the new preload ignores an old
response without invocationId. Mixed versions are unsupported and fail closed;
do not add a compatibility mode that restores ID-only matching. Normal clients
using the public JSON-string entry do not add or inspect this internal field.

Business request/response message caps, pending lane limits (128/128/1), native
capacity, nesting limit, original 1..60000 ms timeouts and default 10000 ms are
unchanged. Timeout still releases only the local pending slot and does not cancel
native work. The identifier adds at most 16 decimal bytes to internal metadata;
no existing size cap is widened. No request body, credentials or new diagnostic
data are logged.

## Verification scope

The existing `tests/web/webview2-preload.test.mjs` fake WebView2 and fake timers
provide a regression: first call times out, a second call reuses the same ID,
and the old response must neither settle nor release the second call. The
original implementation failed that assertion in a real Node run. Additional
cases cover exact metadata matching, legitimate ID reuse and counter exhaustion.
The exhaustion fixture changes only the counter's initial scalar in its isolated
VM to reach the real boundary branch; production always starts at one.

Run only `node --test tests/web/webview2-preload.test.mjs` for this change. Source
identities, actual command, exit and logs are retained under
`out/devtools-stage0/webview2-invocation-correlation/`. These tests do not prove C++
compilation, COM delivery, prototype integration or GUI/Stage0B runtime acceptance.
The old implementation failed the targeted regression (8 pass, 1 fail); the
changed preload passed all 12 affected cases. An independent source review
checked native response branches, bounds and ownership; it found no determinate
blocker. The x64 Release `soui_webview2` component compiled and archived in
4.391 seconds with unchanged nine scoped inputs. This intentionally skipped
project-reference builds and does not establish a full SDK link or Runtime test.
The embedded literal matches the actual preload exactly after only the measured
CMake LF-to-CRLF conversion is normalized; both raw identities are retained.
The first raw-byte precheck failure remains recorded. No unrelated test, GUI or
VM run was repeated. The existing cancelled/late Controller cleanup is outside
this change.
