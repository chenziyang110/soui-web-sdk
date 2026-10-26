'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const crypto=require('node:crypto');
const {launch,stop}=require('../../../benchmarks/protocol/run-one.cjs');
const executable=process.env.BENCHMARK_BURST_INPUT_EXE;
const runner=process.env.BENCHMARK_OWNED_RUNNER_EXE;
const available=process.platform==='win32' && executable && runner && fs.existsSync(executable) && fs.existsSync(runner);
const hostExecutable=process.env.BENCHMARK_INPUT_UI_HOST_EXE;
// SOURCE ONLY in this delivery. The verifier explicitly supplies compiled tools.
test('owned burst preflight refuses missing target and wrong birth before output/input',
  {skip:!available,timeout:15000},async()=>{
    for(const [label,pid,birth] of [['no-target','0','1'],['wrong-birth',String(process.pid),'1']]) {
      const folder=path.resolve(__dirname,'../../../out','burst-preflight-'+label+'-'+crypto.randomUUID());
      fs.mkdirSync(folder,{recursive:true});
      const input=path.join(folder,'burst-input.ndjson'),desktop=path.join(folder,'burst-desktop.ndjson');
      const run=launch('input',executable,[pid,birth,'00000000000000010000000000000002','125','3',input,desktop,'pointer_move'],
        path.dirname(executable),folder,process.env,{runner,timeoutMs:5000});
      try {
        const ready=await run.ready,result=await run.finished;
        assert.ok(Number.isSafeInteger(ready.pid)&&ready.pid>0);
        assert.match(ready.creationFileTime,/^[1-9][0-9]*$/);
        assert.equal(result.code,1);assert.equal(fs.existsSync(input),false);assert.equal(fs.existsSync(desktop),false);
        const report=fs.readFileSync(path.join(folder,'input.owned.ndjson'),'utf8').trimEnd().split('\n').map(JSON.parse);
        const end=report.at(-1);
        assert.equal(report[0].pid,ready.pid);assert.equal(report[0].creationFileTime,ready.creationFileTime);
        assert.equal(end.rootExitCode,1);assert.equal(end.activeProcesses,0);
        assert.equal(end.closureConfirmed,true);assert.equal(end.terminatedOwnedJob,false);
      } finally { stop([run]);await run.finished; }
    }
  });
test('real public SDK harness rejects missing configuration under owned supervision',
  {skip:!(process.platform==='win32'&&hostExecutable&&runner&&fs.existsSync(hostExecutable)&&fs.existsSync(runner)),timeout:10000},async()=>{
    const folder=path.resolve(__dirname,'../../../out','input-ui-host-preflight-'+crypto.randomUUID());
    fs.mkdirSync(folder,{recursive:true});
    const run=launch('host',hostExecutable,[],path.dirname(hostExecutable),folder,process.env,{runner,timeoutMs:5000});
    try{await run.ready;const result=await run.finished;assert.equal(result.code,1);
      assert.equal(fs.existsSync(path.join(folder,'host-ready.json')),false);
      assert.equal(result.ownedProcess.terminal.activeProcesses,0);
    }finally{stop([run]);await run.finished;}
  });
