'use strict';
const fs=require('node:fs');
const path=require('node:path');
const crypto=require('node:crypto');
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const ownPin=file=>({path:file,bytes:fs.statSync(file).size,sha256:sha(fs.readFileSync(file))});
function check(pin){
  if(!path.isAbsolute(pin.path)||!fs.lstatSync(pin.path).isFile()||fs.lstatSync(pin.path).isSymbolicLink())throw new Error('Input is not a plain absolute file');
  const raw=fs.readFileSync(pin.path);
  if(raw.length!==pin.bytes||sha(raw)!==pin.sha256)throw new Error('Input changed: '+pin.path);
}
function members(root){
  const visit=dir=>fs.readdirSync(dir,{withFileTypes:true}).flatMap(entry=>{
    const file=path.join(dir,entry.name);
    if(entry.isSymbolicLink())throw new Error('Input reparse member');
    return entry.isDirectory()?visit(file):entry.isFile()?[path.relative(root,file).replaceAll('\\','/')]:[];
  });
  return visit(root).sort();
}
async function main(){
  if(process.argv.length!==5||process.argv[2]!=='ROOT_FINITE_OWNED_V1')throw new Error('Explicit frozen root gate required');
  const file=process.argv[3],expected=process.argv[4],raw=fs.readFileSync(file);
  if(sha(raw)!==expected)throw new Error('Plan changed');
  const cfg=JSON.parse(raw),stages=[];
  if(cfg.schema!=='soui.root-finite-owned-plan/v1'||!path.isAbsolute(cfg.output)||fs.existsSync(cfg.output))throw new Error('Fresh absolute output required');
  const verify=()=>{
    cfg.pins.forEach(check); check(ownPin(file));
    for(const inventory of cfg.inventories){
      if(JSON.stringify(members(inventory.root))!==JSON.stringify(inventory.members))throw new Error('Input inventory drift');
    }
    if(sha(fs.readFileSync(file))!==expected)throw new Error('Plan drift');
  };
  verify();
  const {launchOwned}=require(cfg.client.path);
  fs.mkdirSync(cfg.output);
  let failure=null;
  try{
    for(const job of cfg.jobs){
      if(!/^[a-z0-9-]+$/.test(job.name)||!Array.isArray(job.args)||job.args.some(x=>typeof x!=='string')||
          ![0,1].includes(job.expectedExitCode)||!Number.isSafeInteger(job.timeoutMs)||job.timeoutMs<=0)throw new Error('Invalid frozen job');
      verify();
      const report=path.join(cfg.output,job.name+'.owned.jsonl'),stdoutFile=path.join(cfg.output,job.name+'.stdout.raw'),stderrFile=path.join(cfg.output,job.name+'.stderr.raw');
      const stdout=fs.openSync(stdoutFile,'wx'),stderr=fs.openSync(stderrFile,'wx');
      let handle,result;
      try{
        handle=launchOwned(job.executable,job.args,{runner:cfg.runner.path,cwd:job.cwd,report,timeoutMs:job.timeoutMs,drainMs:2000,stdout,stderr});
        const identity=await handle.ready;
        fs.writeFileSync(path.join(cfg.output,job.name+'.ready.json'),JSON.stringify(identity)+'\n',{flag:'wx'});
        result=await handle.finished;
      }catch(error){if(handle){handle.stop();result=await handle.finished;}throw error;}
      finally{fs.closeSync(stdout);fs.closeSync(stderr);}
      const terminal=result.terminal;
      const accepted=result.exitCode===job.expectedExitCode&&result.supervisorExitCode===job.expectedExitCode&&
        !result.signal&&!result.spawnError&&!result.reportError&&!result.stopRequested&&!result.fallbackUsed&&
        result.closureConfirmed&&terminal?.rootExited===true&&terminal.activeProcesses===0&&
        terminal.terminatedOwnedJob===false&&(job.expectedExitCode===0?result.passed===true:result.passed===false);
      const stdoutPin=ownPin(stdoutFile),stderrPin=ownPin(stderrFile);
      const diagnostic=job.requiredDiagnostic===undefined||fs.readFileSync(stderrFile,'utf8').includes(job.requiredDiagnostic);
      const receipt={job,result,accepted:accepted&&diagnostic,raw:ownPin(report),context:ownPin(report+'.context.json'),stdout:stdoutPin,stderr:stderrPin};
      stages.push(receipt);
      fs.writeFileSync(path.join(cfg.output,job.name+'.finished.json'),JSON.stringify(receipt,null,2)+'\n',{flag:'wx'});
      verify();
      if(!receipt.accepted)throw new Error('Finite stage did not satisfy frozen predicates: '+job.name);
    }
  }catch(error){failure=error.message;}
  const summary={schema:'soui.root-finite-owned-result/v1',completed:failure===null,failure,plan:ownPin(file),stages,
    qualificationPassed:false,releaseReady:false};
  fs.writeFileSync(path.join(cfg.output,'summary.json'),JSON.stringify(summary,null,2)+'\n',{flag:'wx'});
  process.stdout.write(JSON.stringify({completed:summary.completed,failure,output:cfg.output,stages:stages.length})+'\n');
  process.exitCode=failure?1:0;
}
main().catch(error=>{process.stderr.write(error.stack+'\n');process.exitCode=1;});

