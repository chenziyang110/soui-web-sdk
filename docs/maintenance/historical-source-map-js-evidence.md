# Historical source-map-js evidence retirement

Recorded 2026-10-08. The eleven archived lock files below retain `source-map-js@1.2.1`, which is affected by [GHSA-68fv-2mgg-jv7q / CVE-2026-93749](https://github.com/advisories/GHSA-68fv-2mgg-jv7q). The advisory rates the issue high and identifies `1.2.2` as the first patched release. A crafted indexed source map can cause synchronous denial of service during map processing. These are real vulnerable dependency records, not false positives.

The listed historical fixture directories are retired from execution. Their supported uses are read-only forensic inspection and byte/hash verification. Do not install dependencies, build, launch, replay, or refresh evidence inside these directories. The original bytes, raw manifests and previous fixed or signed packages remain unchanged. This is a maintenance policy boundary; the retained script text is not technically disabled and must not be treated as a supported runnable fixture.

Future compatibility, upgrade, interruption and rollback validation must create an independent new fixture using current patched templates and toolchains, with a new fixture identity and evidence. Existing customers' business Web dependencies require their own audit and upgrade. Updating the SDK alone does not update a customer's retained business `web/package-lock.json`.

## Retired locations and preserved byte evidence

All eleven alerts were `open` at capture, transitive npm dependencies, and reported as development scope by GitHub. All resolve `source-map-js` to `1.2.1`; all share GHSA-68fv-2mgg-jv7q / CVE-2026-93749 and the affected range `>=1.0.0, <1.2.2`. Scope metadata describes the archived dependency, not permission to execute it.

| Alert | Repository-relative lock path | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| [57](https://github.com/chenziyang110/soui-web-sdk/security/dependabot/57) | `verification/production-validation/preview11-followup-audit-2026-09-26/raw/web/package-lock.json` | 130657 | `626872d385918a4e6f91f738cd865ae80bc9231102b9936f87a456acd984842d` |
| [58](https://github.com/chenziyang110/soui-web-sdk/security/dependabot/58) | `verification/production-validation/preview11-real-renewal-build-2026-09-26/raw/attempts/x64-attempt1/toolchain/package-lock.json` | 122928 | `072d216489333de113f7d4177ed327f082709f72836893b60c53b46c98a3da8a` |
| [59](https://github.com/chenziyang110/soui-web-sdk/security/dependabot/59) | `verification/production-validation/preview11-real-renewal-build-2026-09-26/raw/attempts/x86-attempt1/toolchain/package-lock.json` | 122928 | `072d216489333de113f7d4177ed327f082709f72836893b60c53b46c98a3da8a` |
| [60](https://github.com/chenziyang110/soui-web-sdk/security/dependabot/60) | `verification/production-validation/preview11-real-renewal-build-2026-09-26/raw/attempts/x86-attempt2/toolchain/package-lock.json` | 122928 | `072d216489333de113f7d4177ed327f082709f72836893b60c53b46c98a3da8a` |
| [61](https://github.com/chenziyang110/soui-web-sdk/security/dependabot/61) | `verification/production-validation/preview11-real-renewal-build-2026-09-26/raw/attempts/x86-attempt3/toolchain/package-lock.json` | 122928 | `072d216489333de113f7d4177ed327f082709f72836893b60c53b46c98a3da8a` |
| [62](https://github.com/chenziyang110/soui-web-sdk/security/dependabot/62) | `verification/production-validation/preview11-real-renewal-build-2026-09-26/raw/attempts/x86-attempt4/toolchain/package-lock.json` | 122928 | `072d216489333de113f7d4177ed327f082709f72836893b60c53b46c98a3da8a` |
| [63](https://github.com/chenziyang110/soui-web-sdk/security/dependabot/63) | `verification/production-validation/preview11-real-renewal-v2-build-2026-09-26/raw/attempts/x64-attempt1/toolchain/package-lock.json` | 122928 | `072d216489333de113f7d4177ed327f082709f72836893b60c53b46c98a3da8a` |
| [64](https://github.com/chenziyang110/soui-web-sdk/security/dependabot/64) | `verification/production-validation/preview11-real-renewal-v2-build-2026-09-26/raw/attempts/x86-attempt1/toolchain/package-lock.json` | 122928 | `072d216489333de113f7d4177ed327f082709f72836893b60c53b46c98a3da8a` |
| [65](https://github.com/chenziyang110/soui-web-sdk/security/dependabot/65) | `verification/production-validation/preview11-real-renewal-v2-build-2026-09-26/raw/toolchain/package-lock.json` | 122928 | `072d216489333de113f7d4177ed327f082709f72836893b60c53b46c98a3da8a` |
| [66](https://github.com/chenziyang110/soui-web-sdk/security/dependabot/66) | `verification/production-validation/preview11-upgrade-interruption-2026-09-26/raw/business-source/web/package-lock.json` | 130657 | `626872d385918a4e6f91f738cd865ae80bc9231102b9936f87a456acd984842d` |
| [67](https://github.com/chenziyang110/soui-web-sdk/security/dependabot/67) | `verification/production-validation/preview11-upgrade-interruption-2026-09-26/raw/old-generator/templates/desktop-app/web/package-lock.json` | 130655 | `8fe6b1e6386cccfb071b26ee5fd7ab3ba426b3a2efb14365c20b5e37f91e47d1` |

The containing fixture families are `preview11-followup-audit-2026-09-26`, `preview11-real-renewal-build-2026-09-26`, `preview11-real-renewal-v2-build-2026-09-26`, and `preview11-upgrade-interruption-2026-09-26`, under `verification/production-validation/`. Their historical execution entry points are also retired. Read-only verifiers may inspect them; no historical runner or archiver should regenerate their contents.

Each observed size/hash above equals the corresponding entry in its existing `raw-manifest.json`. The v2 renewal manifest uses `files[].archivedPath`; the other three use `files[].path`. The [maintenance index](../../verification/production-validation/0.5-maintenance-2026-10-08/index.json) binds the exact manifest entry key, path field, declared size/hash, observed size/hash and byte-identical inert manifest snapshot. No lock or historical manifest was rewritten to obtain a match.

## Current lock sources

The GitHub default branch was independently read before and after capture as `main@05e7d93b6bb36904cfabaea6147552b4d62dfb70`. Its six active lock blobs were read with `git show <verified-main-SHA>:<path>`; the full commands and retained bytes are in `mainLocks` in the index. They contain four patched `1.2.2` resolutions and two locks without this package:

| Main lock path | source-map-js | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| `sdk/web/package-lock.json` | absent | 735 | `c3fd19039e976dcb639b830d5947f398e321d2317889df868f6eecd238e1176d` |
| `templates/desktop-app/web/package-lock.json` | 1.2.2 | 130655 | `56553b7df7d73417629ddb7459df06c0ff1274a745f35bfedde8d28376714d33` |
| `templates/modern-app/web/package-lock.json` | 1.2.2 | 28768 | `366801daa343066c7d1b4f6d5a61758662449929f0c55b933fda70d35a2cccde` |
| `demos/workbench/web/package-lock.json` | 1.2.2 | 167788 | `74ff68042f8b210cac922db10969a7acb0d13ae2ab1b9f7aa1178e7c901376ac` |
| `tests/acceptance/package-lock.json` | 1.2.2 | 122928 | `a52a6433c859865dd884101e3340723afb4492e4ba89bec31b7158bdd7701a66` |
| `benchmarks/hosts/electron/package-lock.json` | absent | 5786 | `db51b0767e5a03de455d8dae6beb3719ec3ab60f458346caa6fb460c190307f4` |

The six active locks in the integration worktree `F:/work/sm05` were read separately at HEAD `2154ed80ebe5825e398b87992d0f666d28bab260`, including its working changes. Their resolutions are the same as main; the integration `sdk/web` metadata has a different hash (`91acf56b6e121149ca01cec69b3d447ab33ad439a992241da6ca9f75ecb00bf0`). The index records all six integration hashes and snapshots rather than equating a worktree with a commit.

The optional GUI slice was read from `F:/work/sm05-gui`, whose baseline HEAD was `05e7d93b6bb36904cfabaea6147552b4d62dfb70`; its bindings and locks were working-tree additions at capture, not asserted to exist in that commit:

| GUI working-tree lock path | source-map-js | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| `sdk/bindings/react/package-lock.json` | absent | 3436 | `c962e202a795a3305b873a71969a168fae0fcbf9f52ad470661474efbfd36172` |
| `sdk/bindings/vue/package-lock.json` | 1.2.2 | 11782 | `de67abd439c5b6957db8302b5b8cac71b03747bfdd6e870f0741e21fabc08e97` |
| `tests/gui-bindings/package-lock.json` | 1.2.2 | 32159 | `2eea6d481d924a344565ac38c33ccf4c9b3ae04556e60889ea344362a37b1cdd` |
| `tests/gui-bindings/react18/package-lock.json` | absent | 4311 | `b8e9391cce89e8e816c3cb5d431945cecd9394e5190f456fa5b094595963c669` |

This maintenance pass checked raw lock contents and this advisory's version range. It did not run a new `npm audit` or install dependencies. Earlier separately recorded audit results must retain their own identity and timing; they are not a fresh audit of this maintenance record or a claim about every artifact.

## Execution-path evidence

The following line numbers refer to the byte-bound source snapshots captured in the maintenance index, not an assertion that later source edits cannot change them.

| Entry point and captured lines | Observed behavior |
| --- | --- |
| `tools/Build.ps1:39-49` | Explicit Web roots are `sdk/web` and either the generated project's `web`, or the current desktop/modern templates and Workbench. `npm ci` uses only these selected roots; it does not discover archived `raw` locks. |
| `tools/New-Project.ps1:31-40` | Copies the selected current template from the generator's source tree, excluding build/install directories. Selecting an old SDK package does not select the archived old generator. |
| `tools/sdk/soui-sdk.cjs:15-17,35` | SDK-managed roots exclude `verification` and customer business `web`. The SDK's install/upgrade does not repair a retained customer's Web lock. |
| `tools/Verify-Release.ps1:19,95-100,213-219` | The default previous SDK package is `dist/sdk/0.3.0-preview.10`. Fresh and upgrade projects use the current `New-Project.ps1`; upgrade/rollback switches SDK packages in a newly generated independent fixture. It does not invoke the eleven historical lock locations. No previous package was built or exercised in this maintenance pass. |
| `tools/Test.ps1:16-32`; `tools/Test-Acceptance.ps1:28` | Standard testing uses explicit SDK/Web/acceptance tests and the current native build. Acceptance installs its current `tests/acceptance` lock. The inspected entry points contain no installation path to the retired archive fixtures. |
| `tests/acceptance/verify-upgrade.cjs:57-81` | Reads retained business files and hashes evidence; it does not install or build archived dependencies. This forensic role remains permitted. |
| `tools/Package.ps1:15-21`; `tools/internal/source-archive.cjs:117-130` | The full source archive copies source-manifest entries byte-for-byte and archives them. Copying historical locks is not installing them: full source delivery still contains these vulnerable archived records. |
| `tools/Package-Applications.ps1:15`; `tools/sdk/soui-sdk.cjs:15-17` | Application staging selects an application payload, while SDK packs select managed SDK roots. Their standard staging/install paths do not execute these historical fixtures. This is not a vulnerability assessment of all old package contents. |
| Both `preview11-real-renewal[-v2]-build-2026-09-26/raw/Build.ps1:124-130` | Preserved historical runners explicitly prepare an attempt and run `npm ci` in its toolchain directory. They remain executable text and are now retired by this policy. |
| `preview11-upgrade-interruption-2026-09-26/raw/Prepare.cjs:20-26`; `raw/Run-Stage.cjs:43-45` | The old runner reconstructs the old generator and invokes it, then builds the retained business project with Web enabled on x86. It could install the old business/template lock if manually replayed. Its execution role is now retired. |

All paths in the last two rows are beneath `verification/production-validation/`. Neither dependency-update ignore rules nor the absence of a standard invocation removes an existing dependency-graph alert by itself. These eleven archived paths must not be described as repaired merely because active templates are patched.

## Captured responses and disposition boundary

The [report directory](../../verification/production-validation/0.5-maintenance-2026-10-08/README.md) preserves the exact stdout/stderr bytes of four read-only GitHub GET requests, their executable/arguments, start/end time, exit code, and SHA-256. No credentials or auth headers were captured. The open-alert response binds each alert number, full manifest path, advisory and scope. The advisory response and two main-commit responses bind advisory facts and the six default-branch lock sources. The index also preserves 32 byte-identical source snapshots and binds their original root/path/size/hash list to SHA-256 `3f5e3abcac8e85fec82c218b672614a6c0720837a1ff94b274d1b1d1ec0e7c3f`; the selected source bytes were unchanged during capture.

GitHub disposition was not changed by this record. After maintainer review of the retirement policy, the proposed disposition is limited to alerts **57–67** at the exact listed paths, using `not_used`: the dependency remains present in preserved evidence and is no longer used in a supported execution path. This does not mean false positive, patched historical bytes, harmless to execute, or vulnerability-free full archives. The maintainer must record any actual GitHub change separately; the captured `open` state is historical, not a live status claim. GitHub documents the [alert management process](https://docs.github.com/en/code-security/how-tos/manage-security-alerts/manage-dependabot-alerts/view-dependabot-alerts) and the [available REST disposition reasons](https://docs.github.com/en/rest/dependabot/alerts).

No new native build, GUI/VM run, historical fixture execution, npm installation or audit was performed. Source/service independent review and native runtime evidence belong to their separately identified records and are not duplicated or certified by this maintenance report. No existing raw evidence or previous fixed/signed package was modified.
