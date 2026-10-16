# ADR-0076: Bound direct native task input before admission

Status: proposed, Source-only. C++ compilation/tests, SDK/public consumer and Owned runtime
qualification have not executed for this Source. full cacheBudgetGate remains not-evaluated,
releaseReady=false; requested-byte budgets/quota configuration/calibration pins are null.

## Decision and production scope

Direct task_service::start formerly parsed bridge_request.params_json without the existing Bridge
32KiB/depth32 policy, allowing a trusted native caller to retain an arbitrarily large input in
job invocation, owner.scope_key and TaskStore scope history. Typed start_local bounded the raw
envelope but did not check normalized JSON length after dump; compact number representations
can expand during normalization. The1024 history count and TaskStore map-request ledger do not
bound those retained string buffers.

Reuse bridge_transport::max_params_bytes/max_json_depth for generic native start parsing. Both
generic and typed starts reject a normalized input longer than max_params_bytes immediately after
serialization. Rejection occurs before workers are started, GUI context preparation, the service
lock/admission transaction and any Store mutation. Hard admission applies with diagnostics disabled.
Raw/depth/normalized violations use the existing E_INVALID_PARAMS error code; they are not E_BUSY
byte-quota exhaustion and do not create a task ID/receipt or authorize an automatic replay.

No public method signature, std::string/task_snapshot type, independent storage schema, old
renderer168/model264/cache216 POD, collector/reader/policy or kernel Source changes. Valid bounded
duplicate/conflict, authorization, cancellation, renewal, terminal retention/lookup and eviction
semantics remain unchanged. Normalized input at32768 is admitted if the complete raw envelope also
fits its own32768 limit. Raw outer fields and retained normalized input have separate checks.

## What this does not account or qualify

The check is a logical retained input bound. The caller's preexisting string, temporary parsed JSON,
normalized serialization before its length check, std::string capacity/SSO/node buffers, permission
context/operation metadata, service containers/shared jobs/results/subscriptions/callback captures
and kernel memory are not made allocator-exact by it. No capacity value is treated as requested
bytes; no history is cleared, string truncated, hidden quota inferred or cache fullBudget promoted.
PR87's exact map allocation requests remain their narrower declared domain and have no hard quota.

The next actual byte slice requires private retained string allocation ownership plus pre-allocation
credits, including prepare/old+candidate/retiring lifetime and a configured/calibrated quota. Service
shared pointees, Engine/reply retirement, externally retained permission/family owners and kernel
retained cache policy need their real single-owner accounting/bounds. Missing configured credits or
calibration remain explicit null/NE, not zero or a guessed default.

## Verification

Existing real SDK test targets gain direct generic raw oversize/depth/native-bypass and normalized
at/over/below-limit cases with live native authorization; the generic path exercises diagnostics
disabled and scalar modes. Typed regression uses a legitimate closed schema with bounded numeric
array and string data, compact exponents and normalized at/over-limit inputs. Existing metrics and
TaskStore allocation ledger must show no worker slots, handlers, committed starts, queue/owner/
operation/result entries or Store allocation/retention change after rejection; valid later reuse
must receive the first task ID and preserve duplicate identity. Fixtures derive normalized lengths
with the real nlohmann parser/dump, so an incorrectly sized sample cannot silently pass.

Current verification is limited to Source diff/pins and pure data/static checks. Actual normal
full Source build/unit/installed and relocated public consumer plus architecture/Owned lifetime
validation remain required after adoption. Current canonical cf1d binaries cannot prove this changed
worktree; the unchanged kernel ABI does not substitute for rebuilt SDK Source identity. Actual100
reload collection and same-tuple8h evidence remain separate and all new calibration pins are null.
