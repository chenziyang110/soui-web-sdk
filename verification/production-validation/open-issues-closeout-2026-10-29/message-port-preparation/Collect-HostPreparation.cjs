'use strict';

// Preparation evidence only. Copies frozen original bytes; never invokes a runner/test/VM.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const output=__dirname;
const source='F:/work/SOUI_WEB/out/open-issues-closeout-20261029/message-port';
const carrier=source+'/carrier-1249be88-0f1f-4f2f-840f-bb159c691d63';
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const slash=name=>name.replaceAll('\\','/');
function load(name){
 const entry=fs.lstatSync(name,{bigint:true});assert(entry.isFile()&&!entry.isSymbolicLink());
 assert(entry.size<=16777216n,'Only bounded preparation metadata/source/raw logs may be archived');
 const fd=fs.openSync(name,'r');let bytes;
 try{const before=fs.fstatSync(fd,{bigint:true});assert.equal(before.dev,entry.dev);assert.equal(before.ino,entry.ino);
  bytes=fs.readFileSync(fd);const after=fs.fstatSync(fd,{bigint:true});
  for(const field of ['dev','ino','size','mtimeNs','ctimeNs'])assert.equal(before[field],after[field]);
  assert.equal(BigInt(bytes.length),before.size);
 }finally{fs.closeSync(fd)}
 return {bytes,pin:{path:slash(name),bytes:bytes.length,sha256:hash(bytes)}};
}
const readJson=name=>JSON.parse(load(name).bytes.toString('utf8'));
function save(name,value){const bytes=Buffer.from(JSON.stringify(value,null,2)+'\n');fs.writeFileSync(path.join(output,name),bytes,{flag:'wx'});return{path:name,bytes:bytes.length,sha256:hash(bytes)}}
function walk(directory,prefix=''){
 return fs.readdirSync(directory,{withFileTypes:true}).flatMap(entry=>{assert(!entry.isSymbolicLink());
  const name=prefix+entry.name,file=path.join(directory,entry.name);
  return entry.isDirectory()?walk(file,name+'/'):(assert(entry.isFile()),[name]);}).sort();
}
assert(!fs.existsSync(path.join(output,'Files.index.json')),'Preserve the existing preparation snapshot');
const selected=[];
function select(original,relative,classification){assert(!path.isAbsolute(relative)&&!relative.split('/').some(x=>!x||x==='.'||x==='..'));selected.push({original,relative,classification});}
const authorFiles=[
 'BoundedIsoIO.longpath.cs','BrowserABInputs.approved.json','BrowserABInputs.draft.json','Build-Carrier.cjs',
 'CandidateStageInputs.approved.json','CandidateStageInputs.draft.json','Decode-MessagePortExport.cjs',
 'Diagnose-IsoLease.ps1','Diagnose-IsoLease.v1.ps1','Export-MessagePort.ps1','Guest-Copy.ps1',
 'Guest-MessagePort.cjs','Guest-MessagePort.ps1','GuestReadClosure.approved.json','GuestReadClosure.plan.json',
 'IsoR3.boundary.json','IsoTransportDiagnosis.actual.json','LongPathIso.derivative.json','LongPathIso.derivative.v1.json',
 'OriginalPrepareJobs.review-projection.json','Prepare-GuestReadClosure.cjs','Prepare-IsoR3.cjs','Prepare-LongPathIso.cjs',
 'Prepare-ReuseProjection.cjs','ProducerReuse.metadata-review.actual.json','RootBrowserReview.actual.json',
 'RootIndependentReview.actual.json','Run-CandidateStage.cjs','Run-IsoWriter.cjs','Run-IsoWriter.r2.cjs',
 'Run-IsoWriter.r2.review-failed.cjs','Run-IsoWriter.r3.before-udf.cjs','Run-IsoWriter.r3.cjs',
 'Write-IssuesIso.ps1','Write-IssuesIso.r2.ps1','Write-IssuesIso.r2.review-failed.ps1',
 'Write-IssuesIso.r2.review-failed2.ps1','Write-IssuesIso.r3.before-udf.ps1','Write-IssuesIso.r3.ps1'
];
const drafts=new Set(['Diagnose-IsoLease.v1.ps1','LongPathIso.derivative.json','LongPathIso.derivative.v1.json',
 'IsoR3.boundary.json','Prepare-IsoR3.cjs','Prepare-LongPathIso.cjs',
 'Run-IsoWriter.r2.review-failed.cjs','Run-IsoWriter.r3.before-udf.cjs',
 'Write-IssuesIso.r2.review-failed.ps1','Write-IssuesIso.r2.review-failed2.ps1','Write-IssuesIso.r3.before-udf.ps1']);
