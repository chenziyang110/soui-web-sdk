# Current compiler lifecycle: bounded real short harness

Prepared source only. **Not executed by the author.** Root executes only after
the integrated source review and unified logical suite are ready:

```powershell
node F:/work/SOUI_WEB/out/windows-tooling-failure-closeout-20261029/native-short/Run-NativeShort.cjs
```

The harness uses the original sealed `C:/sr06` x64 runner:

- `out/benchmark-protocol/Release/OwnedProcessRunner.exe`: 170496 bytes,
  SHA256 `f254dadd4eccad3c57d69e98f260d15ee598c0b6441b7fe68fc4ef5b3b7dea83`.
- Original `measurement-tools.json`:
  `0de724e35238813efe68f35c0f286f6432934d695d12e2575a8e95caa76ba629`.
- Original preparation remains `genericSupervisionPassed=null` and
  `supervision=external-caller-evidence-required`. Its pinned original reader
  verifies its original source/output closure before runner reuse. No new
  Build-ValidationTools preparation is produced or inferred.

A single owned root per architecture runs x64 first, then x86 only if x64
passes. Each root has the original 120000 ms deadline and 2000 ms drain for
both phases together. It calls the actual current `C:/sc07` configure,
toolchain and compiler-lifecycle helpers, preserving v143 Hostx64,
MSVC 14.44.35207, SDK 10.0.19041.0, `/nr:false`, and the discovered VS instance.
Each phase starts a pinned VCTIP with its self-described `-timeout:20`, waits
naturally, and retains the current helper metadata. The generated program is
one `int main() { return 0; }` console executable; it is compiled and never run.

The source fixture stays here. Runtime creates fresh, explicitly owned short
workspaces `C:/sct8-<8hex>-x64` and `C:/sct8-<8hex>-x86` to avoid long generated
object paths. The short workspaces are retained with ownership records. This
harness has no directory or process cleanup action, no retry and no force-kill
path of its own; original supervisor timeout behavior remains a failure.

Each attempt retains before/after current source, entry, helper, harness,
original runner and provenance pins; original capture raw NDJSON, context,
actual ready and finished records, root stdout/stderr and phase logs; phase
helper PID/birth/image/ready/return metadata; actual CMake cache/compiler
selection, PE/object/tracking evidence, and copied key observations. Acceptance
requires the existing strict stream reader plus real root 0/supervisor 0,
whole Job active 0, no force/STOP/fallback/limit termination, and both exact
phase helper identities in the owned-member stream. Extra VCTIP identities
fail. Any failure stops the sequence and preserves its raw result.

This is a current-helper short mechanism check. It does not rerun or close the
eight original failures, qualify all compiler workload shapes, execute the
full build entries, rebuild the SDK/engine, execute a product/GUI/VM, or change
production/release qualification.
