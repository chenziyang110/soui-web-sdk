# Pending combined delivery proposal

The current candidate C:/work/sm05-release-050 remains unchanged. This folder copies all nine files from sm05-uia-delivery-1/proposal and adds the shared CMake log fix. It supersedes v1 only after sealing; PROPOSAL.json does not yet exist.

New actual failure: Source24 x64 Debug desktop stopped because Release fresh configure changed out/x64/CMakeFiles/CMakeConfigureLog.yaml. All other current observation evidence pins scanned for x64 Debug remained unchanged. Original full pipeline 95062 stopped at that point. Do not relabel its failure.

New implementation files: tools/internal/Observe-LlvmBuild.ps1; tools/internal/configure-log-evidence.cjs; tools/internal/Capture-CMakeConfigureLog.ps1; docs/adr/0111-immutable-cmake-configure-log.md. Existing v1 llvm-build-observer.cjs and llvm-evidence-readers.test.cjs additionally changed. All edits remain private.

Capture code copies the live shared log to a create-new observer-owned snapshot, checks both raw identities after copy and before final observation write. Reader validates exact source/snapshot ownership, byte equality, unique evidence membership and actual observer -ObservationPath context. Current cache, compiler, source, object and binary pins are retained. Legacy records still require their original live log. New helper source pins included in codeNames. Historical priorObservation validates new capture binding as well.

Original actual CMake failure is RED. integration-red.txt is a separate pure RED: replacing only the reader with v1 silently accepts a forged new capture field; integrated reader rejects it. integration-green2.txt is 14/14 PASS. Actual repeated CMake probe ACTUAL-LOG-RESULT.json: natural 0/0; two fresh configurations really changed the shared log, first snapshot stayed byte-identical, overwrite refused. This probes the production capture helper with exact original production Need/Plain/ObsPin/MatchPin functions extracted by AST; not a full SDK observation or retroactive Source24 recovery.

Other latest pure results: standard-build.test.cjs.txt 46; host64-build-debug-observer.test.cjs.txt 175; devkit-command.test.cjs.txt 24; build-source-manifest.test.cjs.txt 6; sdk-build-inputs.test.cjs.txt 2. All passed. llvm-profile-inputs.test.cjs.txt FAILED because the private override harness did not redirect fs.copyFileSync for newly added helper files (candidate intentionally lacks them). Harness now maps source-side copyFileSync to proposal; current rerun exec58950 writes llvm-profile-green2.txt. Need inspect final result. Three proposed PowerShell sources parsed successfully, including actual observer and new capture helper.

Next: inspect profile rerun, inspect diff / strengthen any remaining integration case, record checks and raw pins, seal combined proposal (13 files expected). Keep v1 sealed packet and both actual installed UIA proofs unchanged. Do not mutate active Source24 before finite calibration/rest finishes.
