# Ordinary acceptance reload pending fix

This work changes only `C:/sc07/tests/acceptance-app/main.cpp`. The original ordinary lifecycle consumer called `reload()` once and returned `E_RELOAD_PENDING` to `step.failed`, although the public adapter retains the original reload receipt for polling. The same consumer-side defect was observed and fixed in the current MessagePort fixture. There is no new SDK, Web, Runtime or ownership change here.

The replacement captures the ready count once, retries only exact `E_RELOAD_PENDING`, stops calling reload after the first `ok`, and then waits for that document's ready result. Operation polling and ready share one original `until`; its `settings_.timeout_ms` (20,000 ms by default), message service, 10 ms `MsgWaitForMultipleObjectsEx`, genuine error return, exception and timeout paths remain unchanged. The implementation matches the MessagePort code apart from its two explanatory comments. It does not weaken any resource/ownership fence or discard pending input.

Original main: 42,015 bytes / SHA256 `02bc1ff85205acec289b2489896d107c4c34fcad5a28f91577f3bf72754c734b`.

Patched main: 42,218 bytes / SHA256 `4d58281549806425f097ee0ca9bc04160f43ca112c027b46b4dfc199a8173be4`.

`source-before/main.cpp` preserves the original exact bytes. `source/main.cpp` is the exact patched copy. `SourceChange.actual.diff` records the two lines replaced by ten lines at original line 686. `Patch.prepared.json` records prefix/suffix preservation and the bounded contract. `PatchPreparation.first-call-failed.json` honestly retains the author's first preparation assertion failure: the source had already been correctly patched, but an exact MessagePort comparison included two comments. Completion compared the actual code lines, wrote metadata/diff, and did not rewrite the source a second time. That JSON is a transcribed preparation summary, not raw product/test evidence.

## Existing ordinary Web assets

`Prepare-WebReuse.cjs` was run once for local read/copy metadata preparation. It executed no compiler, typechecker, package manager, app, reader test or VM command. The immutable original Web producer is:

`F:/work/soui-issue96/out/acceptance/2566629a-7a06-4302-8fbf-04809a50da86`

Its original v3 whole-consumer, Web SDK and Web-output snapshots match the existing actual records. The original main is byte-identical to this patch's saved pre-fix main. The two ordinary Web input files are unchanged. All 26 public SDK executable/declaration dist files match `C:/sr06/sdk/web/dist` exactly; current `C:/sc07/sdk/web/src` also matches sealed source. SDK package metadata differs only by version. The original build aliases SDK dist files directly. Five actual Web output files, 169,771 bytes, are copied unchanged and pinned in `web-reuse/Reuse.actual.json` (SHA256 `29fb458b38dc052a2272ec47f16b9eda42a1c8373f860dc598f6892adffd40bf`).

The original typecheck record, absolute config/CWD, log and completion time stay original. This preparation does not retag them as a fresh native consumer or invent a new typecheck. The original asset producer used Vite 6.4.4; current tooling declares 6.4.5, and the current classic-entry generator adds Web target handling. Their actual pins and differences are explicit. **No current-toolchain output equivalence or current bundler execution is claimed.** These are unchanged original test assets with matching ordinary Web program/SDK dist inputs for the affected native lifecycle regression. Root should approve this narrow reuse scope before building. No standard v3 `web-build.json` was rewritten to bypass its whole-consumer fingerprint guard.

## Consumer-only build preparation

`Run-OrdinaryBuild.cjs` and `Invoke-OrdinaryBuild.ps1` derive from the already reviewed current MessagePort consumer-only recipe. Their `.prepared.diff` files show the complete textual delta. Five native inputs are required: CMake, main, the existing shutdown helper and the two existing metrics/census headers. The current CMake already selects `SouiSdk 0.6.0 EXACT`; no CMake source edit was needed.

The preparation keeps the original 120,000 ms Owned / 2,000 ms drain, natural VCTIP `-timeout:20` and 30-second lifecycle wait, selected MSVC143/Windows SDK, no active foreign compiler guard, complete installed SDK freeze, whole Job natural closure, exact raw streams/context/tool pins and before/after inputs. Only fresh `C:/sco1-<uuid8>-<architecture>` workspaces are accepted. It compiles the `SouiAcceptance` target against the existing sealed installed SDKs; it does not build SDK, kernel or Web, execute any resulting EXE, terminate a helper, or repeat passed Workbench/MessagePort/default/policy cases.

Root-reviewed future invocation, **not executed**:

```text
node F:/work/SOUI_WEB/out/open-issues-closeout-20261029/ordinary-reload-current/Run-OrdinaryBuild.cjs ROOT_REVIEWED_ORDINARY_RELOAD_BUILD_V1
```

`BuildPreparation.prepared.json` pins the recipe and inputs. Node and PowerShell static syntax parsing passed without invoking either build script. Root remains the only build/Git/VM operator. A missing or failed original lifecycle guard must remain a refusal/failure, not be bypassed because host process enumeration is currently unreliable.

After an actual successful build, the only affected runtime short is the ordinary fixture's `reload` mode with three cycles, zero warmup, original 20-second step timeout and normal close/Runtime closure. Run it only in the task VM with a fresh exact EXE/SDK/Web manifest and original Owned identity/closure validation. The public ordinary `runner.validateRecords` and `verifyNativeIdentity` may consume actual raw records; do not use the MessagePort-specific sidecar validator or pretend ordinary assets contain MessagePort's 32 behaviors. No VM stage/automatic execution was prepared by this task.

