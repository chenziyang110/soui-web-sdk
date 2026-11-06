'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const source=fs.readFileSync(path.join(__dirname,'../project/resources/fairness.js'),'utf8');
async function execute(options={}){
 let clock=0,id=0,nativePending=0,peak=0,echoes=0,firstReply=false;const timers=new Map(),requests=[],observed={};
 const context={performance:{now:()=>clock},document:{title:''},unescape,encodeURIComponent,console,
  setTimeout:(fn,delay)=>{const n=++id;timers.set(n,{fn,due:clock+Math.max(0,delay)});return n;},clearTimeout:n=>timers.delete(n)};
 context.window=context;context.__souiNativePostMessage=wire=>{
  const e=JSON.parse(wire);requests.push({method:e.method,time:clock});assert(Buffer.byteLength(wire)<=65536);
  if(e.method!=='bridge.bootstrap'){assert.equal(e.sessionId,'session');assert.equal(e.token,'token');assert.equal(e.origin,'app://test');}
  ++nativePending;peak=Math.max(peak,nativePending);assert(peak<=8);
  return new Promise(resolve=>{
   if(e.method==='fairness.echo'&&options.timeoutEcho)return;
   context.setTimeout(()=>{--nativePending;let result={};
    if(e.method==='bridge.bootstrap')result={sessionId:'session',origin:'app://test',token:'token'};
    else if(e.method==='fairness.inputReady')result={ready:!options.neverReady&&clock>=300};
    else if(e.method==='fairness.mark')result={stage:e.params.stage,nativeQpc:String(100000+Math.round(clock*10000))};
    else if(e.method==='fairness.echo'){++echoes;firstReply=true;result={sequence:e.params.sequence,payload:options.wrongPayload?'wrong':e.params.payload};}
    else if(e.method==='fairness.loadActive'){assert(firstReply);observed.armedAt=clock;result={armed:!options.badArm,nativeQpc:'123456789'};}
    else if(e.method==='fairness.inputFinished'){observed.fenceCalls=(observed.fenceCalls||0)+1;result={finished:!options.unfinished,nativeQpc:'ignored',cohortEndQpc:'123456790',observedQpc:'123456791'};}
    else if(e.method==='fairness.report'){observed.report=e.params;result={recorded:true};}
    else if(e.method==='fairness.done'){observed.done=true;result={closing:true};}
    resolve(JSON.stringify({requestId:e.requestId,ok:true,result}));
   },e.method==='fairness.echo'?20:e.method==='fairness.inputFinished'&&options.lateFinish?2000:1);
  });
 };
 vm.runInNewContext(source,context,{timeout:1000});
 for(let step=0;step<50000;step++){
  // A real event-loop turn drains the VM/native cross-realm promise chain.
  // Advancing virtual timers after an arbitrary8 microtasks can otherwise
  // stop before the actual page schedules its next timer/report.
  await new Promise(setImmediate);
  if(observed.done)return{...observed,requests,echoes,peak,clock};
  if(!timers.size)break;const [key,t]=[...timers.entries()].sort((a,b)=>a[1].due-b[1].due||a[0]-b[0])[0];timers.delete(key);clock=t.due;t.fn();
 }
 throw Error('Bounded fake clock did not reach report/done');
}
test('actual page cold-waits, offers full30s7-lane load, arms once after a real reply and closes normally',async()=>{
 const r=await execute();assert.equal(r.report.valid,true);assert.equal(r.report.inputArmed,true);assert.equal(r.report.failed,0);assert.equal(r.report.inflight,0);assert.equal(r.report.attempted,r.report.success);assert(r.echoes>1000);assert(r.peak<=8);
 assert.equal(r.report.times.loadDeadlineMs-r.report.times.loadStartedMs,30000);assert(r.armedAt>r.report.times.loadStartedMs);assert.equal(r.requests.filter(x=>x.method==='fairness.loadActive').length,1);assert.equal(r.requests.filter(x=>x.method==='bridge.bootstrap').length,1);
 assert.equal(r.fenceCalls,1);assert.equal(r.report.inputFinishFenced,true);assert(r.report.finishFence.responseMs<r.report.times.loadDeadlineMs);assert.equal(r.report.peakInflight,7);
 assert(r.requests.filter(x=>x.method==='fairness.echo').every(x=>x.time>=r.report.times.loadStartedMs&&x.time<r.report.times.loadDeadlineMs));assert(r.report.maxLoadNoReplyMs>0);
});
test('wrong echo payload is preserved as failure and never arms native input',async()=>{const r=await execute({wrongPayload:true});assert.equal(r.report.valid,false);assert.equal(r.report.error,'E_ECHO_MISMATCH');assert.equal(r.requests.filter(x=>x.method==='fairness.loadActive').length,0);});
test('bad load-active answer does not invent input authorization or a passing report',async()=>{const r=await execute({badArm:true});assert.equal(r.report.valid,false);assert.equal(r.report.inputArmed,false);assert.equal(r.report.error,'E_LOAD_ACTIVE');});
test('native input not ready stops at the original cold bound without starting flood',async()=>{const r=await execute({neverReady:true});assert.equal(r.report.valid,false);assert.equal(r.report.error,'E_COLD_INPUT_READY_DEADLINE');assert.equal(r.echoes,0);assert(r.clock<26000);});
test('echo timeout retains unknown native terminal and nonpassing partial report',async()=>{const r=await execute({timeoutEcho:true});assert.equal(r.report.valid,false);assert.equal(r.report.nativeTerminalUnknown,true);assert(r.report.timeouts>0);assert.equal(r.requests.filter(x=>x.method==='fairness.loadActive').length,0);});
test('false single Finish fence locks invalid without stopping or retrying the original30s load',async()=>{const r=await execute({unfinished:true});assert.equal(r.report.valid,false);assert.equal(r.report.error,'E_INPUT_FINISH_FENCE_FALSE');assert.equal(r.fenceCalls,1);assert.equal(r.report.inputFinishFenced,false);assert(r.report.times.stopSendingMs>=r.report.times.loadDeadlineMs);assert(r.requests.some(x=>x.method==='fairness.echo'&&x.time>r.report.finishFence.responseMs));});
test('late single Finish fence cannot certify overlap or extend the offered30s window',async()=>{const r=await execute({lateFinish:true});assert.equal(r.report.valid,false);assert.equal(r.report.error,'E_INPUT_FINISH_FENCE_LATE');assert.equal(r.fenceCalls,1);assert.equal(r.report.inputFinishFenced,false);assert(r.report.finishFence.responseMs>=r.report.times.loadDeadlineMs);assert(r.requests.filter(x=>x.method==='fairness.echo').every(x=>x.time<r.report.times.loadDeadlineMs));});
