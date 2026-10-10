import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {ReferenceTask,taskControls} from '../common/task-controller.mjs';
const fixture=JSON.parse(fs.readFileSync(new URL('../common/fixture.json',import.meta.url),'utf8'));
function setup() {
  const calls=[],intervals=new Map(),timeouts=new Map();let next=1,unsubscribed=0;
  let query={taskId:'sdk-task-1',state:'running',progress:0,message:'',sequence:1};
  const client={
    async call(method,input) {
      calls.push({method,input});
      if(method==='task.start')return {accepted:true,taskId:'sdk-task-1',duplicate:false};
      if(method==='task.query'){if(query instanceof Error)throw query;return query;}
      if(method==='task.cancel')return {taskId:'sdk-task-1',state:'cancel-pending',cancelRequested:true};
      throw new Error('Unexpected method');
    },
    subscribe(){return {ready:Promise.resolve(),async unsubscribe(){unsubscribed++;}};},
  };
  const timers={setInterval(fn){const id=next++;intervals.set(id,fn);return id;},clearInterval(id){intervals.delete(id);},
    setTimeout(fn){const id=next++;timeouts.set(id,fn);return id;},clearTimeout(id){timeouts.delete(id);}};
  let operation=0;
  const task=new ReferenceTask(client,fixture,{timers,operationId:()=> ++operation===1?'test-operation':`test-operation-${operation}`});
  return {task,client,calls,intervals,timeouts,setQuery:value=>query=value,unsubscribed:()=>unsubscribed};
}
test('start calls actual client interface once; initial zero-progress running is valid',async()=>{
  const f=setup();await f.task.start();assert.equal(f.task.snapshot().state,'running');assert.equal(f.task.snapshot().progress,0);
  assert.deepEqual(f.calls[0],{method:'task.start',input:{kind:'reference.summary',operationId:'test-operation',
    input:{fixtureId:fixture.fixtureId,category:'all',durationMs:5000}}});
  assert.equal(f.calls.filter(call=>call.method==='task.start').length,1);f.task.dispose();
});
test('native success stops local observation without manufacturing a task result',async()=>{
  const f=setup();await f.task.start();f.setQuery({taskId:'sdk-task-1',state:'succeeded',progress:100,message:'real receipt',result:{processed:10000}});
  await f.task.query();assert.equal(f.task.snapshot().result.processed,10000);assert.equal(f.intervals.size,0);assert.equal(f.timeouts.size,0);
  await Promise.resolve();assert.equal(f.unsubscribed(),1);f.task.dispose();
});
test('cancel-request is not a cancelled terminal',async()=>{
  const f=setup();await f.task.start();f.setQuery({taskId:'sdk-task-1',state:'cancel-pending',progress:5,message:''});
  await f.task.cancel();assert.equal(f.task.snapshot().state,'cancel-pending');assert.equal(f.intervals.size,1);
  f.setQuery({taskId:'sdk-task-1',state:'cancelled',progress:5,message:'actual terminal',result:{}});
  await f.task.query();assert.equal(f.task.snapshot().state,'cancelled');assert.equal(f.intervals.size,0);f.task.dispose();
});
test('public timeout state remains observable and does not mean cancellation',async()=>{
  const f=setup();await f.task.start();f.setQuery({taskId:'sdk-task-1',state:'timeout',progress:2,message:'timeout observed'});
  await f.task.query();assert.equal(f.task.snapshot().state,'timeout');assert.equal(f.intervals.size,1);f.task.dispose();
});
test('query not-found means unknown and cannot cause automatic replay',async()=>{
  const f=setup();await f.task.start();f.setQuery(Object.assign(new Error('missing'),{code:'E_TASK_NOT_FOUND'}));
  await f.task.query();assert.equal(f.task.snapshot().state,'unknown');assert.equal(f.task.snapshot().observationError,'E_TASK_NOT_FOUND');
  await assert.rejects(f.task.start());assert.equal(f.calls.filter(call=>call.method==='task.start').length,1);f.task.dispose();
});
test('local observation deadline retains unknown and never cancels or restarts native work',async()=>{
  const f=setup();await f.task.start();Array.from(f.timeouts.values())[0]();
  assert.equal(f.task.snapshot().state,'unknown');assert.equal(f.calls.filter(call=>call.method==='task.cancel').length,0);
  assert.equal(f.calls.filter(call=>call.method==='task.start').length,1);f.task.dispose();
});
test('contradictory task identity and malformed progress are rejected',async()=>{
  for(const next of [{taskId:'sdk-task-other',state:'running',progress:0},{taskId:'sdk-task-1',state:'running',progress:101},
    {taskId:'sdk-task-1',state:'invented',progress:0}]) {
    const f=setup();await f.task.start();f.setQuery(next);await f.task.query();assert.equal(f.task.snapshot().state,'unknown');f.task.dispose();
  }
});
test('missing start acknowledgement permits only query by same operation, never a second start',async()=>{
  const f=setup();const original=f.client.call;
  f.client.call=async(method,input)=>method==='task.start'?{accepted:false}:original(method,input);
  await f.task.start();assert.equal(f.task.snapshot().state,'unknown');
  await f.task.query();assert.deepEqual(f.calls.at(-1).input,{operationId:'test-operation'});
  assert.equal(f.task.snapshot().taskId,'sdk-task-1');f.task.dispose();
});
test('dispose removes observation and subscription without claiming native completion',async()=>{
  const f=setup();await f.task.start();f.task.dispose();f.task.dispose();await Promise.resolve();
  assert.equal(f.intervals.size,0);assert.equal(f.timeouts.size,0);assert.equal(f.unsubscribed(),1);
  assert.equal(f.calls.filter(call=>call.method==='task.cancel').length,0);
});
test('late accepted start after disposal requests only best-effort cancellation',async()=>{
  const f=setup();let accept;const original=f.client.call;
  f.client.call=(method,input)=>method==='task.start'?new Promise(resolve=>accept=resolve):original(method,input);
  const starting=f.task.start();f.task.dispose();accept({accepted:true,taskId:'sdk-task-1'});await starting;
  assert.equal(f.calls.filter(call=>call.method==='task.cancel').length,1);assert.equal(f.calls.filter(call=>call.method==='task.query').length,0);
});
test('bounded duration and categories reject before crossing the client',async()=>{
  for(const [category,duration] of [['wrong',5000],['all',0],['all',60001],['all',NaN]]) {
    const f=setup();await assert.rejects(f.task.start(category,duration));assert.equal(f.calls.length,0);f.task.dispose();
  }
});
function deferred() {
  let resolve,reject;
  const promise=new Promise((accept,refuse)=>{resolve=accept;reject=refuse;});
  return {promise,resolve,reject};
}
async function startSuccessor(f) {
  f.setQuery({taskId:'sdk-task-1',state:'succeeded',progress:100,message:'first complete'});
  await f.task.query();
  const original=f.client.call;
  f.client.call=async(method,input)=>{
    const receipt=await original(method,input);
    return method==='task.start'?{...receipt,taskId:'sdk-task-2'}:receipt;
  };
  f.setQuery({taskId:'sdk-task-2',state:'running',progress:12,message:'successor untouched'});
  await f.task.start();
}
test('late cancel acknowledgement cannot mutate or query a successor task',async()=>{
  const f=setup(),pending=deferred();await f.task.start();const original=f.client.call;
  f.client.call=(method,input)=>method==='task.cancel'?pending.promise:original(method,input);
  const cancelling=f.task.cancel();await startSuccessor(f);const before=f.task.snapshot(),calls=f.calls.length;
  pending.resolve({taskId:'sdk-task-1',cancelRequested:true});await cancelling;
  assert.deepEqual(f.task.snapshot(),before);assert.equal(f.calls.length,calls);f.task.dispose();
});
test('late cancel rejection cannot publish an error into a successor task',async()=>{
  const f=setup(),pending=deferred();await f.task.start();const original=f.client.call;
  f.client.call=(method,input)=>method==='task.cancel'?pending.promise:original(method,input);
  const cancelling=f.task.cancel();await startSuccessor(f);const before=f.task.snapshot();
  pending.reject(Object.assign(new Error('old cancel refused'),{code:'E_OLD_CANCEL'}));await cancelling;
  assert.deepEqual(f.task.snapshot(),before);f.task.dispose();
});
test('old subscription ready rejection is inert after a new task starts',async()=>{
  const f=setup(),pending=deferred();let subscriptions=0;
  f.client.subscribe=()=>({ready:++subscriptions===1?pending.promise:Promise.resolve(),async unsubscribe(){}});
  await f.task.start();await startSuccessor(f);const before=f.task.snapshot();
  pending.reject(Object.assign(new Error('old subscribe refused'),{code:'E_OLD_SUBSCRIBE'}));
  await pending.promise.catch(()=>undefined);assert.deepEqual(f.task.snapshot(),before);f.task.dispose();
});
test('old unsubscribe rejection is inert after a new task starts',async()=>{
  const f=setup(),pending=deferred();let subscriptions=0;
  f.client.subscribe=()=>{const first=++subscriptions===1;return {ready:Promise.resolve(),unsubscribe:()=>first?pending.promise:Promise.resolve()};};
  await f.task.start();await startSuccessor(f);const before=f.task.snapshot();
  pending.reject(Object.assign(new Error('old unsubscribe refused'),{code:'E_OLD_UNSUBSCRIBE'}));
  await pending.promise.catch(()=>undefined);assert.deepEqual(f.task.snapshot(),before);f.task.dispose();
});
test('current terminal unsubscribe failure remains visible without changing native result',async()=>{
  const f=setup(),pending=deferred();
  f.client.subscribe=()=>({ready:Promise.resolve(),unsubscribe:()=>pending.promise});
  await f.task.start();f.setQuery({taskId:'sdk-task-1',state:'succeeded',progress:100,message:'actual complete'});
  await f.task.query();pending.reject(Object.assign(new Error('unsubscribe refused'),{code:'E_CURRENT_UNSUBSCRIBE'}));
  await pending.promise.catch(()=>undefined);
  assert.equal(f.task.snapshot().state,'succeeded');assert.equal(f.task.snapshot().subscriptionError,'E_CURRENT_UNSUBSCRIBE');f.task.dispose();
});
test('shared GUI and view controls bind enabled and busy to current task identity and state',()=>{
  const idle={state:'idle',operationId:'',taskId:''};
  assert.deepEqual(taskControls(true,idle),{start:{enabled:true,busy:false},query:{enabled:false,busy:false},cancel:{enabled:false,busy:false}});
  const pending={state:'cancel-pending',operationId:'op-current',taskId:'task-current'};
  assert.deepEqual(taskControls(true,pending),{start:{enabled:false,busy:true},query:{enabled:true,busy:false},cancel:{enabled:true,busy:true}});
  assert.equal(taskControls(true,{...pending,state:'succeeded'}).cancel.enabled,false);
  assert.equal(taskControls(true,{...pending,state:'unknown'}).start.enabled,false);
  assert.equal(taskControls(false,pending).query.enabled,false);
});
test('starting observer disposal stops native dispatch and every observation resource',async()=>{
  const f=setup();let subscriptions=0;
  f.client.subscribe=()=>{subscriptions++;throw new Error('Unexpected subscription after disposal');};
  f.task.observe(view=>{if(view.state==='starting')f.task.dispose();});
  await f.task.start();
  assert.equal(f.calls.length,0);assert.equal(subscriptions,0);
  assert.equal(f.intervals.size,0);assert.equal(f.timeouts.size,0);assert.equal(f.task.starting,false);
});