for(const name of authorFiles)select(source+'/'+name,'host/'+name,drafts.has(name)?'Historical source/review draft; not the final effective writer':
 name.includes('.draft.')?'Preserved non-executing input draft':name.includes('Root')?'Actual Root review record with its original scope':
 'Original host preparation source or frozen metadata; no browser outcome');
for(const name of ['stage-x64-db0b7592-f329-42cc-b29a-eca780a8e08f','stage-x86-8c01d26b-10e6-4cc9-ba4d-00fa529073bd'])
 for(const file of walk(source+'/'+name))select(source+'/'+name+'/'+file,'stages/'+name+'/'+file,'Actual public stage preparation raw/Owned/stdio/context');
for(const name of fs.readdirSync(carrier)){
 const file=carrier+'/'+name;if(!fs.lstatSync(file).isFile()||name.endsWith('.iso'))continue;
 assert(/\.(json|raw|ndjson)$/.test(name),'Unexpected carrier top-level metadata type');
 select(file,'carrier/'+name,'Frozen carrier copy/plan/directory readback or original first ISO failure metadata');
}
for(const name of ['iso-writer-r2-c2d2ed7e-ccbe-45fa-8600-7ef2f5e67091','iso-writer-r3-3be2d976-6f91-4a49-9123-a0875404ebb2']){
 for(const file of fs.readdirSync(carrier+'/'+name)){
  const original=carrier+'/'+name+'/'+file;if(!fs.lstatSync(original).isFile())continue;
  assert(/\.(json|raw|ndjson)$/.test(file));
  select(original,'carrier/'+name+'/'+file,name.includes('r3-')?'Actual final strict-UDF writer raw and proof':'Actual second ISO failure raw and proof');
 }
}
const originalTools='F:/work/SOUI_WEB/out/release-v0.6.0-20261028/vm-short-prep/media-tooldeps-4';
select(originalTools+'/BoundedIsoIO.cs','reference/BoundedIsoIO.original.cs','Exact historical trusted bounded helper, unchanged');
select(originalTools+'/Write-Iso.ps1','reference/Write-Iso.original.ps1','Exact historical trusted writer, unchanged; original separate media plan is not executed here');
assert.equal(new Set(selected.map(row=>row.relative.toLowerCase())).size,selected.length);

