'use strict';
// Explicit post-build data preparation only; no build, product or VM execution.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
function pin(f){const a=fs.lstatSync(f,{bigint:true});assert(a.isFile()&&!a.isSymbolicLink());const b=fs.readFileSync(f),z=fs.lstatSync(f,{bigint:true});for(const k of ['dev','ino','size','mtimeNs','ctimeNs','nlink'])assert.equal(a[k],z[k]);return {path:f,bytes:b.length,sha256:hash(b)};}
const json=f=>JSON.parse(fs.readFileSync(f,'utf8'));
const write=(f,o)=>fs.writeFileSync(f,JSON.stringify(o,null,2)+'\n',{flag:'wx'});
const same=(a,b)=>{assert.equal(a.bytes,b.bytes);assert.equal(a.sha256,b.sha256);};
function names(d,p=''){return fs.readdirSync(path.join(d,p),{withFileTypes:true}).flatMap(e=>{assert(!e.isSymbolicLink());const n=p?p+'/'+e.name:e.name;return e.isDirectory()?names(d,n):(assert(e.isFile()),[n]);}).sort();}
function natural(r){const t=r.terminal;assert(r.passed===true&&r.exitCode===0&&r.supervisorExitCode===0&&r.signal===null&&r.spawnError===null&&r.reportError===null&&!r.stopRequested&&!r.fallbackUsed&&r.closureConfirmed);assert(t?.reason==='completed'&&t.rootExited&&t.rootExitCode===0&&t.activeBeforeCleanup===0&&t.activeProcesses===0&&!t.terminatedOwnedJob&&t.limitTerminatedProcesses===0&&t.closureConfirmed&&t.passed);}
const [gate,attempt]=process.argv.slice(2);assert.equal(process.argv.length,4);assert.equal(gate,'ROOT_REVIEWED_CURRENT_ORDINARY_BUILD_V1');assert.equal(process.platform,'win32');assert(path.isAbsolute(attempt));assert.equal(path.dirname(path.resolve(attempt)),path.resolve(__dirname));assert(/^attempt-[a-f0-9-]{36}$/.test(path.basename(attempt)));
const summaryFile=path.join(attempt,'summary.json'),summary=json(summaryFile);assert.equal(summary.schema,'soui.ordinary-acceptance-reload-consumer-build-only/v1');assert.equal(summary.passed,true);assert.equal(summary.failure,null);assert.deepEqual(summary.verdicts.map(v=>v.architecture),['x64','x86']);assert.equal(summary.executableExecuted,false);assert.equal(summary.productExecuted,false);
const before=json(path.join(attempt,'inputs-before.json')),after=json(path.join(attempt,'inputs-after.json'));assert.deepEqual(before.sourcePins,after.sourcePins);
for(const f of before.sourcePins)same(pin(f.path),f);
const reuseFile=path.join(__dirname,'web-reuse/Reuse.actual.json'),reuse=json(reuseFile);assert.equal(reuse.schema,'soui.current-ordinary-web-byte-reuse/v1');assert.equal(reuse.currentInstalledSdkVersion,'0.6.0');assert.equal(reuse.fixtureWebByteEquivalence.length,2);assert.equal(reuse.publicSdkExecutableDistByteEquivalence.length,26);assert.equal(reuse.webOutput.files.length,5);assert.equal(reuse.currentToolchainOutputEquivalenceClaimed,false);assert.equal(reuse.webRebuilt,false);assert.equal(reuse.typecheckReexecuted,false);
assert.deepEqual(names(path.join(__dirname,'web-output')),reuse.webOutput.files.map(f=>f.path).sort());for(const f of reuse.webOutput.files)same(pin(path.join(__dirname,'web-output',f.path)),f);
const nativeNames=['CMakeLists.txt','main.cpp','shutdown-poll.h','native-owner-metrics.h','process-address-census.h','README.md'];
for(const n of nativeNames)same(pin('C:/sc07/tests/acceptance-app/'+n),pin(path.join(__dirname,'source',n)));
assert.equal(pin(path.join(__dirname,'source/main.cpp')).sha256,'4d58281549806425f097ee0ca9bc04160f43ca112c027b46b4dfc199a8173be4');
const folder=path.join(__dirname,'light-payload-'+crypto.randomUUID()),payload=path.join(folder,'payload');fs.mkdirSync(folder);fs.mkdirSync(payload);
const copies=[];function copy(source,name){const a=pin(source);fs.copyFileSync(source,path.join(payload,name),fs.constants.COPYFILE_EXCL);const b=pin(path.join(payload,name)),z=pin(source);same(a,b);assert.deepEqual(a,z);copies.push({source:a,sourceAfter:z,payload:{relative:name,bytes:b.bytes,sha256:b.sha256}});return b;}
for(const n of ['Guest-Ordinary.cjs','Guest-Ordinary.ps1'])copy(path.join(__dirname,n),n);
copy(summaryFile,'HostBuild.summary.json');copy(reuseFile,'WebReuse.actual.json');
for(const n of nativeNames)copy(path.join(__dirname,'source',n),'NativeSource.'+n);
// Flat carrier names transport only 170KiB of actual ordinary assets. Do not
// assume the guest has the old ordinary suite or reuse MessagePort Web assets.
const webAssets=reuse.webOutput.files.map((f,i)=>{const name='WebAsset-'+i+path.extname(f.path);copy(path.join(__dirname,'web-output',f.path),name);return {...f,carrierName:name};});
const readers={runner:'F:/work/soui-issue96/tests/acceptance/runner.cjs',supervisor:'F:/work/soui-issue96/tests/acceptance/monotonic-supervisor.cjs'};
const modules=new Map();function dependencies(f){const key=path.resolve(f).toLowerCase();if(modules.has(key))return;modules.set(key,pin(f));for(const m of fs.readFileSync(f,'utf8').matchAll(/require\(\s*['"](\.[^'"]+)['"]\s*\)/g)){let n=path.resolve(path.dirname(f),m[1]);if(!fs.existsSync(n)&&fs.existsSync(n+'.cjs'))n+='.cjs';assert(fs.existsSync(n),'Missing literal reader dependency');dependencies(n);}}
for(const f of Object.values(readers))dependencies(f);
const proofFile=path.join(__dirname,'OrdinaryValidatorProof.prepared.json'),proof=json(proofFile);same(pin(proof.original.path),proof.original);same(pin(proof.current.path),proof.current);assert.equal(proof.regions.length,2);assert(proof.regions.every(r=>r.byteEqual&&r.original.sha256===r.current.sha256&&r.original.bytes===r.current.bytes));
const tools={node:{...pin(process.execPath),path:'C:/issues8/tools/node.exe'},runner:{...pin('C:/sr06/out/benchmark-protocol/Release/OwnedProcessRunner.exe'),path:'C:/issues8/tools/OwnedProcessRunner.exe'},ownedModule:{...pin('C:/sc07/tools/internal/owned-process.cjs'),path:'C:/issues8/tools/owned-process.cjs'}};
assert.equal(tools.runner.sha256,'f254dadd4eccad3c57d69e98f260d15ee598c0b6441b7fe68fc4ef5b3b7dea83');
const arms=[],installedSdkProducers={};
for(const arch of ['x64','x86']){
 const verdict=summary.verdicts.find(v=>v.architecture===arch);assert(verdict.wholeJobNaturallyClosed&&verdict.genericSupervisionPassed&&!verdict.executableExecuted);natural(json(path.join(attempt,arch,'native-finished.json')));
 const exe=pin(verdict.compiledExecutable.path);same(exe,verdict.compiledExecutable);const exeName='SouiAcceptance-'+arch+'.exe';copy(exe.path,exeName);copy(path.join(attempt,arch,'case-verdict.json'),'HostBuild.'+arch+'.json');
 const buildFile='C:/sr06/out/'+arch+'/build-Release.json',build=json(buildFile);assert.equal(build.version,'0.6.0');assert.equal(build.architecture,arch);
 const native=build.payloads.SouiStarter.files.filter(f=>!f.path.includes('/')&&/\.dll$/i.test(f.path));assert(native.some(f=>f.path==='node.dll'));
 const dlls=native.map(f=>{const source='C:/sr06/out/'+arch+'/bin/Release/SouiStarter/'+f.path,p=pin(source);same(p,f);return {relative:f.path,source,bytes:p.bytes,sha256:p.sha256,fromPayload:false,kind:'current0.6.0-runtime-DLL'};});
 const generated=json(path.join(attempt,arch,'resource-pins.json'));assert.equal(generated.resources.length,15);assert.deepEqual(generated.originalWebOutput,reuse.webOutput);
 const resources=[1,2,3].flatMap(window=>webAssets.map(f=>{const relative='resources/window-'+window+'/'+f.path,actual=generated.resources.find(p=>p.path.replaceAll('\\','/').endsWith('/window-'+window+'/'+f.path));assert(actual);same(pin(actual.path),f);return {relative,source:f.carrierName,bytes:f.bytes,sha256:f.sha256,fromPayload:true,kind:'exact original ordinary Web asset reuse'};}));
 const files=[{relative:'SouiAcceptance.exe',source:exeName,bytes:exe.bytes,sha256:exe.sha256,fromPayload:true,kind:'new independently compiled current installed SDK ordinary consumer'},...dlls,...resources];assert.equal(new Set(files.map(f=>f.relative.toLowerCase())).size,files.length);
 const prefix='C:/sr06/out/release-install-'+arch+'-release';installedSdkProducers[arch]={version:'0.6.0',prefix,library:pin(prefix+'/lib/Release/soui_sdk.lib'),versionHeader:pin(prefix+'/include/soui/version.h'),compileVerdict:pin(path.join(attempt,arch,'case-verdict.json')),selectedCurrentApplicationBuild:pin(buildFile),sdkRebuilt:false};
 arms.push({architecture:arch,runtimeDirectory:'runtime-'+arch,files,currentKernel:dlls.find(f=>f.relative==='node.dll'),oldSdkNativeExeReused:false});
}
const payloadFiles=copies.map(c=>c.payload).sort((a,b)=>a.relative<b.relative?-1:a.relative>b.relative?1:0);assert.equal(payloadFiles.length,19);
const manifest={schema:'soui.current-ordinary-acceptance-light-payload/v1',version:'0.6.0',createdUtc:new Date().toISOString(),preparationHost:os.hostname(),payloadFiles,readers,readerPins:[...modules.values()],tools,installedSdkProducers,arms,
 currentNativeSource:'tests/acceptance-app exact pending-only reload consumption under one original deadline',validatorByteProof:proof,webAssets,
 webReuse:{record:'WebReuse.actual.json',executableSdkDistByteIdenticalFiles:26,fixtureWebByteIdenticalFiles:2,originalPackageVersion:'0.5.1-preview.1',currentSdkPackageVersion:'0.6.0',changedPackageKeys:['version'],originalToolsVite:'6.4.4',currentToolsVite:'6.4.5',currentToolchainOutputEquivalenceClaimed:false,webRebuilt:false,typecheckReexecuted:false},
 config:{mode:'reload',cycles:3,warmup:0,settleMs:500,timeoutMs:20000},processTimeoutMs:214500,ownedTimeoutMs:394500,drainMs:2000,
 oldSuitesReadOnly:true,browserBehaviorQualified:false,messagePortBehaviorAsserted:false,currentSdk060Qualified:false,resourcePassed:false,productionQualificationComplete:false,fullDevtoolsGoalComplete:false,productExecuted:false,vmTouched:false,releaseReady:false};
