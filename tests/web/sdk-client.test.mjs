import test from 'node:test';
import assert from 'node:assert/strict';
import {SouiNativeClientImpl,SouiBridgeError,connectSouiNative} from '../../sdk/web/dist/index.js';
const options={sessionId:'test-session',origin:'app://test',token:'test-token',requestTimeoutMs:50};
const client=reply=>new SouiNativeClientImpl({request:async text=>reply(JSON.parse(text)),onEvent:()=>()=>{}},options);
test('custom method sends the scoped envelope and returns native data',async()=>{
  let seen;
  const sdk=client(request=>{seen=request;return JSON.stringify({requestId:request.requestId,ok:true,result:{message:'native'}})});
  assert.deepEqual(await sdk.call('app.custom',{name:'SDK'}),{message:'native'});
  assert.equal(seen.sessionId,options.sessionId);assert.equal(seen.token,options.token);assert.equal(seen.method,'app.custom');
});
test('a response from another request is rejected',async()=>{
  await assert.rejects(client(()=>JSON.stringify({requestId:'other',ok:true})).call('app.custom',{}),error=>error instanceof SouiBridgeError && error.code==='E_INVALID_RESPONSE');
});
test('malformed JSON becomes a structured response error',async()=>{
  await assert.rejects(client(()=>'{broken').call('app.custom',{}),{code:'E_INVALID_RESPONSE'});
});
test('native denials retain the native error code',async()=>{
  await assert.rejects(client(request=>JSON.stringify({requestId:request.requestId,ok:false,error:{code:'E_UNAUTHORIZED',message:'Denied'}})).call('app.custom',{}),{code:'E_UNAUTHORIZED'});
});

test('valid JSON with an invalid response shape produces a structured error',async()=>{
  for(const value of [null,[],true,'response']) {
    await assert.rejects(client(()=>JSON.stringify(value)).call('app.custom',{}),{code:'E_INVALID_RESPONSE'});
  }
  await assert.rejects(client(request=>JSON.stringify({requestId:request.requestId,ok:'true'})).call('app.custom',{}),{code:'E_INVALID_RESPONSE'});
  await assert.rejects(client(request=>JSON.stringify({requestId:request.requestId,ok:false,error:{code:42,message:[]}})).call('app.custom',{}),{code:'E_INVALID_RESPONSE'});
});
test('timeout is bounded without claiming native cancellation',async()=>{
  const sdk=new SouiNativeClientImpl({request:()=>new Promise(()=>{}),onEvent:()=>()=>{}},options);
  await assert.rejects(sdk.call('app.custom',{}),{code:'E_TIMEOUT'});
});

const flush=()=>new Promise(resolve=>setImmediate(resolve));
function deferred(){let resolve,reject;const promise=new Promise((yes,no)=>{resolve=yes;reject=no;});return {promise,resolve,reject};}
function fixture(t,extra={}) {
  const requests=[],listeners=new Set();let removals=0,disposals=0;
  const port={request:raw=>{const wait=deferred();requests.push({...wait,request:JSON.parse(raw)});return wait.promise;},
    onEvent:listener=>{listeners.add(listener);return ()=>{removals++;listeners.delete(listener);};},dispose:()=>{disposals++;}};
  const sdk=new SouiNativeClientImpl(port,{...options,requestTimeoutMs:1000,...extra});
  const reply=(index,result)=>requests[index].resolve(JSON.stringify({requestId:requests[index].request.requestId,ok:true,result}));
  const deny=(index,code)=>requests[index].resolve(JSON.stringify({requestId:requests[index].request.requestId,ok:false,error:{code,message:'denied'}}));
  t.after(async()=>{sdk.dispose();await flush();for(let i=0;i<requests.length;i++)if(requests[i].request.method==='task.unsubscribe')reply(i,{removed:true});await flush();});
  return {sdk,requests,listeners,reply,deny,emit:value=>{for(const listener of Array.from(listeners))listener(value);},
    get removals(){return removals;},get disposals(){return disposals;}};
}
const event=(changes={})=>({sessionId:options.sessionId,taskId:'task-1',eventName:'task.progress',sequence:1,payload:{progress:5},...changes});

