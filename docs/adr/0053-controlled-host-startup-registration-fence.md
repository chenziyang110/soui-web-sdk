# ADR 0053: Controlled Host startup confirms real OLE registration before navigation

Status: implemented in the normal SDK; finite x86/x64 public consumer observations exist. SDK and consumers require a coordinated rebuild. Full qualification remains open.

## Observed problem and bounded evidence

The unchanged public Host can return a successful start before its owner thread has dispatched the controlled endpoint's queued OLE_REGISTER task. The isolated real public-Host replay recorded start success, no caller owner pump, OLE0 and queued2, then genuine Host/owner/Bridge/runtime cleanup. Its default return2 and generic-false result remain a failed startup regression. That observation does not establish the cause of every earlier frontend failure.

The separate actual prime probe recorded public startfalse/E_STARTUP_PRIME_PROBE/sessionnull in both cases. Before owner dispatch, the real controlled tuple was1/1/1, OLE0 and queued1. After one verified real WM_TIMER DispatchMessage, the actual kind3/task1/scope0 completion was uncancelled and matched the endpoint/tuple; a subsequent real engine inspect reported OK, OLE1 and zero queued/running/scopes/callbacks. Both probes naturally retired their true owners. Their configure/build compiler-tail partials remain genericfalse. The diagnostic seam is evidence, not the implementation of normal start.

## Decision and ordering

The normal controlled SDK Host uses three phases, preserving the existing public lifecycle_operation_ guard:

1. Prime on the real shared engine: create/bind the actual cursor endpoint, real WebView and interaction endpoint, with the true child hidden. Bridge admission remains false; interaction routing remains inactive. No first navigation is requested. This engine task returns; the shared engine is never parked waiting for the owner.
2. On the actual UI owner, admit only the fixed controlled vendor timer for that child. A first OLE_REGISTER completion is recorded once under the existing SDK broker mutex. A completion is not success: cancelled, malformed, old-token or changed-identity observations fail closed. The owner posts a real engine inspect and waits for it without a global caller message pump. Because the vendor callback precedes running--, only a later actual OK/OLE1/quiet inspect closes the fence.
3. Finalize on the same real engine. Install the original navigation/context/document/paint callbacks, views lookup, existing JS bindings, Bridge owner and event sink, and original resource entry selection. Recheck actual registration and identity after setup. Serialize the final cancellation check and the sole Bridge-accepting commit under state_mutex_, release that lock, and immediately call the original first wkeLoadFileW. No native call occurs while this cancellation/commit lock is held. Retain original wkeResize. Any finalize exception or failure immediately closes the gate before original cleanup.

After engine finalize, activate the original interaction router and show/focus the child. Each sent-message callout is followed by a cancellation and owned-window check. Only then does mount succeed and the unchanged public start publish its session. This does not promise first-document or application readiness at start return.

The registration completion ledger is private SDK implementation data. It records a real kind3 receipt including task and scope identity, and never admits or retries anything. Read-only opt-in diagnostics expose only fixed types and generation/counter values, with nonAtomic=true; no token, HWND, thread identity, user content, arbitrary evaluator or permission side effect is added. Existing native admission, document witness, geometry ACK, terminal revoke and epoch checks stay strict.

## Timer admission and sent-message reentrancy

The admission interval is5000ms of the real monotonic GetTickCount64 clock. It stops admission of a new timer dispatch after the deadline. It cannot preempt a synchronous native call, an already admitted TIMERPROC, or an engine task whose captured owners must remain alive until the task truly returns. It is not an overall hard startup timeout.

The allowed timer must name the actual retained child, WM_TIMER, the fixed vendor task timer ID0x15324547 and a nonzero procedure in executable MEM_IMAGE memory whose AllocationBase is the still-held actual kernel module. The child must remain owned by this adapter, the current UI thread/process and original parent, and remain hidden. The original receipt's strict endpoint/full-generation checks remain necessary. AllocationBase validation does not authenticate an arbitrary function name or defend against hostile same-process message injection.

