'use strict';
// Output-only derivative preparation. No product/process/VM launch.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict'),os=require('node:os');
const root=__dirname,prior=path.resolve(root,'../handoff-6d09f9b2-1585-4187-95ba-2cd3d40cc832/payload');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const pin=p=>{const s=fs.lstatSync(p);assert(s.isFile()&&!s.isSymbolicLink());const b=fs.readFileSync(p);return {path:p,bytes:b.length,sha256:hash(b)};};
const write=(p,b)=>fs.writeFileSync(p,b,{flag:'wx'});
const writeJson=(p,v)=>write(p,JSON.stringify(v,null,2)+'\n');
const controller=path.join(prior,'Guest-Shorts.cjs'),original=fs.readFileSync(controller,'utf8');
assert.equal(hash(Buffer.from(original)),'f3927063064d8e50c18e045b8be3dbba1411caa94ad95f741d616d0797c2671d');
const inputPins=[pin(controller),pin(path.join(prior,'Payload.manifest.json')),pin(path.join(root,'Guest-Invoke.ps1'))];
const manifest=JSON.parse(fs.readFileSync(path.join(prior,'Payload.manifest.json'))),originalCases=JSON.parse(JSON.stringify(manifest.cases));
function replaceOnce(text,from,to){assert.equal(text.split(from).length,2,'Expected exactly one bounded derivative edit');return text.replace(from,to);}
let next=original;
next=replaceOnce(next,"function natural(r)","function observeLogs(directory){return walk(directory).filter(p=>/\\.log$/i.test(p)).map(p=>{const target=path.join(directory,p);const stat=fs.lstatSync(target);assert(stat.isFile()&&!stat.isSymbolicLink()&&stat.size<=16777216,'Bounded plain runtime log required');return {relative:p,...pin(target)};});}\nfunction natural(r)");
next=replaceOnce(next,"let runtime,executable,nativeArgs;const ignored=['resources/source-navigation.log','resources/webview-smoke.log'];","let runtime,executable,nativeArgs;const ignored=plan.kind==='resident'?plan.mutableRuntimeLogs:[];if(plan.kind==='resident')assert.deepEqual(ignored,['webview-smoke.log']);let caseError=null;\n    try{");
next=replaceOnce(next,"runtime=plan.runtime;verifyFiles(runtime,plan.files,ignored);executable=","runtime=plan.runtime;executable=");
next=replaceOnce(next,"write(folder,'ResidentPayload.before.json',{root:runtime,files:plan.files,allowedGeneratedLogObservations:ignored.filter(p=>fs.existsSync(path.join(runtime,p))).map(p=>pin(path.join(runtime,p)))});","write(folder,'ResidentPayload.before.json',{root:runtime,files:plan.files,mutableRuntimeLogs:ignored,observedRuntimeLogs:observeLogs(runtime)});\n      verifyFiles(runtime,plan.files,ignored);");
next=replaceOnce(next,"    if(plan.kind==='resident'){verifyFiles(runtime,plan.files,ignored);write(folder,'ResidentPayload.after.json',{root:runtime,files:plan.files,allowedGeneratedLogObservations:ignored.filter(p=>fs.existsSync(path.join(runtime,p))).map(p=>pin(path.join(runtime,p)))});}\n    verify();\n  }}catch(error)","    verify();\n    }catch(error){caseError=error;throw error;}finally{\n      if(plan.kind==='resident'&&runtime){try{write(folder,'ResidentPayload.after.json',{root:runtime,files:plan.files,mutableRuntimeLogs:ignored,observedRuntimeLogs:observeLogs(runtime)});verifyFiles(runtime,plan.files,ignored);}catch(error){if(caseError){write(folder,'ResidentPayload.postcheck-error.json',{message:error.message,primaryFailureRetained:caseError.message});}else throw error;}}\n    }\n  }}catch(error)");
assert(!next.includes("const ignored=['resources/"));
fs.mkdirSync(path.join(root,'payload'));
write(path.join(root,'payload/Guest-Shorts.cjs'),next);
const updatedPin=pin(path.join(root,'payload/Guest-Shorts.cjs'));
const oldFileRows=JSON.parse(JSON.stringify(manifest.files));
const row=manifest.files.find(f=>f.path==='Guest-Shorts.cjs');assert(row);row.bytes=updatedPin.bytes;row.sha256=updatedPin.sha256;
for(const c of manifest.cases)if(c.kind==='resident')c.mutableRuntimeLogs=['webview-smoke.log'];
manifest.revision=2;manifest.preparationHost=os.hostname();
const launcherPin=pin(path.join(root,'Guest-Invoke.ps1'));
manifest.launcher={script:{name:'Guest-Invoke.ps1',bytes:launcherPin.bytes,sha256:launcherPin.sha256},location:'outside payload, e.g. C:/issue-control/Guest-Invoke.ps1',desktopMutex:'Local\\SouiDevKit.DesktopValidation',nodeRelative:'tools/node.exe',crtPrefixRelative:'tray-x64',clearedEnvironment:['NODE_OPTIONS','NODE_PATH','NODE_REPL_EXTERNAL_MODULE','SOUI_DIAGNOSTIC_SCRIPT','SOUI_BRIDGE_TRACE','SOUI_OWNED_PROCESS_RUNNER'],evidence:'Fresh controller output plus separate sibling -launcher catalog; no prior output rewrite'};
assert.deepEqual(manifest.files.filter(f=>f.path!=='Guest-Shorts.cjs'),oldFileRows.filter(f=>f.path!=='Guest-Shorts.cjs'));
for(let i=0;i<manifest.cases.length;i++){const copy={...manifest.cases[i]};delete copy.mutableRuntimeLogs;assert.deepEqual(copy,originalCases[i]);}
writeJson(path.join(root,'payload/Payload.manifest.json'),manifest);
const sources=['C:/sr06/sdk/src/host/soui_miniblink_host.cpp','C:/sr06/demos/workbench/app/main.cpp','C:/sr06/docs/adr/0087-navigation-default-trace-removal.md'];
const observedHost=[];
for(const c of manifest.cases.filter(c=>c.kind==='resident')){
  const observed=[];for(const name of ['webview-smoke.log','source-navigation.log','resources/source-navigation.log','resources/webview-smoke.log']){const target=path.join(c.runtime,name);observed.push(fs.existsSync(target)?{relative:name,present:true,...pin(target)}:{relative:name,present:false});}
  observedHost.push({case:c.name,runtime:c.runtime,immutablePayloadFiles:c.files.length,logObservations:observed});
}
for(const before of inputPins)assert.deepEqual(pin(before.path),before);
writeJson(path.join(root,'Preparation.actual.json'),{schema:'soui.desktop-short-r2-preparation/v1',createdAtUtc:new Date().toISOString(),preparer:pin(__filename),node:pin(process.execPath),argv:process.argv,cwd:process.cwd(),inputs:inputPins,sourceReferences:sources.map(pin),observedHost,outputs:[pin(path.join(root,'payload/Guest-Shorts.cjs')),pin(path.join(root,'payload/Payload.manifest.json')),launcherPin],immutableManifestRowsUnchanged:36,immutableResidentRowsUnchanged:manifest.cases.filter(c=>c.kind==='resident').map(c=>({case:c.name,files:c.files.length})),onlyMutableRuntimeLogs:['webview-smoke.log'],legacySourceNavigationExcluded:false,previousPayloadEdited:false,productExecuted:false,vmControlled:false,independentReviewPending:true,releaseReady:false,productionQualificationComplete:false,fullDevtoolsGoalComplete:false});
console.log(JSON.stringify({controller:updatedPin,manifest:pin(path.join(root,'payload/Payload.manifest.json')),launcher:launcherPin,independentReviewPending:true}));
