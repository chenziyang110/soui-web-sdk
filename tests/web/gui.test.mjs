import test from 'node:test';
import assert from 'node:assert/strict';
import {createSouiGui,SouiBridgeError} from '../../sdk/web/dist/index.js';

function element(extra={}) {return {nodeType:1,isConnected:true,disabled:false,getAttribute:()=>null,...extra};}
function deferred(){let resolve,reject;const promise=new Promise((yes,no)=>{resolve=yes;reject=no;});return{promise,resolve,reject};}
async function flush(){for(let i=0;i<5;i++)await Promise.resolve();}
function fixture(t,changes={}){
  const gui=createSouiGui();t.after(()=>gui.dispose());
  const node=element();let phase='idle';
  const remove=gui.register({id:'greet',role:'button',name:'问候',element:node,
    actions:{activate:async input=>{phase='ready';return{message:input.name||'hello'};}},
    state:()=>({enabled:true,state:{phase}}),...changes});
  return{gui,node,remove,setPhase:value=>{phase=value;}};
}

test('only explicitly registered semantics and named actions are exposed',async t=>{
  const f=fixture(t);
  assert.deepEqual(f.gui.snapshot(),{protocol:'soui.gui/1',nodes:[{id:'greet',role:'button',name:'问候',enabled:true,busy:false,state:{phase:'idle'},actions:['activate']}]});
  assert.deepEqual(await f.gui.perform({id:'greet',action:'activate',input:{name:'中文😀'}}),{message:'中文😀'});
  assert.equal((await f.gui.waitFor({id:'greet',state:{phase:'ready'}})).state.phase,'ready');
  await assert.rejects(f.gui.perform({id:'body',action:'activate'}),{code:'E_GUI_NOT_FOUND'});
  await assert.rejects(f.gui.perform({id:'greet',action:'eval'}),{code:'E_GUI_ACTION_NOT_FOUND'});
  await assert.rejects(f.gui.perform({id:'greet',action:'activate',selector:'button'}),{code:'E_INVALID_PARAMS'});
  assert.equal('textContent' in f.gui.snapshot().nodes[0],false);
});

test('registrations, inputs and snapshots are owned copies',async t=>{
  const work=deferred();let seen;
  const actions={activate:input=>{seen=input;return work.promise;}};
  const f=fixture(t,{actions});const input={name:'original'};
  const call=f.gui.perform({id:'greet',action:'activate',input});
  input.name='changed';actions.activate=()=>{throw new Error('replacement must not run');};
  await flush();assert.deepEqual(seen,{name:'original'});
  const snapshot=f.gui.snapshot();snapshot.nodes[0].state.phase='fake';snapshot.nodes[0].actions.length=0;
  assert.equal(f.gui.snapshot().nodes[0].state.phase,'idle');
  assert.deepEqual(f.gui.snapshot().nodes[0].actions,['activate']);
  work.resolve({ok:true});assert.deepEqual(await call,{ok:true});
});

test('duplicate ids and capacity are enforced and unregister permits reuse',t=>{
  const f=fixture(t);
  const registration={id:'greet',role:'button',name:'registered',element:element(),actions:{activate:()=>null}};
  assert.throws(()=>f.gui.register(registration),{code:'E_GUI_DUPLICATE_ID'});
  for(let i=1;i<256;i++)f.gui.register({...registration,id:`node-${i}`});
  assert.throws(()=>f.gui.register({...registration,id:'overflow'}),{code:'E_BUSY'});
  f.remove();f.remove();f.gui.register(registration);
  assert.equal(f.gui.snapshot().nodes.length,256);
});

test('disabled property, aria-disabled and explicit app state all deny actions',async t=>{
  let enabled=true;const f=fixture(t,{state:()=>({enabled,state:{}})});
  f.node.disabled=true;await assert.rejects(f.gui.perform({id:'greet',action:'activate'}),{code:'E_GUI_DISABLED'});
  f.node.disabled=false;f.node.getAttribute=()=> 'true';await assert.rejects(f.gui.perform({id:'greet',action:'activate'}),{code:'E_GUI_DISABLED'});
  f.node.getAttribute=()=>null;enabled=false;await assert.rejects(f.gui.perform({id:'greet',action:'activate'}),{code:'E_GUI_DISABLED'});
  enabled=true;assert.deepEqual(await f.gui.perform({id:'greet',action:'activate'}),{message:'hello'});
});

