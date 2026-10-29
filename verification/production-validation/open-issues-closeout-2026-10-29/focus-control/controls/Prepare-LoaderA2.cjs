'use strict';
// Host-side file preparation only. No child processes, VM control or native probe.
const fs = require('node:fs'), path = require('node:path'), crypto = require('node:crypto'), assert = require('node:assert/strict');
const root = __dirname;
const originalPayload = 'F:/work/SOUI_WEB/out/open-issues-closeout-20261029/tray-build/handoff-6d09f9b2-1585-4187-95ba-2cd3d40cc832/payload';
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
function pin(file) { const bytes = fs.readFileSync(file); return {path:file, bytes:bytes.length, sha256:hash(bytes)}; }
function put(name, value) { fs.writeFileSync(path.join(root,name), typeof value === 'string' ? value : JSON.stringify(value,null,2)+'\n', {flag:'wx'}); }
function replaceOne(source, before, after) { assert.equal(source.split(before).length,2,'Expected exactly one source replacement'); return source.replace(before,after); }
const oldOwned = fs.readFileSync(path.join(root,'Focus-Owned.cjs'),'utf8');
const oldExport = fs.readFileSync(path.join(root,'FocusExport.ps1'),'utf8');
assert.equal(hash(Buffer.from(oldOwned)),'29fac428862351d58d28cb3841f34fd4fe5082aed94499e7a684eee5fb5bc1d1');
assert.equal(hash(Buffer.from(oldExport)),'5cc5469a02c12b9db4372d14f6cbab71db2acd70e9f7c1ebcfd58604ed04db89');
const manifestFile = path.join(originalPayload,'Payload.manifest.json');
const manifestPin = pin(manifestFile);
assert.equal(manifestPin.sha256,'c6bb34afb81252ca16dd2bec955e3a8b36bfe830041e52ba417e9c34445eb897');
const manifest = JSON.parse(fs.readFileSync(manifestFile,'utf8'));
const rows = manifest.files.filter(row=>row.path.startsWith('tray-x64/')).sort((a,b)=>a.path.localeCompare(b.path,'en'));
assert.equal(rows.length,16);
assert.deepEqual(fs.readdirSync(path.join(originalPayload,'tray-x64')).sort(),rows.map(row=>row.path.slice(9)).sort());
for(const row of rows) { const actual=pin(path.join(originalPayload,row.path)); assert.equal(actual.bytes,row.bytes); assert.equal(actual.sha256,row.sha256); }
const crtSpec={schema:'soui.native-focus-child-crt-inputs/v1',guestDirectory:'C:/issues8/tray-x64',
  manifest:{path:'C:/issues8/Payload.manifest.json',bytes:manifestPin.bytes,sha256:manifestPin.sha256},
  files:rows.map(row=>({path:'C:/issues8/'+row.path,bytes:row.bytes,sha256:row.sha256})),
  scope:'Existing transported complete x64 tray directory supplies child-process DLL search only; the consumer EXE is pinned for membership, never executed.',
  productExecuted:false,executed:false,releaseReady:false};