// Scan only the selected byte sources, with no sensitive match values in output.
const patterns=[['github-token',/\bgh[pousr]_[A-Za-z0-9]{20,}\b/],['github-pat',/\bgithub_pat_[A-Za-z0-9_]{20,}\b/],
 ['api-key',/\bsk-proj-[A-Za-z0-9_-]{20,}\b/],['bearer',/\bBearer\s+[A-Za-z0-9._~-]{16,}\b/],
 ['credential-value',/"(?:leaseToken|lease_token|password|secret|credential|authorizationToken)"\s*:\s*"[^"\r\n]+"/i]];
const cached=selected.map(row=>({...row,source:load(row.original)}));
const findings=[];
for(const row of cached)for(const [kind,pattern]of patterns)if(pattern.test(row.source.bytes.toString('utf8')))findings.push({path:row.relative,kind});
assert.equal(findings.length,0,'Recognizable token/key value found; preserve source and do not archive it');
const copies=[];
for(const row of cached){
 const target=path.join(output,row.relative);fs.mkdirSync(path.dirname(target),{recursive:true});
 fs.copyFileSync(row.original,target,fs.constants.COPYFILE_EXCL);
 const copied=load(target).pin,after=load(row.original).pin;
 for(const current of [copied,after]){assert.equal(current.bytes,row.source.pin.bytes);assert.equal(current.sha256,row.source.pin.sha256);}
 copies.push({original:row.source.pin,originalAfterCopy:after,archived:{...copied,path:row.relative},rawExact:true,classification:row.classification});
}
save('Sources.index.json',{schema:'soui.message-port-host-preparation-sources/v1',copies,
 originalEmbeddedProducerPathsAndTimestampsPreserved:true,sourceMetadataModified:false});
const publicInputs=readJson(source+'/CandidateStageInputs.approved.json');
const browserInputs=readJson(source+'/BrowserABInputs.approved.json');
const final=readJson(carrier+'/iso-writer-r3-3be2d976-6f91-4a49-9123-a0875404ebb2/Final.actual.json');
const sourcePlan=readJson(carrier+'/IsoSource.plan.json');
const effectiveWriter=readJson(carrier+'/iso-writer-r3-3be2d976-6f91-4a49-9123-a0875404ebb2/IsoWriter.result.json');
const stages=['stage-x64-db0b7592-f329-42cc-b29a-eca780a8e08f','stage-x86-8c01d26b-10e6-4cc9-ba4d-00fa529073bd'].map(name=>readJson(source+'/'+name+'/Summary.actual.json'));
const laterReview=source+'/RootGuestPreflightReview.actual.json';
const laterReviewReference=fs.existsSync(laterReview)?load(laterReview).pin:null;
save('Snapshot.actual.json',{schema:'soui.message-port-host-preparation-snapshot/v1',recordedUtc:new Date().toISOString(),
 sourceVersion:'0.5.1-preview.1',sourceSha256:browserInputs.sourceSha256,
 scope:'Historical installed SDK/consumer/Web reuse and paired old/current exact DLL preparation only; no browser outcomes in this snapshot.',
 historicalActualPreparation:true,newCompilationExecuted:false,newWebBuildExecuted:false,
 publicStages:stages.map(stage=>({architecture:stage.architecture,candidateSuite:stage.answer.actualCandidateSuite,
  consumerBuildId:stage.consumerBuildId,sdkBuildId:stage.sdkBuildId,naturalTerminal:stage.naturalStageClosure,
  actualSdkInstallExecuted:stage.actualSdkInstallExecuted,rawOwnedVerified:stage.rawOwnedVerified,
  payloadDifferences:stage.answer.payloadDifferences})),
 effectiveMedia:{sourcePlanFileSystems:sourcePlan.fileSystems,effectiveFileSystems:effectiveWriter.fileSystems,
  udfRevision:effectiveWriter.udfRevision,strictCompliance:effectiveWriter.strictCompliance,
  sourceFiles:sourcePlan.files.length,sourceDirectories:sourcePlan.directories.length,
  payloadBytes:sourcePlan.files.reduce((sum,row)=>sum+row.bytes,0),isoPin:final.iso,
  naturalOutcome:final.finished,fullGuestContentHashVerificationStillRequiredForThisHostPreparation:true},
 excludedArtifacts:{iso:final.iso,isoTree:{path:sourcePlan.sourceRoot,exactMemberPins:'carrier/IsoSource.plan.json'},
  fullSourceAndInstalledCopies:{exactReadClosurePins:'host/GuestReadClosure.approved.json',exactCopyPins:'carrier/CarrierCopy.actual.json'},
  nativeAndToolImages:[publicInputs.tools.node,publicInputs.tools.cmake,...publicInputs.architectures.flatMap(row=>[row.consumerExe,row.candidateKernel,row.baselineKernel])],
  scratchAndGeneratedImageData:'Excluded; no scratch artifacts are needed to interpret actual writer results.'},
 laterRootGuestReviewReference:laterReviewReference,
 laterRootGuestReviewMeaning:'External Root-owned later artifact reference only; not interpreted as a browser outcome or included raw guest evidence.',
 currentSdk060Qualified:false,browserOutcomesIncluded:false,browserBehaviorExecutedByCollector:false,
 productionQualificationComplete:false,fullDevtoolsGoalComplete:false,releaseReady:false});
save('Integrity.actual.json',{schema:'soui.message-port-host-preparation-copy-integrity/v1',recordedUtc:new Date().toISOString(),
 collector:load(__filename).pin,argv:process.argv,cwd:slash(process.cwd()),
 copiedFiles:copies.length,copiedBytes:copies.reduce((sum,row)=>sum+row.archived.bytes,0),
 sourceBeforeAfterAndCopyBytesEqual:true,sourceEvidenceModified:false,testsOrValidationRerun:false,
 tokenAudit:{selectedFilesScanned:cached.length,mechanicalPatternClasses:patterns.map(row=>row[0]),findings,
  scope:'Recognizable token/key and credential-value patterns in selected preparation sources only; no lease file or raw VM result is selected.'},
 productExecuted:false,vmTouched:false,browserOutcomesIncluded:false,releaseReady:false});
const names=walk(output);
save('Files.index.json',{schema:'soui.message-port-host-preparation-closed-files/v1',excluded:['Files.index.json'],
 files:names.map(name=>{const record=load(path.join(output,name)).pin;return{...record,path:name}})});
assert.deepEqual(walk(output),[...names,'Files.index.json'].sort(),'Final archive closed file set differs');
console.log(JSON.stringify({snapshot:slash(output),copiedFiles:copies.length,
 copiedBytes:copies.reduce((sum,row)=>sum+row.archived.bytes,0),indexedFiles:names.length,
 closedFileSetVerified:true,sourceBeforeAfterAndCopyBytesEqual:true,tokenPatternFindings:findings.length,
 browserOutcomesIncluded:false,productExecuted:false,vmTouched:false,releaseReady:false}));
