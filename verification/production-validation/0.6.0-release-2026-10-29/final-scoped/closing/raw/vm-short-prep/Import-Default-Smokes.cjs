'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const root='C:/sr06',base=path.join(__dirname,'default-vm-attempt-2');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex'),read=f=>JSON.parse(fs.readFileSync(f,'utf8'));
const eRoot=path.join(base,'guest/release06-control/evidence'),dirs=fs.readdirSync(eRoot);assert.equal(dirs.length,1);
const folder=path.join(eRoot,dirs[0]),summary=read(path.join(folder,'Summary.actual.json')),inputs=read(path.join(folder,'Inputs.actual.json'));
assert.equal(summary.passed,true);assert.equal(summary.fullEightApplicationCohortExecutedInThisAttempt,true);assert.equal(summary.failed,null);
assert.deepEqual(summary.selection,['x86:desktop','x86:modern','x64:desktop','x64:modern']);assert.equal(summary.stages.length,4);
assert.equal(inputs.controllerRunner.sha256,'f254dadd4eccad3c57d69e98f260d15ee598c0b6441b7fe68fc4ef5b3b7dea83');
const {validateStage}=require(path.join(root,'tools/internal/devkit-command.cjs'));
const id=require(path.join(root,'tools/internal/delivery-identity.cjs')),build=require(path.join(root,'tools/internal/build-evidence.cjs'));
const outer=[];
for(const label of summary.selection){const [arch,kind]=label.split(':'),name=arch+'-'+kind;
 const raw=fs.readFileSync(path.join(folder,name+'.ndjson')),contextBytes=fs.readFileSync(path.join(folder,name+'.ndjson.context.json'));
 const expected={executable:inputs.powershell.path,executableSha256:inputs.powershell.sha256,runnerSha256:inputs.controllerRunner.sha256,
  args:['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',path.join(root,'tools',kind==='desktop'?'Test.ps1':'Test-Modern.ps1'),'-Architecture',arch,'-Configuration','Release',...(kind==='desktop'?['-NativeOnly']:[])],
  cwd:path.resolve(root),timeoutMs:kind==='desktop'?180000:90000,drainMs:2000,contextSha256:hash(contextBytes)};
 const result=validateStage(raw,contextBytes,read(path.join(folder,name+'-ready.json')),read(path.join(folder,name+'-finished.json')),expected);
 outer.push({stage:label,...result});
}
const extraction=read(path.join(base,'Extraction.actual.json'));assert.equal(extraction.allRawMatched,true);
const copied=[],unchanged=[];
for(const row of extraction.rawFiles){
 if(!row.path.startsWith('guest/sr06/out/'))continue;
 const relative=row.path.slice('guest/sr06/'.length);assert.ok(relative.startsWith('out/'));
 const source=path.join(base,row.path),target=id.safePath(root,relative),bytes=fs.readFileSync(source);
 assert.equal(bytes.length,row.bytes);assert.equal(hash(bytes),row.sha256);
 if(fs.existsSync(target)){assert.ok(fs.readFileSync(target).equals(bytes),'refuse different existing host evidence: '+relative);unchanged.push(relative);}
 else{fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes,{flag:'wx'});copied.push(relative);}
}
const cases=[];
for(const arch of ['x86','x64']){
 const desktop=build.verifySmoke(root,arch,'Release'),modern=build.verifyModernSmoke(root,arch,'Release');
 assert.equal(desktop.smoke.runs.length,3);assert.equal(modern.smoke.passed,true);
 cases.push({architecture:arch,buildId:desktop.build.buildId,sourceSha256:desktop.build.source.sha256,
  desktopRuns:desktop.smoke.runs.map(r=>({target:r.target,scenario:r.scenario,pid:r.pid,creationFileTime:r.creationFileTime,exitCode:r.exitCode,passed:r.passed})),
  modern:{pid:modern.smoke.pid,creationFileTime:modern.smoke.creationFileTime,exitCode:modern.smoke.exitCode,log:modern.smoke.log,passed:modern.smoke.passed}});
}
const result={schema:'soui.release-vm-default-smoke-import/v1',version:'0.6.0',importedAt:new Date().toISOString(),
 extraction:hash(fs.readFileSync(path.join(base,'Extraction.actual.json'))),outer,copied,unchanged,cases,
 publicSmokeReadersPassed:true,applicationRuns:8,guiInteractionInferred:false,releaseReady:false,productionQualificationComplete:false};
fs.writeFileSync(path.join(__dirname,'DefaultSmokes.actual.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({publicSmokeReadersPassed:true,applicationRuns:8,copied:copied.length,unchanged:unchanged.length,outerNatural:outer.length}));