No compile or runtime short has passed for this new ordinary EXE because neither has run. Three reloads do not establish full Collector, resource, long stability, Win7 or production qualification. All such flags remain false, and previously passed unchanged inputs stay reusable under their original identities.

## Separate finite VM recipe (post-build preparation)

The earlier statement that no build had run describes this preparation's original checkpoint. Root subsequently executed the original frozen consumer-only recipe at `attempt-bdeb6e02-7baf-4384-a5ea-c0b79a26b892`. Its actual summary now reports both architectures passed. This agent did not execute or repeat that build. The new VM recipe remains **unexecuted**; Root is its only finalizer/media/VM operator.

`Finalize-OrdinaryPayload.cjs` is a data-only post-build template. It requires the exact actual ordinary successful summary, original input before/after equality, actual natural native results for both architectures, compiled EXE pins, five generated resource copies per window, fixed patched source and original unchanged Web-reuse proof. It preserves complete DLL metadata from the sealed current0.6 Starter build. Root must review it before this future invocation:

```text
node F:/work/SOUI_WEB/out/open-issues-closeout-20261029/ordinary-reload-current/Finalize-OrdinaryPayload.cjs ROOT_REVIEWED_CURRENT_ORDINARY_BUILD_V1 F:/work/SOUI_WEB/out/open-issues-closeout-20261029/ordinary-reload-current/attempt-bdeb6e02-7baf-4384-a5ea-c0b79a26b892
```

Root's first data-only finalizer attempt failed at the author's incorrect member-count assertion (`19 !== 20`), before writing a manifest or selection. The partial `light-payload-79d898d5-d5d6-4378-a308-b204f50c757e` remains unchanged; the author-received summary is `DataFinalizer.count-failure.received.json`, not raw tool or app evidence. All19required files were present:2guest controllers +2metadata +6native source +5Web assets +4architecture EXE/case records. No padding file is needed. The original prepared source bytes are preserved in `vm-recipe-count-v1-prepared/`.

The corrected template has not run. It will create a fresh `light-payload-<uuid>` only, with a 20-file flat payload (including `Ordinary.manifest.json`) and a 24-entry ISO selection including four launcher/export wrappers. The five original ordinary Web assets add only169,771 bytes and are transported under flat `WebAsset-*` names; the manifest maps them into all three window resource directories. The guest is not assumed to contain the old ordinary producer suite, and no MessagePort Web assets substitute for ordinary Web. Current DLLs and existing authenticated tools/readers are checked at their existing guest locations.

`Guest-Ordinary.cjs` / `.ps1` retain the current MP recipe's original native lifecycle/monotonic supervision and environment/mutex handling, with an `ordc` namespace. The native result must contain nine completed steps, three reloads, seven actual samples and27native events, with natural root0/supervisor0/Job0 and no stop/fallback/forced termination. Original inner20s, native214,500ms, Owned394,500ms and drain2,000ms remain unchanged. Actual nonzero exits and prior failures stay failed. Each staged runtime source is checked immediately before and after its exact copy; final runtime membership/bytes remain checked.

The ordinary consumer does not contain MP's dedicated browser behavior fixture. Its guest recipe never loads the MessagePort behavior reader, never requires a MessagePort sidecar and never claims the32behavior cases. It only calls the existing ordinary `settings`, `expectedSteps`, `verifyNativeIdentity` and `validateRecords`. `OrdinaryValidatorProof.prepared.json` byte-binds their actual original/current source ranges; no substitute validator algorithm was written. Resource and production gates remain false even if this finite native lifecycle passes.

The four small wrappers are narrow derivatives of `current-guest-launch`: `Ordinary-Lib.ps1`, `Stage-Ordinary.ps1`, `Run-Ordinary.ps1`, `Export-Ordinary.ps1`. They accept only kind `ord`, use fresh `C:/ordc-<stamp>` and `C:/issue-closeout-evidence/ordc-<stamp>-stage|run`, validate all20flat payload files, retain the same read-only `D:` and named task-VM requirements, and preserve actual closed controller/wrapper identities before export. Stage never automatically runs the app. The unchanged `C:/issues8/Guest-Export.ps1` remains pinned;512files/8MiB/full-tree export checks remain unchanged.

After mounting Root's actual 24-entry control ISO, example separate future commands are:

```text
powershell -NoProfile -File D:\Stage-Ordinary.ps1 ord <actual-manifest-sha> a1
powershell -NoProfile -File D:\Export-Ordinary.ps1 ord a1 stage
```

Root must independently decode and review the actual stage before:

```text
powershell -NoProfile -File D:\Run-Ordinary.ps1 ord a1 <actual-manifest-sha> <actual-stage-sha> ROOT_REVIEWED_CURRENT_STAGE_V1
powershell -NoProfile -File D:\Export-Ordinary.ps1 ord a1 run
```

`Decode-OrdinaryExport.cjs` changes only the original decoder's permitted evidence prefix alternation to also accept `ordc`; its exact-one-tag, canonical Base64/UTF8/paths, complete closed catalog, hashes, fresh destination and8MiB checks remain original. It is output-only and has not decoded a new result. New guest/finalizer/decoder Node scripts and all five new PS scripts passed static syntax parsing only; no VM command, finalizer, build or app was executed by this preparation task.
