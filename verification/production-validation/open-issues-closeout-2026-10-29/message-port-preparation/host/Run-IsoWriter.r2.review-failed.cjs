'use strict';
const fs=require('node:fs'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const here=__dirname.replaceAll('\\','/'),carrier=here+'/carrier-1249be88-0f1f-4f2f-840f-bb159c691d63';
const hash=b=>crypto.createHash('sha256').update(b).digest('hex'),pin=p=>{const b=fs.readFileSync(p);return{path:p,bytes:b.length,sha256:hash(b)}};
const write=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n',{flag:'wx'});
async function main(){
 assert.equal(process.argv.length,3);assert.equal(process.argv[2],'ROOT_REVIEWED_LONG_PATH_ISO_V1');
 const sourcePlan=pin(carrier+'/IsoSource.plan.json');assert.equal(sourcePlan.sha256,'49c4073010ee942ba9a60bbb250230e3e3094a87fa423310ed9bc0d6f0aaff99');
 const original='F:/work/SOUI_WEB/out/release-v0.6.0-20261028/vm-short-prep/media-tooldeps-4';
 const originalHelper=pin(original+'/BoundedIsoIO.cs');assert.equal(originalHelper.sha256,'fbedad4f3f1dd8357df8615f15917572e191681c676eba56d2b23507bf24dc72');
 const helper=pin(here+'/BoundedIsoIO.longpath.cs');assert.equal(helper.sha256,'00789c652e6e2c3639ee2896c05e56023974fc6ef8d334b8c4d5098d3003417a');
 const before=fs.readFileSync(originalHelper.path,'utf8'),after=fs.readFileSync(helper.path,'utf8');
 assert.equal(after.replace('handle=Native.CreateFileW(@"\\\\?\\"+PathName,isDirectory?','handle=Native.CreateFileW(PathName,isDirectory?'),before);
 const writer=pin(here+'/Write-IssuesIso.r2.ps1');assert.equal(writer.sha256,'cad4e9bb0b99fe3aca471117815d278818d41e682ae924ab127f7c83711301be');
 const pwsh=pin('C:/Users/11034/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe');
 const runner=pin('F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe');assert.equal(runner.sha256,'1a4cb4bea83a70ac06efb9c50e26a5c3345aab589f9c2e1306b244f68b09713f');
 const attemptRoot=carrier+'/iso-writer-r2-'+crypto.randomUUID();fs.mkdirSync(attemptRoot);
 const input={schema:'soui.issue77-data-iso-writer-inputs/v1',sourcePlan,attemptRoot,originalHelper,helper,writer,
  trustedOriginalWriter:pin(original+'/Write-Iso.ps1'),comServer:pin('C:/Windows/System32/imapi2fs.dll'),pwsh,runner,
  originalAttemptFailure:pin(carrier+'/IsoWriter.failure.json'),originalAttemptOutcome:pin(carrier+'/IsoWriter.finished.json'),
  minimalChange:'Native CreateFileW and COM AddTree extended local path spelling; original path/identity/reparse/link/share/hash fences retained.',
  wholeJobTimeoutMs:120000,drainMs:2000,dataOnly:true,productExecuted:false,vmTouched:false};
 write(attemptRoot+'/IsoWriter.inputs.json',input);const inputPin=pin(attemptRoot+'/IsoWriter.inputs.json');
 const args=['-NoProfile','-NonInteractive','-STA','-ExecutionPolicy','Bypass','-File',writer.path,'-Inputs',inputPin.path,'-InputsSha256',inputPin.sha256];
 write(attemptRoot+'/IsoWriter.invocation.json',{executable:pwsh,args,cwd:carrier,runner,wrapper:pin(__filename),timeoutMs:120000,drainMs:2000});
 const out=fs.openSync(attemptRoot+'/stdout.raw','wx'),err=fs.openSync(attemptRoot+'/stderr.raw','wx');let finished,ready;
 try{
  const child=require('F:/work/soui-issue96/tools/internal/owned-process.cjs').launchOwned(pwsh.path,args,{runner:runner.path,cwd:carrier,report:attemptRoot+'/owned.ndjson',timeoutMs:120000,drainMs:2000,stdout:out,stderr:err});
  ready=await child.ready;write(attemptRoot+'/ready.json',ready);finished=await child.finished;
 }finally{fs.closeSync(out);fs.closeSync(err);}
 write(attemptRoot+'/finished.json',finished);
 assert(finished.passed&&finished.exitCode===0&&finished.supervisorExitCode===0&&!finished.terminal.terminatedOwnedJob&&finished.terminal.activeBeforeCleanup===0&&finished.terminal.activeProcesses===0,'Data writer r2 did not close naturally; preserve exact attempt');
 const stream=require('F:/work/soui-issue96/tools/internal/standard-build.cjs').verifyStream(fs.readFileSync(attemptRoot+'/owned.ndjson'),JSON.parse(fs.readFileSync(attemptRoot+'/owned.ndjson.context.json','utf8')),ready,finished);
 const result=JSON.parse(fs.readFileSync(attemptRoot+'/IsoWriter.result.json','utf8')),iso=pin(carrier+'/SouiOpenIssues.iso');
 assert.equal(iso.sha256,result.iso.sha256);assert.equal(iso.bytes,result.iso.bytes);
 const actual={iso,finished,result:pin(attemptRoot+'/IsoWriter.result.json'),inputs:inputPin,sourcePlan,
  sourceAndCopiesHashVerified:true,rawOwnedVerified:true,naturalTerminal:stream.terminal,
  fullGuestContentHashVerificationStillRequired:true,productExecuted:false,vmTouched:false,releaseReady:false};
 write(attemptRoot+'/Final.actual.json',actual);console.log(JSON.stringify({attemptRoot,...actual}));
}
main().catch(error=>{console.error(error.stack);process.exitCode=1});
