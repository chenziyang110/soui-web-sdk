'use strict';
// One-shot host evidence archival. No builds, tests, product execution, VM control or Git operations.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const destination=__dirname,origin='F:/work/SOUI_WEB/out/open-issues-closeout-20261029';
const skill='C:/Users/11034/.codex/skills/qemu-novnc-lab',repository='C:/sc07';
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const slash=s=>s.replaceAll('\\','/');
const index=[],omitted=[];
function read(file){
  const before=fs.lstatSync(file,{bigint:true});assert(before.isFile()&&!before.isSymbolicLink(),'Plain evidence input required');
  const bytes=fs.readFileSync(file),after=fs.lstatSync(file,{bigint:true});
  for(const key of ['dev','ino','size','mtimeNs','ctimeNs'])assert.equal(before[key],after[key],'Evidence changed while read');
  assert.equal(BigInt(bytes.length),before.size);return bytes;
}
function pin(file,bytes=read(file)){return {path:slash(file),bytes:bytes.length,sha256:hash(bytes)};}
function walk(dir,prefix=''){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>{assert(!e.isSymbolicLink());const name=prefix+e.name;if(e.isDirectory())return walk(path.join(dir,e.name),name+'/');assert(e.isFile());return [name];}).sort();}
const tokens=[];
const catalogFile='C:/Users/11034/.qemu-novnc-lab/catalog.json';
if(fs.existsSync(catalogFile)){
  const catalog=JSON.parse(read(catalogFile));
  for(const vm of Object.values(catalog.vms||{}))if(typeof vm.lease?.token==='string'&&vm.lease.token.length>=16)tokens.push(vm.lease.token);
}
function safe(bytes){for(const token of tokens)assert(!bytes.includes(Buffer.from(token)),'Current registry lease token in selected evidence; refuse archival');}
function save(relative,bytes){
  assert(!path.isAbsolute(relative)&&relative.split('/').every(p=>p&&p!=='.'&&p!=='..'&&!p.includes(':')));
  safe(bytes);const target=path.join(destination,relative);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes,{flag:'wx'});
  assert(read(target).equals(bytes),'Archived copy bytes differ');return {path:relative,bytes:bytes.length,sha256:hash(bytes)};
}
function json(relative,value){return save(relative,Buffer.from(JSON.stringify(value,null,2)+'\n'));}
function copy(source,relative){const bytes=read(source),sourcePin=pin(source,bytes),archived=save(relative,bytes);assert.deepEqual(pin(source),sourcePin);index.push({source:sourcePin,archived});return archived;}
assert(!fs.existsSync(path.join(destination,'HostPreparation.actual.json')),'Preserve existing host archive');
// Compile preparation source and complete actual textual observations. Executables/objects retain exact source pins locally.
const tray=path.join(origin,'tray-build');
for(const name of ['README.md','Run-TrayBuild.cjs','Invoke-TrayBuild.ps1','Prepare-Handoff.cjs','Finalize-Handoff.cjs','Guest-Shorts.cjs','Guest-Export.ps1','source/CMakeLists.txt','source/main.cpp'])copy(path.join(tray,name),'tray/preparation/'+name);
for(const name of fs.readdirSync(tray).filter(n=>/^deferred-compiler-.*\.json$/.test(n)))copy(path.join(tray,name),'tray/preparation/'+name);
const attempt='attempt-e36e27f5-e352-4f39-a955-f7df561e9f57';
for(const name of walk(path.join(tray,attempt))){
  const source=path.join(tray,attempt,name);
  if(/\.(?:exe|obj)$/i.test(name)){omitted.push({source:pin(source),reason:'Local compiled binary/object copy; full pins, PE and tracking observations retained, avoid redundant binary bulk'});continue;}
  copy(source,'tray/'+attempt+'/'+name);
}
const handoff='handoff-6d09f9b2-1585-4187-95ba-2cd3d40cc832';
for(const name of ['Handoff.actual.json','PayloadPreparation.actual.json','payload/Payload.manifest.json'])copy(path.join(tray,handoff,name),'tray/'+handoff+'/'+name);
// Local skill changes are infrastructure evidence, not SDK implementation or product qualification.
for(const name of walk(path.join(origin,'infra-before')))copy(path.join(origin,'infra-before',name),'infrastructure/before/'+name);
const v4=JSON.parse(read(path.join(origin,'infra-implementation-v4.actual.json')));
for(const expected of v4.files){const source=path.join(skill,expected.path),actual=pin(source);assert.equal(actual.bytes,expected.bytes);assert.equal(actual.sha256,expected.sha256);copy(source,'infrastructure/reviewed-v4/'+expected.path);}
for(const name of ['infra-implementation.actual.json','infra-implementation-v2.actual.json','infra-implementation-v3.actual.json','infra-implementation-v4.actual.json','Recover-TaskVm.ps1'])copy(path.join(origin,name),'infrastructure/'+name);
// Exact successful recovery and the safe prior timeout. The caller-argument failure with a lease token is intentionally not copied.
for(const run of ['vm-recovery-3a5bd9c7-a434-480e-903a-71b76ecff581','vm-recovery-a62bdbf8-ff7f-4ee0-b058-fa10900ab676'])
  for(const name of ['RootReview.actual.json','Exit.actual.json','recover.stdout.json','recover.stderr.log','lab.json','started.json','active-session.json','start.lock'])copy(path.join(origin,run,name),'infrastructure/'+run+'/'+name);
