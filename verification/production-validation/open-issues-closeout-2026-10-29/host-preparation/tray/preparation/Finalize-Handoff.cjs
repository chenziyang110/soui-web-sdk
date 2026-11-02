'use strict';
// Output-only payload assembly. No VM mount or GUI execution.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const base=path.join(__dirname,'handoff-6d09f9b2-1585-4187-95ba-2cd3d40cc832'),payload=path.join(base,'payload');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const pin=f=>{const s=fs.lstatSync(f);assert(s.isFile()&&!s.isSymbolicLink());const b=fs.readFileSync(f);return {path:f,bytes:b.length,sha256:hash(b)};};
const write=(f,o)=>fs.writeFileSync(f,JSON.stringify(o,null,2)+'\n',{flag:'wx'});
const copies=[];fs.mkdirSync(path.join(payload,'tools'));
function copy(source,relative){const before=pin(source),target=path.join(payload,relative);fs.copyFileSync(source,target,fs.constants.COPYFILE_EXCL);const after=pin(target);assert.equal(after.bytes,before.bytes);assert.equal(after.sha256,before.sha256);assert.deepEqual(pin(source),before);copies.push({source:before,copy:{path:relative,bytes:after.bytes,sha256:after.sha256}});}
copy(process.execPath,'tools/node.exe');
const runner='C:/sr06/out/benchmark-protocol/Release/OwnedProcessRunner.exe';assert.equal(pin(runner).sha256,'f254dadd4eccad3c57d69e98f260d15ee598c0b6441b7fe68fc4ef5b3b7dea83');
copy(runner,'tools/OwnedProcessRunner.exe');
for(const file of ['owned-process.cjs','owned-tail-evidence.cjs'])copy('C:/sc07/tools/internal/'+file,'tools/'+file);
copy(path.join(__dirname,'Guest-Shorts.cjs'),'Guest-Shorts.cjs');copy(path.join(__dirname,'Guest-Export.ps1'),'Guest-Export.ps1');
const cases=['x64','x86'].map(architecture=>({name:'tray-'+architecture,architecture,kind:'tray',runtime:'tray-'+architecture,timeoutMs:190000,drainMs:2000,nativeInteractiveMs:180000,uiInput:'Root CUA only; no synthetic callback or hotkey messages'}));
const workbenchPins=[];
for(const architecture of ['x64','x86']){
  const buildFile='C:/sr06/out/'+architecture+'/build-Release.json',buildPin=pin(buildFile),build=JSON.parse(fs.readFileSync(buildFile)),runtime='C:/sr06/out/'+architecture+'/bin/Release/SouiWorkbench';
  assert.equal(build.version,'0.6.0');assert.equal(build.architecture,architecture);
  const files=build.payloads.SouiWorkbench.files;assert.equal(new Set(files.map(f=>f.path)).size,files.length);
  for(const expected of files){const p=pin(path.join(runtime,expected.path));assert.equal(p.bytes,expected.bytes);assert.equal(p.sha256,expected.sha256);}
  assert.deepEqual(pin(buildFile),buildPin);workbenchPins.push(buildPin);
  cases.push({name:'resident-'+architecture,architecture,kind:'resident',runtime,files,sourceBuild:buildPin,timeoutMs:200000,drainMs:2000,nativeReadyMs:45000,nativeSmokeMs:180000,scope:'existing current guest Workbench resident lifecycle, not task-isolation or true input coverage'});
}
function walk(dir,prefix=''){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>{assert(!e.isSymbolicLink());const relative=prefix+e.name;if(e.isDirectory())return walk(path.join(dir,e.name),relative+'/');assert(e.isFile());const p=pin(path.join(dir,e.name));return [{path:relative,bytes:p.bytes,sha256:p.sha256}];}).sort((a,b)=>a.path<b.path?-1:a.path>b.path?1:0);}
const manifest={schema:'soui.current-public-tray-vm-payload-manifest/v1',files:walk(payload),cases,scope:'two current installed tray consumers and optional existing guest current Workbench resident shorts only',runnerPreparationGenericSupervisionPassed:null,sourcePreparationRequiresActualCallerProof:true,productExecuted:false,vmMounted:false,uiInteractionQualified:false,productionQualificationComplete:false,releaseReady:false};
const manifestFile=path.join(payload,'Payload.manifest.json');write(manifestFile,manifest);
write(path.join(base,'PayloadPreparation.actual.json'),{schema:'soui.current-public-tray-payload-preparation/v1',priorAssembly:pin(path.join(base,'Handoff.actual.json')),finalizer:pin(__filename),copies,workbenchPins,manifest:pin(manifestFile),payloadRoot:payload,files:manifest.files.length,bytes:manifest.files.reduce((n,f)=>n+f.bytes,0),cases:cases.map(c=>({name:c.name,timeoutMs:c.timeoutMs,drainMs:c.drainMs})),productExecuted:false,vmMounted:false,uiInteractionQualified:false,productionQualificationComplete:false,releaseReady:false});
console.log(JSON.stringify({payload,manifest:pin(manifestFile),files:manifest.files.length,bytes:manifest.files.reduce((n,f)=>n+f.bytes,0)}));
