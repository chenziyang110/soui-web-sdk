import test from 'node:test';
import assert from 'node:assert/strict';
import {initializeReference} from '../common/initialization.mjs';
function deferred() {
  let resolve;
  const promise=new Promise(accept=>{resolve=accept;});
  return {promise,resolve};
}
function setup(block) {
  const trace=[],entered=deferred(),pending=deferred(),client={identity:'current-document'};
  let closed=false;
  const step=(name,value)=>{trace.push(name);if(name===block){entered.resolve();return pending.promise;}return Promise.resolve(value);};
  const steps={alive:()=>!closed,connect:()=>step('connect',client),adoptClient:value=>{assert.equal(value,client);trace.push('adopt');},
    discover:value=>{assert.equal(value,client);return step('discover',['authorized']);},validateTools:()=>trace.push('validate'),
    describe:value=>{assert.equal(value,client);return step('describe',{fixture:'actual'});},createTask:()=>trace.push('task'),
    probe:value=>{assert.equal(value,client);return step('probe',{});},ready:()=>trace.push('ready')};
  return {trace,steps,entered:entered.promise,close:()=>{closed=true;},release:()=>pending.resolve(block==='connect'?client:{}),client};
}
for(const [boundary,expected] of [
  ['connect',['connect']],
  ['discover',['connect','adopt','discover']],
  ['describe',['connect','adopt','discover','validate','describe']],
  ['probe',['connect','adopt','discover','validate','describe','task','probe']],
]) {
  test(`document close during ${boundary} prevents every later request and readiness publication`,async()=>{
    const f=setup(boundary),initializing=initializeReference(f.steps);await f.entered;f.close();f.release();
    assert.equal(await initializing,false);assert.deepEqual(f.trace,expected);
  });
}
test('already closed document does not start a native connection',async()=>{
  const f=setup();f.close();assert.equal(await initializeReference(f.steps),false);assert.deepEqual(f.trace,[]);
});
test('actual initializer publishes readiness only after all native boundaries',async()=>{
  const f=setup();assert.equal(await initializeReference(f.steps),true);
  assert.deepEqual(f.trace,['connect','adopt','discover','validate','describe','task','probe','ready']);
});
test('synchronous task observer closure prevents the next native probe',async()=>{
  const f=setup();f.steps.createTask=()=>{f.trace.push('task');f.close();};
  assert.equal(await initializeReference(f.steps),false);assert.equal(f.trace.includes('probe'),false);assert.equal(f.trace.includes('ready'),false);
});
test('native discovery rejection propagates without task or ready publication',async()=>{
  const f=setup();f.steps.discover=async()=>{f.trace.push('discover');throw new Error('E_NATIVE_DISCOVERY');};
  await assert.rejects(initializeReference(f.steps),{message:'E_NATIVE_DISCOVERY'});
  assert.deepEqual(f.trace,['connect','adopt','discover']);
});
