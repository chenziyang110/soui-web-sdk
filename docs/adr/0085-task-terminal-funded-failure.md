# 0085: Task service terminal commit under private Store quota

Status: Source candidate. No compiler/native/SDK/Owned/GUI/VM actor ran for this
revision. The prior RequestQuota Store tests have their own real x86/x64 record;
that record does not validate these changed terminal paths.

The service accepts an explicit optional TaskStore requested-byte quota. Existing
constructors delegate to nullopt. Bridge, Engine, permission, queue, service
maps and result-cache string allocators receive no quota/default here. No
production limit is selected, and this is not a full heap/memory budget.

Every service admission prepares an actual same-Store-allocation-owner failed
terminal before publishing the task. Two retained message buffers request a
fixed capacity floor of 64 characters for the 47-character constant error, plus
the actual task ID in the retained result. Supported STL physical requested
bytes can exceed this logical floor; only real allocator requests are charged.
There is no separate uncharged promise or phase credit. Candidate map copies
deep-copy their owned buffers and are concurrently charged; moves/swaps do not
refund them. Fixed inflight 32 and Store retention 1024 bound admission count,
but admission may reject any insufficient quota. These constants are not a
calibrated production quota. A buffer's credit is returned only after its actual
deallocation, including old buffers moved into the retiring fallback object.

Normal terminal preparation holds the existing Store lock and constructs
message/result candidates without changing the live snapshot. Commit uses
only same-owner swaps and scalar writes. Service holds its own lock throughout
Store prepare/commit and the result-cache/active/inflight commit. Thus a public
service observer sees the previous coherent task or the committed coherent
terminal. Result assembly/projection occurs before those locks, preserving
the existing reentrant projection contract.

If normal private preparation rejects quota or allocation, the unwound
candidate refunds its actual allocations, and a single no-allocation fallback
swaps admission's funded record into failed state. The service then swaps a
prepared valid result into its cache, records error/outcome metadata, and
releases active/inflight in that same service lock envelope. If result assembly
itself failed, the preexisting safe cache is retained, with projection error;
there is no second result-allocation attempt. Unexpected worker setup or
mark-running exceptions also use the funded path. No forced exit, fake
cancellation, repeated allocation retry or diagnostic-based permission exists.

Fallback Store state/result.kind are failure; original handler outcome remains
separate. `terminalStorageError` distinguishes `E_TASK_STORE_QUOTA` from
`E_TERMINAL_ALLOCATION`; `handlerOutcome` reports the actual returned kind,
not-executed or unknown; `handlerError` identifies a thrown handler and
`outcomeError` identifies rejected/nonterminal result normalization. A confirmed
cancelled handler may therefore have failed terminal publication, with the
confirmed kind separately retained. Successful work is never replayed because
publication failed. A valid prepared JSON result is preserved on Store failure.
Local-family result projection failure continues to keep its actual handler
state and `E_RESULT_PROJECTION`, as the existing public tests require; the
new optional error fields are included in the typed family query contract.

The active owner must have its admission-funded record. If that invariant is
violated, the code asserts and retains active/inflight instead of falsely
publishing success. Normal allocation/quota faults cannot remove that record.
Mutex/allocator deallocation failure and process-wide corruption are not a
recoverable allocation policy claim.

Proposed native tests exercise the real candidate Store and task service:
each actual private candidate allocation is failed; quota is derived from a
fresh actual admission/cancellation peak, not a guessed production constant;
failed prepare preserves snapshot/sequence/funding/credit; fallback attempts
zero new private allocations, keeps result invariants and retires all buffers;
successful/confirmed-cancelled handler results remain distinguishable; duplicate
operationId does not rerun; a thrown handler plus failed storage retains both
error categories with unknown returned outcome; service inflight, owner, cache
and Store agree; final
real quota retirement is valid with zero live bytes in disabled/scalar modes.
The fault target explicitly links result.cpp to avoid the known non-inline
operation_result constructor omission. Those native tests remain unexecuted.

Store/full cacheBudget subgates, production quota/calibration, full SDK
compatibility, performance, qualification and release remain not-evaluated/false.
