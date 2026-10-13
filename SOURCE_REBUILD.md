# Full-source archive inputs and rebuild boundary

The portable SDK contains SDK/application code and historical prebuilt payloads. A full-source archive additionally carries the complete selected Native tree at `third_party/sources/miniblink` and the standard SOUI tree at `third_party/sources/soui`. Its top-level `source-manifest.json` lists every delivered content file. The Native `SOURCE-MANIFEST.json` and `LOCAL-CHANGES.patch` may describe an older upstream lineage; they do not replace the top-level current inventory. Source verification does not accept a compiler, current DLL, license review, runtime or release gate.

An unpacked full-source archive is already composed. Verify it with `node tools/internal/source-archive.cjs verify`; export it into a fresh external directory with `node tools/internal/source-archive.cjs stage D:/export/full-source`. No SourceRoots binding or Git checkout is needed for these operations. The existing `tools/Build-Dependencies.ps1 -Architecture x64|x86 -Component All|Miniblink|SOUI` uses the standard source locations and retains the default MSVC route. The explicit `-NativeToolchain LLVM22MSVC142 -Component Miniblink` adapter supports Release with GPU disabled, one x86 or x64 architecture per invocation. Its tool paths, supported scope and example are documented in [PUBLIC-KERNEL-LLVM.md](PUBLIC-KERNEL-LLVM.md). The adapter is implemented; complete public rebuild and runtime qualification remain pending. Packaging must not be reported as a successful LLVM rebuild.

For maintainers assembling a new full-source archive from separately frozen source inputs, use:

```powershell
./tools/Package.ps1 -SourceRootsBinding D:/bindings/full-source.json -PackageWorkspace D:/packages/new-full-source
```

`PackageWorkspace` must be a fresh absolute external directory using the existing local Windows drive-path contract (`D:/...`); UNC workspaces are not supported. The source stage, archive logs and `dist/soui-sdk-<actual-staged-version>-full-source.zip` are created there. Optional `-OutputFile` must remain inside that workspace's `dist` directory. The actual staged SDK VERSION supplies the archive version, even if the tool host has another VERSION. No input source directory is a package workspace. Without `SourceRootsBinding`, the original Git/full-manifest route and host `out/package`/`dist` layout remain available.

The binding is data with exactly these fields (replace example paths, sizes and 64-digit lowercase SHA-256 values):

```json
{
  "schema": "soui.full-source-roots/v1",
  "sdk": {"root": "D:/source/sdk", "manifest": {"path": "D:/bindings/sdk-raw.json", "bytes": 1234, "sha256": "<64 lowercase hex>"}},
  "miniblink": {"root": "D:/source/miniblink", "manifest": {"path": "D:/bindings/miniblink-raw.json", "bytes": 2345, "sha256": "<64 lowercase hex>"}},
  "publicSource": {"root": "D:/source/public", "manifest": {"path": "D:/bindings/public-full-source.json", "bytes": 3456, "sha256": "<64 lowercase hex>"}}
}
```

The SDK and Miniblink manifests can use the public `soui.raw-source/v1` data format:

```json
{"schema":"soui.raw-source/v1","sourceOnly":true,"root":"D:/source/sdk","files":[{"path":"VERSION","bytes":17,"sha256":"<64 lowercase hex>"}]}
```

The example row alone is not a complete inventory: `files` must contain every regular file under the selected root, using exact relative forward-slash names, raw byte lengths and hashes. Linked paths, duplicate/case-colliding names, root generated/private directories, missing/edited/undeclared files and mismatched owner roots are rejected. Keep the inventory file outside that source root. Freeze a coherent SDK containing all reviewed packaging changes first; an inventory of an older SDK does not include a later repair merely because the packing tool has it.

Existing filesystem-only identity functions can generate that public raw inventory without Git, for example from the reviewed tool host:

```javascript
const fs = require('node:fs');
const id = require('./tools/internal/delivery-identity.cjs');
const root = id.safePath('D:/source/sdk');
const files = id.snapshot(root, id.walk(root)).files;
fs.writeFileSync('D:/bindings/sdk-raw.json', id.encoded({schema:'soui.raw-source/v1',sourceOnly:true,root,files}), {flag:'wx'});
```

Compute the exact manifest file size and SHA-256 into the external binding. Review/freeze those inputs independently before packaging. The historical SDK raw-renderer-v2-abi and full-kernel-composed raw manifest formats are also accepted as input adapters; neither is required by a public caller or grants build qualification.

`publicSource.manifest` is a pinned `soui.full-source/v2` manifest with matching VERSION and `releaseReady:false`. Only four fixed subtrees are selected: `third_party/sources/soui`, `.github`, `experiments/webview2-consumer`, and `third_party/patches/miniblink`. Every selected row must exist and match the original raw or declared canonical LF bytes, and each selected subtree must have exactly those files. Unselected repository directories (including historical `verification/**` and private `out`) are never imported. The locator root may contain the separate SDK/Native roots, but the selected source subtrees and output must be disjoint.

The composition authenticates manifest pins before parsing and again after copying, rechecks every selected source member and exact raw inventories, and verifies the fresh full-source stage. Every fresh stage must contain exactly the declared Source files plus its manifest; an unexpected generated cache in that new stage rejects export. A consumed full-source directory may subsequently contain the original generated `out`, `dist`, or other excluded build caches: ordinary verify/stage retain the existing cache exclusion rule and do not copy those caches into a re-export. Immutable output-like trees explicitly named by Source manifest rows are still fully checked, and undeclared real source files remain errors. Explicit SourceRoots inputs are frozen exact raw trees and cannot gain those caches after their inventories are frozen.

The SDK's old root `source-manifest.json` is replaced by the newly generated exact public v2 manifest. All other current SDK and complete Native bytes are preserved. Source-only composition keeps `releaseReady:false`, labels prebuilt DLLs historical pending replacement, and leaves current binary provenance null.
