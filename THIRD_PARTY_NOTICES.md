# Third-party notices

Retain upstream license and copyright files under third_party/licenses/ and the source trees. The current inventory is third_party/manifests/dependencies.json.

| Component | Materials | License / provenance |
|---|---|---|
| SOUI 2.9.0.3 lineage | Sources, headers, x86/x64 DLLs and import libraries | SOUI upstream license and retained notices |
| Miniblink49 | Sources, import and post-import patches, headers, x86/x64 preview DLLs | Upstream project plus Chromium/V8/Skia and dependency licenses; an API header declaration alone does not cover the complete binary |
| nlohmann/json | Single header | MIT, notices in header and licenses |
| zlib 1.2.11 | Source and runtime dependency | zlib license retained in source |
| Microsoft VC runtime / UCRT | App-local binaries | Microsoft redistribution terms; compiler, Windows SDK and OS source are not included |
| Microsoft.Web.WebView2 1.0.4258.31 (optional) | Pinned SDK package restored explicitly; static loader linked only by the modern component | Microsoft SDK LICENSE and NOTICE retained; browser Runtime distributed separately under its own terms |
| React / Vue / Vite / TypeScript and dependencies | Application source and npm lockfiles | Consult exact resolved npm package licenses; retain notices with distributed tooling/assets |
| Element Plus 2.14.7 | Workbench component assets; exact version pinned in its npm lockfile | MIT; exact upstream license retained in `third_party/licenses/element-plus-2.14.7-LICENSE.txt`; older version notices remain for historical deliveries |
| Tailwind CSS 3.4.17 generated utility rules | Static Demo stylesheet; build plugin removed | MIT; original copyright and license retained in `third_party/licenses/tailwindcss-3.4.17-LICENSE.txt` |

The x86 kernel imports Windows msvcp60.dll; this delivery does not redistribute that OS file. Source snapshots include provenance in SOURCE-MANIFEST.json. The source SDK includes this notice and `third_party/licenses/`; CMake installation copies them to `share/SouiSdk/`, and application deployment copies them to `resources/legal/`. Those copies are current materials for review, not a complete inventory of the runtime's linked components.

third_party/manifests/license-review.json retains the historical preview.14 binary review with status=unverified and releaseReady=false. The [kernel material index](third_party/licenses/KERNEL-NOTICES.md) locates original declarations for the twelve D52 source/build components and identifies archived preview evidence. These files are review materials, not approval of any new DLL. First-party licensing status is recorded in LICENSE.md; third-party terms apply independently.
