import test from 'node:test';
import assert from 'node:assert/strict';
import {AgentTaskObservation,submitReferenceForm} from '../common/gui-effects.mjs';
const fixture={fixtureId:'electron-smoothness-10k-v1',groups:['素材','项目','缓存','导出','归档']};
const input={fixtureId:fixture.fixtureId,category:'all',durationMs:500};
const start={accepted:true,taskId:'actual-child-1',duplicate:false};
const queried=(state='running')=>({taskId:start.taskId,state,progress:state==='succeeded'?100:7,sequence:'4'});
const context=result=>({active:true,calls:0,async effect(value){this.calls++;this.input=value;return result;}});
test('actual admission receipt does not manufacture a queued or terminal observation',async()=>{
  const view=new AgentTaskObservation(fixture),command=context(start);
  assert.deepEqual(await view.start(input,command),{admitted:true,taskId:start.taskId,duplicate:false});
  assert.equal(view.snapshot().state,'admitted');assert.equal(view.snapshot().progress,0);assert.equal(command.calls,1);
});
test('only a new command effect query supplies the actual child terminal',async()=>{
  const view=new AgentTaskObservation(fixture);await view.start(input,context(start));
  const command=context(queried('succeeded'));await view.query({taskId:start.taskId},command);
  assert.equal(view.snapshot().state,'succeeded');assert.equal(view.snapshot().sequence,'4');assert.equal(command.calls,1);
});
test('cancel receipt stays a request until a later actual query',async()=>{
  const view=new AgentTaskObservation(fixture);await view.start(input,context(start));
  const command=context({taskId:start.taskId,state:'cancel-pending',cancelRequested:true});
  await view.cancel({taskId:start.taskId},command);assert.equal(view.snapshot().state,'admitted');
  assert.equal(view.snapshot().cancelRequested,true);assert.equal(command.calls,1);
});
test('a selector for another child is rejected before consuming an effect',async()=>{
  const view=new AgentTaskObservation(fixture);await view.start(input,context(start));const command=context(queried());
  await assert.rejects(view.query({taskId:'another-child'},command),{code:'E_REFERENCE_AGENT_SELECTOR'});assert.equal(command.calls,0);
});
test('a manual page invocation cannot acquire an Agent effect',async()=>{
  const view=new AgentTaskObservation(fixture);await assert.rejects(view.start(input),{code:'E_GUI_NATIVE_REQUIRED'});
  assert.equal(view.snapshot().state,'idle');
});
test('disposed observation rejects a late start receipt without automatic cancel or retry',async()=>{
  let resolve;const command={active:true,calls:0,effect(){this.calls++;return new Promise(done=>{resolve=done;});}};
  const view=new AgentTaskObservation(fixture);const pending=view.start(input,command);view.dispose();resolve(start);
  await assert.rejects(pending,{code:'E_GUI_EFFECT_STALE'});assert.equal(command.calls,1);assert.equal(view.snapshot().taskId,'');
});
test('reentrant disposal during admission publication prevents the native effect',async()=>{
  const command=context(start);let view;view=new AgentTaskObservation(fixture,()=>view.dispose());
  await assert.rejects(view.start(input,command),{code:'E_GUI_EFFECT_STALE'});assert.equal(command.calls,0);
});
test('late query after disposal cannot publish a terminal into the old view',async()=>{
  const view=new AgentTaskObservation(fixture);await view.start(input,context(start));let resolve;
  const command={active:true,effect(){return new Promise(done=>{resolve=done;});}};
  const pending=view.query({taskId:start.taskId},command);view.dispose();resolve(queried('succeeded'));
  await assert.rejects(pending,{code:'E_GUI_EFFECT_STALE'});assert.equal(view.snapshot().state,'admitted');
});
test('failed effect remains unknown and is never silently replayed',async()=>{
  let calls=0;const command={active:true,async effect(){calls++;throw Object.assign(new Error('lost'),{code:'E_LOST'});}};
  const view=new AgentTaskObservation(fixture);await assert.rejects(view.start(input,command));
  assert.equal(view.snapshot().state,'unknown');await assert.rejects(view.start(input,command));assert.equal(calls,1);
});
test('decimal sequence boundary is validated without Number conversion',async()=>{
  const view=new AgentTaskObservation(fixture);await view.start(input,context(start));
  await view.query({taskId:start.taskId},context({...queried(),sequence:'18446744073709551615'}));
  assert.equal(view.snapshot().sequence,'18446744073709551615');
  await assert.rejects(view.query({taskId:start.taskId},context({...queried(),sequence:'18446744073709551616'})),{code:'E_REFERENCE_AGENT_QUERY_RECEIPT'});
});
test('a failed command displays a bounded error code instead of exception text',async()=>{
  const view=new AgentTaskObservation(fixture);await view.start(input,context(start));
  await assert.rejects(view.query({taskId:start.taskId},{active:true,async effect(){throw {code:'raw secret or exception text'};}}));
  assert.equal(view.snapshot().error,'E_REFERENCE_AGENT_QUERY');assert.equal(view.snapshot().state,'admitted');
});
test('manual form still calls the original typed path once',async()=>{
  const draft={name:'名称',notes:'备注',category:'素材'},result={accepted:true,message:'fixed',receiptId:'receipt-1'};let calls=0;
  assert.equal(await submitReferenceForm({},undefined,draft,async supplied=>{calls++;assert.equal(supplied,draft);return result;}),result);
  assert.equal(calls,1);
});
test('native form effect requires the displayed approved draft and never uses the page client',async()=>{
  const draft={name:'名称',notes:'备注',category:'素材'},result={accepted:true,message:'fixed',receiptId:'receipt-1'},command=context(result);
  assert.equal(await submitReferenceForm({...draft},command,draft,()=>assert.fail('page client used')),result);assert.equal(command.calls,1);
  const mismatch=context(result);await assert.rejects(submitReferenceForm({...draft,name:'另一名称'},mismatch,draft,()=>assert.fail('page client used')),
    {code:'E_REFERENCE_FORM_CHANGED'});assert.equal(mismatch.calls,0);
});
