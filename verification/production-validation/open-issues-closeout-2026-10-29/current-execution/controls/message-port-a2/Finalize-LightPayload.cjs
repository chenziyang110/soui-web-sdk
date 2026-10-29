'use strict';
// Run only after the reviewed one-TU builds succeed. No VM/product execution.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
function pin(f){const s=fs.lstatSync(f);assert(s.isFile()&&!s.isSymbolicLink());const b=fs.readFileSync(f);return {path:f,bytes:b.length,sha256:hash(b)};}
const write=(f,o)=>fs.writeFileSync(f,JSON.stringify(o,null,2)+'\n',{flag:'wx'});
const json=f=>JSON.parse(fs.readFileSync(f));
const [gate,attempt]=process.argv.slice(2);assert.equal(gate,'ROOT_REVIEWED_CURRENT_MESSAGE_PORT_BUILD_V1');assert(path.isAbsolute(attempt));
const summaryFile=path.join(attempt,'summary.json'),summary=json(summaryFile);assert.equal(summary.passed,true);assert.deepEqual(summary.verdicts.map(v=>v.architecture),['x64','x86']);assert.equal(summary.executableExecuted,false);
const folder=path.join(__dirname,'light-payload-'+crypto.randomUUID()),payload=path.join(folder,'payload');fs.mkdirSync(folder);fs.mkdirSync(payload);
const copies=[];function copy(source,name){const before=pin(source);fs.copyFileSync(source,path.join(payload,name),fs.constants.COPYFILE_EXCL);const after=pin(path.join(payload,name));assert.equal(before.bytes,after.bytes);assert.equal(before.sha256,after.sha256);assert.deepEqual(pin(source),before);copies.push({source:before,payload:{relative:name,bytes:after.bytes,sha256:after.sha256}});return after;}
for(const f of ['Guest-Current.cjs','Guest-Current.ps1'])copy(path.join(__dirname,f),f);
copy(summaryFile,'HostBuild.summary.json');copy(path.join(__dirname,'web-reuse/Reuse.actual.json'),'WebReuse.actual.json');
for(const f of ['CMakeLists.txt','main.cpp','shutdown-poll.h','README.md'])copy('C:/sc07/tests/message-port-app/'+f,'NativeSource.'+f);
const readers={runner:'F:/work/soui-issue96/tests/acceptance/runner.cjs',behavior:'F:/work/soui-issue96/tests/acceptance/message-port-report-reader.cjs',supervisor:'F:/work/soui-issue96/tests/acceptance/monotonic-supervisor.cjs'};
// Literal local requires are read, never executed during this closure collection.
const modules=new Map();function dependencies(f){const key=path.resolve(f).toLowerCase();if(modules.has(key))return;modules.set(key,pin(f));const text=fs.readFileSync(f,'utf8');for(const m of text.matchAll(/require\(\s*['"](\.[^'"]+)['"]\s*\)/g)){let next=path.resolve(path.dirname(f),m[1]);if(!fs.existsSync(next)&&fs.existsSync(next+'.cjs'))next+='.cjs';assert(fs.existsSync(next),'Missing literal reader dependency');dependencies(next);}}
for(const f of Object.values(readers))dependencies(f);
const tools={node:{...pin(process.execPath),path:'C:/issues8/tools/node.exe'},
  runner:{...pin('C:/sr06/out/benchmark-protocol/Release/OwnedProcessRunner.exe'),path:'C:/issues8/tools/OwnedProcessRunner.exe'},
  ownedModule:{...pin('C:/sc07/tools/internal/owned-process.cjs'),path:'C:/issues8/tools/owned-process.cjs'}};
