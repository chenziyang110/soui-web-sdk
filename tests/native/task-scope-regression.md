# Native grant and task scope regression

This service-layer slice is based on `05e7d93b`. It does not implement a local
Agent transport, Windows principal authentication, UIA, or directed events.
The public page event sink remains unchanged.

Before implementation, the new `soui_task_scope_tests` was compiled against
the unmodified SDK and each selected regression ran in a separate owned Job:

| Case | Actual baseline result |
| --- | --- |
| `multi-grant` | exit 1: same-document operationId collided across native grants/stores |
| `honest-cancel` | exit 1: cancel request overwrote actual handler outcome |
| `queued-expiry` | exit 1: expired queued grant entered native business callback |

The same cases now run in the standard `sdk.native.task-scope` CTest target,
together with the module-local allocator's concurrency/exhaustion policy,
stable identity over renewal, exact revocation including both bearer aliases,
task-store terminal truth, an event-gated worker's final authorization check,
scope-local subscription cleanup, transport-validation/admission revocation,
shared inflight capacity and queued-scope shutdown, and retained-task pruning.
Barriers hold actual workers; a controlled monotonic clock advances grant
expiry. No elapsed eight-hour or target-system claim comes from these tests.

For both architectures, configure with the exact Windows SDK and build only:

```powershell
$env:MSBUILDDISABLENODEREUSE = '1'
cmake -S . -B out/scopes-x64 -G 'Visual Studio 17 2022' `
  -A 'x64,version=10.0.19041.0' -T v143 `
  '-DCMAKE_SYSTEM_VERSION=10.0.19041.0' `
  -DSOUI_BUILD_TEMPLATE=OFF -DSOUI_BUILD_DEMOS=OFF
cmake --build out/scopes-x64 --config Release --parallel 2 --target `
  soui_task_scope_tests soui_task_service_tests soui_session_renewal_tests `
  soui_task_store_capacity_tests
ctest --test-dir out/scopes-x64 -C Release --output-on-failure `
  -R '^sdk\.native\.(task-scope|task-service|session-renewal|task-store-capacity)$'
```

For x86 replace the build directory and use
`-A 'Win32,version=10.0.19041.0'`. Actual verification wraps each configure,
build and CTest invocation with `OwnedProcessRunner`, using its own Job,
absolute executable/working-directory/report paths, bounded timeout and drain.
MSBuild success alone is insufficient if owned descendants remain after drain;
check its terminal `rootExitCode`, `closureConfirmed` and `passed` values.
No global process-name cleanup is performed.

Related Web compatibility verification uses the unmodified Web SDK:

```powershell
npm ci --prefix sdk/web --ignore-scripts
npm run build --prefix sdk/web
node --test tests/web/sdk-client.test.mjs tests/web/session-renewal.test.mjs
```

The current local evidence is under `out/scope-evidence/` (ignored build
artifacts), including the three RED reports and each architecture's separate
configure/build/test Job reports. The version identity and canonical release
acceptance records are unchanged.

On 2026-10-08, all four selected native CTest targets passed on both x64 and
x86; the two Web test files passed 56/56 and the SDK's npm install audit found
zero vulnerabilities. Final configure/build/test Job reports for both
architectures recorded root exit 0, `closureConfirmed: true`, `passed: true`,
and zero active descendants. Both generated SDK vcxproj files specify Windows
SDK `10.0.19041.0` and toolset `v143`. These results do not certify real Win7/PE,
GUI behavior, eight-hour stability, or independent Agent event delivery.
