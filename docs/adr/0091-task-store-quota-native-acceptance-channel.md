# ADR 0091: Same-Bridge Native TaskStore quota sampling channel

Status: Proposed, SOURCE_ONLY. No Native/C++/SDK/GUI/100reload actor or calibration
has run for this Source. The new production policy API comes from the separately
sealed Source16 and its genuine Win reviewer, not this author's self-approval.

The acceptance application already samples actual native owners synchronously
through each live test_window's Bridge. Add one independent, versioned
`taskStoreRequestQuota` record on that same existing call. It calls
`bridge_.read_task_store_request_quota` exactly once. It does not construct a
second Bridge/Store/owner, change the existing constructor's nullopt policy,
register a page method, start a sampler thread or wait for a busy Store.

The Native caller obtains actual process ID/creation FILETIME, current thread,
actual HWND value and the window's owner process/thread using Windows APIs.
It checks the window and caller identities before and after the getter.
`QueryPerformanceCounter(begin)` precedes the getter and the actual QPC end
follows it. Frequency/ticks are stored as exact decimal strings. QPC failure or
unsupported/invalid clock sets captureState invalid and clock.valid false with
null timestamps/frequency; it is not a successful zero observation. JSON
allocation/serialization happens after this independent read envelope.

Pool epoch comes from the real quota pool and is separate from the earlier
Service diagnostics epoch. Numeric equality of the two independent identities
does not imply a shared ledger. Diagnostics may be disabled/busy; its copied
metadata remains separate, and its microsecond end timestamp is never reused
as the quota capture end. New QPC ticks are compared only with the existing
actual caller QPC clock, including its frequency and thread identity.

The payload schema is `soui.acceptance-task-store-request-quota/1`, scope
`task-store-private-request-byte-quota`. It preserves the actual getter/state,
immutable null/zero/finite cap, charged/successful live/peak, quota refusal count,
exactness and credit/identity validity. The old storage serializers, schema1,
configuration/budgets/quota/calibrationPin nulls and not-evaluated cache gate are
entirely unchanged. The old long-run owner reader is copied byte exactly and is
not imported or redefined by the new independent subreader.

The read-only entry accepts the original raw binary LF log and an exact caller
binding. It independently hashes original bytes, bounds UTF-8/JSON decoding,
rejects duplicate fields and validates canonical uint64 strings. Complete
source/payload/executable/build/raw/Owned/context/subject binding is required;
the trusted Root caller must actually verify those pointers before setting
inputsMatched true. A pure fixture that supplies those fields is only a model.
This subreader does not execute the SDK or replace the complete acceptance,
native business, original natural capture, renderer, owner/drain or source gates.

The narrow reader admits the current known reload producer's original complete
run sequence: one process/mounted/closed/runtime-closed sample, ordered reload
steps and one sample per declared cycle including warmup, one actual owner
window, and the real pre-destruction close sample. It binds each observation to
the same HWND/subject/thread/frequency/configuration/pool within that owner
lifetime, with monotonically advancing caller QPC, successful/charged peaks and
denials. Live totals may fall after real frees. It never equates a new empty
owner with the old pool or a replayed close copy with a fresh capture.

Unknown/malformed, status/identity/clock/configuration mismatch, credit invalid,
pool exhaustion or inexact refusal counter yield invalid, and remain sticky.
An inexact refusal count must still be UINT64_MAX; enforcement may remain
valid but observation is invalid. Cleared busy/unavailable or a missing new
record is partial and cannot imply an unconfigured available pool. A later
successful point cannot erase an earlier definite failure. Native run/step/
cleanup failures override otherwise valid quota data.

An available unconfigured/zero/finite snapshot can be sampled, including decimal
values above JavaScript's exact integer range. It cannot qualify a production
default. productionDefaultGate and fullCacheBudgetGate stay not-evaluated,
calibrationPin stays null, qualified/releaseReady stay false. A 100reload sample
does not itself exercise max legal input/32inflight/1024history or calibrate
headroom. JSON/Service queues/Agent/Engine/GUI/heap/kernel/other caches are outside
this private Store requested-byte channel.

The close snapshot is captured after Host close and before window.reset while
Bridge/Store are still alive. It is not a post-destruction refund or final
disposal receipt. Existing natural destructor/worker join remains unchanged.
Root will combine the two admitted Source slices in a fresh SDK, bind a fresh
MF/producer/installed SDK and real acceptance executable/inputs, then obtain
actual original captures and independent review before using measured data.

Revision 2: independently count one total mount and one close on the known single-owner reload producer; close requires all declared reload steps. Samples must follow process -> mounted -> cycle* -> closed -> runtime-closed, and completion requires the last phase. Missing observations stay partial; extra owner lifetimes or reversed phases stay invalid. Two original serialized reader counterexamples are retained and are accepted by the original reader, rejected by this revision. Native main/header and old owner reader are byte exact.

Revision 3 additionally binds actual producer cycle labels: mount/process/mounted are zero; close/closed/runtime-closed are declared total plus one. Numeric safe integer form is mandatory. A structurally complete transcript with wrong terminal cycles is invalid. Original wrong-cycle raw models were accepted by Source1/2 and are rejected by revision3; models do not prove Native execution.
