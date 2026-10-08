# Historical dependency maintenance evidence, 2026-10-08

This directory supports the [retirement policy](../../../docs/maintenance/historical-source-map-js-evidence.md) for the eleven archived `source-map-js@1.2.1` alerts 57–67. It preserves read-only capture evidence; it is not a runtime validation suite or a fresh dependency audit.

- `index.json` binds GitHub GET commands/results, source identities, all eleven observed lock sizes/hashes and their existing manifest entries, six verified-main lock blobs, ten integration/GUI working-tree locks, and 32 original source snapshots.
- `queries/*.response.json` and `*.stderr.log` are unchanged response bytes. All four requests exited zero. No credential values, auth headers, or complete process environment were recorded.
- `snapshots/**/*.inert.snapshot` are unchanged source bytes under inert names. They are data for inspection, not nested runnable projects, package-manager lock names, or Git attribute files. Original archived lock files remain in their original locations and are not duplicated as active locks here.
- `Verify-Evidence.cjs` only reads files and checks capture consistency. It does not spawn commands, install, build, query GitHub or execute historical scripts. `--source-root` adds a check of the eleven live historical lock bytes at a relocated source root. Other live worktree files may legitimately change after capture; their immutable snapshots identify the analyzed source.

Run the offline hash and cross-reference check from the repository root:

```powershell
node verification/production-validation/0.5-maintenance-2026-10-08/Verify-Evidence.cjs --source-root .
```

Omit `--source-root .` to inspect the stored evidence alone. The verifier's stdout is a hash-consistency result, never a native/runtime/production acceptance result. Its success does not authenticate GitHub independently or establish that current alert states match the captured response. `index.json` itself is not self-hashed; external repository/release provenance binds the index and this directory.

Source capture started `2026-10-08T13:10:44.984Z` and finished `2026-10-08T13:10:50.115Z`. Default-branch queries before/after agreed on `main@05e7d93b6bb36904cfabaea6147552b4d62dfb70`. The integration HEAD was `2154ed80ebe5825e398b87992d0f666d28bab260`; captured integration changes are identified by exact bytes rather than assumed committed. GUI locks were captured from working additions in `F:/work/sm05-gui` at baseline HEAD `05e7d93b6bb36904cfabaea6147552b4d62dfb70`.

`sourceSnapshotBindingSha256` is computed as SHA-256 of UTF-8 `JSON.stringify(sourceSnapshots.map(s => ({root: s.originalRoot, path: s.originalPath, bytes: s.originalBytes, sha256: s.originalSha256})))`, preserving recorded order. This binds analyzed source context in addition to each snapshot's individual hash.

The retirement applies to exact archived fixtures, preserves original bytes, and requires future compatibility/upgrade/rollback work to use new independently identified fixtures from current patched templates/toolchains. SDK upgrading does not patch a customer's retained business Web lock. Complete source archives still contain the preserved vulnerable locks; existing fixed/signed packages remain untouched. `not_used` is a maintainer-review recommendation scoped to the eleven retired archived alerts, not an applied GitHub change or a repaired-dependency claim.

No fresh npm audit, dependency installation, historical runner, native compilation, GUI or VM scenario was executed. Independent source/native evidence is tracked separately. Root attributes must preserve captured response and snapshot bytes when storing the report; a byte-normalized checkout will correctly fail the verifier.
