# Issue42 necessary Win7 short investigation

Read-only investigation,2026-10-29. No VM/media/claim/start, build, product, Git, archive copy or passed-test rerun occurred. Only this report is written. Root remains sole VM operator.

## Decision

The placement code is already implemented and its fixed historical input passed real Win7 resize/close. No new placement patch is justified by the evidence read. The smallest outstanding original-issue slice is a finite Win7 wheel + real Chinese IME composition + public-task overlap + natural closure/before-after identity check. Full V02,800×600/DPI/multimonitor, production and long-run goals are separate.

The immediate environment blocker is concrete: the historical Win7 base `D:/SOUI-SDK-QEMU-validation/system.qcow2` referenced by the successful V11 launcher **does not exist now**. The default shared catalogue has no demonstrated Win7 VM entry. It is not safe to claim an existing ready Win7 desktop or start the old launcher unchanged. This bounded investigation did not search every disk or prove no Win7 image exists elsewhere.

## Current issue contract

GitHub issue42 and its sole comment were fetched through the GitHub connector during this investigation:

- https://github.com/chenziyang110/soui-web-sdk/issues/42
- https://github.com/chenziyang110/soui-web-sdk/issues/42#issuecomment-6211423338

The issue body requires usable visible content and real resize at1024×768/default taskbar, a new frozen fixed-build/input identity, native wheel, task overlap, Chinese IME composition, closure, serial/input hashes and modern Windows x86/x64 regressions. The original V7 build `f0c38056-058a-456e-be99-17446127f7ec` failed because its lower-right border was under the taskbar. Twelve wheel actions occurred, then the probe correctly refused covered-border resize; later task/IME/inner-list/close/post-payload branches were untouched. It is not a passing V02 result.

The sole October16 comment missed the later already-existing V11 finite resize proof. Local docs now correct that historical status. Its broader remaining interaction acceptance is still valid; it does not require another8-hour run merely to close the placement regression.

## Passed evidence to retain without rerun

`C:/sc07/verification/production-validation/win7-v11-native-resize-2026-09-29/README.md` and its raw manifest bind:

- preview.15 x86 Release buildId `b6caec6c-117e-49eb-86fa-4182e64d5b55`, source `60d729c8c0fe55dcabd72bf3a1aa2255af42017c10c63151a880fecd7d511640`.
- Win7 SP1 x86,6.1.7601,1024×768/default taskbar; original input `win7-v11-transfer-attempt1`, freeze `6d712b263f6318aacd8077418d1906aa013104ed6c7113ca2faf98c637716aa2`.
- Actual HWND/PID and real80×60 resize: `[87,24,937,664]` → `[87,24,1017,724]`; native close/exit0,54serial frames,200,483bytes, input/payload/legal hashes and no observed external desktop load.
- Archive raw manifest `f4bd57e52a6014734dbc3135b7ebc2318b0bbc2545c5d33c2b693792de6c06fb`.

This proves that exact historical build's resize/close subset. It does not prove current0.6 Win7, IME, task overlap or full V02. Do not rerun that unchanged resize just to create a new date or splice the result onto a new EXE.

`work-area-placement-gui-2026-09-28/README.md` retains modern Windows historical x64/x86 real80×60 resize/close passes for the same source. x64 buildId `c8d857b1-0083-40bf-ae3a-3b5ed5f4ad47`, x86 `b6caec6c…`. Their earlier first-attempt failure remains. Reuse their exact original scope; source-only similarity does not qualify current0.6 GUI behavior.

## Live code

`C:/sc07/demos/workbench/app/work_area_placement.h` implements `fit_initial_window`: real work-area bounds,8px leading margin,84px/64px growth room when a640×480 window fits, checked64-bit arithmetic and nullopt for invalid bounds.

`main.cpp:132` obtains `MONITORINFO.rcWork`, with `SPI_GETWORKAREA` fallback. `main.cpp:1190` passes the fitted rectangle to ordinary `Create`; explicit850×640 is still accepted as the requested maximum. Thus the old unconditional `CW_USEDEFAULT` placement is no longer the normal successful-work-area path. This is static current truth, not a new Win7 execution.

## Available assets and limits

