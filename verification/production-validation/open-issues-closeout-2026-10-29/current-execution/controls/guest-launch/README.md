# Current finite VM checks — convenience wrappers, prepared only

These wrappers have not been executed. Root must review their current bytes, build the read-only combined data ISO and operate only the existing task VM `SOUI-7AEMMK5Q0T` through noVNC. No SDK or app source is modified here. No stage automatically runs a product. Existing controller scripts retain their own desktop mutex, configured deadlines, resource checks and native/Owned exit judgment.

The ISO root contains 13 MessagePort payload files, 13 Workbench payload files and these six files: Current-Lib.ps1, Stage-Current.ps1, Run-Current.ps1, Export-Current.ps1, README.md and Preparation.actual.json — exactly32 total. Names must remain flat and unique. The wrappers run from `D:\`; they copy only the selected manifest's13 files to a fresh `C:\issue77c<stamp>` or `C:\issue31c<stamp>`, pin source/copy/source again, and preserve all existing roots. The separate copy receipt is extra evidence; it does not rewrite the payload manifest.

At preparation time the actual MessagePort manifest is `message-port-current/light-payload-e4044c6a-1537-4da3-a96b-063bcd90adbb/payload/Current.manifest.json`, SHA256 `4195e228c9d09ca92d1dba20986058feda5669929da5119d953be6bcebc025b1`. Workbench's final manifest is intentionally supplied by Root after its actual successful build/short-policy results. A stamp is1–24 lowercase letters/digits/hyphens, starting with a letter/digit; always choose a fresh one.

Use the GUI to type short stage/export commands (example fresh stamp `a1`):

```text
powershell -NoProfile -File D:\Stage-Current.ps1 mp 4195e228c9d09ca92d1dba20986058feda5669929da5119d953be6bcebc025b1 a1
powershell -NoProfile -File D:\Export-Current.ps1 mp a1 stage
```

The exact stage evidence is `C:\issue-closeout-evidence\mp77c-a1-stage` (`wb31c-a1-stage` for Workbench). Export checks the actual original controller PID and creation FILETIME from Lease.actual.json, requires PowerShell.finished.json, checks both the original controller and wrapper identities are gone (a reused PID with a different birth is distinguished), and preserves any nonzero result. It exports only after both processes ended, not merely after a ready message. Root decodes and reviews the complete stage result and records the actual SHA256 of Stage.actual.json. Then a separate GUI command, with the real digest in place of STAGE_SHA, is allowed:

```text
powershell -NoProfile -File D:\Run-Current.ps1 mp a1 4195e228c9d09ca92d1dba20986058feda5669929da5119d953be6bcebc025b1 STAGE_SHA ROOT_REVIEWED_CURRENT_STAGE_V1
powershell -NoProfile -File D:\Export-Current.ps1 mp a1 run
```

The generic explicit Root marker maps to the existing kind-specific marker only after the frozen manifest, actual stage SHA, task VM identity and successful stage-launcher records match. It does not add approval, infer review, modify the controller, renew timeouts or accept failed cleanup. Workbench uses `wb` and its actual manifest SHA with exactly the same sequence. Run evidence uses the separate `-run` suffix and is never merged into stage evidence.

Stage's copy work is product-free; the original stage/run controller obtains the desktop mutex itself. The wrappers must not hold that mutex across a child launch because that would block the existing controller. Root remains the sole VM input owner throughout. Windows PowerShell5 native stderr is retained while the actual child exit code is captured. A warning/error never becomes a success result.

Export recursively rejects reparse points, catalogs the exact complete closed tree (≤512 original files and≤8MiB including catalog), pins bytes and membership before/after, and invokes only the existing unchanged `C:\issues8\Guest-Export.ps1` once. Its actual fixed SHA is in Current-Lib/Export preparation. The existing exporter supplies the only SerialExport marker and COM1 frames; no whole serial log, host/private VM catalog, credentials or unrelated files are read. No files are omitted to fit. Oversize, changed or already exported evidence stops and remains intact for an explicit complete batching plan. A failed serial send remains failed; never rerun unchanged export blindly.

The wrappers establish transport/copy/launcher closure checks only. They cannot establish a GUI gesture, application pass, old MessagePort four-arm qualification, tray focus recovery, long residency, DevTools completeness or production readiness. All test judgments remain with the original actual controller evidence, and all production flags stayfalse.
