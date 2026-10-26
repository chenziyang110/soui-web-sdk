# Native UI trace producer lease SOURCE

Target `soui_native_trace_ui_lease_tests`, CTest
`sdk.native.native-trace-ui-lease`; Root registers this leaf globally. It links
the actual SDK scalar diagnostics and compiles the actual lease header with
fake module retain/release dependencies. No build/test/DLL/COM/UI/VM execution
was performed for this delivery.

Source covers missing/mismatched initialized process module, shared cold retain for multiple Hosts, hot active-call pin across
last Host unmount, distinct getter/module identities, multiple modules on the
same actual UI thread, pin/native-retain failure, and foreign final destruction.
The foreign case must leave the native release uncalled and the acquired module
reference retained, with explicit causal loss. That expected safe retained pin
is not a successful native shutdown/producer-quiescence receipt.

The cold cache has eight weak entries per actual thread. Its unique cookie is
retained by live tokens and is compared through trivial TLS, avoiding numeric
thread-ID reuse; hot borrow only copies an existing shared token. SDK mount
checks ABI/field boundary/cap0x200/function pointers after successful
`acquire_runtime` and before input activation. Initialization/start failure
returns before this registration; subsequent mount failure can release the
scoped lease because the initialized runtime's original LoadLibrary reference
remains process-lifetime, even after actual finalization. The factory requires
that process-pinned module and checks the getter belongs to it. This helper
does not authorize unloading a general DLL: native detach only pauses its TLS
registration/storage until actual thread exit. Last successful unmount releases its own reference
on that actual UI thread. Root's native retain/release owns the distinction
between an existing core producer borrow and a self-created external writer.

Actual matched-DLL registration, UI→TaskPair.task_posted export identity and
final drain remain specialist qualification. DLL reference ownership follows
[GetModuleHandleExW](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandleexw):
only the acquired FROM_ADDRESS reference is freed, never unchanged-refcount
identity queries.
