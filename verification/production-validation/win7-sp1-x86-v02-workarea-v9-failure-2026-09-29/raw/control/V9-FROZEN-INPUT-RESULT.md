# Win7 V9 frozen-input result — 2026-09-28

The shared host became idle after unrelated QEMU PID 32268 exited. The four separate V9 preparation captures then completed with both wrapper and child exit code 0, empty stderr, no reported gate error, and no observed external QEMU/app load:

- `workarea-v9-payload-attempt1-driver`: official x86 Release build `b6caec6c-117e-49eb-86fa-4182e64d5b55`, 720-file source SHA-256 `60d729c8c0fe55dcabd72bf3a1aa2255af42017c10c63151a880fecd7d511640`; Starter 71-file ZIP SHA-256 `c3c492cb528eb851e4829f35cf2ed411335cf202353c6e0852e13cc900e822df`; Workbench 475-file ZIP SHA-256 `6b3ab3f6f68f180b5104eafa4de9b86d0b1111f8d3fb9a72a48595e5996310d9`. The preparation record binds the runtime payload and all five legal files per app.
- `workarea-v9-transfer-attempt1-driver`: 11 transport files prepared, input SHA-256 `4d7a5d2973140b5bb4e5e80e90c10f93c3bf903ec48d2e2cb4e7d4ef0788f53f`.
- `workarea-v9-freeze-attempt1-driver`: 13 files frozen, `freeze.json` SHA-256 `98acf9d01407a3c4bcbe843b6d9289598d607f3c32e20485759e6fc4b1dc6e80`.
- `workarea-v9-verify-attempt1-driver`: independent frozen-input verification exited 0 and returned the same 13-file freeze hash.

The original command results, stdout/stderr, load gates, ZIP creation/test logs, payload manifests and frozen transfer remain under `C:\SOUI-SDK-production-validation`. A new unrelated QEMU PID 31300 appeared before the Win7 guest launch gate; the launch command exited 2 at its outer gate, before `Run-V9.ps1` ran, without creating `guest-work-area-v9-attempt1` or sending a guest command. Its name remains unused. The frozen input is valid as an input; no V9 guest result exists. In particular, no current Win7 resize, Chinese IME composition, task concurrency or production pass is claimed.

