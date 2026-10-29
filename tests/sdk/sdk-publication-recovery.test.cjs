'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto');
const cp=require('node:child_process'),sdk=require('../../tools/sdk/soui-sdk.cjs');
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
function fixture(t){
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'soui-publication-recovery-'));
 t.after(()=>fs.rmSync(root,{recursive:true,force:true}));
 const source=path.join(root,'source'),output=path.join(root,'output');
 for(const name of [...sdk.managedRoots.map(n=>n+'/fixture.txt'),...sdk.managedFiles]){
  const file=path.join(source,name);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,'fixture\n');
 }
 return {root,source,output};
}
function retained(f){
 const stage=f.output+'.staging-'+crypto.randomUUID();sdk.pack(f.source,stage,'0.6.0');
 return {stage,pin:sha(fs.readFileSync(path.join(stage,'sdk-manifest.json')))};
}
test('explicit retention preserves the verified stage and original denial, then independently recovers',t=>{
  const f=fixture(t),rename=fs.renameSync,denied=Object.assign(new Error('denied'),{code:'EACCES'});
  let error,calls=0;
  fs.renameSync=(from,to)=>{if(to===f.output){++calls;throw denied;}return rename(from,to);};
  try{sdk.pack(f.source,f.output,'0.6.0',{retainFailedPublication:true});}catch(value){error=value;}
  finally{fs.renameSync=rename;}
  assert.equal(error,denied);assert.equal(calls,process.platform==='win32'?6:1);
  assert.equal(error.publication.totalRetryDelayMs,process.platform==='win32'?450:0);
  const receipt=error.publication.recovery;assert.equal(receipt.stageRetained,true);
  assert.equal(fs.existsSync(f.output),false);sdk.readPackage(receipt.stage);
  const result=sdk.recoverPublication(receipt.stage,f.output,receipt.manifestSha256);
  assert.equal(result.status,'publication-recovered');assert.equal(fs.existsSync(receipt.stage),false);
  assert.equal(sdk.readPackage(f.output).version,'0.6.0');
});
test('recovery rejects changed manifests, changed bytes, extra files and mismatched sibling targets',t=>{
 for(const kind of ['manifest','payload','extra','target']){
  const f=fixture(t),r=retained(f);
  if(kind==='manifest')fs.appendFileSync(path.join(r.stage,'sdk-manifest.json'),' ');
  if(kind==='payload')fs.appendFileSync(path.join(r.stage,'payload/sdk/fixture.txt'),'changed');
  if(kind==='extra')fs.writeFileSync(path.join(r.stage,'undeclared.txt'),'extra');
  assert.throws(()=>sdk.recoverPublication(r.stage,kind==='target'?f.output+'-other':f.output,r.pin));
  assert.equal(fs.existsSync(r.stage),true);assert.equal(fs.existsSync(f.output),false);
 }
});
test('recovery does not overwrite a completed output and leaves its retained input intact',t=>{
 const f=fixture(t),r=retained(f);fs.mkdirSync(f.output);fs.writeFileSync(path.join(f.output,'user.txt'),'existing');
 assert.throws(()=>sdk.recoverPublication(r.stage,f.output,r.pin),/already exists/);
 assert.equal(fs.readFileSync(path.join(f.output,'user.txt'),'utf8'),'existing');assert.ok(fs.existsSync(r.stage));
});
test('recovery denial keeps the stage and each operation retains its own original retry budget',t=>{
 const f=fixture(t),r=retained(f),rename=fs.renameSync,denied=Object.assign(new Error('still denied'),{code:'EPERM'});
 let calls=0;fs.renameSync=(from,to)=>{if(to===f.output){++calls;throw denied;}return rename(from,to);};
 try{assert.throws(()=>sdk.recoverPublication(r.stage,f.output,r.pin),error=>{
  assert.equal(error,denied);assert.equal(error.publication.recovery.stageRetained,true);
  assert.equal(error.publication.totalRetryDelayMs,process.platform==='win32'?450:0);return true;
 });}finally{fs.renameSync=rename;}
 assert.equal(calls,process.platform==='win32'?6:1);assert.ok(fs.existsSync(r.stage));assert.equal(fs.existsSync(f.output),false);
});
test('CLI retention is explicit and supplies the exact pin accepted by recovery',t=>{
 const f=fixture(t),preload=path.join(f.root,'denied.cjs'),tool=path.resolve(__dirname,'../../tools/sdk/soui-sdk.cjs');
 fs.writeFileSync(preload,"const fs=require('node:fs');const rename=fs.renameSync;const output="+JSON.stringify(f.output)+";fs.renameSync=(a,b)=>{if(b===output)throw Object.assign(new Error('controlled denial'),{code:'EPERM'});return rename(a,b);};\n");
 const blocked=cp.spawnSync(process.execPath,['--require',preload,tool,'pack','--source',f.source,'--output',f.output,
  '--version','0.6.0','--retain-failed-publication'],{encoding:'utf8',windowsHide:true,timeout:10000});
 assert.equal(blocked.status,1);assert.equal(blocked.stdout,'');
 const line=blocked.stderr.split(/\r?\n/).find(row=>row.startsWith('SDK diagnostics: '));
 const recovery=JSON.parse(line.slice('SDK diagnostics: '.length)).publication.recovery;
 assert.equal(recovery.stageRetained,true);assert.ok(fs.existsSync(recovery.stage));
 const result=cp.spawnSync(process.execPath,[tool,'recover-publication','--stage',recovery.stage,'--output',f.output,
  '--manifest-sha256',recovery.manifestSha256],{encoding:'utf8',windowsHide:true,timeout:10000});
 assert.equal(result.status,0,result.stderr);assert.equal(JSON.parse(result.stdout).status,'publication-recovered');
});
test('real PS5 descendant handle denial remains failure, then closes and recovers with the same bytes',
 {skip:process.platform!=='win32'},t=>{
 const f=fixture(t),r=retained(f),driver=path.join(f.root,'hold-and-recover.ps1');
 fs.writeFileSync(driver,String.raw`param([string]$Stage,[string]$Output,[string]$Pin,[string]$Tool,[string]$Node,[string]$Log)
$ErrorActionPreference='Stop'
$handle=[IO.File]::Open((Join-Path $Stage 'payload/sdk/fixture.txt'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
try{
 $ErrorActionPreference='Continue'
 & $Node $Tool recover-publication --stage $Stage --output $Output --manifest-sha256 $Pin 1> ($Log+'.blocked.out') 2> ($Log+'.blocked.err')
 $blocked=$LASTEXITCODE
 $ErrorActionPreference='Stop'
 if($blocked-eq0-or(Test-Path -LiteralPath $Output)-or-not(Test-Path -LiteralPath $Stage)){throw 'Blocked publication falsely succeeded or lost its stage'}
}finally{$handle.Dispose()}
$ErrorActionPreference='Continue'
& $Node $Tool recover-publication --stage $Stage --output $Output --manifest-sha256 $Pin 1> ($Log+'.recovered.out') 2> ($Log+'.recovered.err')
$recovered=$LASTEXITCODE
$ErrorActionPreference='Stop'
if($recovered-ne0){throw 'Recovery after the actual handle closed failed'}
@{blockedExit=$blocked;recoveredExit=$recovered}|ConvertTo-Json -Compress
`);
 const env={...process.env};for(const key of Object.keys(env))if(key.toLowerCase()==='psmodulepath')delete env[key];
 const result=cp.spawnSync('powershell.exe',['-NoProfile','-NonInteractive','-File',driver,'-Stage',r.stage,'-Output',f.output,
  '-Pin',r.pin,'-Tool',path.resolve(__dirname,'../../tools/sdk/soui-sdk.cjs'),'-Node',process.execPath,'-Log',path.join(f.root,'native')],
 {encoding:'utf8',windowsHide:true,timeout:20000,env});
 assert.equal(result.status,0,`${result.error||''}\n${result.stdout}\n${result.stderr}`);
 assert.deepEqual(JSON.parse(result.stdout),{blockedExit:1,recoveredExit:0});
 const blocked=fs.readFileSync(path.join(f.root,'native.blocked.err'),'utf16le');
 assert.match(blocked,/EPERM/);assert.match(blocked,/stageRetained/);
 assert.equal(sha(fs.readFileSync(path.join(f.output,'sdk-manifest.json'))),r.pin);sdk.readPackage(f.output);
 console.log('Native publication observation: '+JSON.stringify({blockedExit:1,recoveredExit:0,
  blockedNativeError:blocked.match(/SDK: (EPERM[^\r\n]*)/)[1],manifestSha256:r.pin,
  recoveredManifestSha256:sha(fs.readFileSync(path.join(f.output,'sdk-manifest.json')))}));
});
