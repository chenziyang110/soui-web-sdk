# Tray recovery consumer build preparation

This prepares only the new public installed-SDK consumer `SouiTrayRecoveryConsumer` for x64 then x86 Release. The source author explicitly froze exactly `CMakeLists.txt`, `main.cpp`, and `README.md`; their byte-exact copies and unchanged build-helper identities are bound by `SourceInputs.frozen.json`. No header, Web payload, SDK source change or future EXE identity is invented.

Root must review `Recipe.prepared.diff`, both scripts, the source freeze and `BuildPreparation.prepared.json` before invoking:

```powershell
node 'F:/work/SOUI_WEB/out/open-issues-closeout-20261029/tray-recovery-current/Run-TrayRecoveryBuild.cjs' ROOT_REVIEWED_TRAY_RECOVERY_BUILD_V1
```

The preparation itself has not configured, compiled or executed anything. Node syntax checking and PowerShell parsing are recorded separately; they do not establish a native build result.

The wrapper creates one fresh attempt and unique `C:/sctr-<attempt8>-{x64,x86}` workspaces. It invokes only CMake configure and the single-TU target build, linking the existing `C:/sr06/out/release-install-{architecture}-release` public `Soui::Sdk`. It never rebuilds SDK, kernel or Web and never executes the produced EXE on the host.

Original tray-build safeguards remain: current MSVC v143 tool/SDK selection, absence of ambient compiler/linker options, global foreign compiler/ready-event rejection, the existing compiler lifecycle mutex, owned VCTIP `-timeout:20` and natural 30-second retirement, original Owned root/whole-Job natural 0 with 120000ms/2000ms limits, exact input and installed-prefix membership before/after, actual PE architecture and EXE hashes, CMake compiler identity, and exclusive observation copies. No helper is terminated or existing output overwritten.

The read-only snapshot at `2026-10-29T13:35:22.5290707Z` found no compiler processes and no VCTIP ready event. It did not acquire/wait/signal the event or clean up processes. It is advisory: the unchanged lifecycle guard must recheck immediately before each real phase.

Actual compiler outcomes, EXE identities and runtime validity remain unknown until Root executes the reviewed recipe and the separate necessary VM short. Old tray consumers and their passed or failed evidence remain separate. Source author and Root retain ownership of source and runtime behavior; this directory owns only build preparation and reports. Long tests and production qualification are outside this preparation.