test('Chrome49 connection fallback works and disconnected nodes cannot be acted upon',async t=>{
  let inDocument=true;const node=element({isConnected:undefined,ownerDocument:{documentElement:{contains:()=>inDocument}}});
  const f=fixture(t,{element:node});inDocument=false;
  assert.throws(()=>f.gui.snapshot(),{code:'E_GUI_DISCONNECTED'});
  await assert.rejects(f.gui.perform({id:'greet',action:'activate'}),{code:'E_GUI_DISCONNECTED'});
  assert.throws(()=>f.gui.register({id:'other',role:'button',name:'other',element:node,actions:{activate:()=>null}}),{code:'E_INVALID_PARAMS'});
});

test('busy remains until the actual handler settles after a caller timeout',async t=>{
  t.mock.timers.enable({apis:['setTimeout','setInterval']});
  const work=deferred();const f=fixture(t,{actions:{activate:()=>work.promise}});
  const call=f.gui.perform({id:'greet',action:'activate',timeoutMs:100});
  await flush();assert.equal(f.gui.snapshot().nodes[0].busy,true);
  const rejection=assert.rejects(call,{code:'E_TIMEOUT'});t.mock.timers.tick(100);await rejection;
  await assert.rejects(f.gui.perform({id:'greet',action:'activate'}),{code:'E_BUSY'});
  work.resolve({late:true});await flush();assert.equal(f.gui.snapshot().nodes[0].busy,false);
});

test('synchronous and native handler errors propagate and free busy state',async t=>{
  let count=0;const f=fixture(t,{actions:{activate:()=>{count++;if(count===1)throw new SouiBridgeError('E_UNAUTHORIZED','native denial');if(count===2)throw new Error('business failure');return{ok:true};}}});
  await assert.rejects(f.gui.perform({id:'greet',action:'activate'}),{code:'E_UNAUTHORIZED',message:'native denial'});
  assert.equal(f.gui.snapshot().nodes[0].busy,false);
  await assert.rejects(f.gui.perform({id:'greet',action:'activate'}),{code:'E_GUI_ACTION',message:'business failure'});
  assert.deepEqual(await f.gui.perform({id:'greet',action:'activate'}),{ok:true});
});

test('waitFor observes explicit state, defaults to enabled idle, and times out honestly',async t=>{
  t.mock.timers.enable({apis:['setTimeout','setInterval']});
  let busy=true,phase='loading';const f=fixture(t,{state:()=>({busy,state:{phase,detail:{a:1,b:2}}})});
  const idle=f.gui.waitFor({id:'greet',timeoutMs:100});
  const ready=f.gui.waitFor({id:'greet',state:{phase:'ready',detail:{b:2,a:1}},timeoutMs:100});
  phase='ready';busy=false;t.mock.timers.tick(25);
  assert.equal((await idle).busy,false);assert.equal((await ready).state.phase,'ready');
  const impossible=f.gui.waitFor({id:'greet',state:{phase:'never'},timeoutMs:50});
  const rejected=assert.rejects(impossible,{code:'E_TIMEOUT'});t.mock.timers.tick(50);await rejected;
});

test('unregistration rejects running actions and waits; late results cannot target a reused id',async t=>{
  const work=deferred();const f=fixture(t,{actions:{activate:()=>work.promise}});
  const running=f.gui.perform({id:'greet',action:'activate'});const waiting=f.gui.waitFor({id:'greet',state:{phase:'later'}});
  const denied=[assert.rejects(running,{code:'E_GUI_REMOVED'}),assert.rejects(waiting,{code:'E_GUI_REMOVED'})];
  await flush();f.remove();await Promise.all(denied);
  f.gui.register({id:'greet',role:'button',name:'new',element:element(),actions:{activate:()=>({new:true})}});
  work.resolve({old:true});await flush();assert.equal(f.gui.snapshot().nodes[0].busy,false);
  assert.deepEqual(await f.gui.perform({id:'greet',action:'activate'}),{new:true});
});

test('dispose immediately rejects pending waits and actions, refuses future work, and is idempotent',async t=>{
  const work=deferred();const f=fixture(t,{actions:{activate:()=>work.promise}});
  const call=f.gui.perform({id:'greet',action:'activate'});const wait=f.gui.waitFor({id:'greet',state:{phase:'never'}});
  const rejected=[assert.rejects(call,{code:'E_DISPOSED'}),assert.rejects(wait,{code:'E_DISPOSED'})];
  await flush();f.gui.dispose();f.gui.dispose();await Promise.all(rejected);
  work.resolve({late:true});await flush();
  assert.equal(f.gui.disposed,true);assert.throws(()=>f.gui.snapshot(),{code:'E_DISPOSED'});
  await assert.rejects(f.gui.perform({id:'greet',action:'activate'}),{code:'E_DISPOSED'});
  await assert.rejects(f.gui.waitFor({id:'greet'}),{code:'E_DISPOSED'});
});

