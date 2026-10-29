The first reviewed execution failed because recipe v2 treated the source SDK package root as its physical payload root. The package contains `payload/` plus `sdk-manifest.json`; legal materials live in `C:/s06sdk/payload`. This is a recipe path error. All 207 failed reads were at the wrong root, with the resulting count1031 rather than1238. No other mismatch was recorded.

V2 plan, script, and `attempt-980978e4-8027-4770-b664-94fcf114f036` remain unchanged. V3 changes only the physical source root and the associated plan schema, identity, predecessor, and executed filename. `Recipe.v2-to-v3.diff` gives all six changed script lines; `RootPathDelta.v3.prepared.json` pins both versions, the retained failure, and existing source-archive layout proof. The manifest remains `C:/s06sdk/sdk-manifest.json`. Counts206/207/412/1238, input guards, typed text relations, and false legal/production flags are unchanged.

After reviewing v3, Root may execute:

```powershell
node 'F:/work/SOUI_WEB/out/open-issues-closeout-20261029/license-delivery-reuse-current/Run-ReuseComposition-v3.cjs' --root-reviewed ROOT_REVIEWED_PUBLISHED_060_MATERIAL_REUSE_V1 --plan-sha256 4ad52d1643061eede40a7f3ba51b61f1f590153df4017607ddcd5c0c3c4284da
```

V3 has not executed. No ZIP extraction, archive-byte reading, whole-prefix or source-SDK closure test is added. The existing layout proof is metadata reuse, not a new archive validation.
