'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),vm=require('node:vm');
const {createRequire}=require('node:module');
const filename=path.resolve(__dirname,'../../../benchmarks/protocol/run-input-ui.cjs');
const source=fs.readFileSync(filename,'utf8'),localRequire=createRequire(filename);
const tick=()=>new Promise(resolve=>setImmediate(resolve));
function deferred(){let resolve,reject;const promise=new Promise((a,b)=>{resolve=a;reject=b;});return {promise,resolve,reject};}
// Authored orchestration substitutes only. No executable, input, window or
// native ownership proof is created or claimed by these regression contracts.
function fixture(t,options={}){
  const directory=fs.mkdtempSync(path.join(os.tmpdir(),'soui-run-input-drain-'));
  t.after(()=>fs.rmSync(directory,{recursive:true,force:true}));
  const write=(name,value)=>{const file=path.join(directory,name);fs.writeFileSync(file,typeof value==='string'?value:JSON.stringify(value));return file;};
  const page=path.join(directory,'page');fs.mkdirSync(page);
  fs.writeFileSync(path.join(page,'burst-page.html'),'authored HTML');fs.writeFileSync(path.join(page,'burst-page.js'),'authored JS');
  const request={schema:'soui.input-ui-run-request/v2',source_commit:'a'.repeat(40),measurement_source_commit:'b'.repeat(40),
    product_id:'authored-runner-fixture',trace_mode:'e2e',kind:'pointer_move',hz:125,count:64,runtime_version:'authored',
    host_executable:write('host.exe','not executable'),input_executable:write('input.exe','not executable'),
    owned_runner:write('runner.exe','not executable'),miniblink_binary:write('node.dll','not executable'),
    sdk_library:write('sdk.lib','not a library'),page_root:page,frozen_environment:write('environment.json',{}),
    frozen_configuration:write('configuration.json',{}),calibration_template:write('calibration.json',{
      schema:'soui.burst-calibration-template/v1',qpc_frequency:'10000000',observation_error_ms:100,device_pixel_ratio:1}),
    frozen_inputs:{measurement_sources:[]},capture_backend:'gdi-bitblt'};
  const requestFile=write('request.json',request),output=path.join(directory,'run');
  const runs={},launched={host:deferred(),input:deferred()},timers=[],stopAttempts=[],actualStops=[];
  let checks=0,freezes=0;
  function launch(name,executable,args,cwd,root,env,ownership){
    const complete=deferred(),subject={pid:name==='host'?41:42,creationFileTime:'134360000000000001'};
    let closed=false,stopThrows=options[name+'StopThrows']||0;
    const value={name,pid:subject.pid,args,ownership,finished:complete.promise,
      ready:options[name+'ReadyRejected']?Promise.reject(new Error(name+' ready rejected')):Promise.resolve(subject),
      finish(code=0,error=null){if(closed)return;closed=true;complete.resolve({name,pid:subject.pid,code,error,signal:null,
        ownedProcess:{closureConfirmed:true,passed:code===0,terminal:{rootExitCode:code,activeProcesses:0,terminatedOwnedJob:false}}});},
      reject(error){complete.reject(error);},
      stop(){stopAttempts.push(name);if(stopThrows>0){--stopThrows;throw Error(name+' stop rejected');}
        if(closed)return false;actualStops.push(name);closed=true;
        complete.resolve({name,pid:subject.pid,code:124,error:'supervisor-stop',signal:null,
          ownedProcess:{closureConfirmed:true,passed:false,terminal:{rootExitCode:124,activeProcesses:0,terminatedOwnedJob:true}}});return true;}
    };
    runs[name]=value;
    if(name==='host'){
      const run=args.find(argument=>argument.startsWith('--run=')).slice(6);
      fs.writeFileSync(path.join(root,'host-ready.json'),JSON.stringify({schema:'soui.input-ui-host-ready/v1',run_id:run,...subject,
        qpc_frequency:'10000000',measurement_path:'public-sdk',diagnostics:{suppress_focus:false,borderless:false,foreground:false,logging:false}}));
      for(const file of ['host-result.json','burst-input.ndjson','burst-desktop.ndjson'])fs.writeFileSync(path.join(root,file),'authored fixture only');
    }
    launched[name].resolve(value);return value;
  }
  const module={exports:{}};
  const substitutions={
    './run-one.cjs':{launch,stop:items=>{for(const item of items)item.stop();}},
    './owned-trace-subject.cjs':{
      readRunJson:(root,name)=>JSON.parse(fs.readFileSync(path.join(root,name),'utf8')),
      describe:(root,name)=>({path:name,bytes:fs.statSync(path.join(root,name)).size,sha256:'0'.repeat(64)}),
      captureOwnedEvidence:(root,processes)=>{if(processes.some(item=>item.completion_rejected))throw Error('Native completion proof unavailable');return [];}
    },
    './same-product-input-pair.cjs':{checkRun:()=>{++checks;if(options.postCheckRejected)throw Error('post-check rejected');
      return {result:{stage_evidence:'unavailable',classified_input_count:0,desktop_samples:0}};}},
    './freeze-input-ui-request.cjs':{selectedCaptureBackend:()=>request.capture_backend,verify:()=>{++freezes;},sourceDigest:()=> 'c'.repeat(64)}
  };
  const requireSubstitute=name=>substitutions[name]||localRequire(name);
  const schedule=(callback,delay)=>{const token={callback,delay,cleared:false};timers.push(token);return token;};
  vm.compileFunction(source,['module','require','process','setTimeout','clearTimeout','Buffer'],{filename})(module,requireSubstitute,
    {platform:'win32',env:{}},schedule,token=>{token.cleared=true;},Buffer);
  const result=module.exports.runRequest(requestFile,output);
  // Tests deliberately leave the result pending while checking ordering.
  void result.catch(()=>{});
  return {runs,launched,timers,stopAttempts,actualStops,result,output,
    checks:()=>checks,freezes:()=>freezes,record:()=>JSON.parse(fs.readFileSync(path.join(output,'input-ui-run.json'),'utf8'))};
}
test('failed input retains failure while the Host drains naturally under the original deadlines',async t=>{
  const f=fixture(t);await f.launched.input.promise;
  f.runs.input.finish(1,'root-failed');await tick();
  assert.deepEqual(f.actualStops,[]);assert.equal(f.timers[0].cleared,false);
  assert.deepEqual([f.runs.host.ownership.timeoutMs,f.runs.input.ownership.timeoutMs,f.timers[0].delay],[190000,155000,195000]);
  f.runs.host.finish();await assert.rejects(f.result,/Native owned Host\/input run failed/);
  const record=f.record();assert.equal(record.status,'failed');assert.deepEqual(record.processes.map(item=>item.code),[0,1]);
  assert.equal(f.checks(),0);assert.equal(f.timers[0].cleared,true);assert.equal(f.stopAttempts.includes('host'),false);
  assert.equal(fs.existsSync(path.join(f.output,'input-ui-check.json')),false);
});
test('Host failure stops only the still-live injector and never reports success',async t=>{
  const f=fixture(t);await f.launched.input.promise;f.runs.host.finish(2,'root-failed');
  await assert.rejects(f.result,/Native owned Host\/input run failed/);
  assert.deepEqual(f.actualStops,['input']);assert.deepEqual(f.record().processes.map(item=>item.code),[2,124]);
  assert.equal(f.checks(),0);
});
test('the original overall watchdog still closes both held jobs and forced cleanup remains failed',async t=>{
  const f=fixture(t);await f.launched.input.promise;
  assert.equal(f.timers.length,1);assert.equal(f.timers[0].delay,195000);f.timers[0].callback();
  await assert.rejects(f.result,/Native owned Host\/input run failed/);
  assert.deepEqual(f.actualStops,['host','input']);assert.deepEqual(f.record().processes.map(item=>item.code),[124,124]);
  assert.equal(f.record().status,'failed');
});
test('Host ready rejection preserves its error and awaits the existing Host completion without a new timer',async t=>{
  const f=fixture(t,{hostReadyRejected:true});await f.launched.host.promise;await tick();
  assert.deepEqual(f.actualStops,[]);assert.equal(f.timers.length,1);assert.equal(f.timers[0].cleared,false);
  f.runs.host.finish(3,'activity-expired');await assert.rejects(f.result,/host ready rejected/);
  assert.equal(f.record().error,'host ready rejected');assert.equal(f.record().processes[0].code,3);
});
test('input ready rejection stops injection while allowing the Host to finish normally',async t=>{
  const f=fixture(t,{inputReadyRejected:true});await f.launched.input.promise;await tick();
  assert.deepEqual(f.actualStops,['input']);assert.equal(f.timers[0].cleared,false);
  f.runs.host.finish();await assert.rejects(f.result,/input ready rejected/);
  assert.deepEqual(f.record().processes.map(item=>item.code),[0,124]);assert.equal(f.record().status,'failed');
});
test('a rejected input completion channel is retained as missing proof, not natural completion',async t=>{
  const f=fixture(t);await f.launched.input.promise;f.runs.input.reject(Error('input completion rejected'));await tick();
  assert.deepEqual(f.actualStops,['input']);f.runs.host.finish();
  await assert.rejects(f.result,/Native owned Host\/input run failed/);
  const record=f.record();assert.equal(record.processes[1].completion_rejected,true);assert.equal(record.processes[1].code,null);
  assert.match(record.evidence_error,/proof unavailable/);assert.equal(f.stopAttempts.includes('host'),false);
});
test('a rejected Host completion channel stops its held supervisor and the injector without inventing closure proof',async t=>{
  const f=fixture(t);await f.launched.input.promise;f.runs.host.reject(Error('Host completion rejected'));
  await assert.rejects(f.result,/Native owned Host\/input run failed/);
  assert.deepEqual(f.actualStops,['host','input']);assert.equal(f.record().processes[0].completion_rejected,true);
  assert.match(f.record().evidence_error,/proof unavailable/);
});
test('successful native results retain the existing verification path without a cleanup stop',async t=>{
  const f=fixture(t);await f.launched.input.promise;f.runs.input.finish();f.runs.host.finish();
  assert.equal((await f.result).status,'completed');assert.equal(f.record().status,'completed');
  assert.deepEqual(f.actualStops,[]);assert.equal(f.checks(),1);assert.equal(f.freezes(),2);assert.equal(f.timers[0].cleared,true);
});
test('a post-run verifier error remains failure and never stops an already completed Host',async t=>{
  const f=fixture(t,{postCheckRejected:true});await f.launched.input.promise;f.runs.input.finish();f.runs.host.finish();
  await assert.rejects(f.result,/post-check rejected/);assert.deepEqual(f.actualStops,[]);
  assert.equal(fs.existsSync(path.join(f.output,'input-ui-check.json')),false);
  assert.equal(JSON.parse(fs.readFileSync(path.join(f.output,'post-check-failure.json'),'utf8')).performanceGate,false);
});
test('a stop exception after Host failure is retained while original completions remain awaited',async t=>{
  const f=fixture(t,{inputStopThrows:1});await f.launched.input.promise;f.runs.host.finish(2,'root-failed');await tick();
  assert.equal(f.timers[0].cleared,false);f.runs.input.finish(1,'root-failed');
  await assert.rejects(f.result,/Native owned Host\/input run failed/);
  assert.deepEqual(f.record().stop_errors,[{name:'input',error:'input stop rejected'}]);
  assert.equal(f.record().status,'failed');
});
test('completion rejection plus a stop exception cannot create an unhandled rejection or invented native proof',async t=>{
  const f=fixture(t,{inputStopThrows:1});await f.launched.input.promise;f.runs.input.reject(Error('input completion rejected'));await tick();
  f.runs.host.finish();await assert.rejects(f.result,/Native owned Host\/input run failed/);
  assert.equal(f.record().processes[1].completion_rejected,true);assert.equal(f.record().processes[1].code,null);
  assert.deepEqual(f.record().stop_errors,[{name:'input',error:'input stop rejected'}]);
  assert.match(f.record().evidence_error,/proof unavailable/);
});