const privateFailure=path.join(origin,'vm-recovery-fd2b4ee8-2c6f-464a-8eb4-fb5a1f79ff31');
const failedExit=JSON.parse(read(path.join(privateFailure,'Exit.actual.json')));assert.equal(failedExit.exitCode,2);
json('infrastructure/RedactedFailure.actual.json',{schema:'soui.redacted-local-failure-reference/v1',originalDirectory:slash(privateFailure),exitCode:2,
  stderr:pin(path.join(privateFailure,'recover.stderr.log')),rawStderrArchived:false,originalRawPublic:false,
  reason:'Caller argument error printed a current VM lease token. Original raw is preserved locally and deliberately excluded from this source archive. This narrative is explicitly redacted and not the original raw.',
  productExecuted:false,oldExitReclassified:false,automaticRetry:false});
copy(path.join(origin,'Clear-Verified-Orphans.ps1'),'orphan-cleanup/Clear-Verified-Orphans.ps1');
const cleanup='orphan-cleanup-48e38c18-cb17-4ab2-ae69-44badd4e82ad';
for(const name of ['before.json','result.json'])copy(path.join(origin,cleanup,name),'orphan-cleanup/'+name);
// Independent reviews authored by this reviewer, not reconstructed by Root or the implementation author.
const linkDirectory='verification/production-validation/current-link-members-0.6.0-2026-10-29';
const linkIndex=JSON.parse(read(path.join(repository,linkDirectory,'Files.index.json')));
const linkPins=[...linkIndex.files.map(f=>({path:linkDirectory+'/'+f.path,bytes:f.bytes,sha256:f.sha256})),pin(path.join(repository,linkDirectory,'Files.index.json'))];
for(const expected of linkPins){const actual=pin(path.isAbsolute(expected.path)?expected.path:path.join(repository,expected.path));assert.equal(actual.bytes,expected.bytes);assert.equal(actual.sha256,expected.sha256);}
assert.equal(linkPins.find(p=>p.path.endsWith('/collect.cjs')).sha256,'e2b49203d77b4d878672a0778eed4a8de13c56d182d8da608167ec8397111ee0');
assert.equal(linkPins.find(p=>p.path.endsWith('/verify.cjs')).sha256,'c59f03fe3c5f9883ae060c656e9a1afad13fb28ac5a15b65b27cec7fcdae475b');
json('IndependentReview.actual.json',{schema:'soui.independent-host-evidence-review/v1',reviewer:'/root/issues_runtime_audit',createdUtc:new Date().toISOString(),
  authors:{linkArchive:'/root/issues_qualification_audit',infrastructure:'/root/issues_ui_fixture_audit',trayPreparation:'/root/issues_runtime_audit'},
  linkArchive:{sourcePins:linkPins,blockers:0,verifierReexecuted:false,scope:'Read collector/verifier/index/summary/both architecture records/README; independent bounded MAP read for skipped-row and section-bound concerns',
    actualMapInspection:[{architecture:'x64',symbolRows:342174,sectionSizeRows:46,unexpectedSkippedRows:0,outsideSectionCount:0},{architecture:'x86',symbolRows:304462,sectionSizeRows:44,unexpectedSkippedRows:0,outsideSectionCount:0}],
    boundaries:'Mechanical MAP membership and exact source/DLL identities only; no legal approval, exhaustive transitive inventory or product run'},
  infrastructureV3:{blockers:0,scope:'Native all-disk deny-write/delete handle fence, metadata/session/PID identity, archive rollback/pending and UNKNOWN outcome; known availability restrictions corrected by v4'},
  infrastructureV4:{sourcePins:v4.files,blockers:0,testsReexecutedByReviewer:false,scope:'Dual-family native TCP fallback and truthful QMP methods; foreign visible CLI-only classification; unchanged all-disk fences; final process/endpoint checks held under fence; no early archive on uncertainty',
    labSha256:'ed4216f706238f2ec8277da190e6929f1688d4a60be5b1817b683f9a71846d50',cimProvesNoWriters:false,performanceIsolationProved:false},
  selfAuthoredTrayPreparationIsIndependentReview:false,buildsExecutedByThisArchive:false,testsExecutedByThisArchive:false,vmInputByReviewer:false,productExecutedByReviewer:false,
  binaryLicenseReviewComplete:false,productionQualificationComplete:false,fullDevtoolsGoalComplete:false,releaseReady:false});