test('32 in-flight calls resolve to their own results in reverse order',async t=>{
  const f=fixture(t,{maxPendingRequests:32});const calls=Array.from({length:32},(_,i)=>f.sdk.call('app.custom',{i}));
  assert.equal(f.sdk.pendingRequestCount,32);
  for(let i=31;i>=0;i--)f.reply(i,{i});
  assert.deepEqual(await Promise.all(calls),Array.from({length:32},(_,i)=>({i})));
  assert.equal(f.sdk.pendingRequestCount,0);
});
test('request capacity rejects overload and recovers after completion',async t=>{
  const f=fixture(t,{maxPendingRequests:1});const first=f.sdk.call('app.custom',{});
  await assert.rejects(f.sdk.call('app.custom',{}),{code:'E_BUSY'});assert.equal(f.requests.length,1);
  f.reply(0,{ok:1});await first;
  const next=f.sdk.call('app.custom',{});f.reply(1,{ok:2});assert.deepEqual(await next,{ok:2});
});
test('UTF-8 byte limits and non-JSON parameters fail before transport',async t=>{
  const f=fixture(t,{maxMessageBytes:256});
  await assert.rejects(f.sdk.call('app.custom',{text:'汉'.repeat(100)}),{code:'E_LIMIT_MESSAGE_SIZE'});
  const circular={};circular.self=circular;
  await assert.rejects(f.sdk.call('app.custom',circular),{code:'E_INVALID_PARAMS'});
  assert.equal(f.requests.length,0);assert.equal(f.sdk.pendingRequestCount,0);
  const small=f.sdk.call('app.custom',{text:'ok'});f.reply(0,'small');assert.equal(await small,'small');
});
test('dispose rejects outstanding waits, removes the owned listener once and rejects new calls',async t=>{
  const f=fixture(t);const wait=f.sdk.call('app.custom',{});const rejected=assert.rejects(wait,{code:'E_DISPOSED'});
  f.sdk.dispose();f.sdk.dispose();await rejected;
  assert.equal(f.sdk.disposed,true);assert.equal(f.sdk.pendingRequestCount,0);
  assert.equal(f.removals,1);assert.equal(f.disposals,1);assert.equal(f.listeners.size,0);
  f.reply(0,{late:true});await flush();
  await assert.rejects(f.sdk.call('app.custom',{}),{code:'E_DISPOSED'});assert.equal(f.requests.length,1);
});
test('timeout frees a page slot without pretending native work was cancelled',async t=>{
  const f=fixture(t,{maxPendingRequests:1,requestTimeoutMs:20});
  await assert.rejects(f.sdk.call('app.custom',{}),{code:'E_TIMEOUT'});assert.equal(f.sdk.pendingRequestCount,0);
  const next=f.sdk.call('app.custom',{});f.reply(0,'late');f.reply(1,'current');assert.equal(await next,'current');
  assert.equal(f.requests.some(x=>x.request.method==='task.cancel'),false);
});
test('invalid client options and null native error envelopes are structured failures',async()=>{
  const port={request:async()=>'',onEvent:()=>()=>{}};
  for(const change of [{sessionId:''},{token:3},{requestTimeoutMs:0},{requestTimeoutMs:60001},{maxPendingRequests:Infinity},{maxSubscriptions:0},{maxMessageBytes:65537}])
    assert.throws(()=>new SouiNativeClientImpl(port,{...options,...change}),{code:'E_INVALID_OPTIONS'});
  await assert.rejects(client(request=>JSON.stringify({requestId:request.requestId,ok:false,error:null})).call('app.custom',{}),{code:'E_INVALID_RESPONSE'});
});
test('caller mutation of options cannot change the connection scope',async()=>{
  const supplied={...options};let seen;
  const sdk=new SouiNativeClientImpl({onEvent:()=>()=>{},request:async raw=>{seen=JSON.parse(raw);return JSON.stringify({requestId:seen.requestId,ok:true});}},supplied);
  supplied.sessionId='another-session';supplied.token='another-token';await sdk.call('app.custom',{});
  assert.equal(seen.sessionId,options.sessionId);assert.equal(seen.token,options.token);sdk.dispose();
});
test('event validation, sequence filtering and callback isolation apply to each observer',async t=>{
  const errors=[],f=fixture(t,{onError:error=>{errors.push(error.code);throw Error('observer failed');}}),seen=[];
  const a=f.sdk.subscribe('task-1','task.progress',()=>{throw Error('business callback failed');});
  const b=f.sdk.subscribe('task-1','task.progress',e=>seen.push(e.sequence));
  f.reply(0,{subscriptionId:'a'});f.reply(1,{subscriptionId:'b'});await Promise.all([a.ready,b.ready]);
  for(const value of [null,[],event({sequence:0}),event({sequence:NaN}),event({sessionId:'other'}),event({taskId:'other'}),event({eventName:'other'})])f.emit(value);
  f.emit(event());f.emit(event());f.emit(event({sequence:2}));f.emit(event({sequence:1}));
  assert.deepEqual(seen,[1,2]);assert.deepEqual(errors,['E_EVENT_LISTENER','E_EVENT_LISTENER']);
  const removed=b.unsubscribe();f.emit(event({sequence:3}));f.reply(2,{removed:true});await removed;
  assert.deepEqual(seen,[1,2]);assert.equal(b.closed,true);
});
test('native subscription denial is observable and releases the local subscription slot',async t=>{
  const f=fixture(t,{maxSubscriptions:1});const sub=f.sdk.subscribe('task-1','task.progress',()=>{});
  assert.throws(()=>f.sdk.subscribe('task-2','task.progress',()=>{}),{code:'E_BUSY'});
  f.deny(0,'E_PERMISSION_DENIED');await assert.rejects(sub.ready,{code:'E_PERMISSION_DENIED'});
  assert.equal(sub.closed,true);assert.equal(f.sdk.subscriptionCount,0);
  await assert.rejects(sub.unsubscribe(),{code:'E_PERMISSION_DENIED'});assert.equal(f.requests.length,1);
});
test('removal before subscription acknowledgement is idempotent and sends one late unsubscribe',async t=>{
  const f=fixture(t);let delivered=0;const sub=f.sdk.subscribe('task-1','task.progress',()=>{delivered++;});
  const removal=sub.unsubscribe();assert.equal(sub.unsubscribe(),removal);sub();sub();f.emit(event());
  assert.equal(sub.closed,true);assert.equal(f.requests.length,1);
  f.reply(0,{subscriptionId:'late-sub'});await sub.ready;await flush();
  assert.equal(f.requests.length,2);assert.equal(f.requests[1].request.method,'task.unsubscribe');
  assert.deepEqual(f.requests[1].request.params,{subscriptionId:'late-sub'});
  f.reply(1,{removed:true});await removal;assert.equal(delivered,0);sub();assert.equal(f.requests.length,2);
});
test('a disposed pending subscription cleans up a late native success once',async t=>{
  const f=fixture(t);const sub=f.sdk.subscribe('task-1','task.progress',()=>assert.fail('disposed callback'));
  const rejection=assert.rejects(sub.ready,{code:'E_DISPOSED'});f.sdk.dispose();await rejection;
  assert.equal(sub.closed,true);assert.equal(f.sdk.subscriptionCount,0);
  f.reply(0,{subscriptionId:'created-after-disposal'});await flush();
  assert.equal(f.requests.length,2);f.reply(1,{removed:true});await flush();
  assert.equal(f.sdk.pendingRequestCount,0);
});
test('a timed-out subscription also cleans up a late successful acknowledgement',async t=>{
  const f=fixture(t,{requestTimeoutMs:20});const sub=f.sdk.subscribe('task-1','task.progress',()=>{});
  await assert.rejects(sub.ready,{code:'E_TIMEOUT'});f.reply(0,{subscriptionId:'timed-out'});await flush();
  assert.equal(f.requests[1].request.method,'task.unsubscribe');f.reply(1,{removed:true});await flush();
  assert.equal(f.sdk.pendingRequestCount,0);assert.equal(f.sdk.subscriptionCount,0);
});
test('malformed subscription identities and failed native removal remain visible',async t=>{
  const f=fixture(t);const bad=f.sdk.subscribe('task-1','task.progress',()=>{});
  f.reply(0,{subscriptionId:4});await assert.rejects(bad.ready,{code:'E_INVALID_RESPONSE'});
  const sub=f.sdk.subscribe('task-1','task.progress',()=>{});f.reply(1,{subscriptionId:'valid'});await sub.ready;
  const removal=sub.unsubscribe();f.reply(2,{removed:false});await assert.rejects(removal,{code:'E_INVALID_RESPONSE'});
  assert.equal(sub.closed,true);assert.equal(sub.unsubscribe(),removal);
});
test('bounded control capacity permits unsubscribe when ordinary request capacity is full',async t=>{
  const f=fixture(t,{maxPendingRequests:1,maxSubscriptions:1});const sub=f.sdk.subscribe('task-1','task.progress',()=>{});
  f.reply(0,{subscriptionId:'one'});await sub.ready;
  const work=f.sdk.call('app.custom',{});const removal=sub.unsubscribe();
  assert.equal(f.requests.length,3);assert.equal(f.sdk.pendingRequestCount,2);
  f.reply(2,{removed:true});await removal;f.reply(1,'done');assert.equal(await work,'done');
});
test('toJSON reentrancy cannot bypass disposal or capacity checks',async t=>{
  const f=fixture(t,{maxPendingRequests:1});let inner;
  await assert.rejects(f.sdk.call('app.custom',{toJSON(){inner=f.sdk.call('app.custom',{});return {};}}),{code:'E_BUSY'});
  assert.equal(f.requests.length,1);f.reply(0,'inner');await inner;
  await assert.rejects(f.sdk.call('app.custom',{toJSON(){f.sdk.dispose();return {};}}),{code:'E_DISPOSED'});
  assert.equal(f.requests.length,1);
});
test('a reentrant port can dispose during subscription creation without leaking a late subscription',async()=>{
  let sdk;const methods=[];
  sdk=new SouiNativeClientImpl({onEvent:()=>()=>{},request:async raw=>{
    const request=JSON.parse(raw);methods.push(request.method);
    if(request.method==='task.subscribe')sdk.dispose();
    return JSON.stringify({requestId:request.requestId,ok:true,result:request.method==='task.subscribe'?{subscriptionId:'reentrant'}:{removed:true}});
  }},options);
  const sub=sdk.subscribe('task-1','task.progress',()=>{});await assert.rejects(sub.ready,{code:'E_DISPOSED'});await flush();
  assert.deepEqual(methods,['task.subscribe','task.unsubscribe']);assert.equal(sdk.pendingRequestCount,0);
});

