This is a data-only recipe for Root review. It has not executed the material composition, copied any canonical archive, read ZIP bytes, or run builds, products, tests, or a VM.

The existing published 0.6.0 facts already verify all six application archive extractions. The recipe reuses those exact archive/sidecar tuples and the source SDK's 1496-member manifest and two 473-member installed-prefix closure records. It reads only their small metadata and the relevant sealed material subset. It does not repeat extraction, archive hashing, or whole-source/prefix closure verification.

The common set is 206 declared `resources/legal/` files per application. Each stripped relative path must match the pinned source SDK manifest and the physical sealed source and source SDK. Both installed prefixes contain 206 original paths, 203 license aliases, and three WebView2 supplement files: 412 material files each. Including the source-only WebView2 dependency entry gives 207 selected files per source root and 1238 physical selected files in total. A missing file, changed pin, changed input identity, unexpected source relation, or count mismatch produces FAIL and retains the attempted result.

Raw equality is kept distinct from two reviewed text relations:

- Installed `THIRD_PARTY_NOTICES.md` has different raw bytes. Only the exact five CMake link rewrites from `](third_party/licenses/` to `](licenses/`, with LF/CRLF compared as text, are permitted. The captured EOL representation is recorded; its origin is not inferred.
- The installed WebView2 `LICENSE.txt` is one byte shorter. Only removal of exactly one final LF byte from the source material is permitted. There is no general trimming or normalization.

`InstalledNotice.field-diff.prepared.json` records these small-text preflight observations and the exact five changed lines. `Plan.v2.prepared.json` adds the three WebView2 mappings and the pinned install control to the preserved first plan; both plans remain immutable preparation records. Neither preparation nor byte equivalence grants terms or redistribution approval. Current branch notices and broader runtime inventory/version ambiguities are outside this published-0.6 proof.

After reviewing the exact recipe and plan, Root may execute this single command:

```powershell
node 'F:/work/SOUI_WEB/out/open-issues-closeout-20261029/license-delivery-reuse-current/Run-ReuseComposition.cjs' --root-reviewed ROOT_REVIEWED_PUBLISHED_060_MATERIAL_REUSE_V1 --plan-sha256 f36644214df785062d6f936e5b5bb0a3200437dca423a5e9bfef7bcec8bc5bd4
```

The explicit execution gate leaves the prepared plan's `executionApproved:false` truthful. A fresh `attempt-<uuid>` directory receives only `Inputs.actual.json`, `Summary.actual.json`, and `Files.index.json`. Source, SDK, prefixes, prior evidence, and ZIPs are read-only; failures and old attempts are preserved. All human legal, redistribution, release-ready, and full production flags remain false even when this narrow engineering composition passes.