const summary=JSON.parse(read(path.join(tray,attempt,'summary.json')));assert.equal(summary.passed,true);assert.equal(summary.verdicts.length,2);
const recovered=JSON.parse(read(path.join(origin,'vm-recovery-3a5bd9c7-a434-480e-903a-71b76ecff581/recover.stdout.json')));
assert.equal(recovered.state,'offline-recovered');assert.equal(recovered.receipt.priorOutcome,'UNKNOWN');assert.equal(recovered.receipt.cleanShutdownVerified,false);assert.equal(recovered.receipt.originalExitCode,null);
json('HostPreparation.actual.json',{schema:'soui.open-issues-host-preparation/v1',createdUtc:new Date().toISOString(),collector:pin(__filename),copies:index,omittedBinaryObservations:omitted,
  currentTrayConsumerBuilds:summary.verdicts.map(v=>({architecture:v.architecture,root:v.root,terminal:v.terminal,compiledExecutable:v.compiledExecutable,wholeJobNaturallyClosed:v.wholeJobNaturallyClosed,executableExecuted:false})),
  trayBuildScope:'One real independent installed SDK consumer configure/build per architecture; no product GUI executed on host; unchanged full SDK/contract tests not repeated',
  infrastructureRecoverySucceeded:true,recoveredSession:recovered.receipt.session,originalVmOutcome:'UNKNOWN',originalVmExitCode:null,cleanShutdownVerified:false,
  orphanCleanup:'Four separately identity/parent/CPU verified stale helpers were forcibly stopped under persistent user authorization. This is not any compiler test natural completion.',
  sourceDocumentStatus:'Implementation/preparation and host evidence only. VM runtime/UI results belong to a separate Root-owned archive and are not included here.',
  guiRuntimeEvidenceIncluded:false,guiQualificationPassed:false,archiveBuildsExecuted:false,archiveTestsExecuted:false,archiveProductExecuted:false,
  sourceDocStatusIsProductPass:false,currentRegistryLeaseTokenValuesExcluded:true,firstCallerFailureRawIntentionallyNotArchived:true,
  binaryLicenseReviewComplete:false,productionQualificationComplete:false,fullDevtoolsGoalComplete:false,releaseReady:false});
const files=walk(destination);assert(!files.includes('HostFiles.index.json'));
const rows=files.map(name=>{const p=pin(path.join(destination,name));safe(read(path.join(destination,name)));return {path:name,bytes:p.bytes,sha256:p.sha256};});
json('HostFiles.index.json',{schema:'soui.host-evidence-closed-file-index/v1',excluded:['HostFiles.index.json'],files:rows,totalBytes:rows.reduce((n,p)=>n+p.bytes,0)});
assert.deepEqual(walk(destination),[...files,'HostFiles.index.json'].sort());
console.log(JSON.stringify({directory:slash(destination),files:rows.length,totalBytes:rows.reduce((n,p)=>n+p.bytes,0),hostPreparation:pin(path.join(destination,'HostPreparation.actual.json')),guiRuntimeEvidenceIncluded:false,releaseReady:false}));
