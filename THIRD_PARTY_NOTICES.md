# Third-party notices

Retain upstream license and copyright files under third_party/licenses/ and the source trees. The current inventory is third_party/manifests/dependencies.json.

| Component | Materials | License / provenance |
|---|---|---|
| SOUI 2.9.0.3 lineage | Sources, headers, x86/x64 DLLs and import libraries | SOUI upstream license and retained notices |
| Miniblink49 | Sources, import and post-import patches, headers, x86/x64 preview DLLs | Upstream project plus Chromium/V8/Skia and dependency licenses; an API header declaration alone does not cover the complete binary |
| nlohmann/json | Single header | MIT, notices in header and licenses |
| zlib 1.2.11 | Source and runtime dependency | zlib license retained in source |
| Microsoft VC runtime / UCRT | App-local binaries | Microsoft redistribution terms; compiler, Windows SDK and OS source are not included |
| React / Vue / Vite / TypeScript and dependencies | Application source and npm lockfiles | Consult exact resolved npm package licenses; retain notices with distributed tooling/assets |
| Tailwind CSS 3.4.17 generated utility rules | Static Demo stylesheet; build plugin removed | MIT; original copyright and license retained in `third_party/licenses/tailwindcss-3.4.17-LICENSE.txt` |

The x86 kernel imports Windows msvcp60.dll; this delivery does not redistribute that OS file. Source snapshots include provenance in SOURCE-MANIFEST.json. The source SDK includes this notice and `third_party/licenses/`; CMake installation copies them to `share/SouiSdk/`, and application deployment copies them to `resources/legal/`. Those copies are current materials for review, not a complete inventory of the runtime's linked components.

third_party/manifests/license-review.json records the inherited, incomplete binary redistribution review. The [fixed-kernel source notice inventory](verification/production-validation/kernel-notice-inventory-2026-09-25/README.md) identifies in-tree libxml, libpng and c-ares text. The [libcurl, IJG and OTS source notice recovery](verification/production-validation/kernel-notice-recovery-2026-09-25/README.md) adds version-matched original texts under `third_party/licenses/`. These materials do not certify redistribution readiness. First-party licensing status is recorded in LICENSE.md; third-party terms apply independently.