const manifestFile=path.join(payload,'Ordinary.manifest.json');write(manifestFile,manifest);
const selection=[...payloadFiles.map(f=>({name:f.relative,path:path.join(payload,f.relative),bytes:f.bytes,sha256:f.sha256})),{name:'Ordinary.manifest.json',...pin(manifestFile)}];assert.equal(selection.length,20);assert(selection.every(f=>f.bytes<=16777216));write(path.join(folder,'ControlIso.selection.json'),selection);
const wrapperNames=['Ordinary-Lib.ps1','Stage-Ordinary.ps1','Run-Ordinary.ps1','Export-Ordinary.ps1'];const controlSelection=[...selection,...wrapperNames.map(name=>({name,...pin(path.join(__dirname,name))}))];assert.equal(controlSelection.length,24);write(path.join(folder,'ControlIso.with-wrappers.selection.json'),controlSelection);
write(path.join(folder,'Preparation.actual.json'),{schema:'soui.current-ordinary-acceptance-light-preparation/v1',preparer:pin(__filename),buildSummary:pin(summaryFile),copies,manifest:pin(manifestFile),selection:pin(path.join(folder,'ControlIso.selection.json')),selectionWithWrappers:pin(path.join(folder,'ControlIso.with-wrappers.selection.json')),payloadBytes:selection.reduce((n,f)=>n+f.bytes,0),oldSuitesChanged:false,productExecuted:false,vmTouched:false,browserBehaviorQualified:false,messagePortBehaviorAsserted:false,releaseReady:false});
console.log(JSON.stringify({payload,manifest:pin(manifestFile),selection:pin(path.join(folder,'ControlIso.selection.json')),selectionWithWrappers:pin(path.join(folder,'ControlIso.with-wrappers.selection.json')),files:selection.length,bytes:selection.reduce((n,f)=>n+f.bytes,0),productExecuted:false}));
