'use strict';
// Selective exact-byte archive only. It never runs a compiler, product, verifier,
// browser, VM command or Git. Authoring/checking this file does not run it.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const base=path.resolve('F:/work/SOUI_WEB/out/open-issues-closeout-20261029');
const canonical=path.resolve('C:/sc07/verification/production-validation/open-issues-closeout-2026-10-29/focus-control');
const catalog='C:/Users/11034/.qemu-novnc-lab/catalog.json';
const digest=b=>crypto.createHash('sha256').update(b).digest('hex');
const encode=o=>Buffer.from(JSON.stringify(o,null,2)+'\n');
const json=b=>JSON.parse(b.toString('utf8'));
const norm=p=>path.resolve(p).toLowerCase();
const inside=(root,p)=>norm(p).startsWith(norm(root)+path.sep);
const rel=s=>{assert(typeof s==='string'&&s&&!s.includes('\\')&&!s.includes(':')&&!s.split('/').some(c=>!c||c==='.'||c==='..'),'Unsafe archive relative path');return s;};
const forbidden=/\.(?:exe|dll|lib|pdb|obj|iso|qcow2|vhd|vhdx|wim|zip|7z|cab|msi)$/i;
const allowed=/\.(?:json|ndjson|bin|raw|log|txt|tlog|vcxproj|cmake|cjs|ps1|md|png|cpp|h|cs|diff)$/i;
function ancestors(p){let current=path.resolve(p);const items=[];while(true){items.push(current);const parent=path.dirname(current);if(parent===current)break;current=parent;}for(const item of items.reverse()){if(!fs.existsSync(item))continue;const st=fs.lstatSync(item);assert(!st.isSymbolicLink(),'Linked source/destination ancestor rejected: '+item);}}
function read(p,max=16777216){
 ancestors(p);const a=fs.lstatSync(p,{bigint:true});assert(a.isFile()&&!a.isSymbolicLink()&&a.nlink===1n&&a.size<=BigInt(max),'Bounded single-link plain file required: '+p);
 const bytes=fs.readFileSync(p),b=fs.lstatSync(p,{bigint:true});
 assert(['dev','ino','nlink','size','mtimeNs','ctimeNs'].every(k=>a[k]===b[k])&&BigInt(bytes.length)===a.size,'Source changed while read: '+p);
 return {bytes,pin:{path:path.resolve(p),bytes:bytes.length,sha256:digest(bytes)},identity:{dev:a.dev.toString(),fileId:a.ino.toString(),nlink:a.nlink.toString(),mtimeNs:a.mtimeNs.toString(),ctimeNs:a.ctimeNs.toString()}};
}
function walk(root,prefix=''){ancestors(root);const out=[];for(const e of fs.readdirSync(root,{withFileTypes:true})){assert(!e.isSymbolicLink(),'Linked directory member rejected');const name=rel(prefix+e.name),p=path.join(root,e.name);if(e.isDirectory())out.push(...walk(p,name+'/'));else{assert(e.isFile(),'Non-file directory member rejected');out.push(name);}}return out.sort();}
function samePin(actual,expected){assert.deepEqual(actual,{path:path.resolve(expected.path),bytes:expected.bytes,sha256:expected.sha256},'Pinned input bytes differ');}
function sensitiveKeys(value){
 if(Array.isArray(value)){for(const v of value)sensitiveKeys(v);return;}
 if(!value||typeof value!=='object')return;
 for(const [key,v] of Object.entries(value)){assert(!/^(?:password|passwd|plainTextPassword|userPassword|vmPassword|leaseToken|privateToken|secretValue)$/i.test(key)||v===null||v==='','Private credential value field rejected');sensitiveKeys(v);}
}
function inspectSource(row){
 assert(typeof row.source==='string'&&path.isAbsolute(row.source)&&inside(base,row.source),'Only explicit task-output source files allowed');
 rel(row.destination);assert(!forbidden.test(row.source)&&!forbidden.test(row.destination)&&allowed.test(row.source)&&allowed.test(row.destination),'Executable/bulk or unknown file kind rejected');
 assert(!/(?:^|[\\/])(?:catalog\.json|serial\.log|session-credentials\.json)$/i.test(row.source),'Private catalog/live serial/credentials rejected');
 const r=read(row.source);samePin(r.pin,{...row.pin,path:row.source});if(/\.json$/i.test(row.source))sensitiveKeys(json(r.bytes));return r;
}
function verifyNatural(result){
 const t=result.terminal;assert(result.passed===true&&result.exitCode===0&&result.supervisorExitCode===0&&result.signal===null&&result.spawnError===null&&result.reportError===null&&result.stopRequested===false&&result.fallbackUsed===false&&result.closureConfirmed===true&&t?.reason==='completed'&&t.rootExited===true&&t.rootExitCode===0&&t.activeBeforeCleanup===0&&t.activeProcesses===0&&t.limitTerminatedProcesses===0&&t.terminatedOwnedJob===false&&t.closureConfirmed===true&&t.passed===true,'Host stage lacks exact natural success');
}
function validateGroup(group,sourceByDestination){
 const load=n=>{const r=sourceByDestination.get(group.destination+'/'+n);assert(r,'Required group input absent: '+n);return json(r.bytes);};
 if(group.kind==='host-compile-only-passed'){
  const summary=load('summary.json');assert(summary.passed===true&&summary.productExecuted===false&&summary.executableExecuted===false&&summary.oldFailuresReclassified===false&&summary.verdicts.length===2,'Compile-only summary differs');
  assert.deepEqual(summary.verdicts.map(v=>v.architecture).sort(),['x64','x86']);
  for(const arch of ['x64','x86']){verifyNatural(load(arch+'/native-finished.json'));const root=load(arch+'/root-return.json');assert(root.completed===true&&root.architecture===arch&&root.productExecuted===false&&root.executableExecuted===false&&root.oldFailureReclassified===false&&root.cleanupFailures.length===0,'Compile-only root return differs');}
 }else if(group.kind==='host-compile-case-composition'){
  const summary=load('summary.json');assert(summary.schema==='soui.workbench-consumer-build-composed/v1'&&summary.passed===true&&summary.composedFromIndependentBuildCases===true&&summary.simultaneousTwoArchitectureAttemptPassed===false&&summary.originalParentAttemptRemainsFailed===true&&summary.noBuildExecutedByComposition===true&&summary.productExecuted===false&&summary.executableExecuted===false&&summary.oldFailuresReclassified===false&&summary.sources.length===2&&summary.verdicts.length===2,'Explicit case composition differs');
  assert.deepEqual(summary.sources.map(s=>s.architecture).sort(),['x64','x86']);assert.deepEqual(summary.verdicts.map(s=>s.architecture).sort(),['x64','x86']);
  assert(summary.sources.find(s=>s.architecture==='x64').parentAttemptPassed===false&&summary.sources.every(s=>s.casePassed===true&&s.reusedWithoutExecution===true),'Original failed parent was relabeled');
  for(const arch of ['x64','x86']){verifyNatural(load(arch+'/native-finished.json'));const root=load(arch+'/root-return.json');assert(root.completed===true&&root.architecture===arch&&root.productExecuted===false&&root.executableExecuted===false&&root.oldFailureReclassified===false&&root.cleanupFailures.length===0,'Composed actual case root return differs');}
  assert(load('source-attempts/x64/summary.json').passed===false&&load('source-attempts/x86/summary.json').passed===true,'Copied original attempt summaries differ');
 }else if(group.kind==='host-partial-failed'){
  const summary=load('summary.json'),arch=group.failedArchitecture;assert(arch==='x86'||arch==='x64','Explicit failed architecture required');
  const root=load(arch+'/root-return.json'),result=load(arch+'/native-finished.json');
  assert(summary.passed===false&&summary.productExecuted===false&&summary.executableExecuted===false&&summary.verdicts.length===1&&summary.verdicts[0].architecture!==arch&&root.completed===false&&root.productExecuted===false&&root.executableExecuted===false&&root.failure===group.expectedFailure&&root.cleanupFailures.length===0,'Original partial failed attempt differs');
  assert(result.passed===false&&result.exitCode===1&&result.supervisorExitCode===1&&result.terminal?.rootExitCode===1&&result.terminal?.activeProcesses===0&&result.terminal?.activeBeforeCleanup===0&&result.terminal?.terminatedOwnedJob===false,'Original failed case closure differs');
 }else if(group.kind==='precompile-failed'){
  const summary=load('summary.json'),root=load('x64/root-return.json'),result=load('x64/native-finished.json');
  assert(summary.passed===false&&summary.productExecuted===false&&summary.executableExecuted===false&&summary.verdicts.length===0&&root.completed===false&&root.productExecuted===false&&root.executableExecuted===false&&root.failure===group.expectedFailure&&root.cleanupFailures.length===0,'Original precompile failure differs');
  assert(result.exitCode===1&&result.supervisorExitCode===1&&result.terminal?.rootExitCode===1&&result.terminal?.activeProcesses===0&&result.terminal?.activeBeforeCleanup===0&&result.terminal?.terminatedOwnedJob===false,'Original precompile failed closure differs');
  assert(!group.members.some(n=>/^x(?:64|86)\/(?:configure|build)(?:\/|\.log$)/.test(n)),'A precompile group unexpectedly contains configure/build evidence');
 }else if(group.kind==='console-policy-passed'){
  const summary=load('Summary.actual.json');assert(summary.passed===true&&summary.failure===null&&summary.unchangedNineCasesReexecuted===false&&summary.productGuiExecution===false&&summary.results.length===2,'Two-case console-only result differs');
  assert.deepEqual(summary.results.map(r=>r.name).sort(),['reload-pending','runtime-pending']);
  for(const result of summary.results){assert(result.passed===true&&result.naturallyClosed===true&&result.consoleOnly===true,'Console case scope differs');verifyNatural(result.result);verifyNatural(load(result.name+'/policy-finished.json'));}
 }else if(group.kind==='console-policy-observations'||group.kind==='decoded-vm-observations'||group.kind==='task-vm-screenshots'||group.kind==='control-source'||group.kind==='derived-analysis-observations'){
  // Selection records the actual evidence source. This collector deliberately
  // does not manufacture a product/policy verdict from file presence.
 }else assert.fail('Unknown reviewed group kind');
}
function main(){
 assert(process.platform==='win32'&&process.argv.length===5&&process.argv[2]==='ROOT_REVIEWED_CURRENT_EXECUTION_ARCHIVE_V1','Explicit reviewed Windows invocation required');
 const planFile=path.resolve(process.argv[3]),expectedPlanSha=process.argv[4];assert(inside(base,planFile)&&/^[0-9a-f]{64}$/.test(expectedPlanSha),'Pinned output-only selection required');
 const planRead=read(planFile);assert.equal(planRead.pin.sha256,expectedPlanSha,'Reviewed selection changed');const plan=json(planRead.bytes);
 assert(plan.schema==='soui.current-execution-archive-selection/v1'&&plan.executionApproved===true&&plan.pendingGroups.length===0&&norm(plan.destination)===norm(canonical),'Selection incomplete or wrong destination');
 assert(!fs.existsSync(canonical),'Refuse existing immutable current-execution output');ancestors(path.dirname(canonical));
 assert(plan.groups.length>0&&plan.rows.length>0&&plan.rows.length<=1024,'Bounded explicit selection required');
 const destinations=plan.rows.map(r=>rel(r.destination));assert.deepEqual(destinations,[...destinations].sort(),'Selection must be sorted');
 assert.equal(new Set(destinations.map(s=>s.toLowerCase())).size,destinations.length,'Case-insensitive destination collision');
 const destinationSet=new Set(destinations.map(s=>s.toLowerCase()));for(const destination of destinations){const parts=destination.toLowerCase().split('/');for(let i=1;i<parts.length;i++)assert(!destinationSet.has(parts.slice(0,i).join('/')),'File/directory destination collision');}
 const reserved=['Collect-CurrentExecution.cjs','Selection.actual.json','SOURCE-MANIFEST.json','Collection.actual.json','TokenAudit.actual.json','Files.index.json'];
 assert(destinations.every(d=>!reserved.some(r=>r.toLowerCase()===d.toLowerCase()||d.toLowerCase().startsWith(r.toLowerCase()+'/'))),'Reserved output collision');
 const token=json(fs.readFileSync(catalog)).vms['soui-devtools-windows'].lease.token;assert(typeof token==='string'&&token.length>=16,'Current actual task lease token unavailable');
 const needles=[Buffer.from(token,'utf8'),Buffer.from(token,'utf16le'),Buffer.from(Buffer.from(token,'utf8').toString('base64'),'ascii')];
 const safe=b=>assert(!needles.some(n=>b.includes(n)),'Selected bytes contain actual private lease token; no redaction or copy permitted');
 safe(planRead.bytes);const self=read(__filename);safe(self.bytes);
 const sourceByDestination=new Map(),beforeRows=[];let totalBytes=0;
 for(const row of plan.rows){const before=inspectSource(row);safe(before.bytes);sourceByDestination.set(row.destination,before);beforeRows.push(before);totalBytes+=before.bytes.length;}
 assert(totalBytes<=33554432,'Selective archive aggregate byte bound exceeded');
 for(const group of plan.groups){assert(path.isAbsolute(group.sourceRoot)&&inside(base,group.sourceRoot));rel(group.destination);assert.deepEqual(walk(group.sourceRoot),group.allSourceMembers,'Reviewed group closed source set changed');assert.deepEqual(group.members,[...group.members].sort(),'Group selected member set is unsorted');const actual=plan.rows.filter(r=>r.destination.startsWith(group.destination+'/'));assert.deepEqual(actual.map(r=>r.destination.slice(group.destination.length+1)),group.members,'Selection differs from group membership');for(const row of actual){const name=row.destination.slice(group.destination.length+1);assert.equal(norm(row.source),norm(path.join(group.sourceRoot,...name.split('/'))),'Group source mapping differs');}validateGroup(group,sourceByDestination);}
 assert.equal(plan.groups.reduce((n,g)=>n+g.members.length,0),plan.rows.length,'Ungrouped source row rejected');
 const startedAtUtc=new Date().toISOString();fs.mkdirSync(canonical);const write=(n,b)=>{rel(n);safe(b);const p=path.join(canonical,...n.split('/'));fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,b,{flag:'wx'});};
 write('Collect-CurrentExecution.cjs',self.bytes);write('Selection.actual.json',planRead.bytes);
 const rows=[];
 for(let i=0;i<plan.rows.length;i++){
  const row=plan.rows[i],before=beforeRows[i],again=inspectSource(row);assert.deepEqual(again.identity,before.identity,'Source identity changed before copy');assert(before.bytes.equals(again.bytes),'Source bytes changed before copy');
  write(row.destination,before.bytes);const copied=read(path.join(canonical,...row.destination.split('/'))),after=inspectSource(row);safe(copied.bytes);safe(after.bytes);
  assert(before.bytes.equals(copied.bytes)&&before.bytes.equals(after.bytes),'Source-before/copy/source-after bytes differ');assert.deepEqual(after.identity,before.identity,'Source identity changed during collection');
  rows.push({source:row.source,destination:row.destination,bytes:before.bytes.length,sha256:before.pin.sha256,sourceBeforeSha256:before.pin.sha256,sourceAfterSha256:after.pin.sha256,copySha256:copied.pin.sha256,sourceIdentityBefore:before.identity,sourceIdentityAfter:after.identity});
 }
 for(const group of plan.groups)assert.deepEqual(walk(group.sourceRoot),group.allSourceMembers,'Source set changed during collection');
 samePin(read(planFile).pin,planRead.pin);samePin(read(__filename).pin,self.pin);
 write('SOURCE-MANIFEST.json',encode({schema:'soui.current-execution-source-manifest/v1',sources:rows,sourceCount:rows.length,sourceBytes:totalBytes,sourceBeforeAfterCopyBytesEqual:true,sourceIdentityBeforeAfterEqual:true,sourceMemberSetsUnchanged:true,exclusions:plan.exclusions}));
 write('TokenAudit.actual.json',encode({schema:'soui.current-execution-private-token-audit/v1',actualTokenReadOnlyInMemory:true,privateCatalogArchived:false,tokenValueOrDigestPersisted:false,encodingsChecked:['UTF8','UTF16LE','Base64UTF8'],selectedSourceFilesScanned:rows.length,selectedSourceBytesScanned:totalBytes,sourceMatches:0,copiedMatches:0,redactionsPerformed:0,credentialValueFieldsRejected:true,meaning:'Exact actual current task lease token checked, no arbitrary secret discovery or screenshot OCR claimed'}));
 write('Collection.actual.json',encode({schema:'soui.current-execution-collection/v1',startedAtUtc,completedAtUtc:new Date().toISOString(),collector:self.pin,selection:planRead.pin,node:read(process.execPath,134217728).pin,argv:process.argv,cwd:process.cwd(),pid:process.pid,copiedSourceFiles:rows.length,copiedSourceBytes:totalBytes,meaning:'Selective exact-byte archival only; grouped source results retain their actual pass/fail and scope; this collector adds no test evidence',buildExecutedByCollector:false,applicationExecutedByCollector:false,vmControlledByCollector:false,gitIndexTouched:false,oldImmutableArchivesModified:false,oldFailuresReclassified:false,longTestsExecuted:false,releaseReady:false,productionQualificationComplete:false,fullDevtoolsGoalComplete:false}));
 const names=walk(canonical);for(const name of names)safe(read(path.join(canonical,...name.split('/'))).bytes);
 const index=names.map(name=>{const r=read(path.join(canonical,...name.split('/')));return {path:name,bytes:r.pin.bytes,sha256:r.pin.sha256};});
 write('Files.index.json',encode({schema:'soui.current-execution-files/v1',policy:'Sorted complete archive file set; only this index excluded to avoid self-reference',files:index}));
 assert.deepEqual(walk(canonical),[...names,'Files.index.json'].sort(),'Final archive is not a closed file set');
 console.log(JSON.stringify({archive:canonical,copiedSourceFiles:rows.length,copiedSourceBytes:totalBytes,indexedFiles:index.length,index:read(path.join(canonical,'Files.index.json')).pin,applicationExecutedByCollector:false,buildExecutedByCollector:false,vmControlledByCollector:false,releaseReady:false}));
}
main();
