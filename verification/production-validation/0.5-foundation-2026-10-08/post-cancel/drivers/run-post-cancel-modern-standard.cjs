'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {launchOwned}=require('../tools/internal/owned-process.cjs');
const id=require('../tools/internal/delivery-identity.cjs');
const evidence=require('../tools/internal/build-evidence.cjs');
const root=path.resolve(__dirname,'..');
const expected='e7519638abe291a21f9b03764677dd1ead8f6e27ede7a0df206de7d676477ff9';
const strictAttempt='out/0.5.0-preview.1-post-cancel-0f4f367d-08e9-445e-be9d-f18a473036b4';
const strict=JSON.parse(fs.readFileSync(path.join(root,strictAttempt,'results.json')));
if(!strict.completed)throw new Error('Prior desktop run is still in progress');
const sha=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const original=path.join(root,'out/benchmark-protocol/Release/OwnedProcessRunner.exe');
const runnerDir=path.join(root,'out/validation-bootstrap',sha(original));fs.mkdirSync(runnerDir,{recursive:true});
const runner=path.join(runnerDir,'OwnedProcessRunner.exe');
if(!fs.existsSync(runner))fs.copyFileSync(original,runner,fs.constants.COPYFILE_EXCL);
if(sha(runner)!==sha(original))throw new Error('Bootstrap identity');
const attempt=path.join(root,'out','0.5.0-preview.1-modern-standard-'+crypto.randomUUID());fs.mkdirSync(attempt);
const results=[];
const ps=path.join(process.env.SystemRoot,'System32/WindowsPowerShell/v1.0/powershell.exe');
const env={...process.env,MSBUILDDISABLENODEREUSE:'1',PSModulePath:path.join(process.env.SystemRoot,'System32/WindowsPowerShell/v1.0/Modules')};
(async()=>{
 console.log('Modern standard attempt: '+attempt);
 for(const architecture of ['x86','x64'])for(const configuration of ['Release','Debug']){
  const name=`modern-${architecture}-${configuration}`;
  if(id.source(root).sha256!==expected)throw new Error('Frozen source drift');
  const build=evidence.verifyBuild(root,architecture,configuration);
  const folder=path.join(attempt,name);fs.mkdirSync(folder);
  const log=fs.openSync(path.join(folder,'run.log'),'wx');
  const child=launchOwned(ps,['-NoProfile','-ExecutionPolicy','Bypass','-File','tools/Test-Modern.ps1',
   '-Architecture',architecture,'-Configuration',configuration],{runner,cwd:root,
   report:path.join(folder,'owned.ndjson'),timeoutMs:180000,drainMs:30000,env,stdout:log,stderr:log});
  const result=await child.finished;fs.closeSync(log);
  const sourceUnchanged=id.source(root).sha256===expected;
  const verified=evidence.verifyModernSmoke(root,architecture,configuration);
  const filename=`modern-smoke-${configuration}.json`;
  fs.copyFileSync(path.join(root,'out',architecture,filename),path.join(folder,filename),fs.constants.COPYFILE_EXCL);
  const record={name,architecture,configuration,buildId:build.buildId,sourceUnchanged,
   smokePassed:verified.smoke.passed,...result,passed:result.passed&&sourceUnchanged&&verified.smoke.passed};
  results.push(record);
  fs.writeFileSync(path.join(attempt,'results.json'),JSON.stringify({schema:'soui.modern-standard-check/v1',
   version:build.version,releaseReady:false,sourceSha256:expected,
   originalStandardDrainMs:30000,strictSupplementaryAttempt:strictAttempt,
   strictSupplementaryDrainMs:2000,strictSupplementaryFailuresPreserved:true,
   completed:results.length===4,allPassed:results.length===4&&results.every(r=>r.passed),
   limitations:['Prior standard local desktop validation used a 30s Job drain; this run preserves that existing window.',
    'The optional 2s Modern probe failed and is retained separately; no generic result is changed.',
    'Short local smoke only; not a production lifetime, package, performance or system matrix acceptance.'],results},null,2)+'\n');
  console.log(JSON.stringify({name,passed:record.passed,exitCode:record.exitCode,
   closureConfirmed:record.closureConfirmed,reason:record.terminal?.reason,elapsedMs:record.terminal?.elapsedMs}));
  if(!record.passed){process.exitCode=1;console.log(fs.readFileSync(path.join(folder,'run.log'),'utf8').slice(-3500));}
 }
})().catch(error=>{console.error(error.stack);process.exitCode=1;});
