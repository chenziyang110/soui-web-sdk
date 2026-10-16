'use strict';
const fs=require('node:fs'),path=require('node:path'),zlib=require('node:zlib'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',out=__dirname+'/reference-gui-proof-3';
const evidence=require(root+'/tools/internal/dependency-evidence.cjs');
const identity=require(root+'/tools/internal/delivery-identity.cjs');
const standard=require(root+'/tools/internal/standard-build.cjs');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8'));
assert(!fs.existsSync(out));fs.mkdirSync(out);
const files=[],secretValues=Object.entries(process.env).filter(([k,v])=>/TOKEN|SECRET|PASSWORD|API_KEY|PRIVATE_KEY/i.test(k)&&v&&v.length>=8);
function copy(source,relative,expected,compress=false){
 const original=evidence.pin(source);if(expected){assert.equal(original.bytes,expected.bytes);assert.equal(original.sha256,expected.sha256);}
 const duplicate=files.find(row=>row.path===relative); if(duplicate){assert.equal(duplicate.original.sha256,original.sha256);assert.equal(duplicate.original.bytes,original.bytes);return;} const raw=fs.readFileSync(source);
 for(const [key,value] of secretValues)for(const encoding of ['utf8','utf16le'])assert(!raw.includes(Buffer.from(value,encoding)),'Credential value in '+relative+' ('+key+')');
 const target=path.join(out,relative);fs.mkdirSync(path.dirname(target),{recursive:true});
 fs.writeFileSync(target,compress?zlib.gzipSync(raw):raw,{flag:'wx'});evidence.checkPin(original);
 const stored=evidence.pin(target);files.push({path:relative,bytes:stored.bytes,sha256:stored.sha256,encoding:compress?'gzip':'identity',original});
}
function captured(folder,prefix){
 const base=__dirname+'/'+folder+'/'+prefix;
 const raw=fs.readFileSync(base+'.ndjson'),contextBytes=fs.readFileSync(base+'.ndjson.context.json');
 const context=JSON.parse(contextBytes),ready=read(base+'-ready.json'),finished=read(base+'-finished.json');
 const proof=standard.verifyStream(raw,context,ready,finished);
 assert(finished.passed&&finished.exitCode===0&&finished.supervisorExitCode===0&&finished.closureConfirmed&&!finished.stopRequested&&!finished.fallbackUsed);
 assert(proof.terminal.rootExited&&!proof.terminal.terminatedOwnedJob&&proof.terminal.activeProcesses===0);
 return {folder,prefix,subject:ready,terminal:proof.terminal};
}
const native=read(__dirname+'/reference-native-x64-2/ARTIFACTS.json');
const gui=read(__dirname+'/reference-gui-x64-2/INPUTS.json');
const captures=[...['configure','build','native-tests'].map(p=>captured('reference-native-x64-2',p)),
 ...['configure','build'].map(p=>captured('reference-gui-build-x64-1',p)),captured('reference-gui-x64-2','desktop'),
 ...native.apps.map(a=>captured('reference-gui-x64-2',a.name))];
for(const folder of ['reference-native-x64-2','reference-gui-build-x64-1','reference-gui-x64-1','reference-gui-x64-2']){
 for(const file of fs.readdirSync(__dirname+'/'+folder,{withFileTypes:true}))if(file.isFile())copy(__dirname+'/'+folder+'/'+file.name,folder+'/'+file.name);
}
for(const row of native.source.files)copy(root+'/demos/reference-workbench/'+row.path,'reference-source/'+row.path,row);
for(const app of native.apps)assert.deepEqual(identity.snapshot(app.root,identity.walk(app.root)),app.payload);
for(const p of gui.pins){
 const portable=p.path.replaceAll('\\','/');
 if(portable===root+'/source-manifest.json')copy(root+'/out/standard-build/attempt-fc486290-c2b6-4a4a-86f0-6a6750e2acee/source-manifest.input.json','source14/source-manifest.json.gz',p,true);
 else if(portable.startsWith(root+'/out/sdk-x64/'))copy(portable.replace(root+'/out/sdk-x64/',root+'/out/release-build-history/sdk-x64-before-observer9/'),'sdk6/'+path.basename(p.path),p);
 else if(!/\.(exe|map)$/i.test(p.path)){
  const target='captured-inputs/'+p.sha256.slice(0,12)+'-'+path.basename(p.path);
  if(!files.some(row=>row.path===target))copy(p.path,target,p);
 }
}
copy(__dirname+'/source14-observer-inputs/Observe-LlvmBuild.ps1','source14/Observe-LlvmBuild.ps1');
copy(__dirname+'/run-reference-gui-1.cjs','negative-control-runner.cjs');copy(__dirname+'/reference-gui-proof-readme.md','README.md');copy(__filename,'archive-recipe.cjs');
const index={schema:'soui.reference-gui-evidence-archive/v1',architecture:'x64',configuration:'Release',
 sdkBuildId:'6edd2cb9-5017-4fbf-9902-bfb9523c8539',scope:'Three real GUI/public native Agent finite business flows and natural shutdown on the captured Source14 SDK6',
 captures,files,originalAppPayloads:native.apps,controllerBinary:gui.controller.executable,
 knownEnvironmentCredentialValuesAbsent:true,nativeBuildAndFourteenCtestsPassed:true,
 guiBusinessAndAgentStateCompared:true,physicalInputQualified:false,targetOsMatrixQualified:false,
 longRunQualified:false,fullCacheBudgetQualified:false,releaseReady:false};
fs.writeFileSync(out+'/index.json',JSON.stringify(index,null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({archive:out,files:files.length,bytes:files.reduce((n,r)=>n+r.bytes,0),captures:captures.length,releaseReady:false}));