test('disconnection during a wait rejects even when the application handler remains pending',async t=>{
  t.mock.timers.enable({apis:['setTimeout','setInterval']});
  const work=deferred();const f=fixture(t,{actions:{activate:()=>work.promise}});
  const call=f.gui.perform({id:'greet',action:'activate'});const rejected=assert.rejects(call,{code:'E_GUI_DISCONNECTED'});
  await flush();f.node.isConnected=false;t.mock.timers.tick(25);await rejected;
  work.resolve({late:true});await flush();
});

test('input, snapshot and handler results enforce UTF-8 byte and JSON limits',async t=>{
  const f=fixture(t);
  await assert.rejects(f.gui.perform({id:'greet',action:'activate',input:{name:'汉'.repeat(11000)}}),{code:'E_INVALID_PARAMS'});
  for(const timeoutMs of [0,60001,1.5,'20'])await assert.rejects(f.gui.perform({id:'greet',action:'activate',timeoutMs}),{code:'E_INVALID_OPTIONS'});
  await assert.rejects(f.gui.perform({id:'greet',action:'activate',input:{name:'\ud800'}}),{code:'E_INVALID_PARAMS'});
  const large=createSouiGui();t.after(()=>large.dispose());
  for(let i=0;i<3;i++)large.register({id:`n${i}`,role:'button',name:'safe',element:element(),actions:{activate:()=>null},state:()=>({state:{payload:'x'.repeat(30000)}})});
  assert.throws(()=>large.snapshot(),{code:'E_LIMIT_MESSAGE_SIZE'});
  const result=fixture(t,{actions:{activate:()=>({payload:'汉'.repeat(22000)})}});
  await assert.rejects(result.gui.perform({id:'greet',action:'activate'}),{code:'E_INVALID_RESPONSE'});
});

test('application state reentrancy cannot revive a disposed or removed registration',async t=>{
  const gui=createSouiGui();t.after(()=>gui.dispose());let remove;
  remove=gui.register({id:'x',role:'button',name:'x',element:element(),actions:{activate:()=>({bad:true})},state:()=>{remove();return{state:{}};}});
  await assert.rejects(gui.perform({id:'x',action:'activate'}),{code:'E_GUI_REMOVED'});
  const other=createSouiGui();t.after(()=>other.dispose());
  other.register({id:'x',role:'button',name:'x',element:element(),actions:{activate:()=>({bad:true})},state:()=>{other.dispose();return{};}});
  assert.throws(()=>other.snapshot(),{code:'E_DISPOSED'});
});

test('caller mutation of an action name cannot execute a different handler',async t=>{
  let seen='';const f=fixture(t,{actions:{activate:()=>{seen='activate';return{};},other:()=>{seen='other';return{};}}});
  const request={id:'greet',action:'activate'};const call=f.gui.perform(request);request.action='other';
  await call;assert.equal(seen,'activate');
});

test('input getter reentrancy cannot invoke an action after removal and serializers are never called',async t=>{
  let called=0;const f=fixture(t,{actions:{activate:()=>{called++;return{};}}});
  const input={get value(){f.remove();return'captured';}};
  await assert.rejects(f.gui.perform({id:'greet',action:'activate',input}),{code:'E_GUI_REMOVED'});
  assert.equal(called,0);
  const next=fixture(t,{actions:{activate:input=>input}});
  const safe={text:'data'};Object.defineProperty(safe,'toJSON',{value:()=>{throw new Error('must not execute');}});
  assert.deepEqual(await next.gui.perform({id:'greet',action:'activate',input:safe}),{text:'data'});
});

test('pending state observations are bounded and capacity recovers after completion',async t=>{
  const f=fixture(t);
  const calls=Array.from({length:256},()=>f.gui.waitFor({id:'greet',state:{phase:'unreachable'}}).catch(error=>error.code));
  await assert.rejects(f.gui.waitFor({id:'greet',state:{phase:'unreachable'}}),{code:'E_BUSY'});
  await assert.rejects(f.gui.perform({id:'greet',action:'activate'}),{code:'E_BUSY'});
  f.gui.dispose();assert.deepEqual(new Set(await Promise.all(calls)),new Set(['E_DISPOSED']));
});
