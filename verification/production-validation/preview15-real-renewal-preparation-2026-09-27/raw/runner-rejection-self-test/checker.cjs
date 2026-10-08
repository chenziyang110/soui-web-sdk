'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const [stage,fixture,run,arch,nodePath]=process.argv.slice(2);
const SOURCE='b0d7cc7d52de3493084e0dec6813f92f9a90a1bbce2f75ad5a62532a6dd87d07';
const VERSION='0.3.0-preview.15';
const PARENT_FREEZE='b58de0c44a2ae96a12aa144f4d8607b800415c40b1311591e05b91f67486faef';
const assert=(ok,message)=>{if(!ok)throw Error(message);};
const encoded=value=>JSON.stringify(value,null,2)+'\n';
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const write=(file,value)=>fs.writeFileSync(file,encoded(value),{flag:'wx'});
function regular(file){const s=fs.lstatSync(file);assert(s.isFile()&&!s.isSymbolicLink(),'Expected regular file: '+file);return fs.readFileSync(file);}
function real(directory){assert(path.isAbsolute(directory),'Expected absolute path');let p=directory;for(;;){assert(!fs.lstatSync(p).isSymbolicLink(),'Linked path: '+p);const parent=path.dirname(p);if(parent===p)break;p=parent;}return fs.realpathSync(directory);}
function relative(root,name){assert(typeof name==='string'&&name&&!name.includes('\\')&&!name.includes(':')&&!name.startsWith('/')&&name.split('/').every(p=>p&&p!=='.'&&p!=='..'),'Unsafe manifest path');return path.join(root,name);}
function walk(root,part=''){const full=part?relative(root,part):root;const s=fs.lstatSync(full);assert(!s.isSymbolicLink(),'Linked manifest input');if(s.isFile())return [part];assert(s.isDirectory(),'Unsupported input');return fs.readdirSync(full).sort().flatMap(p=>walk(root,part?part+'/'+p:p));}
function checked(root,manifest,exact=true){
  real(root);assert(manifest&&Array.isArray(manifest.files)&&manifest.files.length&&hash(encoded(manifest.files))===manifest.sha256,'Manifest digest invalid');
  const expected=manifest.files.map(f=>f.path), seen=new Set();
  assert(JSON.stringify([...expected].sort())===JSON.stringify(expected),'Manifest not sorted');
  const actual=(exact?walk(root):[...expected]).sort();
  assert(JSON.stringify(actual)===JSON.stringify(expected),'Manifest file set changed: '+root);
  let total=0;
  for(const f of manifest.files){assert(!seen.has(f.path.toLowerCase()),'Case-fold duplicate');seen.add(f.path.toLowerCase());let data=regular(relative(root,f.path));
    if(f.normalization!==undefined){assert(f.normalization==='none'||f.normalization==='lf','Invalid normalization');if(f.normalization==='lf')data=Buffer.from(data.toString('latin1').replace(/\r\n/g,'\n'),'latin1');}
    assert(Number.isSafeInteger(f.bytes)&&f.bytes>=0&&/^[a-f0-9]{64}$/.test(f.sha256)&&data.length===f.bytes&&hash(data)===f.sha256,'Input differs: '+f.path);total+=f.bytes;}
  assert(total===manifest.bytes,'Manifest total differs');return manifest.sha256;
}
function payload(runtime){const names=walk(runtime).filter(n=>n!=='source-navigation.log').sort();return names.map(p=>{const b=regular(relative(runtime,p));return {path:p,bytes:b.length,sha256:hash(b)};});}
function outside(root,file){const p=path.relative(root,file);return p!==''&&(p==='..'||p.startsWith('..'+path.sep)||path.isAbsolute(p));}
function archive(runtime,freezeSha){
  const file=path.join(runtime,'source-navigation.log');if(!fs.existsSync(file))return {items:[],provenance:null};
  const bytes=regular(file), digest=hash(bytes);let provenance=null;
  if(stage==='before'){
    const runs=path.dirname(run);
    for(const item of fs.readdirSync(runs,{withFileTypes:true})){
      if(!item.isDirectory()||item.name===path.basename(run))continue;
      const prior=path.join(runs,item.name,'supervisor.json');if(!fs.existsSync(prior))continue;
      try{const record=read(prior);
        if(record.schema!=='soui.real-renewal-supervisor/v1'||record.architecture!==arch||record.freezeSha256!==freezeSha||real(record.runtimePath).toLowerCase()!==runtime.toLowerCase())continue;
        const match=(record.runtimeDiagnostics||[]).find(d=>d.path==='source-navigation.log'&&d.bytes===bytes.length&&d.sha256===digest);
        if(!match||!path.isAbsolute(match.archivePath)||outside(path.dirname(prior),match.archivePath)||!regular(match.archivePath).equals(bytes))continue;
        provenance={supervisorPath:prior,supervisorSha256:hash(regular(prior)),archivePath:match.archivePath};break;
      }catch{}
    }
    assert(provenance,'Existing source-navigation.log has no matching prior run archive; preserve it and investigate');
  }
  const destination=path.join(run,'diagnostics-'+stage);fs.mkdirSync(destination);
  const archivePath=path.join(destination,'source-navigation.log');
  assert(outside(runtime,archivePath),'Diagnostic must be outside runtime');fs.writeFileSync(archivePath,bytes,{flag:'wx'});
  assert(regular(file).equals(bytes)&&regular(archivePath).equals(bytes),'Diagnostic changed during archive');
  return {items:[{path:'source-navigation.log',bytes:bytes.length,sha256:digest,archivePath}],provenance};
}
assert((stage==='before'||stage==='after')&&['x86','x64'].includes(arch),'Invalid checker mode');
real(fixture);real(run);assert(outside(fixture,run),'Run output must be outside preparation root');
const freezePath=path.join(fixture,'freeze.json'),freezeBytes=regular(freezePath),freeze=read(freezePath),freezeSha256=hash(freezeBytes);
assert(freeze.schema==='soui.real-renewal-preparation/v1'&&freeze.status==='compiled-inputs-frozen'&&freeze.version===VERSION&&freeze.sourceInputSha256===SOURCE,'Wrong compiled freeze');
assert(freeze.fixtureRevision===2&&freeze.parent?.freezeSha256===PARENT_FREEZE,'Wrong v2 fixture revision or parent freeze');
assert(freeze.guiExecuted===false&&freeze.realRenewalVerified===false&&freeze.releaseReady===false,'Preparation claims invalid');
const bindings={source:checked(freeze.source.root,freeze.source),inputs:checked(freeze.inputs.root,freeze.inputs),toolchain:{},consumer:{},builds:{}};
assert(freeze.source.sha256===SOURCE&&freeze.source.files.length===717,'Wrong source manifest');
for(const architecture of ['x86','x64']){
  bindings.toolchain[architecture]=checked(freeze.toolchain[architecture].root,freeze.toolchain[architecture]);
  bindings.consumer[architecture]=checked(freeze.consumer[architecture].root,freeze.consumer[architecture]);
  const r=freeze.runtimes[architecture], bbytes=regular(r.buildRecordPath), b=read(r.buildRecordPath);
  assert(hash(bbytes)===r.buildRecordSha256&&b.schema==='soui.real-renewal-build/v1'&&b.architecture===architecture&&b.sourceSha256===SOURCE&&b.buildId===r.buildId&&b.sdkBuildId===r.sdkBuildId&&b.nativeBuild==='passed'&&b.webBuild==='passed'&&b.selfCheck==='passed'&&b.sourceUnchanged===true&&b.guiExecuted===false&&b.realRenewalVerified===false&&b.releaseReady===false,'Wrong build record');
  assert(b.fixtureRevision===2&&b.parent?.freezeSha256===PARENT_FREEZE,'Wrong v2 build revision or parent freeze');
  const base=path.dirname(r.buildRecordPath);
  checked(path.join(base,'drivers'),b.drivers);checked(path.join(base,'protocol'),b.protocol);checked(base,b.evidence,false);
  assert(b.payload.sha256===r.payloadSha256&&hash(encoded(r.payload))===r.payloadSha256&&JSON.stringify(b.payload.files)===JSON.stringify(r.payload),'Build/runtime payload mismatch');
  assert(!r.payload.some(f=>f.path.toLowerCase()==='source-navigation.log'),'Diagnostic cannot be an immutable payload file');
  bindings.builds[architecture]=r.buildRecordSha256;
}
const selected=freeze.runtimes[arch],runtimePath=real(selected.runtimePath);
const selectedBuild=read(selected.buildRecordPath),driverRoot=path.join(path.dirname(selected.buildRecordPath),'drivers');
const runnerEntry=selectedBuild.drivers.files.find(f=>f.path==='Run.ps1');
const analyzerEntry=selectedBuild.drivers.files.find(f=>f.path==='Verify-Run.cjs');
assert(runnerEntry&&analyzerEntry,'Runner/analyzer are not included in frozen build drivers');
const archivedRunner=regular(path.join(run,'Run.ps1'));
assert(archivedRunner.length===runnerEntry.bytes&&hash(archivedRunner)===runnerEntry.sha256&&regular(path.join(fixture,'Run.ps1')).equals(archivedRunner),'Invoked runner differs from selected build');
assert(outside(runtimePath,run),'Output lies inside runtime');
const expectedNode=freeze.toolchain[arch].tools.node;
assert(hash(regular(nodePath))===expectedNode.sha256,'Node executable differs from frozen build tool');
const files=payload(runtimePath);
assert(hash(encoded(files))===selected.payloadSha256&&JSON.stringify(files)===JSON.stringify(selected.payload),'Runtime payload changed');
const executable=regular(path.join(runtimePath,'InstalledRenewalProbe.exe')),pe=executable.readUInt32LE(0x3c);
assert(executable.readUInt16LE(0)===0x5a4d&&executable.readUInt32LE(pe)===0x4550&&executable.readUInt16LE(pe+4)===(arch==='x86'?0x14c:0x8664),'Wrong native PE architecture');
if(stage==='after'){
  const before=read(path.join(run,'identity-before.json'));
  assert(before.freezeSha256===freezeSha256&&before.runtimePath===runtimePath&&JSON.stringify(before.bindings)===JSON.stringify(bindings)&&JSON.stringify(before.payload)===JSON.stringify(files),'Frozen identity changed during execution');
}
const diagnostic=archive(runtimePath,freezeSha256);
const frozenAnalyzer=path.join(driverRoot,'Verify-Run.cjs'),analyzer=path.join(run,'Verify-Run.cjs');
const analyzerBytes=regular(frozenAnalyzer);
assert(analyzerBytes.length===analyzerEntry.bytes&&hash(analyzerBytes)===analyzerEntry.sha256,'Frozen analyzer differs');
if(stage==='before')fs.writeFileSync(analyzer,analyzerBytes,{flag:'wx'});
assert(regular(analyzer).equals(analyzerBytes),'Run analyzer copy changed');
const record={schema:'soui.real-renewal-run-input-check/v1',stage,checkedAt:new Date().toISOString(),architecture:arch,
  freezePath,freezeSha256,runtimePath,payload:files,payloadSha256:selected.payloadSha256,bindings,
  runtimeDiagnostics:diagnostic.items,diagnosticProvenance:diagnostic.provenance,
  analyzerPath:analyzer,analyzerSha256:hash(analyzerBytes),frozenAnalyzerPath:frozenAnalyzer,
  runnerSha256:runnerEntry.sha256,inputsUnchanged:true};
if(stage==='after'){const report=path.join(run,'native.ndjson');record.reportSha256=fs.existsSync(report)?hash(regular(report)):null;}
assert(hash(regular(freezePath))===freezeSha256,'Freeze changed during checks');
write(path.join(run,'identity-'+stage+'.json'),record);
console.log(encoded({status:'identity-verified',stage,architecture:arch,payloadFiles:files.length,diagnosticFiles:diagnostic.items.length,inputsUnchanged:true}));