function nativeWindow(bootstrap={sessionId:'window-session',origin:'app://fixture',token:'window-token'}) {
  const hooks=new Map(),requests=[];
  const target={
    addEventListener:(type,listener)=>{if(!hooks.has(type))hooks.set(type,new Set());hooks.get(type).add(listener);},
    removeEventListener:(type,listener)=>hooks.get(type)?.delete(listener),
    __souiNativePostMessage:raw=>{const request=JSON.parse(raw),wait=deferred();requests.push({...wait,request});
      if(request.method==='bridge.bootstrap')return JSON.stringify({requestId:request.requestId,ok:true,result:bootstrap});
      return wait.promise;},
  };
  return {target,requests,hooks,hookCount:()=>Array.from(hooks.values()).reduce((n,set)=>n+set.size,0),
    fire:type=>{for(const listener of Array.from(hooks.get(type)||[]))listener({type});},
    reply:(index,result)=>requests[index].resolve(JSON.stringify({requestId:requests[index].request.requestId,ok:true,result}))};
}
test('the standard connector shares one bootstrap and waits for asynchronous native responses',async()=>{
  const f=nativeWindow(),a=connectSouiNative({target:f.target}),b=connectSouiNative({target:f.target});assert.equal(a,b);
  const sdk=await a;assert.equal(await b,sdk);assert.equal(f.requests.length,1);
  assert.deepEqual(Object.keys(f.requests[0].request).sort(),['method','origin','params','requestId','sessionId','token']);
  const work=sdk.call('app.custom',{});f.reply(1,'native');assert.equal(await work,'native');
  assert.equal(f.requests[1].request.sessionId,'window-session');sdk.dispose();assert.equal(f.hookCount(),0);
});
test('asynchronous bootstrap is supported without a busy wait',async()=>{
  const f=nativeWindow(),original=f.target.__souiNativePostMessage;
  f.target.__souiNativePostMessage=raw=>Promise.resolve(original(raw));
  const sdk=await connectSouiNative({target:f.target});assert.equal(sdk.disposed,false);sdk.dispose();
});

