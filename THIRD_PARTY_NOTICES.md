# Third-party notices

Retain upstream license and copyright files under third_party/licenses/ and the source trees. The current inventory is third_party/manifests/dependencies.json.

| Component | Materials | License / provenance |
|---|---|---|
| SOUI 2.9.0.3 lineage | Sources, headers, x86/x64 DLLs and import libraries | SOUI upstream license and retained notices |
| Miniblink49 | Sources, local changes patch, headers, x86/x64 preview DLLs | Upstream project plus Chromium/V8/Skia and dependency licenses; an API header declaration alone does not cover the complete binary |
| nlohmann/json | Single header | MIT, notices in header and licenses |
| zlib 1.2.11 | Source and runtime dependency | zlib license retained in source |
| Microsoft VC runtime / UCRT | App-local binaries | Microsoft redistribution terms; compiler, Windows SDK and OS source are not included |
| React / Vue / Vite / TypeScript and dependencies | Application source and npm lockfiles | Consult exact resolved npm package licenses; retain notices with distributed tooling/assets |

The x86 kernel imports Windows msvcp60.dll; this delivery does not redistribute that OS file. Source snapshots include provenance in SOURCE-MANIFEST.json.

third_party/manifests/license-review.json records the inherited, incomplete binary redistribution review. The [fixed-kernel source notice inventory](verification/production-validation/kernel-notice-inventory-2026-09-25/README.md) identifies additional in-tree libxml, libpng and c-ares text, as well as imported libcurl, libjpeg and OTS source files that refer to absent companion license files. This source delivery does not certify redistribution readiness. First-party licensing status is recorded in LICENSE.md; third-party terms apply independently.
