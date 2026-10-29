'use strict';
// Offline stage evidence only. This does not execute any guest/SDK/product code.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const {TextDecoder}=require('node:util');
const expectedManifest='39d10f0236550188ebdbccf06f4cc4d7e54400293ce2b8fda93dc8fd64a50551';
const machine='SOUI-7AEMMK5Q0T',base='F:/work/SOUI_WEB/out/open-issues-closeout-20261029';
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
function pin(f){const s=fs.lstatSync(f);assert(s.isFile()&&!s.isSymbolicLink());const b=fs.readFileSync(f);return {path:f,bytes:b.length,sha256:hash(b)};}
function json(f){const bytes=fs.readFileSync(f),text=new TextDecoder('utf-8',{fatal:true,ignoreBOM:true}).decode(bytes);assert(Buffer.from(text,'utf8').equals(bytes));return JSON.parse(text.charCodeAt(0)===0xfeff?text.slice(1):text);}
function relative(p){assert(typeof p==='string'&&p&&!p.includes('\\')&&!path.isAbsolute(p));assert(p.split('/').every(s=>s&&s!=='.'&&s!=='..'&&!/[<>:"|?*\x00-\x1f]/.test(s)));return p;}
function samePath(a,b){assert.equal(typeof a,'string');assert.equal(typeof b,'string');return path.win32.normalize(a).toLowerCase()===path.win32.normalize(b).toLowerCase();}
function walk(d,p=''){return fs.readdirSync(path.join(d,p),{withFileTypes:true}).flatMap(e=>{assert(!e.isSymbolicLink());const n=p?p+'/'+e.name:e.name;return e.isDirectory()?walk(d,n):(assert(e.isFile()),[n]);}).sort();}
function equalPin(actual,expected){assert.equal(actual.bytes,expected.bytes);assert.equal(actual.sha256,expected.sha256);}
function expectedEntry(p,bytes,sha256){return {path:p,bytes,sha256};}
function readArg(args,name){const i=args.indexOf(name);assert(i>=0&&i<args.length-1);assert.equal(args.lastIndexOf(name),i);return args[i+1];}
function main(){
  const [decoded,manifestFile,output]=process.argv.slice(2);assert.equal(process.argv.length,5);
  assert(path.isAbsolute(decoded)&&path.isAbsolute(manifestFile)&&path.isAbsolute(output));
  const decodeRoot=path.resolve(decoded),analysisRoot=path.resolve(output);
  assert.equal(path.dirname(decodeRoot),path.resolve(base,'current-decoded'));
  assert.equal(path.dirname(analysisRoot),path.resolve(base,'current-analysis'));assert(!fs.existsSync(analysisRoot));
  const startedUtc=new Date().toISOString(),inputs=[pin(__filename),pin(process.execPath)],manifestPin=pin(manifestFile);
  assert.equal(manifestPin.sha256,expectedManifest);inputs.push(manifestPin);
  const manifest=json(manifestFile);assert.equal(manifest.schema,'soui.current-workbench-light-payload/v1');assert.equal(manifest.version,'0.6.0');
  assert.deepEqual(manifest.arms.map(a=>a.architecture),['x64','x86']);assert.equal(manifest.payloadFiles.length,12);
  const indexFile=path.join(decodeRoot,'Files.index.json'),index=json(indexFile);inputs.push(pin(indexFile));
  assert.equal(index.schema,'soui.issue-short-decoded-files/v1');assert(Array.isArray(index.files));
  const indexed=index.files.map(f=>relative(f.path));assert.equal(new Set(indexed.map(n=>n.toLowerCase())).size,indexed.length);
  assert.deepEqual(walk(decodeRoot),[...indexed,'Files.index.json'].sort());
  for(const f of index.files){const p=pin(path.join(decodeRoot,...f.path.split('/')));equalPin(p,f);inputs.push(p);}
  const decodedReceipt=json(path.join(decodeRoot,'Decode.actual.json'));
  assert.equal(decodedReceipt.testPassed,null);assert(decodedReceipt.allOriginalBytesAndSha256Verified&&decodedReceipt.caseInsensitiveClosedSetVerified);
  assert.equal(decodedReceipt.sourceSerial.wholeSerialCopied,false);assert.equal(decodedReceipt.sourceSerial.otherTagsCopied,false);
  const raw=path.join(decodeRoot,'raw'),read=n=>json(path.join(raw,n)),rawPin=n=>pin(path.join(raw,n));
  const catalog=read('ExportFiles.actual.json');assert.equal(catalog.schema,'soui.issue-short-export-files/v1');
  const expectedNames=['Environment.before-and-active.json','ExportPreflight.actual.json','InputCopy.actual.json','Invocation.actual.json','Lease.actual.json','PowerShell.finished.json','Stage.actual.json','Wrapper.Invocation.actual.json','Wrapper.finished.json','stderr.log','stdout.log'];
  assert.deepEqual(walk(raw),['ExportFiles.actual.json',...expectedNames].sort());assert.equal(catalog.files.length,11);
  assert.equal(decodedReceipt.payloadFileCount,11);assert.equal(decodedReceipt.rawFileCountIncludingCatalog,12);
  const first=/^C:\\issue-closeout-evidence\\(wb31c-([a-z0-9][a-z0-9-]{0,23})-stage)\\/.exec(catalog.files[0].path);assert(first);
  const evidence='C:\\issue-closeout-evidence\\'+first[1],stamp=first[2],root='C:\\issue31c'+stamp;
  const names=new Set();let total=0;
  for(const row of catalog.files){assert(row.path.startsWith(evidence+'\\'));const n=row.path.slice(evidence.length+1).replaceAll('\\','/');assert(expectedNames.includes(n));assert(!names.has(n));names.add(n);equalPin(rawPin(n),row);total+=row.bytes;}
  assert.equal(total,catalog.totalBytes);assert(total+rawPin('ExportFiles.actual.json').bytes<=8388608);
  const finished=read('PowerShell.finished.json'),wrapper=read('Wrapper.finished.json'),invocation=read('Invocation.actual.json'),wrapperInvocation=read('Wrapper.Invocation.actual.json'),lease=read('Lease.actual.json'),preflight=read('ExportPreflight.actual.json');
  assert.equal(finished.exitCode,0);assert.equal(finished.environmentRestored,true);assert.equal(finished.leaseReleased,true);assert.deepEqual(finished.restoreErrors,[]);
  assert.equal(wrapper.nativeExitCode,0);assert.equal(wrapper.failure,null);assert.equal(wrapper.controllerReturned,true);assert.equal(wrapper.productExecutedByWrapper,false);
  assert(Number.isSafeInteger(wrapper.wrapperPid)&&wrapper.wrapperPid>0&&wrapper.wrapperPid<=2147483647);
  assert(typeof wrapper.wrapperCreationFileTime==='string'&&/^[0-9]{10,20}$/.test(wrapper.wrapperCreationFileTime));
  assert.equal(wrapperInvocation.wrapperPid,wrapper.wrapperPid);assert.equal(wrapperInvocation.wrapperCreationFileTime,wrapper.wrapperCreationFileTime);
  assert.equal(lease.acquired,true);assert.equal(lease.name,'Local\\SouiDevKit.DesktopValidation');
  assert(Number.isSafeInteger(lease.ownerProcessId)&&lease.ownerProcessId>0);assert(/^[0-9]{10,20}$/.test(lease.ownerCreationFileTime));
  assert.equal(preflight.schema,'soui.current-closed-export-preflight/v1');assert.equal(preflight.kind,'wb');assert.equal(preflight.phase,'stage');assert.equal(preflight.machine,machine);
  const closed=preflight.closedLauncher;
  equalPin(rawPin('Lease.actual.json'),closed.lease);equalPin(rawPin('PowerShell.finished.json'),closed.finished);equalPin(rawPin('Wrapper.finished.json'),closed.wrapperFinished);
  assert.equal(closed.launcherIdentity.pid,lease.ownerProcessId);assert.equal(closed.launcherIdentity.creationFileTime,lease.ownerCreationFileTime);assert.equal(closed.launcherIdentity.identityAbsent,true);
  assert.equal(closed.wrapperIdentity.pid,wrapper.wrapperPid);assert.equal(closed.wrapperIdentity.creationFileTime,wrapper.wrapperCreationFileTime);assert.equal(closed.wrapperIdentity.identityAbsent,true);
  assert.equal(invocation.mode,'stage');assert.equal(invocation.version,'0.6.0');assert.equal(invocation.originalReleaseRelabeled,false);
  assert.deepEqual(invocation.args,[path.win32.join(root,'WB-Guest.cjs'),'stage',path.win32.join(root,'WB.manifest.json'),expectedManifest,machine,evidence]);
  assert.equal(wrapperInvocation.mode,'stage');assert.equal(wrapperInvocation.kind,'wb');assert.equal(wrapperInvocation.manifestSha256,expectedManifest);assert.equal(wrapperInvocation.automaticStageToRun,false);assert.equal(wrapperInvocation.deadlineChanged,false);
  assert.equal(readArg(wrapperInvocation.arguments,'-Mode'),'stage');assert(samePath(readArg(wrapperInvocation.arguments,'-Manifest'),path.win32.join(root,'WB.manifest.json')));assert.equal(readArg(wrapperInvocation.arguments,'-ManifestSha256'),expectedManifest);assert.equal(readArg(wrapperInvocation.arguments,'-ExpectedMachine'),machine);assert(samePath(readArg(wrapperInvocation.arguments,'-EvidenceDirectory'),evidence));
  const environment=read('Environment.before-and-active.json');assert.equal(environment.mode,'stage');assert.equal(environment.version,'0.6.0');assert.equal(environment.activePath,finished.pathAfter);
  const copy=read('InputCopy.actual.json');assert.equal(copy.schema,'soui.current-flat-payload-copy/v1');assert.equal(copy.kind,'wb');assert.equal(copy.stamp,stamp);assert.equal(copy.manifestSha256,expectedManifest);assert.equal(copy.files,13);assert.equal(copy.productExecuted,false);assert.equal(copy.copies.length,13);
  const selection=[...manifest.payloadFiles,{relative:'WB.manifest.json',bytes:manifestPin.bytes,sha256:manifestPin.sha256}],copyTargets=new Set();
  for(const row of copy.copies){const expected=selection.find(f=>samePath(row.copy.path,path.win32.join(root,f.relative)));assert(expected);assert(!copyTargets.has(expected.relative));copyTargets.add(expected.relative);assert(samePath(row.source.path,path.win32.join('D:\\',expected.relative)));equalPin(row.source,expected);equalPin(row.copy,expected);}
  assert.equal(copyTargets.size,13);
  const stageFile=path.join(raw,'Stage.actual.json'),stagePin=pin(stageFile),stage=json(stageFile);
  assert.equal(stage.schema,'soui.current-workbench-stage/v1');assert.equal(stage.passed,true);assert.equal(stage.machine,machine);assert.equal(stage.productExecuted,false);assert.equal(stage.originalRuntimeChanged,false);
  assert.equal(stage.manifest.sha256,expectedManifest);assert.equal(stage.manifest.bytes,manifestPin.bytes);assert(samePath(stage.manifest.path,path.win32.join(root,'WB.manifest.json')));
  const runtimeExpected=[];
  for(const arm of manifest.arms){assert.equal(arm.runtimeDirectory,'runtime-'+arm.architecture);assert.equal(arm.files.length,671);assert.equal(arm.originalFiles.length,671);for(const file of arm.files){const source=file.fromPayload?path.win32.join(root,file.source):file.source;runtimeExpected.push({architecture:arm.architecture,source:expectedEntry(source,file.bytes,file.sha256),copy:expectedEntry(path.win32.join(root,arm.runtimeDirectory,file.relative),file.bytes,file.sha256)});}}
  assert.equal(stage.copies.length,runtimeExpected.length);const runtimeTargets=new Set();
  for(const row of stage.copies){const expected=runtimeExpected.find(e=>samePath(row.copy.path,e.copy.path));assert(expected);const key=path.win32.normalize(row.copy.path).toLowerCase();assert(!runtimeTargets.has(key));runtimeTargets.add(key);assert(samePath(row.source.path,expected.source.path));equalPin(row.source,expected.source);equalPin(row.copy,expected.copy);}
  assert.equal(runtimeTargets.size,runtimeExpected.length);assert.equal(runtimeTargets.size,1342);
  assert(Array.isArray(stage.sourceLogs));assert.deepEqual(stage.sourceLogs.map(a=>a.architecture),['x64','x86']);
  for(const observed of stage.sourceLogs){
    const arm=manifest.arms.find(a=>a.architecture===observed.architecture);assert(arm);assert(Array.isArray(observed.before));assert.deepEqual(observed.before,observed.after);
    const logNames=new Set();for(const row of observed.before){
      relative(row.relative);assert(/\.log$/i.test(row.relative));assert(!logNames.has(row.relative.toLowerCase()));logNames.add(row.relative.toLowerCase());
      assert(samePath(row.path,path.win32.join(arm.originalRuntime,row.relative)));assert(Number.isSafeInteger(row.bytes)&&row.bytes>=0&&row.bytes<=16777216);assert(/^[a-f0-9]{64}$/.test(row.sha256));
    }
  }
  for(const item of inputs)equalPin(pin(item.path),item);
  fs.mkdirSync(path.dirname(analysisRoot),{recursive:true});fs.mkdirSync(analysisRoot);
  const report={schema:'soui.current-workbench-stage-analysis/v1',startedUtc,completedUtc:new Date().toISOString(),decoderTag:decodedReceipt.tag,decodedRoot:decodeRoot,evidence,stamp,
    manifest:manifestPin,actualStage:stagePin,sourceCopyCount:copyTargets.size,runtimeCopyCount:runtimeTargets.size,architectures:['x64','x86'],
    rawTransportClosedSetAndPinsVerified:true,exactHostedManifestVerified:true,sourceAndRuntimeCopyReportsMatchManifest:true,stageControllerAndWrapperExitZero:true,stageOwnerIdentitiesAbsent:true,
    scope:'Offline validation of actual current Workbench staging/transport/launcher only; no product or behavior execution',originalRuntimeUnchangedReportedAndSourceLogPinsMatch:true,
    guestProductExecuted:false,ownedWholeJobProofEstablished:false,originalReleaseRelabeled:false,genuineShellGestureQualified:false,longResidencyQualified:false,currentSdk060Qualified:false,testPassed:null,productionQualificationComplete:false,fullDevtoolsGoalComplete:false,releaseReady:false,inputs};
  const reportFile=path.join(analysisRoot,'StageAnalysis.actual.json');fs.writeFileSync(reportFile,JSON.stringify(report,null,2)+'\n',{flag:'wx'});
  console.log(JSON.stringify({stageSha256:stagePin.sha256,stageBytes:stagePin.bytes,manifestSha256:expectedManifest,sourceCopyCount:copyTargets.size,runtimeCopyCount:runtimeTargets.size,stageControllerAndWrapperExitZero:true,report:pin(reportFile),productExecuted:false,testPassed:null}));
}
try{main();}catch(error){console.error('WB stage analysis refused: '+error.stack);process.exitCode=1;}