test('connector options cannot swap the error observer while bootstrap is pending',async()=>{
  const f=nativeWindow(),seen=[];
  const supplied={target:f.target,onError:error=>seen.push(error.code)};
  const connection=connectSouiNative(supplied);supplied.onError=()=>assert.fail('mutated observer');
  const sdk=await connection;
  const sub=sdk.subscribe('task-1','task.progress',()=>{});
  f.reply(1,{subscriptionId:0});await assert.rejects(sub.ready,{code:'E_INVALID_RESPONSE'});
  assert.deepEqual(seen,['E_INVALID_RESPONSE']);sdk.dispose();
});
test('a second connector cannot silently change options or replace another dispatcher',async()=>{
  const f=nativeWindow(),first=connectSouiNative({target:f.target,maxPendingRequests:32});
  await assert.rejects(connectSouiNative({target:f.target,maxPendingRequests:33}),{code:'E_CONNECTION_OPTIONS'});
  (await first).dispose();
  const occupied=nativeWindow(),foreign=()=>{};occupied.target.__souiNativeDispatchEvent=foreign;
  await assert.rejects(connectSouiNative({target:occupied.target}),{code:'E_CONNECTION_IN_USE'});
  assert.equal(occupied.target.__souiNativeDispatchEvent,foreign);assert.equal(occupied.requests.length,0);
});
test('document unload disposes outstanding waits and removes connector hooks',async()=>{
  const f=nativeWindow(),sdk=await connectSouiNative({target:f.target});const work=sdk.call('app.custom',{});
  const rejection=assert.rejects(work,{code:'E_DISPOSED'});f.fire('pagehide');await rejection;
  assert.equal(f.hookCount(),0);assert.equal(f.target.__souiNativeDispatchEvent,undefined);
  await assert.rejects(connectSouiNative({target:f.target}),{code:'E_DISPOSED'});assert.equal(f.requests.length,2);
});
test('bootstrap timeout and unload release hooks and do not silently rebootstrap',async()=>{
  for(const exit of ['timeout','unload']){
    const f=nativeWindow();f.target.__souiNativePostMessage=()=>new Promise(()=>{});
    const connecting=connectSouiNative({target:f.target,bootstrapTimeoutMs:20});
    const rejection=assert.rejects(connecting,{code:exit==='timeout'?'E_TIMEOUT':'E_DISPOSED'});
    if(exit==='unload'){await flush();f.fire('beforeunload');}
    await rejection;assert.equal(f.hookCount(),0);assert.equal(f.target.__souiNativeDispatchEvent,undefined);
  }
});
test('bootstrap rejects malformed credentials, wrong correlation and invalid JSON',async()=>{
  for(const credentials of [null,[],{sessionId:'s',origin:'https://remote',token:'t'},{sessionId:42,origin:'app://f',token:'t'},
    {sessionId:'s',origin:'app://f',token:42},{sessionId:'s',origin:'app://f',token:''}]){
    const f=nativeWindow(credentials);await assert.rejects(connectSouiNative({target:f.target}),{code:'E_INVALID_RESPONSE'});assert.equal(f.hookCount(),0);
  }
  for(const raw of ['{bad',JSON.stringify({requestId:'other',ok:true,result:{}})]){
    const f=nativeWindow();f.target.__souiNativePostMessage=()=>raw;
    await assert.rejects(connectSouiNative({target:f.target}),{code:'E_INVALID_RESPONSE'});assert.equal(f.hookCount(),0);
  }
});
test('connector native events reach only matching subscriptions and owned hooks are restored',async()=>{
  const f=nativeWindow(),sdk=await connectSouiNative({target:f.target});let received=0;
  const sub=sdk.subscribe('task-1','task.progress',()=>received++);f.reply(1,{subscriptionId:'a'});await sub.ready;
  f.target.__souiNativeDispatchEvent(event());f.target.__souiNativeDispatchEvent(event({sessionId:'window-session'}));assert.equal(received,1);
  const removal=sub.unsubscribe();f.reply(2,{removed:true});await removal;
  const replacement=()=>{};f.target.__souiNativeDispatchEvent=replacement;sdk.dispose();assert.equal(f.target.__souiNativeDispatchEvent,replacement);
});
test('ordinary browser use and broken native binding produce explicit connection errors',async()=>{
  await assert.rejects(connectSouiNative({target:{}}),{code:'E_NATIVE_UNAVAILABLE'});
  const f=nativeWindow();f.target.__souiNativePostMessage=()=>{throw Error('native binding failure');};
  await assert.rejects(connectSouiNative({target:f.target}),{code:'E_NATIVE_CONNECTION'});assert.equal(f.hookCount(),0);
});
