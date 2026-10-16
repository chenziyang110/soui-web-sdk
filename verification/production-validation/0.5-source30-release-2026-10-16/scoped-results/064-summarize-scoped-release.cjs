'use strict';
// Runs only after the actual narrowed release checks. No full acceptance flags
// or historical reports are rewritten by this summary.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',main='C:/work/sm05-source30-pipeline-2',post='C:/work/sm05-source30-post-2';
const e=require(root+'/tools/internal/dependency-evidence.cjs'),id=require(root+'/tools/internal/delivery-identity.cjs');
const proofs=[];
function read(file){const pin=e.pin(file);proofs.push(pin);return JSON.parse(fs.readFileSync(file,'utf8'));}
function sameSource(record,source){assert.equal(record.sourceManifest.sha256,source.sha256);e.checkPin(record.sourceManifest);}
function natural(record){assert(record.natural===true);const r=record.result,t=record.terminal;assert(r.passed&&r.exitCode===0&&r.supervisorExitCode===0&&r.closureConfirmed&&!r.stopRequested&&!r.fallbackUsed);assert(t.closureConfirmed&&t.activeProcesses===0&&!t.terminatedOwnedJob);}
const seal=require(main+'/common.cjs').requireSeal(),source=seal.sourceManifest;
const core=read(main+'/BUILD-AND-DESKTOP-VERIFIED.json');
assert(core.source===30&&core.passed&&core.fourConfigurationBuilds&&core.fourConfigurationAndDesktop&&core.sdkInstallBothArchitectures&&core.dependencyRuntimeBothArchitectures);
sameSource(core,source);
for(const row of core.records){e.checkPin(row.record);const actual=read(row.record.path);natural(actual);assert(actual.stable);}
const consumers=[];
for(const arch of ['x64','x86']){
 const unit=read(main+'/unit-'+arch+'-Release-source30-1/RESULT.json');
 assert(unit.ctest.tests===208&&unit.ctest.failed===0&&unit.ctest.skipped===0);e.checkPin(unit.ctest.stdout);e.checkPin(unit.ctest.log);
 const consumer=read('C:/work/sm05-source30-external-consumer-2/'+arch+'-source30-2/RESULT.json');
 assert(consumer.passed&&consumer.source===30&&consumer.architecture===arch);sameSource(consumer,source);consumer.records.forEach(natural);
 e.checkPin(consumer.installedArchive);e.checkPin(consumer.executable);
 if(consumer.originalAttemptFailure){read(consumer.originalAttemptFailure.path);read(consumer.canonicalPublicVerification.path);}
 consumers.push({architecture:arch,scope:consumer.scope,sdkBuildId:consumer.sdkBuildId,originalAttemptFailure:consumer.originalAttemptFailure||null});
}
let modernCompletion;
for(const stage of ['MODERN-SAMPLE','PACKAGES']){
 const completion=read(post+'/POST-CORE-'+stage+'-COMPLETED.json');assert(completion.passed&&completion.source===30);sameSource(completion,source);
 if(stage==='MODERN-SAMPLE')modernCompletion=completion;
}
e.checkPin(modernCompletion.verified);
const modern=read(modernCompletion.verified.path);
for(const correction of [modernCompletion.privateCallerCorrection,modernCompletion.privateStartupCorrection]){
 for(const value of Object.values(correction||{}))if(value&&typeof value==='object'&&value.path&&value.sha256){e.checkPin(value);proofs.push(value);}
}
assert(modern.passed&&modern.actualBackend==='webview2'&&modern.actualSchedulingDiagnostic&&modern.actualCompositorMarkerObservation===false);sameSource(modern,source);
assert(modern.displayBinding.actualWindowOutputMatched&&modern.displayBinding.configuredSignalHz===100);
assert(modern.minimumFrameRateGateEvaluated===false);e.checkPin(modern.nativeHost);
assert(modern.automaticScene===true&&modern.realInputBenchmark===false);
e.checkPin(modern.hostRun);natural(read(modern.hostRun.path));
for(const pin of [modern.nativeHost,modern.built,modern.prepared]){e.checkPin(pin);proofs.push(pin);}
e.checkPin(modern.pageResult);proofs.push(modern.pageResult);
assert(Number.isFinite(modern.observedMedianRafCadenceHz)&&modern.observedMedianRafCadenceHz>0);
assert(modern.observedMedianRafCadenceHz>60,'The actual Modern sample has not demonstrated JS scheduling cadence above 60 Hz');
const sdk=read(post+'/sdk-package-capture-1/VERIFIED.json');assert(sdk.passed&&sdk.sourcePackageVerified&&sdk.records.length===5);sameSource(sdk,source);sdk.records.forEach(natural);e.checkPin(sdk.sdkManifest);
const extracted=read('C:/work/sm05-source30-extracted-applications-2/VERIFIED.json');
assert(extracted.passed&&extracted.applications===6&&extracted.scenarios===8&&extracted.relocatedZipStartup);sameSource(extracted,source);
e.checkPin(extracted.outer);natural(read(extracted.outer.path));e.checkPin(extracted.actual);
const startups=read(extracted.actual.path);assert(startups.passed&&startups.records.length===8);sameSource(startups,source);
for(const record of startups.records){assert(record.natural);e.checkPin(record.capture);natural(read(record.capture.path));e.checkPin(record.log);if(record.origin)e.checkPin(record.origin);}
e.checkPin(extracted.prepared);const prepared=read(extracted.prepared.path);assert(prepared.passed&&prepared.applications.length===6);
const applicationArchives=prepared.applications.map(app=>{e.checkPin(app.archive);e.checkPin(app.report);return {architecture:app.architecture,profile:app.profile,target:app.target,archive:app.archive,report:app.report};});
const deferred=read(post+'/DEFERRED.json');proofs.push(e.pin(__filename));
proofs.forEach(e.checkPin);
assert.equal(fs.readFileSync(root+'/VERSION','utf8').trim(),'0.5.0');
const summary={schema:'soui.scoped-release-checks/v1',version:'0.5.0',completedAt:new Date().toISOString(),scopedReleaseChecksPassed:true,
 scopeAuthority:'Maintainer request on 2026-10-16 to reduce redundant and long-running tests and release within the demonstrated scope.',
 buildSourceManifest:source,currentBuildSourceSha256:id.source(root).sha256,
 builds:{architectures:['x64','x86'],configurations:['Debug','Release'],nativeContractConfigurations:['Release'],nativeCasesPerArchitecture:208,duplicateDirectCTestExecuted:false},
 installedSdkConsumers:consumers,applications:{archives:applicationArchives,startups:8},sdkPackage:sdk.sdkManifest,
 highRefresh:{backend:'webview2',architecture:'x64',scenario:'S05',sampleDurationMs:120000,displayBinding:modern.displayBinding,
  actualCompositorMarkerObservation:false,actualSchedulingDiagnostic:true,automaticScene:true,realInputBenchmark:false,observedRafIntervalsMs:modern.observedRafIntervalsMs,
  observedMedianRafCadenceHz:modern.observedMedianRafCadenceHz,above60HzSchedulingDemonstrated:true,productionFrameRateGateEvaluated:false,opticalCalibration:false,pairedPerformanceQualified:false,
  compositorQualification:'not-passed: strict desktop capture rejected accumulated frames; original failures retained, no assertion relaxed'},
 knownFailure:'The earlier Source29 UIA typed-owner-focus failure lacked an HRESULT. Its cause is unresolved; later passes do not erase the original failure.',
 resourceDeploymentIncident:core.resourceDeploymentIncident||null,
 deferred,fullVerifyReleaseEvaluated:false,fullCollectorEvaluated:false,productionQualificationComplete:false,licenseReviewComplete:false,codeSigned:false,
 releaseReady:false,published:false,finalSourceArchivePending:true,selfContainedBuildProof:false,externalOriginalEvidenceRequired:true,proofs};
const output=path.join(__dirname,'SCOPED-RELEASE-CHECKS.json');fs.writeFileSync(output,JSON.stringify(summary,null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({scopedReleaseChecksPassed:true,output,productionQualificationComplete:false,published:false}));
