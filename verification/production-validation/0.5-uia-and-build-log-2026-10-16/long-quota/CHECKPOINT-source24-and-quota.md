# Continuation — Source24 pipeline and long-quota proposal (2026-10-16 02:51 local)

Read this before C:/work/sm05-legal-text-fix-1/CHECKPOINT-source24-2.md. User still requires finish 0.5.0 then release, no more display questions; physical 100+Hz not claimed. No commit/push/release/new PR/goal/automation/subagent created. Candidate C:/work/sm05-release-050 remains frozen and unchanged during this continuation. C about75GiB free; F and D near full.

## Active

ONLY exec **95062** is running C:/work/sm05-legal-text-fix-1/run-source24-2.cjs. Progress C:/work/sm05-source24-pipeline-2/pipeline.ndjson. Source create (58,912 files), verify, manifest stage, request and ALL web builds passed. x64 Debug actual Build PASS natural0/0,734994ms,770 owned processes,active0,no cleanup. x64 Debug CTest **187/187 PASS**,58.88sec. x64 Release Build started02:49:30, now actual compiler started. Remaining pipeline unchanged: Release tests/unit, both x86 configurations, all4desktop/Modern/readers, bothSDK, visible cachecomparison, both100reload/calibration+quota,dependencies,reference web/native/controller/GUI. No functions.wait pending. All earlier sessions remain closed per prior checkpoints.

## New implementation prepared OUTSIDE frozen candidate

Folder **C:/work/sm05-long-quota-1**. **PROPOSAL.json** pins13 changed files and originals in before/. Proposal root proposal/. Not adopted or staged. Source24 manifest unchangedSHA52e19555127cff41afabb4ee9e7b0af662630cbdfce35f270d0d4753473d1cf4 (15,734,831B).

Actual gap: tests/acceptance/task-store-request-quota.cjs only accepted reload binding/v1 and one-window protocol, so long validation ignored the actual emitted128MiB quota DTO. Proposal:
- refactors only shared decoder/binding envelope; original reload protocol/32MiB/8192row limits and output preserved.
- new analyzeLongTaskStoreQuota raw reader with separate long binding/1; fixed mixed/session policy and128MiB,256MiB/1m-row parsing bounds; strictUTF8/LF/duplicateJSON rejection, rawSHA/fullinput/EXE/subject binding.
- all live/retired owner DTOs; actual native caller clocks/identity; fixed epochs per lifetime and no reuse after retirement; same-slot/new-life supported; exact cycle/sample roster, peak/cumulative/credit checks; busy/missing NE, knownbad failed. Does NOT require retired pre-destruction bytes0 or claim final disposal.
- recheckLongTaskStoreQuota over retained parsed records is explicitly only consistency checking, grants no raw-binding proof.
- long policy revision **0.5.0-m0-task-request-quota-v3**, same frozen times/loads; required taskStoreQuota gate. Long validator actually calls raw reader, stores result and checks recomputation plus source/payload/consumer/log/subject when classifying. Full cacheBudget remainsNE (unresolved broad scope), not silently waived.
- Test.ps1 + Collect reader group, new28tests + fixture, existing70long tests now include2integration tests and assertnewrevision/default; docsADR0108/README/testing/longpolicy/CHANGELOG inproposal. ADRstatus explicitly proposed/notadopted; update when legitimately adopted.
- No SDK C++/ABI/kernel/publicruntime behaviour changed.

Actual validation (pure data/source, NOT native/eight-hour): **259 checks PASS** =28new +70long +12cache +43resource +106originalreload. PowerShell AST and Collect syntax PASS. red.txt shows originalmissingfeature; integration-red.txt showsmissingrequiredgate. Added2coverage negatives firstreproduced incorrectlypassed, coverage-red.txt; fixed cycle sampling/close-cycle, coverage-green.txt28PASS. All originalrecords retained.

Private harness **run-regression.cjs** loads proposal overrides while retaining actual candidate root for unchanged files. It now transitively loads candidate.cjs dependencies through theoverride map (earlierresource-regression was baseline only; **resource-final.txt** is actual proposal integration and43PASS). Final records integration-final.txt70,cache-final.txt12,resource-final.txt43,legacy-final.json106; coverage-green.txt28. PROPOSAL sealedafterthese and source24 pinchecked. It is not a public launch or actual native proof.

Do not mutate candidate while95062 runs. Prefer finish finite calibration first, then batch needed budget/reader/docs changes; when adopting verifyeveryPROPOSAL beforepin, currentafterpins, preserve raw evidence and use archive -text rule before Git index add. Newsourceidentity invalidates current-source validators, so avoid pointless repeated fullfreeze/builds. Need true current100reloadcalibration and broadcache work before full8h can pass; this proposal alone does notclosefullcache budget.

## Other readonly findings this continuation

- Cognition compass in F root reports status exists but graphDBmissing. Do not start uninvoked sp-workflow; use bounded live reads. learning start --command implement works (release-0.5.0 invalid); selected archived-gitattributes-normalization was read fromFroot. Nativeplan hasallP0gatesunchanged exceptuserwaivershighrefreshhardware/signing.
- No newlicense files or licensing conclusions adopted. Investigated lodash-unified1.0.3: npmofficialdeclaresMIT/JackWorks and archivehasnoLICENSE; npmofficial docs accept SPDXlicensefield; SPDXcanonicalMITtext https://raw.githubusercontent.com/spdx/license-list-data/v3.29.0/text/MIT.txt hasplaceholdercopyright, notupstreampackagecopyright. Do NOT pretendthisisrecoveredupstreamtext. Existinggapremains.
- Currentlicense-review.json stillhistoricalpreview14unverified; currentnativeSBOM158filecomponents includes136delivered+22actualarchiveinputs, deliberatelyincompletewholebinarylicensereview. No firstpartylicensechange. PrivateGitHubvisibilityunchanged.
- Six existingunrelatedQEMU processes mustnotbe touched; priorcheckpoint namesPIDs.

Most recent commentary02:49: x64 Debug buildpassedand187testsrunning; longquota integrationchecks259passed, independentproposalonly. Continuework, no final yet.

## Delta02:53 — proposal v2

Use **PROPOSAL-v2.json** (13files) instead ofv1; v1 retained. Source24stillfrozen. AdditionalactualpureREDfound `checkedAggregate` couldaccept quota result withsubstituted EXE/Owned/context hashes becauseonlysource/payload/log/subjectwerecrosschecked. Fixed quota binding to equal the entireprivateStorageBinding plusfixedschema/policy/config; existingrunner already independentlyauthenticatesprivateStorageBinding andallSamples againstoriginalrawfiles. Negative3fieldsubstitutiontest firstRED(binding-red.txt), GREEN(binding-green.txt **71/71**). Totalpurechecks**260**(28+71+12+43+106). ADR0108updatedhonestly. Allstillprivateproposalnotadopted. PublicTest/Collect wiringandAST/syntaxchecked. x64Debug187/187 passed; x64Releasecurrentbuildongoing.
