'use strict';
// One-off metadata/copy archive only. Does not execute product, verifier, VM or Git.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const root=__dirname,base='F:/work/SOUI_WEB/out/open-issues-closeout-20261029';
const digest=b=>crypto.createHash('sha256').update(b).digest('hex');
const json=p=>JSON.parse(fs.readFileSync(p,'utf8'));
const relative=p=>{assert(typeof p==='string'&&p&&!p.includes('\\')&&!p.includes(':')&&!p.split('/').some(s=>!s||s==='.'||s==='..'));return p;};
function read(p,maximum=16777216){const s=fs.lstatSync(p);assert(s.isFile()&&!s.isSymbolicLink()&&s.nlink===1&&s.size<=maximum,'Bounded plain task file required');const b=fs.readFileSync(p);assert.equal(b.length,s.size);return {bytes:b,meta:{path:p,bytes:b.length,sha256:digest(b),mtimeUtc:s.mtime.toISOString()}};}
function walk(p,prefix=''){return fs.readdirSync(p,{withFileTypes:true}).flatMap(e=>{assert(!e.isSymbolicLink());const name=prefix+e.name;relative(name);if(e.isDirectory())return walk(path.join(p,e.name),name+'/');assert(e.isFile());return [name];}).sort();}
function write(name,b){relative(name);const p=path.join(root,...name.split('/'));fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,b,{flag:'wx'});}
function writeJson(name,v){write(name,Buffer.from(JSON.stringify(v,null,2)+'\n'));}
const groups=[
 ['message-port/guest-preflight2-decoded','message-port/preflight2'],
 ['message-port/guest-run1-decoded','message-port/run1'],
 ['message-port/guest-run2-decoded','message-port/run2'],
 ['tray-build/decoded-tray2','desktop/tray2'],
 ['tray-build/decoded-resident1','desktop/resident1'],
 ['tray-build/decoded-resident2','desktop/resident2'],
 ['tray-build/handoff-r2','control-r2']
];
const media=[
 ['control-media-05b9ebb3-f53f-4911-ae85-57866b930ad1','media/success','iso-writer-r3-0b0e6b42-d641-419a-b6f6-aeae885475dd',
  ['Final.actual.json','finished.json','Invocation.actual.json','IsoWriter.inputs.json','IsoWriter.post.json','IsoWriter.pre.json','IsoWriter.result.json','owned.ndjson','owned.ndjson.context.json','ready.json','stderr.raw','stdout.raw']],
 ['control-media-94500c46-9e74-424f-b9a4-830f21f60a6f','media/e-image-cap-failure','iso-writer-r3-4734b1bf-20ee-420a-b2dd-8da6180d7c3b',
  ['finished.json','Invocation.actual.json','IsoWriter.failure.json','IsoWriter.inputs.json','IsoWriter.pre.json','owned.ndjson','owned.ndjson.context.json','ready.json','stderr.raw','stdout.raw']]
];
const gui=['message-port-run1-failed.png','preflight-machine-guard-refused.png','task-vm-identity-and-free-F.png','task-vm-recovered-desktop.png','tray3-x86-shortcut-menu.png','tray3-x86-submenu.png'];
const targets=[],sourceSets=[];
for(const [source,destination] of groups){const absolute=path.join(base,source),names=walk(absolute);sourceSets.push({source:absolute,destination,members:names});for(const name of names)targets.push({source:path.join(absolute,...name.split('/')),destination:destination+'/'+name});}
for(const [source,destination,phase,names] of media){for(const name of ['IsoSource.plan.json','Preparation.actual.json','Write-ControlIso.ps1'])targets.push({source:path.join(base,source,name),destination:destination+'/'+name});for(const name of names)targets.push({source:path.join(base,source,phase,name),destination:destination+'/phase/'+name});}
for(const name of gui)targets.push({source:path.join(base,'gui',name),destination:'gui/'+name});
targets.push({source:path.join(base,'message-port-current/deferred-compiler-34d59472-bcb3-4ee8-85d9-6e29a2a434ca.json'),destination:'current-source-preparation/deferred-compiler.actual.json'});
const forbidden=/\.(?:exe|dll|lib|pdb|obj|iso|qcow2|vhd|vhdx|wim|zip|7z)$/i;
assert(targets.every(t=>!forbidden.test(t.destination)),'Executable/image/archive bulk must not enter this evidence snapshot');
assert.equal(new Set(targets.map(t=>t.destination.toLowerCase())).size,targets.length);
assert.deepEqual(walk(root).sort(),['Collect-VmObservations.cjs','README.md'].sort(),'Refuse reuse or overwrite of archive output');
const memoryToken=json('C:/Users/11034/.qemu-novnc-lab/catalog.json').vms['soui-devtools-windows'].lease.token;
assert(typeof memoryToken==='string'&&memoryToken.length>=16,'Approved in-memory token unavailable');
const needles=[Buffer.from(memoryToken,'utf8'),Buffer.from(memoryToken,'utf16le'),Buffer.from(Buffer.from(memoryToken,'utf8').toString('base64'),'ascii')];
const safe=b=>assert(!needles.some(n=>b.includes(n)),'Selected archive input contains the actual private lease token; no token bytes may be copied');
const startedAtUtc=new Date().toISOString(),rows=[];let sourceBytes=0;
for(const target of targets){
 const before=read(target.source);safe(before.bytes);
 const destination=path.join(root,...relative(target.destination).split('/'));
 fs.mkdirSync(path.dirname(destination),{recursive:true});fs.copyFileSync(target.source,destination,fs.constants.COPYFILE_EXCL);
 const copied=read(destination),after=read(target.source);safe(copied.bytes);
 assert(before.bytes.equals(copied.bytes)&&before.bytes.equals(after.bytes),'Source-before/source-after/copy bytes differ');
 assert.equal(before.meta.mtimeUtc,after.meta.mtimeUtc,'Source modified during collection');
 fs.utimesSync(destination,new Date(before.meta.mtimeUtc),new Date(before.meta.mtimeUtc));
 rows.push({source:before.meta.path,destination:target.destination,bytes:before.meta.bytes,sha256:before.meta.sha256,sourceMtimeUtc:before.meta.mtimeUtc,sourceBeforeSha256:before.meta.sha256,sourceAfterSha256:after.meta.sha256,copySha256:copied.meta.sha256});sourceBytes+=before.meta.bytes;
}
for(const set of sourceSets)assert.deepEqual(walk(set.source),set.members,'Selected source directory member set changed');
const load=name=>json(path.join(root,...name.split('/')));
const mpEvents=fs.readFileSync(path.join(root,'message-port/run2/raw/x64-baseline/original-reload-206907c6-6e71-49d6-b3b9-0308e6ab7b9f/events.ndjson.message-port.ndjson'),'utf8').trim().split('\n').map(JSON.parse);
const mpPass=mpEvents.filter(e=>e.data&&e.data.status==='pass');
assert.equal(mpEvents.length,40);assert.equal(mpPass.length,32);assert.equal(new Set(mpEvents.map(e=>e.sessionId)).size,4);
const residentEvents=fs.readFileSync(path.join(root,'desktop/resident2/raw/resident-x64/resident.ndjson'),'utf8').trim().split('\n').map(JSON.parse);
const selectedResidentEvents=residentEvents.filter(e=>['host.reload-pending','host.reloaded','smoke.final-three-recreated','exit.cleanup-deadline','exit.requested','runtime.shutdown','runtime.shutdown-fallback','resident.finished'].includes(e.event));
const refs=['C:/sc07/demos/workbench/app/resident_controller.h','C:/sc07/demos/workbench/app/resident_shutdown_policy.h','C:/sc07/tests/native/resident_shutdown/policy_tests.cpp','C:/sc07/tests/native/resident_shutdown/CMakeLists.txt','C:/sc07/tests/message-port-app/main.cpp','C:/sc07/tests/message-port-app/shutdown-poll.h','C:/sc07/tests/message-port-app/README.md'].map(p=>read(p).meta);
const smallIso=load('media/success/phase/Final.actual.json');
assert.equal(smallIso.iso.bytes,1507328);assert.equal(smallIso.iso.sha256,'09c060b2f9917c39a709fe64f6c8f71911a6e6e383b1f653bf7fcc13a8575d03');
writeJson('ClaimSummary.json',{
 schema:'soui.open-issues-vm-observations/v1',snapshotDate:'2026-10-29',
 meaning:'Observed historical results and current source-only repairs; no test/build/VM execution performed by this archive collector',
 messagePort:{sourceVersion:'0.5.1-preview.1',sourceSha256:'652a66973f09c5a90a5e06465944dd7ea8d3a3d9463411db5b07d045cb843973',
  candidateDifference:'Prepared pairs keep the historical EXE/Web/SDK and differ only by old versus exact current0.6.0 node.dll; current candidate arms were not executed',
  preflight2:{passed:load('message-port/preflight2/raw/Preflight.actual.json').passed,rawFiles:2991,rawBytes:816132513,browserBehaviorExecuted:false},
  run1:{attempted:['x64-baseline'],runnerLoaderExit:'0xC0000135',subjectIdentityAvailable:false,wholeJobClosureVerified:false,passed:false},
  run2:{attempted:['x64-baseline'],nativePid:4764,nativeCreationFileTime:'134377352938104405',documentsObserved:4,sidecarRows:mpEvents.length,finiteCasesPassObserved:mpPass.length,workerNativeJoinObserved:false,debuggerWaitObserved:false,
   failedStep:'runtime.shutdown',failureCode:'E_RUNTIME_SHUTDOWN_PENDING',passed:false,nativeOwnedRootExit:1,nativeOwnedSupervisorExit:1,activeBeforeCleanup:0,activeAfterCleanup:0,terminatedOwnedJob:false},
  untouchedArmsNotRun:['x64-candidate','x86-baseline','x86-candidate'],allFourFiniteArmsPassed:false,currentSdk060Qualified:false},
 desktop:{sourceVersion:'0.6.0',tray2:{attempted:'tray-x64',pid:2224,creationFileTime:'134377355941319480',partial:'Tray install, native callback/menu return and explicit cleanup recorded',failureCode:'E_TRAY_FOCUS',rootExit:1,supervisorExit:1,activeBeforeCleanup:0,activeAfterCleanup:0,terminatedOwnedJob:false,x86NotRunByThisBatch:true,passed:false},
  resident1:{preflightOnly:true,failure:'Exact runtime closed set contained additional root webview-smoke.log',productExecuted:false,x86NotRun:true,passed:false},
  resident2:{attempted:'resident-x64',pid:1132,creationFileTime:'134377368324407305',partial:'Three windows ready; middle close/recreate; all-window close and empty residency; final three recreated',selectedEvents:selectedResidentEvents,rootExit:1,supervisorExit:1,activeBeforeCleanup:0,activeAfterCleanup:0,terminatedOwnedJob:false,x86NotRun:true,passed:false},
  tray3X86:{evidence:['gui/tray3-x86-shortcut-menu.png','gui/tray3-x86-submenu.png'],observation:'Chinese menu/submenu visibly observed inside task VM',nativeFinalReceiptAvailable:false,serialExportTagAvailable:false,passed:null}},
 controlR2:{onlyMutableRuntimeLog:'webview-smoke.log',otherPayloadHashesUnchanged:true,sourceNavigationNotExcluded:true,originalDeadlinesRetained:true,review:'Exact scripts independently statically reviewed with zero blockers; receipt retained, not product execution'},
 smallDataIso:{record:smallIso.iso,archiveContainsImageBytes:false,producerNaturalExit:0,supervisorNaturalExit:0,activeJobAtEnd:0,terminatedOwnedJob:false,fullGuestContentVerificationNotInferred:true,previousFailure:'E_IMAGE_CAP from caller32MiB versus exact helper1.4e9 policy; preserved as data-only failure; fixed caller restored original cap, helper/deadline unchanged'},
 currentSourceOnlyRepairs:{status:'Static review zero blockers reported; not yet compiled or VM-retested',workbench:'Exact reload pending no longer enters generic permanent failure; Runtime pending retains controller with original10000ms latch and timer retries',messagePort:'Two Runtime shutdown steps use existing until/service/original deadline; only exact Runtime pending retried',sourcePins:refs,foreignCompilerPreflightDeferred:true},
 productionQualificationComplete:false,fullDevtoolsGoalComplete:false,releaseReady:false,longTestsExecuted:false,oldFailuresPreserved:true
});
writeJson('RootObservations.received.json',{
 schema:'soui.root-observation-receipt/v1',recordedBy:'/root/issues_qualification_audit',source:'Actual Root collaboration messages received on2026-10-29; receiver-authored observation receipt, not a new tool/VM/reviewer execution',
 inputPause:{requestsNotDispatched:2,localRawToolFileAvailable:false,observedAtUtc:null,inputPausedByRoot:true,newInputDispatchedByThisCollector:0},
 tray3X86:'Only VM screenshots show Chinese menu/submenu; no final native receipt or serial tag obtained, so no pass claimed',
 r2Copy:'First read-only controller replacement/SHA guard refusal executed no product. Root subsequently copied only two reviewed control files to fresh C:/issues9 after task-local attribute adjustment, then full preset checks passed before actual PID1132',
 compilation:'Root reported active foreign compiler processes; current Workbench/MessagePort source repairs remain uncompiled and untested. Deferred compiler observation is copied separately; this is waiting, not a product test failure',
 reviewBoundary:'Root and actual independent review messages report current source fixes zero blockers; this collector has not authored a fictitious reviewer run or executed those sources',
 releaseReady:false,productionQualificationComplete:false,fullDevtoolsGoalComplete:false
});
writeJson('SOURCE-MANIFEST.json',{schema:'soui.vm-observations-source-manifest/v1',sources:rows,selectedDirectorySets:sourceSets,sourceCount:rows.length,sourceBytes,sourceBeforeAfterCopyBytesEqual:true,sourceMemberSetsUnchanged:true,exclusions:['Whole live serial.log','Private QEMU catalog/lease metadata and actual tokens','Recovery stderr containing token','ISO/image/scratch and full binaries/source/install trees','Unrelated workspace files']});
writeJson('TokenAudit.actual.json',{schema:'soui.vm-observations-private-token-audit/v1',actualTokenReadOnlyInMemory:true,approvedPrivateCatalogArchived:false,tokenValueOrDigestPersisted:false,encodingsChecked:['UTF8','UTF16LE','Base64UTF8'],selectedSourceFilesScanned:rows.length,selectedSourceBytesScanned:sourceBytes,sourceMatches:0,copiedBytesMatches:0,redactionsPerformed:0,meaning:'Actual current VM lease token checked before each copy; exact bytes retained only when no match. Does not claim arbitrary secret discovery or screenshot OCR.'});
writeJson('Collection.actual.json',{schema:'soui.vm-observations-collection/v1',startedAtUtc,completedAtUtc:new Date().toISOString(),collector:read(__filename).meta,node:read(process.execPath,134217728).meta,argv:process.argv,cwd:process.cwd(),pid:process.pid,copiedSourceFiles:rows.length,copiedSourceBytes:sourceBytes,sourceBeforeAfterCopyBytesEqual:true,existingVerifierRerun:false,applicationExecuted:false,buildExecuted:false,vmControlled:false,gitIndexTouched:false,independentRuntimeReviewCreated:false,releaseReady:false,productionQualificationComplete:false,fullDevtoolsGoalComplete:false});
for(const name of walk(root))safe(read(path.join(root,...name.split('/'))).bytes);
const index=walk(root).map(name=>{const r=read(path.join(root,...name.split('/')));return {path:name,bytes:r.meta.bytes,sha256:r.meta.sha256};});
writeJson('Files.index.json',{schema:'soui.vm-observations-files/v1',policy:'Exact complete archive file set; only this index excluded to avoid self-reference',files:index});
console.log(JSON.stringify({copiedSourceFiles:rows.length,copiedSourceBytes:sourceBytes,indexedFiles:index.length,index:read(path.join(root,'Files.index.json')).meta,actualLeaseTokenMatches:0,applicationExecuted:false}));
