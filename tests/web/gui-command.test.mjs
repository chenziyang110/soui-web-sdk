import test from 'node:test';
import assert from 'node:assert/strict';
import { SouiNativeClientImpl, createSouiGui } from '../../sdk/web/dist/index.js';
import { readGuiCommand } from '../../sdk/web/dist/gui-command.js';
const element=()=>({nodeType:1,isConnected:true,disabled:false,getAttribute:()=>null});
const flush=async()=>{for(let i=0;i<20;++i)await Promise.resolve();};
const deferred=()=>{let resolve,reject;const promise=new Promise((yes,no)=>{resolve=yes;reject=no;});return{promise,resolve,reject};};
const packet=(extra={})=>({protocol:'soui.gui.command/1',requestId:'native:1',documentId:'document:1',generation:'generation:1',
  operation:'perform',id:'button',action:'apply',inputJson:'{"value":2}',effectId:'native',timeoutMs:1000,...extra});
function fixture(t,action,reply=()=>({accepted:true})) {
  let listener;const requests=[],errors=[];
  const gui=createSouiGui();gui.register({id:'button',role:'button',name:'中文',element:element(),actions:{apply:action},state:()=>({state:{safe:2}})});
  const client=new SouiNativeClientImpl({onEvent:fn=>{listener=fn;return()=>{listener=undefined;};},request:async raw=>{
    const request=JSON.parse(raw);requests.push(request);const result=await reply(request);
    return JSON.stringify({requestId:request.requestId,ok:true,result});
  }},{sessionId:'page-session',origin:'app://page',token:'page-only-token',requestTimeoutMs:1000,onError:error=>errors.push(error.code)});
  const route=client.bindGui(gui);
  t.after(()=>{route.dispose();client.dispose();gui.dispose();});
  return{gui,client,route,requests,errors,emit:(payload=packet(),extra={})=>listener?.({sessionId:'page-session',sequence:1,eventName:'gui.command',payload,...extra})};
}
test('gui parser rejects authority fields, invalid identity, deep JSON and Unicode',()=>{
  assert.equal(readGuiCommand(packet()).operation,'perform');
  for(const change of [{scope:'Agent'}, {id:'body selector'}, {generation:''}, {timeoutMs:10001}, {effectId:'bearer'},
    {inputJson:'[]'}, {inputJson:'{"secret":"\\ud800"}'}, {operation:'snapshot',action:'apply'}, {inputJson:'{"x":'.repeat(10)+'0'+'}'.repeat(10)}])
    assert.throws(()=>readGuiCommand(packet(change)));
  const source=packet();const read=readGuiCommand(source);source.id='changed';assert.equal(read.id,'button');
});
test('route disposal before microtask dispatch sends no bind or native request',async t=>{
  const f=fixture(t,()=>({safe:2}));const rejected=assert.rejects(f.route.ready,{code:'E_DISPOSED'});
  f.route.dispose();await rejected;assert.equal(f.requests.length,0);
});
test('manual handler remains unary and receives no command authority',async t=>{
  let arity,context;const f=fixture(t,function(input,command){arity=arguments.length;context=command;return input;});
  await f.route.ready;assert.deepEqual(await f.gui.perform({id:'button',action:'apply',input:{value:2}}),{value:2});
  assert.equal(arity,1);assert.equal(context,undefined);assert.deepEqual(f.requests.map(x=>x.method),['gui.bind']);
});
test('one native command performs actual registered handler then sends bounded effect and ACK',async t=>{
  let supplied;const f=fixture(t,async(input,context)=>{assert.equal(context.active,true);supplied=input;await context.effect();
    await assert.rejects(context.effect(),{code:'E_GUI_EFFECT_USED'});return{safe:2};},request=>request.method==='gui.effect'?{resultJson:'{"taskId":"native-child"}'}:{accepted:true});
  await f.route.ready;f.emit();await flush();
  assert.deepEqual(supplied,{value:2});assert.deepEqual(f.requests.map(x=>x.method),['gui.bind','gui.effect','gui.ack']);
  const effect=f.requests[1];assert.equal(effect.token,'page-only-token');assert.deepEqual(effect.params,
    {requestId:'native:1',documentId:'document:1',effectId:'native',inputJson:'{"value":2}'});
  assert.equal('scope' in effect.params,false);assert.equal(f.requests[2].params.ok,true);
});
test('wrong session, repeated command and replacement generation do not execute a handler',async t=>{
  let calls=0;const f=fixture(t,()=>{++calls;return{safe:2};});await f.route.ready;
  f.emit(packet({effectId:''}),{sessionId:'other'});f.emit(packet({effectId:''}));await flush();
  f.emit(packet({effectId:''}),{sequence:2});f.emit(packet({requestId:'native:2',generation:'replacement',effectId:''}),{sequence:3});await flush();
  assert.equal(calls,1);assert.equal(f.requests.filter(x=>x.method==='gui.ack').length,1);
});
test('dispose during handler suppresses late effect and ACK without retry',async t=>{
  let captured;const work=deferred();const f=fixture(t,async(_input,context)=>{captured=context;await work.promise;return{safe:2};});
  await f.route.ready;f.emit();await flush();f.client.dispose();work.resolve();await flush();
  assert.equal(captured.active,false);await assert.rejects(captured.effect(),{code:'E_GUI_EFFECT_USED'});
  assert.deepEqual(f.requests.map(x=>x.method),['gui.bind']);
});
test('effect response failure consumes the closure and only fixed failed ACK is sent',async t=>{
  let calls=0;const f=fixture(t,async(_input,context)=>{await assert.rejects(context.effect());
    await assert.rejects(context.effect(),{code:'E_GUI_EFFECT_USED'});throw new Error('private-handler-secret');},request=>{
      if(request.method==='gui.effect'){++calls;throw new Error('lost-response');}return{accepted:true};});
  await f.route.ready;f.emit();await flush();assert.equal(calls,1);
  const ack=f.requests.find(x=>x.method==='gui.ack');assert.equal(ack.params.ok,false);assert.equal(ack.params.resultJson,'{}');
  assert.equal(JSON.stringify(ack).includes('private-handler-secret'),false);
});
test('readonly snapshot and wait use registered semantics without calling an effect',async t=>{
  const f=fixture(t,()=>{throw new Error('must not perform');});await f.route.ready;
  f.emit(packet({operation:'snapshot',action:'',inputJson:'{}',effectId:''}));await flush();
  f.emit(packet({requestId:'native:2',operation:'waitFor',action:'',inputJson:'{"state":{"safe":2}}',effectId:''}),{sequence:2});await flush();
  const acks=f.requests.filter(x=>x.method==='gui.ack');assert.equal(acks.length,2);
  for(const ack of acks){assert.equal(ack.params.ok,true);assert.equal(JSON.parse(ack.params.resultJson).state.safe,2);}
  assert.equal(f.requests.some(x=>x.method==='gui.effect'),false);
});
test('a queued command disposed before handler entry cannot execute the application action',async t=>{
  let calls=0;const f=fixture(t,()=>{++calls;return{safe:2};});await f.route.ready;f.emit(packet({effectId:''}));f.route.dispose();
  await flush();assert.equal(calls,0);assert.deepEqual(f.requests.map(x=>x.method),['gui.bind']);
});
test('native result parsing remains bounded and replay ledger closes instead of evicting',async t=>{
  let calls=0;const f=fixture(t,()=>{++calls;return{safe:2};});await f.route.ready;
  for(let i=1;i<=256;++i){f.emit(packet({requestId:`native:${i}`,effectId:''}),{sequence:i});await flush();}
  assert.equal(calls,256);f.emit(packet({requestId:'native:257',effectId:''}),{sequence:257});await flush();
  assert.equal(calls,256);assert.equal(f.route.disposed,true);assert.equal(f.errors.includes('E_BUSY'),true);
});