PeekMessage can process sent messages despite its queue filter. Startup WM_CLOSE therefore records cancellation and gatefalse and returns without destroying the child; it does not reenter public destroy. The original public lifecycle guard continues to reject reentrant start/resize/close/destroy. Other unchanged window-procedure sent callouts remain relevant and must be exercised by the real regression, not assumed absent.

Validation with PM_NOREMOVE and later PM_REMOVE is not atomic. An initially refused timer is left in the OS queue. If removal returns a different actual MSG after sent-message reentrancy, its exact fields are retained in one fixed private slot, never dispatched or reposted, and startup returns the distinct E_CURSOR_STARTUP_TIMER_CONSUMED result. This is a consumed-but-refused observation; it is not described as still queued. Neither path silently treats the input as handled successfully.

Explicit timer-source refusal permanently sets dispatch_refused for that startup attempt and immediately closes admission. Original cleanup may request real terminal revoke, but it returns E_CURSOR_CLOSE_PENDING before entering the old broader child dispatcher. The Host, adapter, borrowed Bridge, child and module must remain alive; the application must fail-stop under its external Owned job if it cannot retain them. No normal recovery, natural closure, broad timer filtering, swallowed bad message, repost or manual TIMERPROC invocation is claimed for this erroneous input. Normal timer, navigation, WM_CLOSE cancellation and timeout paths do not set this flag and must perform genuine original cleanup.

## Failure ownership

Every failed finalize or cancellation closes admission first and calls original unmount. A failed unmount leaves public cleanup_pending and the actual adapter/child/callback storage retained. A destroy failure must not release the borrowed Bridge, destroy its owner HWND, detach the shared engine, FreeLibrary, or unwind the Host through an unresolved callback. The wrapper's explicit fail-stop is failure evidence, not a natural-cleanup qualification.

Fixed failures distinguish unavailable registration state, cancelled/lost owner, changed registration identity, missing/invalid actual registration, fence admission timeout, rejected timer source, removed-but-refused message and unfinished finalize. Automatic initialization Document/epoch changes that invalidate the queued task are failures; OLE_REGISTER is never converted to terminal work or replayed across a generation.

## Validation and compatibility

First tests use the real normal public SDK consumer, true local resources, current actual source-kernel bytes, default clocks and a hidden or already-visible owner. No prime diagnostic compile definition/fake-success seam is present. Immediate diagnostics must observe real OLE1 after a successful public start. A genuine initial page action must use the existing port and authorization path; rendering alone cannot prove Bridge readiness.

The two-Host test must show real engine/page progress for already-running Host A during Host B's fence, followed by separate real revoke/drain/destroy for both owners. A background task worker counter alone is insufficient. The minimal fixed test-only phase/time observation seam is separately reviewed before implementation; no application callback is executed by the production fence, and no arbitrary JS execution API is added.

Source changes include private method/state additions in the public C++ class definition. SDK and consumers must be rebuilt together. This candidate does not claim old SDK binary ABI compatibility. The vendor cursor ABI and loaded-kernel export contract are unchanged.

The existing hardcoded resource entry preference and prefix-based override check are separate entry-contract work and remain unchanged in this bounded startup change. Full toolchain provenance, all OS/Win7, hostile same-process timers, arbitrary GC/OOM recovery, long-duration stability, initialized module unload and release qualification remain false.

## Finite implementation observations and adoption boundary

On 2026-10-10 Root executed the same three real cases on this workstation's x64 and x86. Each direct process and serial CTest3 naturally returned0/0 with no forced owned cleanup. Hidden start confirmed real uncancelled kind3/task1/scope0 registration and an actual OK/OLE1/quiet engine inspection before the first navigation. The immediate post-navigation context could still be pending; the bounded public-port readiness observation subsequently recorded accepted context, document-ready, the public port and at least two real page calls. Application-ready was not established by this fixture.

