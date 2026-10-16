# ADR 0089: Native Bridge TaskStore request policy and independent snapshot

Status: Proposed, SOURCE_ONLY. C++ is not compiled or executed. Production
default/calibration, full cache budget, compatibility and release remain unclosed.

The current SDK has one real quota pool inside each TaskStore, and Service can
construct that Store with an immutable optional requested-byte limit. Native
json_bridge construction did not forward the policy. Admission preparation also
collapsed pool refusals into E_NATIVE_FAILURE. Optional diagnostics cannot be
used to infer either the policy or enforcement when disabled.

Add one Native constructor overload `(origin, diagnostics_mode, optional_policy)`.
It forwards by value into the existing task_service and its existing Store.
Existing constructors delegate to nullopt. The optional value is retained const
by the same pool. There is no page method, runtime setter, second Store or new
production thread. Bridge private fields and Service PIMPL layout stay unchanged.
The public policy and exception keep their original namespace/name/shape in a
small public header. Extending the private Store owner changes its internal
representation and requires a new full SDK/consumer build; binary compatibility
with an old compiled Store object is not asserted.

The separate Native snapshot contains only primitives and optional uint64. It
does not allocate, sample a clock, emit JSON, or reuse/upgrade an old storage DTO.
Store try-lock then copies its actual pool. Service takes its existing Service
try-lock before Store; Bridge directly forwards as its existing storage readers
do. Disabled diagnostics still permit this independent read. Busy/unavailable
clears every field and is never evidence of an unconfigured available pool.

`max_live_requested_bytes` distinguishes null (unconfigured), zero and finite.
The charged live/peak fields include reservations, old storage, candidates,
funded failure buffers and retiring storage. Successful live/peak fields change
only at reservation.commit after the actual allocator succeeds. Canceling a
failed reservation changes charged credit only. Physical deallocate precedes
successful and charged refund; nullptr/0 empty cleanup changes neither. A failed
bottom allocation can therefore increase charged peak without successful peak.
The original nested allocator, Store transactions and funded terminal path are
preserved. This is requested storage accounting, not heap reserve or logical
string size, and not all SDK owners.

Quota refusal increments the independent saturating denial count. Length
overflow and upstream allocation/hook failures are not quota denials. Credit
overflow/underflow invalidates credit and refuses subsequent acquisition.
Counter saturation makes observation explicitly invalid while retaining quota
enforcement. A process/module-local pool epoch is generated without allocation;
its saturating CAS never wraps or reuses a valid epoch. Exhaustion returns zero
with owner_epoch_valid false and invalid observation, without disabling quota.
Epoch does not replace the actual caller PID+FILETIME, monotonic sample envelope,
SDK/module identity and owner lifetime required by future production receipts.

Admission catches the existing quota exception before generic preparation
exceptions and returns E_TASK_STORE_QUOTA, consistently with terminal storage
quota errors. Prepared candidate destruction returns credit before the response.
No Store commit, operation/accepted task ID, queue, inflight or handler follows a
quota refusal. Cold admission may already have created idle workers; that
existing order is preserved. Actual destruction must naturally join them and
the caller must never read a destroyed Bridge/Store for a disposal receipt.
Unrelated error serialization/heap failures retain their original error path;
they do not become quota or calibration evidence.

Source checks reverse the exact catch and compare unchanged full transaction,
worker, Bridge wire/registration, nested allocator and legacy ledger bodies.
The independent pure sample reader rejects mismatched/null/zero policy confusion,
invalid/busy/saturated identity or credit, wrong schema and noncanonical uint64.
Its samples are explicit models. No Native receipt exists in this delivery.

Native regression source links the newly adopted production Soui::Sdk only.
It contains actual public Bridge calls, disabled/scalar reads, runtime-measured
fixture cap/cap-minus-one, owner isolation, rejection/operation/handler checks,
manual lawful reuse, cold-worker counters, no-allocation reads, genuine busy
envelopes and upstream allocation failure distinctions. Its primitive controls
are separately labeled and cannot prove production Bridge allocation behavior.
Root must compile/run both architectures under the exact current profile and
bind the actual SDK objects/archive/MAP; no copied Store/service target can
substitute for the production Bridge proof. Existing funded-terminal nth-fault
tests must be rerun against the new same-owner accounting; their old pass is
not new evidence. Native typed/GUI borrow scenarios remain explicit additional
coverage in the regression contract before production default calibration.

Default public construction remains nullopt. No finite production number or
calibration pin is chosen from fixture values, SSO, reserve/capacity or old peaks.
New tuple-specific full100reload/max-legal-input/32inflight/1024history/retirement
funding peaks, frozen headroom, default opt-in, 8h and required compatibility
work must precede production-default/full-cache/release closure.