const crtText=JSON.stringify(crtSpec,null,2)+'\n';
let owned=oldOwned;
owned=replaceOne(owned,"outer='C:/issue-closeout-evidence/focusc-a1-run',inner='C:/issue-closeout-evidence/focusc-a1'","outer='C:/issue-closeout-evidence/focusc-a2-run',inner='C:/issue-closeout-evidence/focusc-a2'");
const insert=`  const crtFile=path.join(__dirname,'Loader-Crt.inputs.json'),crtFilePin=pin(crtFile);
  assert.equal(crtFilePin.sha256,'${hash(Buffer.from(crtText))}');
  const crt=JSON.parse(fs.readFileSync(crtFile,'utf8'));
  assert.equal(crt.schema,'soui.native-focus-child-crt-inputs/v1');assert.equal(crt.guestDirectory,'C:/issues8/tray-x64');
  assert.equal(crt.files.length,16);check(crt.manifest);
  function checkCrtDirectory(){
    const directory=fs.lstatSync(crt.guestDirectory);assert(directory.isDirectory()&&!directory.isSymbolicLink());
    const expected=crt.files.map(p=>{assert(samePath(path.dirname(p.path),crt.guestDirectory));check(p);return path.basename(p.path);}).sort();
    assert.deepEqual(fs.readdirSync(crt.guestDirectory).sort(),expected,'Original transported x64 CRT prefix closed set differs');
  }
  checkCrtDirectory();
  const parentEnvironment={...process.env},pathKeys=Object.keys(parentEnvironment).filter(k=>k.toLowerCase()==='path');
  assert(pathKeys.length<=1,'Ambiguous inherited Windows PATH keys');
  const inheritedPath=pathKeys.length?parentEnvironment[pathKeys[0]]:undefined,childEnvironment={...parentEnvironment};
  for(const key of pathKeys)delete childEnvironment[key];
  childEnvironment.PATH=crt.guestDirectory+(inheritedPath?';'+inheritedPath:'');
  const environmentPin=value=>({present:value!==undefined,utf8Bytes:value===undefined?0:Buffer.byteLength(value,'utf8'),sha256:value===undefined?null:sha(Buffer.from(value,'utf8'))});
  const childPathEvidence={scope:'Only launchOwned supervisor and its PowerShell descendant inherit this PATH; parent process.env is never assigned',crtDirectory:crt.guestDirectory,
    inheritedPath:environmentPin(inheritedPath),childPath:environmentPin(childEnvironment.PATH),environmentValuesPersisted:false,productExecuted:false};
  let parentEnvironmentUnchanged=null;
`;
owned=replaceOne(owned,"  const prepFile=path.join(__dirname,'Preparation.actual.json')",insert+"  const prepFile=path.join(__dirname,'Preparation.actual.json')");
owned=replaceOne(owned,"pin(path.join(__dirname,'FocusExport.ps1'))","pin(path.join(__dirname,'FocusExport-a2.ps1')),crtFilePin,crt.manifest,...crt.files");
owned=replaceOne(owned,"    sourcePins,innerDirectory:inner","    sourcePins,childPathEvidence,innerDirectory:inner");
owned=replaceOne(owned,"report:path.join(outer,'owned.ndjson'),timeoutMs:90000","report:path.join(outer,'owned.ndjson'),env:childEnvironment,timeoutMs:90000");
owned=replaceOne(owned,"      for(const expected of sourcePins)check(expected);","      const afterEnvironment={...process.env};assert(Object.keys(afterEnvironment).length===Object.keys(parentEnvironment).length&&Object.keys(parentEnvironment).every(k=>afterEnvironment[k]===parentEnvironment[k]),'Parent environment changed');parentEnvironmentUnchanged=true;\n      for(const expected of sourcePins)check(expected);checkCrtDirectory();");
owned=replaceOne(owned,"      copies,missingKnownRecords:missing,failure","      childPathEvidence,parentEnvironmentUnchanged,copies,missingKnownRecords:missing,failure");
const exporter=replaceOne(oldExport,"$folder='C:\\issue-closeout-evidence\\focusc-a1-run'","$folder='C:\\issue-closeout-evidence\\focusc-a2-run'");
const names=['Loader-Crt.inputs.json','Focus-Owned-a2.cjs','FocusExport-a2.ps1','LoaderA2.selection.prepared.json','LoaderA2.prepared.json'];
for(const name of names)assert(!fs.existsSync(path.join(root,name)),'Preserve previous prepared identity: '+name);
put('Loader-Crt.inputs.json',crtText);put('Focus-Owned-a2.cjs',owned);put('FocusExport-a2.ps1',exporter);
const selectionNames=['NativeFocusControl.cs','Invoke-NativeFocusControl.ps1','README.md','Preparation.actual.json','Focus-Owned-a2.cjs','FocusExport-a2.ps1','Loader-Crt.inputs.json'];
const selection=selectionNames.map(name=>({name,...pin(path.join(root,name))}));
put('LoaderA2.selection.prepared.json',selection);
put('LoaderA2.prepared.json',{schema:'soui.native-focus-loader-a2-preparation/v1',preparedUtc:new Date().toISOString(),
  previousOwned:pin(path.join(root,'Focus-Owned.cjs')),previousExport:pin(path.join(root,'FocusExport.ps1')),sourceInputs:selection,
  existingTransportedPrefix:{hostManifest:manifestPin,guestManifest:crtSpec.manifest,guestDirectory:crtSpec.guestDirectory,closedFiles:16},
  frozenBudgets:{outerTimeoutMs:90000,drainMs:2000,innerDeadlineMs:60000,changed:false},
  freshOuter:'C:/issue-closeout-evidence/focusc-a2-run',freshInner:'C:/issue-closeout-evidence/focusc-a2',
  changes:['Fresh a2 evidence leaves and a2 export name only','Exact existing x64 prefix pinned and closed-set checked before and after','launchOwned env supplies only its supervisor/PowerShell descendant PATH; parent environment is checked unchanged','All original native/control code, closure guards and diagnostic result semantics unchanged'],
  startCommand:'C:\\issues8\\tools\\node.exe D:\\Focus-Owned-a2.cjs',exportAfterControllerEnded:'powershell -NoProfile -File D:\\FocusExport-a2.ps1',
  a1:{observationSource:'Root UI observation only; raw files remain in task VM',supervisorExitCodeReported:3221225781,nativeRootExitReported:null,readyRootIdentityReported:null,terminalReported:null,closureConfirmedReported:false,
    originalPreserved:true,newExportPrepared:false,reason:'No supervisor birth or Job receipt exists. PID absence and a loader code do not independently establish Job closure; existing exporter remains fail-closed. Keep a1 originals and screenshot evidence; do not claim a native control or natural closure.'},
  compiled:false,executed:false,vmTouched:false,productExecuted:false,sdkFixed:false,testPassed:null,releaseReady:false});
assert.equal(fs.readFileSync(path.join(root,'Focus-Owned.cjs'),'utf8'),oldOwned);assert.equal(fs.readFileSync(path.join(root,'FocusExport.ps1'),'utf8'),oldExport);
console.log(JSON.stringify({preparedOnly:true,selection:pin(path.join(root,'LoaderA2.selection.prepared.json')),record:pin(path.join(root,'LoaderA2.prepared.json')),files:selection}));
