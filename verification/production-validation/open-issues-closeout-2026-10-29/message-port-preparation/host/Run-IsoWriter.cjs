'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const root=__dirname.replaceAll('\\','/'),carrier=root+'/carrier-1249be88-0f1f-4f2f-840f-bb159c691d63';
const hash=b=>crypto.createHash('sha256').update(b).digest('hex'),pin=p=>{const b=fs.readFileSync(p);return{path:p,bytes:b.length,sha256:hash(b)}};
const write=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n',{flag:'wx'});
async function main(){
 const plan=pin(carrier+'/IsoSource.plan.json');assert.equal(plan.sha256,'49c4073010ee942ba9a60bbb250230e3e3094a87fa423310ed9bc0d6f0aaff99');
 const original='F:/work/SOUI_WEB/out/release-v0.6.0-20261028/vm-short-prep/media-tooldeps-4';
 const helper=pin(original+'/BoundedIsoIO.cs');assert.equal(helper.sha256,'fbedad4f3f1dd8357df8615f15917572e191681c676eba56d2b23507bf24dc72');
 const writer=pin(root+'/Write-IssuesIso.ps1');
 const pwsh=pin('C:/Users/11034/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe');
 const runner=pin('F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe');assert.equal(runner.sha256,'1a4cb4bea83a70ac06efb9c50e26a5c3345aab589f9c2e1306b244f68b09713f');
 const input={schema:'soui.issue77-data-iso-writer-inputs/v1',sourcePlan:plan,helper,writer,
  trustedOriginalWriter:pin(original+'/Write-Iso.ps1'),comServer:pin('C:/Windows/System32/imapi2fs.dll'),pwsh,runner,
  wholeJobTimeoutMs:120000,drainMs:2000,dataOnly:true,productExecuted:false,vmTouched:false};
 write(carrier+'/IsoWriter.inputs.json',input);const inputPin=pin(carrier+'/IsoWriter.inputs.json');
 const args=['-NoProfile','-NonInteractive','-STA','-ExecutionPolicy','Bypass','-File',writer.path,'-Inputs',inputPin.path,'-InputsSha256',inputPin.sha256];
 write(carrier+'/IsoWriter.invocation.json',{executable:pwsh,args,cwd:carrier,runner,wrapper:pin(__filename),timeoutMs:120000,drainMs:2000});
 const out=fs.openSync(carrier+'/IsoWriter.stdout.raw','wx'),err=fs.openSync(carrier+'/IsoWriter.stderr.raw','wx');let finished;
 try{
  const child=require('F:/work/soui-issue96/tools/internal/owned-process.cjs').launchOwned(pwsh.path,args,{runner:runner.path,cwd:carrier,report:carrier+'/IsoWriter.owned.ndjson',timeoutMs:120000,drainMs:2000,stdout:out,stderr:err});
  const ready=await child.ready;write(carrier+'/IsoWriter.ready.json',ready);finished=await child.finished;
 }finally{fs.closeSync(out);fs.closeSync(err);}
 write(carrier+'/IsoWriter.finished.json',finished);
 assert(finished.passed&&finished.exitCode===0&&finished.supervisorExitCode===0&&!finished.terminal.terminatedOwnedJob&&finished.terminal.activeBeforeCleanup===0&&finished.terminal.activeProcesses===0,'Data writer did not close naturally');
 const result=JSON.parse(fs.readFileSync(carrier+'/IsoWriter.result.json','utf8'));const iso=pin(carrier+'/SouiOpenIssues.iso');assert.equal(iso.sha256,result.iso.sha256);assert.equal(iso.bytes,result.iso.bytes);
 write(carrier+'/IsoWriter.final-actual.json',{iso,finished,result:pin(carrier+'/IsoWriter.result.json'),inputs:inputPin,
  sourceAndCopiesHashVerified:true,fullGuestContentHashVerificationStillRequired:true,productExecuted:false,vmTouched:false,releaseReady:false});
 console.log(JSON.stringify({iso,writerResult:pin(carrier+'/IsoWriter.result.json'),root:finished.pid,creationFileTime:finished.creationFileTime,terminal:finished.terminal,fullGuestContentHashVerificationStillRequired:true,productExecuted:false,vmTouched:false}));
}
main().catch(error=>{console.error(error.stack);process.exitCode=1});