assert.equal(tools.runner.sha256,'f254dadd4eccad3c57d69e98f260d15ee598c0b6441b7fe68fc4ef5b3b7dea83');
const arms=[],installedSdkProducers={};
for(const arch of ['x64','x86']){
  const verdict=summary.verdicts.find(v=>v.architecture===arch),exe=pin(verdict.compiledExecutable.path);assert.equal(exe.bytes,verdict.compiledExecutable.bytes);assert.equal(exe.sha256,verdict.compiledExecutable.sha256);assert(verdict.wholeJobNaturallyClosed);
  const exeName='SouiAcceptance-'+arch+'.exe';copy(exe.path,exeName);copy(path.join(attempt,arch,'case-verdict.json'),'HostBuild.'+arch+'.json');
  const buildFile='C:/sr06/out/'+arch+'/build-Release.json',build=json(buildFile);assert.equal(build.version,'0.6.0');assert.equal(build.architecture,arch);
  const native=build.payloads.SouiStarter.files.filter(f=>!f.path.includes('/')&&/\.dll$/i.test(f.path));assert(native.some(f=>f.path==='node.dll'));
  const dlls=native.map(f=>{const source='C:/sr06/out/'+arch+'/bin/Release/SouiStarter/'+f.path,p=pin(source);assert.equal(p.bytes,f.bytes);assert.equal(p.sha256,f.sha256);return {relative:f.path,source,bytes:p.bytes,sha256:p.sha256,fromPayload:false,kind:'current0.6.0-runtime-DLL'};});
  const oldSuite='F:/work/soui-issue96/out/acceptance/'+(arch==='x64'?'bd7afa33-ea0d-4e55-b482-d2b01f64ee19':'5c02d217-f04f-4f32-9794-110e7fb3f734'),old=json(oldSuite+'/build.json');
  const resources=old.payload.files.filter(f=>f.path.startsWith('resources/')).map(f=>{const source=oldSuite+'/runtime/'+f.path,p=pin(source);assert.equal(p.bytes,f.bytes);assert.equal(p.sha256,f.sha256);return {relative:f.path,source,bytes:p.bytes,sha256:p.sha256,fromPayload:false,kind:'byte-equivalent reviewed Web artifact reuse'};});
  assert.equal(resources.length,18);const generated=json(path.join(attempt,arch,'resource-pins.json'));
  for(const f of resources){const window=f.relative.split('/')[1],relative=f.relative.split('/').slice(2).join('/');const actual=generated.resources.find(p=>p.path.replaceAll('\\','/').endsWith('/'+window+'/'+relative));assert(actual);assert.equal(actual.bytes,f.bytes);assert.equal(actual.sha256,f.sha256);}
  const files=[{relative:'SouiAcceptance.exe',source:exeName,bytes:exe.bytes,sha256:exe.sha256,fromPayload:true,kind:'new independently compiled current installed SDK consumer'},...dlls,...resources];assert.equal(new Set(files.map(f=>f.relative.toLowerCase())).size,files.length);
  const prefix='C:/sr06/out/release-install-'+arch+'-release';installedSdkProducers[arch]={version:'0.6.0',prefix,library:pin(prefix+'/lib/Release/soui_sdk.lib'),versionHeader:pin(prefix+'/include/soui/version.h'),compileVerdict:pin(path.join(attempt,arch,'case-verdict.json')),selectedCurrentApplicationBuild:pin(buildFile),sdkRebuilt:false};
  arms.push({architecture:arch,runtimeDirectory:'runtime-'+arch,files,currentKernel:dlls.find(d=>d.relative==='node.dll'),oldResourcesOnlyFrom:oldSuite,oldSdkNativeExeReused:false});
}
const payloadFiles=copies.map(c=>c.payload).sort((a,b)=>a.relative<b.relative?-1:a.relative>b.relative?1:0);
const manifest={schema:'soui.current-message-port-light-payload/v1',version:'0.6.0',createdUtc:new Date().toISOString(),preparationHost:os.hostname(),payloadFiles,readers,readerPins:[...modules.values()],tools,installedSdkProducers,arms,
  currentNativeSource:'tests/message-port-app with existing pending-only shutdown helper',webReuse:{record:'WebReuse.actual.json',executableSdkDistByteIdenticalFiles:26,fixtureWebByteIdenticalFiles:4,originalPackageVersion:'0.5.1-preview.1',currentSdkPackageVersion:'0.6.0',changedPackageKeys:['version'],webRebuilt:false,typecheckReexecuted:false},
  config:{mode:'reload',cycles:3,warmup:0,settleMs:500,timeoutMs:20000},processTimeoutMs:214500,ownedTimeoutMs:394500,drainMs:2000,
  oldSuitesReadOnly:true,oldFourArmsPassed:false,currentSdk060Qualified:false,productionQualificationComplete:false,fullDevtoolsGoalComplete:false,productExecuted:false,vmTouched:false,releaseReady:false};
const manifestFile=path.join(payload,'Current.manifest.json');write(manifestFile,manifest);
const selection=[...payloadFiles.map(f=>({name:f.relative,path:path.join(payload,f.relative),bytes:f.bytes,sha256:f.sha256})),{name:'Current.manifest.json',...pin(manifestFile)}];
assert(selection.length<=32&&selection.every(f=>f.bytes<=16777216));write(path.join(folder,'ControlIso.selection.json'),selection);
write(path.join(folder,'Preparation.actual.json'),{schema:'soui.current-message-port-light-preparation/v1',preparer:pin(__filename),buildSummary:pin(summaryFile),copies,manifest:pin(manifestFile),selection:pin(path.join(folder,'ControlIso.selection.json')),payloadBytes:selection.reduce((n,f)=>n+f.bytes,0),oldSuitesChanged:false,productExecuted:false,vmTouched:false,releaseReady:false});
console.log(JSON.stringify({payload,manifest:pin(manifestFile),selection:pin(path.join(folder,'ControlIso.selection.json')),files:selection.length,bytes:selection.reduce((n,f)=>n+f.bytes,0),productExecuted:false}));