The existing catalogue contains `soui-devtools-windows` (task's modern Server guest), two distinct external-strict SOUI entries (`soui-devtools-mini-strict`, `soui-b-f98-0ea492c7`) and other tasks' Win10/PE instances. Neither naming nor descriptor intake establishes either strict entry as Win7; both are occupied and must not be borrowed. No lease/password/token or whole catalogue was printed or copied. Generic image references inspected identify modern Win10 or the known task Server image, not a verified Win7 environment.

Historical V11/V12 data remain under `C:/SOUI-SDK-production-validation/work-area-placement-20260928/`. Both `win7-v11-transfer-attempt1/input.json` and `win7-v12-locator-transfer-attempt1/input.json` bind the same fixed V11 build/source and Workbench payload `5cef41216d57166b62d277ed064c26decce6a5ccc866ae1901ac6ac57ad98f20`. V12's mode explicitly says locator-only, not task/IME proof. Its fourth attempt stopped before launch on foreign QEMU; there is no successful new guest interaction to reuse from it.

The V12 transfer directory still contains `V02InputProbe.exe`190,464B, display probe116,224B,7za, PS2-compatible guest/export scripts and Starter/Workbench ZIPs. These are existing assets, not a newly reverified complete carrier. The known probe SHA in the frozen input is `2ad0e9eb92c7ef7b2ba8e72c672db58fd9fa250f5dbbb1698795c359cb0e5d96`. Proven entry syntax includes PID/creation identity + `inspect`, `wheel` and `resize-drag`; no existing successfully exercised complete task+IME control sequence was found.

Old guest entry: `cmd.exe /c D:\soui-input\run-display-explore.cmd`. V11 automatically repeats resize and V12 repeats resize plus24wheels, so neither unmodified script is a missing-only task/IME closeout. V12 uses stale-coordinate discovery rather than proving task acceptance/progress. The historical host launcher bypasses the current shared pool and sends keys through HMP; do not replay that host control path. Root must use the supported pool/noVNC/CUA path after confirming an actual Win7 desktop. Keep PS2/.NET2 guest compatibility; current Server/PS5 wrappers are not automatically Win7-compatible.

Current0.6 assets are available without rebuilding:

- Sealed x86 Workbench: `C:/sr06/out/x86/bin/Release/SouiWorkbench/SouiWorkbench.exe`,1,507,328B, SHA `59216d2fb2a4c747a89d78923dd47015438ebc7c582092780c6afdc3bc35e6aa`.
- Current fixed resident consumer: `C:/scw8-55faf327-x86/out/x86/bin/Release/SouiWorkbench/SouiWorkbench.exe`,1,507,328B, SHA `f3da178e5d78b2ccdf36a469d52788a7b729aab4ace3a69e0115ee2ca3be4781`; it differs from the sealed EXE despite equal size.
- Existing x86 app record: buildId `6d3166b8-5989-4356-8970-1ab5c879db00`, source `67a8c649a1a08e96e76dd4d0f7782b53f229e1f26ad65e6a2738bc7173555264`, Workbench671-file payload `eaf6cc3b9aafeb057124d8dc0ded10a721b165d09d89bcb1f993b957fa9613a2`.
- `C:/sr06/out/release-install-x86-release` exists and remains sealed; the runtime/Web/legal payload comes from the matched app record, not from the installed static library alone.

The current x64 equivalent also exists, and current finite residency passed on the modern task VM. None of these modern/current artifacts has Win7 qualification from this investigation. Choose and freeze one exact EXE plus matching current runtime/Web before any new Win7 test; do not mix the two equal-size EXEs or substitute an old kernel to avoid a loader/API failure.

## Minimum actionable route

1. Resolve the missing Win7 environment first: locate the legitimate existing Win7 disk/config if retained elsewhere, or have Root explicitly provision/register a compatible environment only after that bounded asset check. Reuse assets; no new SDK/kernel compilation is needed to diagnose this environment prerequisite. Confirm desktop,1024×768/default taskbar and a usable Chinese IME; no IME-install proof is currently available.
2. For the smallest original-issue completion, use the same fixed V11 payload identity and supplement only missing wheel/content motion, task admission/progress while wheel/typing remain usable, actual IME preedit/commit and final natural close/full identity checks. Keep its already passed resize and modern regression subsets as their original evidence. Root must verify actual task/progress/IME state, not just a click, Unicode text insertion or screenshot. The existing locator is not that proof.
3. If current0.6 qualification is required, use the already compiled current x86 payload in a new frozen input and a single bounded necessary interaction short. A changed EXE/environment cannot inherit V11 GUI passes; the original deadline/owner/foreground/close/hash guards remain. Keep current modern passes that match unchanged inputs. Record missing Win7/loader/IME behavior as actual failure rather than silently falling back to the historical build.

No long soak,1000cycles, full performance matrix,800×600 or DPI/multimonitor expansion is needed for the narrow original1024×768 regression. Those broader V02/production gaps remain open. No new test protocol or source change is supplied by this report.

Project cognition honestly returned `missing_graph_store`; no restricted map workflow was entered. Learning intake was read-only. The relevant QEMU freshness lesson is advisory: preserve original input deadlines and truthful fresh desktop/observed-state claims, not permission to bypass present noVNC/CUA rules.