Sent WM_CLOSE during the bounded fence returned E_CURSOR_STARTUP_CANCELLED without a session. In both two-Host runs A kept its real document/context and retained A engine-call QPC events occurred strictly between B's recorded fence begin and actual registration confirmation. All successful and cancelled cases retained owners through actual Host destruction, HWND withdrawal, Bridge join and runtime shutdown. Initialized modules remained held; no unload qualification follows.

The x64 actual summary SHA256 is c22ee1c467ed6ef773d1806d0bf0131ed956de72179c26d3748ddaff4916d04e. The x86 summary SHA256 is a89f16ab31a18c5edde2b038be333e2f6410bbce12ebad84185f5ea557a1058d. Both controllers remained1 because configure/build root0/supervisor124 compiler tails are generic-false. The finite native cases support this startup implementation; they do not turn the original supervision partials into pass. The x86 kernel is a finite recovery relink of already completed objects, not a full recompile. Original default startup RED, build/capture failures and earlier fixture observations remain preserved.

The public regression compiles the normal SDK once with its production Host unchanged. A generated consumer-only Host translation unit applies the same six pinned read-only observer insertions and links the normal SDK for the other members. Mandatory source/span gates and the actual LINK MAP must prove that the production Host archive member was not additionally extracted. The composition needs its own actual validation, independently of the earlier private-archive build. The parent option is explicitly off by default and enabling it without an actual pinned kernel must fail configuration.

This adoption changes private Host layout and requires SDK and consumer recompilation; old binary ABI compatibility is not claimed. Invalid timer-source refusal retains owners for external fail-stop and is not qualified as natural cleanup. Win7/all OS, general GC/OOM recovery, long-lived resource trends, initialized unloading, complete toolchain provenance and release readiness remain unqualified.

The public source gate treats only CRLF-to-LF normalization as equivalent. It records original raw and canonical byte identities, preserves lone CR and encoding, and reconstructs the private Host from six unique canonical byte spans. This is portability across Git text normalization, not permission to alter source semantics. The normal four-file adoption copies remain the exact reviewed V3 bytes; their staged text identities are separately bound to canonical LF. The frozen SDK inventory must be explicitly refreshed for reviewed future SDK changes.

## Actual normal archive and public consumer composition

Root subsequently built the public composition in fresh x64 and Win32 directories. Each normal archive contains exactly 24 production objects and no observer definition. Each consumer has exactly the fixture and generated observer Host objects. Actual LINK.read tracking names the single full absolute archive path; each MAP includes the private observer definition and excludes the normal Host archive member. The generated source is exactly 175057 bytes with SHA256 e07d66d390ee2592aa5d6c3dbef8f517d434892d0f4418a207b5b06fe6c4b371 on both architectures. The writer configures a single placeholder with LF output and removes exactly the terminal LF that CMake adds back; inserted C++ and JS bytes are not recursively configured.

All three direct cases and the same three serial CTests naturally completed0/0 on each architecture, with held direct PID/creation FILETIME and actual whole Job closure. Independent review also matched all CTest child identities, the real OLE/page/WM_CLOSE/engine-overlap receipts, CL24/OBJ24, generated bytes and LINK/MAP contributions. Actual x64 summary SHA256 is a9687bef836c7e1ae6869bf23fb7ee4c8abcac1b2ebae2b35df1cdc953a5a927; Win32 is b73c49131f12141ad96750279f04224bfd4e7487677401bdf06f6c8ea1122d26.

Configure/build roots returned0 but each retained an owned compiler auxiliary process through the drain, so their supervisor124 and both controller1 results remain failed partials. The first writer attempts failed before any Host case on CRLF conversion and then one extra LF; those records remain intact. This closes the finite public source-consumer composition only. Installed or relocated Host consumers, complete compiler inputs, initialized unloading, application readiness, all systems, performance and release gates remain open.
